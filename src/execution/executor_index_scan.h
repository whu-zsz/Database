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

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class IndexScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;                      // 表名称
    TabMeta tab_;                               // 表的元数据
    std::vector<Condition> conds_;              // 扫描条件
    RmFileHandle *fh_;                          // 表的数据文件句柄
    std::vector<ColMeta> cols_;                 // 需要读取的字段
    size_t len_;                                // 选取出来的一条记录的长度
    std::vector<Condition> fed_conds_;          // 扫描条件，和conds_字段相同

    std::vector<std::string> index_col_names_;  // index scan涉及到的索引包含的字段
    IndexMeta index_meta_;                      // index scan涉及到的索引元数据

    Rid rid_;
    std::unique_ptr<RecScan> scan_;
    std::vector<Rid> rids_;
    size_t rid_pos_ = 0;

    SmManager *sm_manager_;

   public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, std::vector<std::string> index_col_names,
                    Context *context) {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        // index_no_ = index_no;
        index_col_names_ = index_col_names; 
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab_.cols;
        len_ = cols_.back().offset + cols_.back().len;
        std::map<CompOp, CompOp> swap_op = {
            {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
        };

        for (auto &cond : conds_) {
            if (cond.lhs_col.tab_name != tab_name_) {
                // lhs is on other table, now rhs must be on this table
                assert(!cond.is_rhs_val && cond.rhs_col.tab_name == tab_name_);
                // swap lhs and rhs
                std::swap(cond.lhs_col, cond.rhs_col);
                cond.op = swap_op.at(cond.op);
            }
        }
        fed_conds_ = conds_;
    }

    void beginTuple() override {
        // 加表级 S 锁，防止幻读
        context_->lock_mgr_->lock_shared_on_table(context_->txn_, fh_->GetFd());

        rids_.clear();
        rid_pos_ = 0;
        auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_meta_.cols)).get();
        std::vector<char> key(index_meta_.col_tot_len);
        if (build_exact_key(key.data())) {
            ih->get_value(key.data(), &rids_, context_->txn_);
        } else {
            for (IxScan scan(ih, ih->leaf_begin(), ih->leaf_end(), sm_manager_->get_bpm()); !scan.is_end(); scan.next()) {
                rids_.push_back(scan.rid());
            }
        }
        while (rid_pos_ < rids_.size() && !check_conds(rids_[rid_pos_])) {
            rid_pos_++;
        }
        if (rid_pos_ < rids_.size()) {
            rid_ = rids_[rid_pos_];
            // 对满足条件的行加 S 锁
            context_->lock_mgr_->lock_shared_on_record(context_->txn_, rid_, fh_->GetFd());
        }
    }

    void nextTuple() override {
        if (rid_pos_ < rids_.size()) rid_pos_++;
        while (rid_pos_ < rids_.size() && !check_conds(rids_[rid_pos_])) {
            rid_pos_++;
        }
        if (rid_pos_ < rids_.size()) {
            rid_ = rids_[rid_pos_];
        }
    }

    bool is_end() const override { return rid_pos_ >= rids_.size(); }

    std::unique_ptr<RmRecord> Next() override {
        // 读取前加 S 锁
        context_->lock_mgr_->lock_shared_on_record(context_->txn_, rid_, fh_->GetFd());
        return fh_->get_record(rid_, context_);
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return rid_; }

   private:
    bool build_exact_key(char *key) {
        int offset = 0;
        for (auto &idx_col : index_meta_.cols) {
            auto it = std::find_if(fed_conds_.begin(), fed_conds_.end(), [&](const Condition &cond) {
                return cond.is_rhs_val && cond.op == OP_EQ && cond.lhs_col.tab_name == tab_name_ &&
                       cond.lhs_col.col_name == idx_col.name;
            });
            if (it == fed_conds_.end()) return false;
            memcpy(key + offset, it->rhs_val.raw->data, idx_col.len);
            offset += idx_col.len;
        }
        return true;
    }

    bool check_conds(const Rid &rid) {
        if (fed_conds_.empty()) return true;
        auto rec = fh_->get_record(rid, context_);
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
                int64_t rhs = (cond.rhs_val.type == TYPE_BIGINT || cond.rhs_val.type == TYPE_DATETIME) ? cond.rhs_val.bigint_val : (int64_t)cond.rhs_val.int_val;
                if (!cmp_bigint(cond.op, val, rhs)) return false;
            } else if (col_it->type == TYPE_FLOAT) {
                float val = *(float *)data;
                float rhs = (cond.rhs_val.type == TYPE_FLOAT) ? cond.rhs_val.float_val : (float)cond.rhs_val.int_val;
                if (!cmp_float(cond.op, val, rhs)) return false;
            } else if (col_it->type == TYPE_DATETIME) {
                int64_t val = *(int64_t *)data;
                int64_t rhs = cond.rhs_val.bigint_val;
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
