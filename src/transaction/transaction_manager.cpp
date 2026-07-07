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
#include "record/rm_file_handle.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

/**
 * @description: 事务的开始方法
 * @return {Transaction*} 开始事务的指针
 * @param {Transaction*} txn 事务指针，空指针代表需要创建新事务，否则开始已有事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
Transaction * TransactionManager::begin(Transaction* txn, LogManager* log_manager) {
    // 1. 判断传入事务参数是否为空指针
    // 2. 如果为空指针，创建新事务
    // 3. 把开始事务加入到全局事务表中
    // 4. 返回当前事务指针
    std::unique_lock<std::mutex> lock(latch_);
    if (txn == nullptr) {
        txn_id_t txn_id = next_txn_id_++;
        timestamp_t start_ts = next_timestamp_++;
        txn = new Transaction(txn_id);
        txn->set_start_ts(start_ts);
        txn->set_state(TransactionState::GROWING);
        TransactionManager::txn_map[txn_id] = txn;
    }
    return txn;
}

/**
 * @description: 事务的提交方法
 * @param {Transaction*} txn 需要提交的事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
void TransactionManager::commit(Transaction* txn, LogManager* log_manager) {
    // 1. 如果存在未提交的写操作，提交所有的写操作
    // (写操作已通过RmFileHandle直接写入磁盘，无需额外处理)
    // 2. 释放所有锁
    // (基础查询测试不涉及锁)
    // 3. 释放事务相关资源
    // 4. 把事务日志刷入磁盘中
    // (日志模块未完整实现，跳过)
    // 5. 更新事务状态（保留在txn_map中，下次SetTransaction会检测COMMITTED并建新事务）
    std::unique_lock<std::mutex> lock(latch_);
    txn->set_state(TransactionState::COMMITTED);
}

/**
 * @description: 事务的终止（回滚）方法
 * @param {Transaction *} txn 需要回滚的事务
 * @param {LogManager} *log_manager 日志管理器指针
 */
void TransactionManager::abort(Transaction * txn, LogManager *log_manager) {
    // 1. 回滚所有写操作
    // (通过undo日志回滚，日志模块未完整实现)
    // 2. 释放所有锁
    // 3. 清空事务相关资源
    // 4. 把事务日志刷入磁盘中
    // 5. 更新事务状态（保留在txn_map中）
    std::unique_lock<std::mutex> lock(latch_);
    txn->set_state(TransactionState::ABORTED);
}