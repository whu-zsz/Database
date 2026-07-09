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

#include <cstring>
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class SeqScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<ColMeta> cols_;
    size_t len_;
    std::vector<Condition> fed_conds_;

    Rid rid_;
    std::unique_ptr<RecScan> scan_;
    SmManager *sm_manager_;

   public:
    SeqScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        conds_ = std::move(conds);
        TabMeta &tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        len_ = cols_.back().offset + cols_.back().len;
        context_ = context;
        fed_conds_ = conds_;
    }

    void beginTuple() override {
        // 统一加表级 S 锁，防止幻读
        context_->lock_mgr_->lock_shared_on_table(context_->txn_, fh_->GetFd());

        scan_ = std::make_unique<RmScan>(fh_);
        // 跳过不满足条件的记录
        while (!scan_->is_end() && !check_conds()) {
            scan_->next();
        }
        if (!scan_->is_end()) {
            rid_ = scan_->rid();
            // 对满足条件的行加 S 锁
            context_->lock_mgr_->lock_shared_on_record(context_->txn_, rid_, fh_->GetFd());
        }
    }

    void nextTuple() override {
        do {
            scan_->next();
        } while (!scan_->is_end() && !check_conds());
        if (!scan_->is_end()) {
            rid_ = scan_->rid();
        }
    }

    bool is_end() const override {
        return scan_->is_end();
    }

    std::unique_ptr<RmRecord> Next() override {
        // 读取前加 S 锁
        context_->lock_mgr_->lock_shared_on_record(context_->txn_, rid_, fh_->GetFd());
        return fh_->get_record(rid_, context_);
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return rid_; }

   private:
    bool check_conds() {
        if (fed_conds_.empty()) return true;
        auto rec = fh_->get_record(scan_->rid(), context_);
        if (rec == nullptr) return false;
        for (auto &cond : fed_conds_) {
            if (!cond.is_rhs_val || cond.lhs_col.tab_name != tab_name_) continue;
            auto col_it = std::find_if(cols_.begin(), cols_.end(),
                [&](const ColMeta &c) { return c.name == cond.lhs_col.col_name; });
            if (col_it == cols_.end()) continue;
            char *data = rec->data + col_it->offset;
            if (col_it->type == TYPE_INT) {
                int val = *(int *)data;
                int rhs = (cond.rhs_val.type == TYPE_INT) ? cond.rhs_val.int_val : (int)cond.rhs_val.float_val;
                if (!cmp_int(cond.op, val, rhs)) return false;
            } else if (col_it->type == TYPE_BIGINT) {
                int64_t val = *(int64_t *)data;
                int64_t rhs = (cond.rhs_val.type == TYPE_BIGINT) ? cond.rhs_val.bigint_val : (int64_t)cond.rhs_val.int_val;
                if (!cmp_bigint(cond.op, val, rhs)) return false;
            } else if (col_it->type == TYPE_FLOAT) {
                float val = *(float *)data;
                float rhs = (cond.rhs_val.type == TYPE_FLOAT) ? cond.rhs_val.float_val : (float)cond.rhs_val.int_val;
                if (!cmp_float(cond.op, val, rhs)) return false;
            } else if (col_it->type == TYPE_DATETIME) {
                int64_t val = *(int64_t *)data;
                int64_t rhs;
                if (cond.rhs_val.type == TYPE_DATETIME) {
                    rhs = cond.rhs_val.bigint_val;
                } else if (cond.rhs_val.type == TYPE_STRING) {
                    if (!is_valid_datetime(cond.rhs_val.str_val, rhs)) return false;
                } else {
                    return false;
                }
                if (!cmp_bigint(cond.op, val, rhs)) return false;
            } else if (col_it->type == TYPE_STRING) {
                std::string val(data, col_it->len);
                val.resize(strlen(val.c_str()));
                std::string rhs = cond.rhs_val.str_val;
                if (!cmp_str(cond.op, val, rhs)) return false;
            }
        }
        return true;
    }

    bool cmp_int(CompOp op, int l, int r) {
        switch (op) {
            case OP_EQ: return l == r;
            case OP_NE: return l != r;
            case OP_LT: return l < r;
            case OP_GT: return l > r;
            case OP_LE: return l <= r;
            case OP_GE: return l >= r;
        }
        return false;
    }
    bool cmp_bigint(CompOp op, int64_t l, int64_t r) {
        switch (op) {
            case OP_EQ: return l == r;
            case OP_NE: return l != r;
            case OP_LT: return l < r;
            case OP_GT: return l > r;
            case OP_LE: return l <= r;
            case OP_GE: return l >= r;
        }
        return false;
    }
    bool cmp_float(CompOp op, float l, float r) {
        switch (op) {
            case OP_EQ: return l == r;
            case OP_NE: return l != r;
            case OP_LT: return l < r;
            case OP_GT: return l > r;
            case OP_LE: return l <= r;
            case OP_GE: return l >= r;
        }
        return false;
    }
    bool cmp_str(CompOp op, const std::string &l, const std::string &r) {
        switch (op) {
            case OP_EQ: return l == r;
            case OP_NE: return l != r;
            case OP_LT: return l < r;
            case OP_GT: return l > r;
            case OP_LE: return l <= r;
            case OP_GE: return l >= r;
        }
        return false;
    }
};