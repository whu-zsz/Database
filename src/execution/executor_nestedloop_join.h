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
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点
    size_t len_;
    std::vector<ColMeta> cols_;
    std::vector<Condition> fed_conds_;          // join条件
    bool isend;
    std::unique_ptr<RmRecord> left_rec_;        // 当前左记录（保存供多轮使用）

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);
    }

    void beginTuple() override {
        isend = false;
        left_->beginTuple();
        if (left_->is_end()) {
            isend = true;
            return;
        }
        left_rec_ = left_->Next();
        right_->beginTuple();
        // 跳过不满足条件的对
        if (!fed_conds_.empty()) {
            while (!check_current_match()) {
                advance_inner();
                if (isend) return;
            }
        }
    }

    void nextTuple() override {
        advance_inner();
        if (isend) return;
        if (!fed_conds_.empty()) {
            while (!check_current_match()) {
                advance_inner();
                if (isend) return;
            }
        }
    }

    bool is_end() const override { return isend; }

    std::unique_ptr<RmRecord> Next() override {
        auto right_rec = right_->Next();
        auto joined = std::make_unique<RmRecord>(len_);
        memcpy(joined->data, left_rec_->data, left_->tupleLen());
        memcpy(joined->data + left_->tupleLen(), right_rec->data, right_->tupleLen());
        return joined;
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return _abstract_rid; }

   private:
    // 移动内层（右表），右表到底则移动外层（左表）并重置右表
    void advance_inner() {
        right_->nextTuple();
        while (right_->is_end()) {
            left_->nextTuple();
            if (left_->is_end()) {
                isend = true;
                return;
            }
            left_rec_ = left_->Next();
            right_->beginTuple();
        }
    }

    // 检查当前 left+right 是否满足所有连接条件
    bool check_current_match() {
        if (right_->is_end()) return false;
        auto right_rec = right_->Next();
        for (auto &cond : fed_conds_) {
            char *lhs_data;
            char *rhs_data;
            // 确定 lhs 在哪个子节点中
            auto &left_cols = left_->cols();
            auto lit = std::find_if(left_cols.begin(), left_cols.end(),
                [&](const ColMeta &c) { return c.name == cond.lhs_col.col_name &&
                                               c.tab_name == cond.lhs_col.tab_name; });
            if (lit != left_cols.end()) {
                lhs_data = left_rec_->data + lit->offset;
            } else {
                auto &right_cols2 = right_->cols();
                auto rit = std::find_if(right_cols2.begin(), right_cols2.end(),
                    [&](const ColMeta &c) { return c.name == cond.lhs_col.col_name; });
                lhs_data = right_rec->data + rit->offset;
            }
            // 确定 rhs 在哪个子节点中
            if (cond.is_rhs_val) {
                // 值比较
                return eval_cond_val(cond, lhs_data, lit != left_cols.end() ? *lit : *std::find_if(
                    right_->cols().begin(), right_->cols().end(),
                    [&](const ColMeta &c) { return c.name == cond.lhs_col.col_name; }));
            }
            auto &right_cols = right_->cols();
            auto rit = std::find_if(right_cols.begin(), right_cols.end(),
                [&](const ColMeta &c) { return c.name == cond.rhs_col.col_name &&
                                               c.tab_name == cond.rhs_col.tab_name; });
            if (rit != right_cols.end()) {
                rhs_data = right_rec->data + rit->offset;
            } else {
                rhs_data = left_rec_->data + std::find_if(left_cols.begin(), left_cols.end(),
                    [&](const ColMeta &c) { return c.name == cond.rhs_col.col_name; })->offset;
            }
            // 比较
            ColType type = (lit != left_cols.end()) ? lit->type : std::find_if(right_->cols().begin(), right_->cols().end(),
                [&](const ColMeta &c) { return c.name == cond.lhs_col.col_name; })->type;
            if (type == TYPE_INT) {
                int lv = *(int *)lhs_data, rv = *(int *)rhs_data;
                switch (cond.op) {
                    case OP_EQ: if (lv != rv) return false; break;
                    case OP_NE: if (lv == rv) return false; break;
                    case OP_LT: if (!(lv < rv)) return false; break;
                    case OP_GT: if (!(lv > rv)) return false; break;
                    case OP_LE: if (!(lv <= rv)) return false; break;
                    case OP_GE: if (!(lv >= rv)) return false; break;
                }
            } else if (type == TYPE_FLOAT) {
                float lv = *(float *)lhs_data, rv = *(float *)rhs_data;
                switch (cond.op) {
                    case OP_EQ: if (lv != rv) return false; break;
                    case OP_NE: if (lv == rv) return false; break;
                    case OP_LT: if (!(lv < rv)) return false; break;
                    case OP_GT: if (!(lv > rv)) return false; break;
                    case OP_LE: if (!(lv <= rv)) return false; break;
                    case OP_GE: if (!(lv >= rv)) return false; break;
                }
            } else if (type == TYPE_STRING) {
                std::string ls(lhs_data, std::find_if(left_cols.begin(), left_cols.end(),
                    [&](const ColMeta &c) { return c.name == cond.lhs_col.col_name; })->len);
                ls.resize(strlen(ls.c_str()));
                std::string rs(rhs_data, std::find_if(left_cols.begin(), left_cols.end(),
                    [&](const ColMeta &c) { return c.name == cond.lhs_col.col_name; })->len);
                if (rit == right_cols.end()) rs = std::string(rhs_data, std::find_if(left_cols.begin(), left_cols.end(),
                    [&](const ColMeta &c) { return c.name == cond.rhs_col.col_name; })->len);
                rs.resize(strlen(rs.c_str()));
                switch (cond.op) {
                    case OP_EQ: if (ls != rs) return false; break;
                    case OP_NE: if (ls == rs) return false; break;
                    case OP_LT: if (!(ls < rs)) return false; break;
                    case OP_GT: if (!(ls > rs)) return false; break;
                    case OP_LE: if (!(ls <= rs)) return false; break;
                    case OP_GE: if (!(ls >= rs)) return false; break;
                }
            }
        }
        return true;
    }

    bool eval_cond_val(const Condition &cond, char *lhs_data, const ColMeta &col) {
        if (col.type == TYPE_INT) {
            int lv = *(int *)lhs_data, rv = cond.rhs_val.int_val;
            switch (cond.op) {
                case OP_EQ: return lv == rv;
                case OP_NE: return lv != rv;
                case OP_LT: return lv < rv;
                case OP_GT: return lv > rv;
                case OP_LE: return lv <= rv;
                case OP_GE: return lv >= rv;
            }
        } else if (col.type == TYPE_FLOAT) {
            float lv = *(float *)lhs_data, rv = cond.rhs_val.float_val;
            switch (cond.op) {
                case OP_EQ: return lv == rv;
                case OP_NE: return lv != rv;
                case OP_LT: return lv < rv;
                case OP_GT: return lv > rv;
                case OP_LE: return lv <= rv;
                case OP_GE: return lv >= rv;
            }
        } else if (col.type == TYPE_STRING) {
            std::string ls(lhs_data, col.len);
            ls.resize(strlen(ls.c_str()));
            std::string rs = cond.rhs_val.str_val;
            switch (cond.op) {
                case OP_EQ: return ls == rs;
                case OP_NE: return ls != rs;
                case OP_LT: return ls < rs;
                case OP_GT: return ls > rs;
                case OP_LE: return ls <= rs;
                case OP_GE: return ls >= rs;
            }
        }
        return false;
    }
};