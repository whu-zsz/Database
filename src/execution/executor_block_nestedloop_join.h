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
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class BlockNestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子（外层，分块读取）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子（内层，每块重新扫描）
    size_t len_;
    std::vector<ColMeta> cols_;
    std::vector<Condition> fed_conds_;          // join条件
    bool isend;

    int block_size_;                                 // 每个块缓存的左表元组数
    std::vector<std::unique_ptr<RmRecord>> block_;  // 当前缓存的左表元组
    size_t block_idx_;                               // 块内当前位置
    std::unique_ptr<RmRecord> right_rec_;            // 当前右表元组

   public:
    BlockNestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                                std::vector<Condition> conds) {
        // 先读取子节点信息
        len_ = left->tupleLen() + right->tupleLen();
        cols_ = left->cols();
        auto right_cols = right->cols();
        for (auto &col : right_cols) {
            col.offset += left->tupleLen();
        }
        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        fed_conds_ = std::move(conds);
        block_size_ = JOIN_BUFFER_SIZE / left->tupleLen();
        if (block_size_ < 1) block_size_ = 1;
        left_ = std::move(left);
        right_ = std::move(right);
        isend = false;
        block_idx_ = 0;
    }

    void beginTuple() override {
        isend = false;
        left_->beginTuple();
        load_block();
        if (block_.empty()) {
            isend = true;
            return;
        }
        block_idx_ = 0;
        right_->beginTuple();
        // 定位到第一个满足条件的匹配对
        if (!find_match()) {
            isend = true;
        }
    }

    void nextTuple() override {
        advance();
    }

    bool is_end() const override { return isend; }

    std::unique_ptr<RmRecord> Next() override {
        auto joined = std::make_unique<RmRecord>(len_);
        memcpy(joined->data, block_[block_idx_]->data, left_->tupleLen());
        memcpy(joined->data + left_->tupleLen(), right_rec_->data, right_->tupleLen());
        return joined;
    }

    const std::vector<ColMeta> &cols() const override { return cols_; }
    size_t tupleLen() const override { return len_; }
    Rid &rid() override { return _abstract_rid; }

   private:
    // 从 left_ 加载一批元组到 block_
    void load_block() {
        block_.clear();
        for (int i = 0; i < block_size_ && !left_->is_end(); i++, left_->nextTuple()) {
            auto tuple = left_->Next();
            if (tuple != nullptr) {
                block_.push_back(std::move(tuple));
            }
        }
    }

    // 前进：block_idx_++，若 block 耗尽则 right 前进一步，若 right 耗尽则换下一块
    void advance() {
        if (isend) return;
        block_idx_++;
        while (block_idx_ >= block_.size()) {
            right_->nextTuple();
            if (right_->is_end()) {
                load_block();
                if (block_.empty()) {
                    isend = true;
                    return;
                }
                right_->beginTuple();
            }
            block_idx_ = 0;
        }
        // 拿到当前 right 元组
        right_rec_ = right_->Next();
        if (!check_current_match()) {
            if (!find_match()) {
                isend = true;
            }
        }
    }

    // 从当前位置开始找下一个匹配对
    bool find_match() {
        while (!isend) {
            if (right_->is_end()) {
                load_block();
                if (block_.empty()) return false;
                block_idx_ = 0;
                right_->beginTuple();
            }
            right_rec_ = right_->Next();
            if (check_current_match()) return true;
            block_idx_++;
            if (block_idx_ >= block_.size()) {
                right_->nextTuple();
                block_idx_ = 0;
            }
        }
        return false;
    }

    // 检查当前 (block_[block_idx_], right_rec_) 是否满足所有条件
    bool check_current_match() {
        if (block_idx_ >= block_.size() || right_rec_ == nullptr) return false;
        for (auto &cond : fed_conds_) {
            char *lhs_data = nullptr;
            char *rhs_data = nullptr;
            // 在 left 表中找 lhs
            auto &lcols = left_->cols();
            for (auto &c : lcols) {
                if (c.name == cond.lhs_col.col_name && c.tab_name == cond.lhs_col.tab_name) {
                    lhs_data = block_[block_idx_]->data + c.offset;
                    break;
                }
            }
            // 若不在 left，则在 right 中找
            auto &rcols = right_->cols();
            if (lhs_data == nullptr) {
                for (auto &c : rcols) {
                    if (c.name == cond.lhs_col.col_name) {
                        lhs_data = right_rec_->data + c.offset;
                        break;
                    }
                }
            }
            // 处理值比较的 rhs
            if (cond.is_rhs_val) {
                // 值比较已在 scan 算子中下推，不应出现在 join 条件中
                continue;
            }
            // 在 right 表中找 rhs
            for (auto &c : rcols) {
                if (c.name == cond.rhs_col.col_name && c.tab_name == cond.rhs_col.tab_name) {
                    rhs_data = right_rec_->data + c.offset;
                    break;
                }
            }
            if (rhs_data == nullptr) {
                for (auto &c : lcols) {
                    if (c.name == cond.rhs_col.col_name) {
                        rhs_data = block_[block_idx_]->data + c.offset;
                        break;
                    }
                }
            }
            // 类型比较
            ColType type = TYPE_INT;
            for (auto &c : lcols) {
                if (c.name == cond.lhs_col.col_name) { type = c.type; break; }
            }
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
            } else if (type == TYPE_BIGINT || type == TYPE_DATETIME) {
                int64_t lv = *(int64_t *)lhs_data, rv = *(int64_t *)rhs_data;
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
                int lhs_len = 0;
                for (auto &c : lcols) if (c.name == cond.lhs_col.col_name) { lhs_len = c.len; break; }
                if (lhs_len == 0) {
                    for (auto &c : rcols) if (c.name == cond.lhs_col.col_name) { lhs_len = c.len; break; }
                }
                std::string ls(lhs_data, lhs_len); ls.resize(strlen(ls.c_str()));
                int rhs_len = 0;
                for (auto &c : rcols) if (c.name == cond.rhs_col.col_name) { rhs_len = c.len; break; }
                if (rhs_len == 0) {
                    for (auto &c : lcols) if (c.name == cond.rhs_col.col_name) { rhs_len = c.len; break; }
                }
                std::string rs(rhs_data, rhs_len); rs.resize(strlen(rs.c_str()));
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
};
