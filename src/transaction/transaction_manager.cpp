/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"
#include <cstring>
#include <vector>
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

namespace {
std::vector<char> make_index_key(const IndexMeta &index, const RmRecord &record) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (size_t i = 0; i < index.cols.size(); ++i) {
        memcpy(key.data() + offset, record.data + index.cols[i].offset, index.cols[i].len);
        offset += index.cols[i].len;
    }
    return key;
}

void insert_indexes(SmManager *sm_manager, const std::string &tab_name, const TabMeta &tab,
                    const RmRecord &record, const Rid &rid, Transaction *txn) {
    for (const auto &index : tab.indexes) {
        auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        auto key = make_index_key(index, record);
        ih->insert_entry(key.data(), rid, txn);
    }
}

void delete_indexes(SmManager *sm_manager, const std::string &tab_name, const TabMeta &tab,
                    const RmRecord &record, Transaction *txn) {
    for (const auto &index : tab.indexes) {
        auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        auto key = make_index_key(index, record);
        ih->delete_entry(key.data(), txn);
    }
}
}

/**
 * @description: 事务的开始方法
 * @return {Transaction*} 开始事务的指针
 * @param {Transaction*} txn 事务指针，空指针代表需要创建新事务，否则开始已有事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
Transaction * TransactionManager::begin(Transaction* txn, LogManager* log_manager) {
    std::unique_lock<std::mutex> lock(latch_);
    if (txn == nullptr) {
        txn_id_t txn_id = next_txn_id_++;
        timestamp_t start_ts = next_timestamp_++;
        txn = new Transaction(txn_id);
        txn->set_start_ts(start_ts);
        txn->set_state(TransactionState::GROWING);
        TransactionManager::txn_map[txn_id] = txn;
        // 写 BEGIN 日志
        if (log_manager != nullptr) {
            BeginLogRecord begin_log(txn_id);
            log_manager->add_log_to_buffer(&begin_log);
        }
    }
    return txn;
}

/**
 * @description: 事务的提交方法
 * @param {Transaction*} txn 需要提交的事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
void TransactionManager::commit(Transaction* txn, LogManager* log_manager) {
    std::unique_lock<std::mutex> lock(latch_);

    // 写 COMMIT 日志并刷盘（WAL: commit日志必须先于数据落盘）
    if (log_manager != nullptr) {
        CommitLogRecord commit_log(txn->get_transaction_id());
        txn->set_prev_lsn(log_manager->add_log_to_buffer(&commit_log));
        log_manager->flush_log_to_disk();
    }

    // 释放所有锁
    lock_manager_->unlock_all(txn);

    // 清空写操作集
    for (auto *write_record : *txn->get_write_set()) {
        delete write_record;
    }
    txn->get_write_set()->clear();

    txn->set_state(TransactionState::COMMITTED);
}

/**
 * @description: 事务的终止（回滚）方法
 * @param {Transaction *} txn 需要回滚的事务
 * @param {LogManager} *log_manager 日志管理器指针
 */
void TransactionManager::abort(Transaction * txn, LogManager *log_manager) {
    std::unique_lock<std::mutex> lock(latch_);

    // 写 ABORT 日志并刷盘
    if (log_manager != nullptr) {
        AbortLogRecord abort_log(txn->get_transaction_id());
        txn->set_prev_lsn(log_manager->add_log_to_buffer(&abort_log));
        log_manager->flush_log_to_disk();
    }

    // 回滚写操作
    auto write_set = txn->get_write_set();
    while (!write_set->empty()) {
        WriteRecord *write_record = write_set->back();
        write_set->pop_back();

        const std::string &tab_name = write_record->GetTableName();
        TabMeta tab = sm_manager_->db_.get_table(tab_name);
        RmFileHandle *fh = sm_manager_->fhs_.at(tab_name).get();
        Rid rid = write_record->GetRid();

        if (write_record->GetWriteType() == WType::INSERT_TUPLE) {
            auto rec = fh->get_record(rid, nullptr);
            if (rec != nullptr) {
                delete_indexes(sm_manager_, tab_name, tab, *rec, txn);
                fh->delete_record(rid, nullptr);
            }
        } else if (write_record->GetWriteType() == WType::DELETE_TUPLE) {
            RmRecord &old_rec = write_record->GetRecord();
            fh->insert_record(rid, old_rec.data);
            insert_indexes(sm_manager_, tab_name, tab, old_rec, rid, txn);
        } else if (write_record->GetWriteType() == WType::UPDATE_TUPLE) {
            auto current_rec = fh->get_record(rid, nullptr);
            if (current_rec != nullptr) {
                delete_indexes(sm_manager_, tab_name, tab, *current_rec, txn);
            }
            RmRecord &old_rec = write_record->GetRecord();
            fh->update_record(rid, old_rec.data, nullptr);
            insert_indexes(sm_manager_, tab_name, tab, old_rec, rid, txn);
        }
        delete write_record;
    }

    // 释放所有锁
    lock_manager_->unlock_all(txn);

    txn->set_state(TransactionState::ABORTED);
}
