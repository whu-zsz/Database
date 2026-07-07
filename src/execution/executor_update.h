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

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }
    std::unique_ptr<RmRecord> Next() override {
        // 预先初始化所有 SET 子句的值（只需初始化一次，后续行复用）
        std::vector<std::tuple<int, char*, int>> set_vals;  // (col_offset, raw_data, col_len)
        for (auto &set_clause : set_clauses_) {
            auto col_it = std::find_if(tab_.cols.begin(), tab_.cols.end(),
                [&](const ColMeta &c) { return c.name == set_clause.lhs.col_name; });
            // Handle type conversions
            if (col_it->type == TYPE_DATETIME && set_clause.rhs.type == TYPE_STRING) {
                int64_t dt_val;
                if (!is_valid_datetime(set_clause.rhs.str_val, dt_val)) {
                    throw InternalError("Invalid datetime value");
                }
                set_clause.rhs.set_datetime(dt_val);
            } else if (col_it->type == TYPE_BIGINT && set_clause.rhs.type == TYPE_INT) {
                set_clause.rhs.set_bigint((int64_t)set_clause.rhs.int_val);
            }
            set_clause.rhs.init_raw(col_it->len);
            set_vals.push_back({col_it->offset, set_clause.rhs.raw->data, col_it->len});
        }
        for (auto &rid : rids_) {
            // 获取原记录
            auto old_rec = fh_->get_record(rid, context_);
            if (old_rec == nullptr) continue;
            // 从索引中删除旧记录
            for (size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto &index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(
                    sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char *key = new char[index.col_tot_len];
                int offset = 0;
                for (size_t j = 0; j < index.col_num; ++j) {
                    memcpy(key + offset, old_rec->data + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                ih->delete_entry(key, context_->txn_);
                delete[] key;
            }
            // 构建新记录：复制旧记录然后覆盖 SET 字段
            auto new_buf = new char[fh_->get_file_hdr().record_size];
            memcpy(new_buf, old_rec->data, fh_->get_file_hdr().record_size);
            for (auto &sv : set_vals) {
                memcpy(new_buf + std::get<0>(sv), std::get<1>(sv), std::get<2>(sv));
            }
            // 更新记录
            fh_->update_record(rid, new_buf, context_);
            // 插入新记录到索引
            for (size_t i = 0; i < tab_.indexes.size(); ++i) {
                auto &index = tab_.indexes[i];
                auto ih = sm_manager_->ihs_.at(
                    sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                char *key = new char[index.col_tot_len];
                int offset = 0;
                for (size_t j = 0; j < index.col_num; ++j) {
                    memcpy(key + offset, new_buf + index.cols[j].offset, index.cols[j].len);
                    offset += index.cols[j].len;
                }
                ih->insert_entry(key, rid, context_->txn_);
                delete[] key;
            }
            delete[] new_buf;
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};