/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<ColMeta> sort_cols_;          // 多个排序键的元数据
    std::vector<bool> is_desc_;
    int limit_;
    std::vector<std::unique_ptr<RmRecord>> sorted_tuples_;
    size_t curr_idx_;
    size_t len_;

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, std::vector<TabCol> sel_cols,
                 std::vector<bool> is_desc, int limit = -1) {
        // 先读取子节点信息，再移动所有权
        len_ = prev->tupleLen();
        auto &prev_cols = prev->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sort_cols_.push_back(*pos);
        }
        is_desc_ = is_desc;
        prev_ = std::move(prev);
        limit_ = limit;
        curr_idx_ = 0;
    }

    void beginTuple() override {
        // 1. 收集所有元组
        sorted_tuples_.clear();
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            auto tuple = prev_->Next();
            if (tuple != nullptr) {
                sorted_tuples_.push_back(std::move(tuple));
            }
        }

        // 2. 多列排序
        std::sort(sorted_tuples_.begin(), sorted_tuples_.end(),
            [this](const std::unique_ptr<RmRecord> &a, const std::unique_ptr<RmRecord> &b) {
                for (size_t i = 0; i < sort_cols_.size(); i++) {
                    char *da = a->data + sort_cols_[i].offset;
                    char *db = b->data + sort_cols_[i].offset;
                    int cmp = compare(da, db, sort_cols_[i].type, sort_cols_[i].len);
                    if (cmp != 0) {
                        return is_desc_[i] ? cmp > 0 : cmp < 0;
                    }
                }
                return false;
            });

        // 3. 应用 LIMIT
        if (limit_ >= 0 && (int)sorted_tuples_.size() > limit_) {
            sorted_tuples_.resize(limit_);
        }
        curr_idx_ = 0;
    }

    void nextTuple() override {
        curr_idx_++;
    }

    bool is_end() const override {
        return curr_idx_ >= sorted_tuples_.size();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (curr_idx_ >= sorted_tuples_.size()) return nullptr;
        auto rec = std::make_unique<RmRecord>(len_);
        memcpy(rec->data, sorted_tuples_[curr_idx_]->data, len_);
        return rec;
    }

    const std::vector<ColMeta> &cols() const override {
        return prev_->cols();
    }

    size_t tupleLen() const override {
        return len_;
    }

    Rid &rid() override { return _abstract_rid; }

   private:
    int compare(char *a, char *b, ColType type, int len) {
        if (type == TYPE_INT) {
            int va = *(int *)a, vb = *(int *)b;
            if (va < vb) return -1;
            if (va > vb) return 1;
            return 0;
        } else if (type == TYPE_BIGINT || type == TYPE_DATETIME) {
            int64_t va = *(int64_t *)a, vb = *(int64_t *)b;
            if (va < vb) return -1;
            if (va > vb) return 1;
            return 0;
        } else if (type == TYPE_FLOAT) {
            float va = *(float *)a, vb = *(float *)b;
            if (va < vb) return -1;
            if (va > vb) return 1;
            return 0;
        } else if (type == TYPE_STRING) {
            std::string sa(a, len), sb(b, len);
            sa.resize(strlen(sa.c_str()));
            sb.resize(strlen(sb.c_str()));
            if (sa < sb) return -1;
            if (sa > sb) return 1;
            return 0;
        }
        return 0;
    }
};
