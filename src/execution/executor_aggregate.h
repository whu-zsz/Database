/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <cstring>
#include <vector>
#include "execution_defs.h"
#include "executor_abstract.h"
#include "system/sm.h"

class AggregateExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    AggCall agg_;
    std::vector<ColMeta> cols_;
    size_t len_;
    bool emitted_ = false;
    std::unique_ptr<RmRecord> result_;

   public:
    AggregateExecutor(std::unique_ptr<AbstractExecutor> prev, AggCall agg)
        : prev_(std::move(prev)), agg_(std::move(agg)) {
        ColMeta out_col;
        out_col.tab_name = "";
        out_col.name = agg_.alias;
        out_col.offset = 0;
        out_col.index = false;
        if (agg_.type == AGG_COUNT) {
            out_col.type = TYPE_INT;
            out_col.len = sizeof(int);
        } else {
            auto &prev_cols = prev_->cols();
            auto pos = get_col(prev_cols, agg_.col);
            out_col.type = pos->type;
            out_col.len = pos->len;
        }
        len_ = out_col.len;
        cols_.push_back(out_col);
    }

    void beginTuple() override {
        build_result();
        emitted_ = false;
    }

    void nextTuple() override { emitted_ = true; }

    bool is_end() const override { return emitted_; }

    std::unique_ptr<RmRecord> Next() override {
        auto rec = std::make_unique<RmRecord>(len_);
        memcpy(rec->data, result_->data, len_);
        return rec;
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return _abstract_rid; }

   private:
    void build_result() {
        result_ = std::make_unique<RmRecord>(len_);
        int count = 0;
        bool has_value = false;
        int int_acc = 0;
        float float_acc = 0;
        int int_best = 0;
        float float_best = 0;
        std::vector<char> string_best;

        int value_offset = 0;
        ColType value_type = TYPE_INT;
        if (!agg_.count_star) {
            auto pos = get_col(prev_->cols(), agg_.col);
            value_offset = pos->offset;
            value_type = pos->type;
        }

        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            auto rec = prev_->Next();
            if (agg_.type == AGG_COUNT) {
                count++;
                continue;
            }
            char *data = rec->data + value_offset;
            if (value_type == TYPE_INT) {
                int v = *(int *)data;
                if (agg_.type == AGG_SUM) int_acc += v;
                if (!has_value || (agg_.type == AGG_MAX && v > int_best) || (agg_.type == AGG_MIN && v < int_best)) {
                    int_best = v;
                }
            } else if (value_type == TYPE_FLOAT) {
                float v = *(float *)data;
                if (agg_.type == AGG_SUM) float_acc += v;
                if (!has_value || (agg_.type == AGG_MAX && v > float_best) || (agg_.type == AGG_MIN && v < float_best)) {
                    float_best = v;
                }
            } else if (value_type == TYPE_STRING && agg_.type != AGG_SUM) {
                if (!has_value) {
                    string_best.assign(data, data + len_);
                } else {
                    int cmp = strncmp(data, string_best.data(), len_);
                    if ((agg_.type == AGG_MAX && cmp > 0) || (agg_.type == AGG_MIN && cmp < 0)) {
                        string_best.assign(data, data + len_);
                    }
                }
            } else {
                throw IncompatibleTypeError("aggregate", coltype2str(value_type));
            }
            has_value = true;
        }

        if (agg_.type == AGG_COUNT) {
            *(int *)result_->data = count;
        } else if (value_type == TYPE_INT) {
            *(int *)result_->data = (agg_.type == AGG_SUM) ? int_acc : int_best;
        } else if (value_type == TYPE_FLOAT) {
            *(float *)result_->data = (agg_.type == AGG_SUM) ? float_acc : float_best;
        } else if (value_type == TYPE_STRING) {
            if (!string_best.empty()) {
                memcpy(result_->data, string_best.data(), len_);
            } else {
                memset(result_->data, 0, len_);
            }
        }
    }
};
