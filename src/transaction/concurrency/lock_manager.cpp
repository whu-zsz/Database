/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "lock_manager.h"

/**
 * @description: 申请行级共享锁
 */
bool LockManager::lock_shared_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    if (txn->get_state() == TransactionState::DEFAULT) {
        txn->set_state(TransactionState::GROWING);
    }

    LockDataId lock_data(tab_fd, rid, LockDataType::RECORD);
    auto& queue = lock_table_[lock_data];

    // 检查是否已持有锁
    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_) {
            return true;
        }
    }

    // S 与 IS、S 兼容
    if (queue.group_lock_mode_ == GroupLockMode::NON_LOCK ||
        queue.group_lock_mode_ == GroupLockMode::IS ||
        queue.group_lock_mode_ == GroupLockMode::S) {

        LockRequest req(txn->get_transaction_id(), LockMode::SHARED);
        req.granted_ = true;
        queue.request_queue_.push_back(req);
        queue.group_lock_mode_ = GroupLockMode::S;
        return true;
    }

    throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
}

/**
 * @description: 申请行级排他锁
 */
bool LockManager::lock_exclusive_on_record(Transaction* txn, const Rid& rid, int tab_fd) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    if (txn->get_state() == TransactionState::DEFAULT) {
        txn->set_state(TransactionState::GROWING);
    }

    LockDataId lock_data(tab_fd, rid, LockDataType::RECORD);
    auto& queue = lock_table_[lock_data];

    // 检查是否已持有 X 锁
    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_ &&
            req.lock_mode_ == LockMode::EXLUCSIVE) {
            return true;
        }
    }

    // 检查是否可以升级（S → X）
    bool has_my_s_lock = false;
    bool only_me = true;
    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id()) {
            if (req.granted_ && req.lock_mode_ == LockMode::SHARED) {
                has_my_s_lock = true;
            }
        } else if (req.granted_) {
            only_me = false;
        }
    }

    if (has_my_s_lock && only_me && !queue.upgrading_) {
        queue.upgrading_ = true;
        for (auto& req : queue.request_queue_) {
            if (req.txn_id_ == txn->get_transaction_id()) {
                req.lock_mode_ = LockMode::EXLUCSIVE;
                break;
            }
        }
        queue.group_lock_mode_ = GroupLockMode::X;
        queue.upgrading_ = false;
        return true;
    }

    // 无其他事务持有锁时可直接加 X 锁
    if (queue.group_lock_mode_ == GroupLockMode::NON_LOCK) {
        LockRequest req(txn->get_transaction_id(), LockMode::EXLUCSIVE);
        req.granted_ = true;
        queue.request_queue_.push_back(req);
        queue.group_lock_mode_ = GroupLockMode::X;
        return true;
    }

    throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
}

/**
 * @description: 申请表级读锁
 */
bool LockManager::lock_shared_on_table(Transaction* txn, int tab_fd) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    if (txn->get_state() == TransactionState::DEFAULT) {
        txn->set_state(TransactionState::GROWING);
    }

    LockDataId lock_data(tab_fd, LockDataType::TABLE);
    auto& queue = lock_table_[lock_data];

    // 检查是否已持有锁
    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_) {
            if (req.lock_mode_ == LockMode::SHARED ||
                req.lock_mode_ == LockMode::EXLUCSIVE ||
                req.lock_mode_ == LockMode::S_IX) {
                return true;  // 已持有S、X或SIX锁
            }
            if (req.lock_mode_ == LockMode::INTENTION_EXCLUSIVE) {
                // 已持有IX锁，升级为SIX
                req.lock_mode_ = LockMode::S_IX;
                queue.group_lock_mode_ = GroupLockMode::SIX;
                return true;
            }
            if (req.lock_mode_ == LockMode::INTENTION_SHARED) {
                // 已持有IS锁，升级为S
                req.lock_mode_ = LockMode::SHARED;
                queue.group_lock_mode_ = GroupLockMode::S;
                return true;
            }
        }
    }

    if (queue.group_lock_mode_ == GroupLockMode::NON_LOCK ||
        queue.group_lock_mode_ == GroupLockMode::IS ||
        queue.group_lock_mode_ == GroupLockMode::S) {

        LockRequest req(txn->get_transaction_id(), LockMode::SHARED);
        req.granted_ = true;
        queue.request_queue_.push_back(req);
        queue.group_lock_mode_ = GroupLockMode::S;
        return true;
    }

    throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
}

/**
 * @description: 申请表级写锁
 */
bool LockManager::lock_exclusive_on_table(Transaction* txn, int tab_fd) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    if (txn->get_state() == TransactionState::DEFAULT) {
        txn->set_state(TransactionState::GROWING);
    }

    LockDataId lock_data(tab_fd, LockDataType::TABLE);
    auto& queue = lock_table_[lock_data];

    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_ &&
            req.lock_mode_ == LockMode::EXLUCSIVE) {
            return true;
        }
    }

    if (queue.group_lock_mode_ == GroupLockMode::NON_LOCK) {
        LockRequest req(txn->get_transaction_id(), LockMode::EXLUCSIVE);
        req.granted_ = true;
        queue.request_queue_.push_back(req);
        queue.group_lock_mode_ = GroupLockMode::X;
        return true;
    }

    throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
}

/**
 * @description: 申请表级意向读锁
 */
bool LockManager::lock_IS_on_table(Transaction* txn, int tab_fd) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    if (txn->get_state() == TransactionState::DEFAULT) {
        txn->set_state(TransactionState::GROWING);
    }

    LockDataId lock_data(tab_fd, LockDataType::TABLE);
    auto& queue = lock_table_[lock_data];

    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_) {
            return true;
        }
    }

    // IS 锁与所有模式兼容
    LockRequest req(txn->get_transaction_id(), LockMode::INTENTION_SHARED);
    req.granted_ = true;
    queue.request_queue_.push_back(req);

    if (queue.group_lock_mode_ == GroupLockMode::NON_LOCK) {
        queue.group_lock_mode_ = GroupLockMode::IS;
    }

    return true;
}

/**
 * @description: 申请表级意向写锁
 */
bool LockManager::lock_IX_on_table(Transaction* txn, int tab_fd) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    if (txn->get_state() == TransactionState::SHRINKING) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::LOCK_ON_SHIRINKING);
    }
    if (txn->get_state() == TransactionState::DEFAULT) {
        txn->set_state(TransactionState::GROWING);
    }

    LockDataId lock_data(tab_fd, LockDataType::TABLE);
    auto& queue = lock_table_[lock_data];

    // 检查是否已持有锁
    for (auto& req : queue.request_queue_) {
        if (req.txn_id_ == txn->get_transaction_id() && req.granted_) {
            if (req.lock_mode_ == LockMode::INTENTION_EXCLUSIVE ||
                req.lock_mode_ == LockMode::EXLUCSIVE ||
                req.lock_mode_ == LockMode::S_IX) {
                return true;  // 已持有IX、X或SIX锁，兼容
            }
            if (req.lock_mode_ == LockMode::SHARED) {
                // 已持有S锁，需要升级为SIX
                // 检查是否有其他事务也持有S锁
                bool has_other_s = false;
                for (auto& other : queue.request_queue_) {
                    if (other.txn_id_ != txn->get_transaction_id() && other.granted_ &&
                        other.lock_mode_ == LockMode::SHARED) {
                        has_other_s = true;
                        break;
                    }
                }
                if (!has_other_s && !queue.upgrading_) {
                    // 升级 S → SIX
                    queue.upgrading_ = true;
                    req.lock_mode_ = LockMode::S_IX;
                    queue.group_lock_mode_ = GroupLockMode::SIX;
                    queue.upgrading_ = false;
                    return true;
                }
                // 有其他事务持有S锁，不能升级
                throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
            }
            if (req.lock_mode_ == LockMode::INTENTION_SHARED) {
                // 已持有IS锁，升级为IX
                req.lock_mode_ = LockMode::INTENTION_EXCLUSIVE;
                if (queue.group_lock_mode_ == GroupLockMode::IS) {
                    queue.group_lock_mode_ = GroupLockMode::IX;
                }
                return true;
            }
        }
    }

    // IX 与 IS、IX 兼容
    if (queue.group_lock_mode_ == GroupLockMode::NON_LOCK ||
        queue.group_lock_mode_ == GroupLockMode::IS ||
        queue.group_lock_mode_ == GroupLockMode::IX) {

        LockRequest req(txn->get_transaction_id(), LockMode::INTENTION_EXCLUSIVE);
        req.granted_ = true;
        queue.request_queue_.push_back(req);

        if (queue.group_lock_mode_ == GroupLockMode::NON_LOCK ||
            queue.group_lock_mode_ == GroupLockMode::IS) {
            queue.group_lock_mode_ = GroupLockMode::IX;
        }

        return true;
    }

    throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
}

/**
 * @description: 释放锁
 */
bool LockManager::unlock(Transaction* txn, LockDataId lock_data_id) {
    if (txn == nullptr) return true;

    std::unique_lock<std::mutex> lock(latch_);

    auto it = lock_table_.find(lock_data_id);
    if (it == lock_table_.end()) return true;

    auto& queue = it->second;
    auto& req_list = queue.request_queue_;

    // 移除该事务的锁请求
    for (auto req_it = req_list.begin(); req_it != req_list.end(); ++req_it) {
        if (req_it->txn_id_ == txn->get_transaction_id()) {
            req_list.erase(req_it);
            break;
        }
    }

    // 更新 group_lock_mode
    queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
    for (auto& req : req_list) {
        if (req.granted_) {
            switch (req.lock_mode_) {
                case LockMode::EXLUCSIVE:
                    queue.group_lock_mode_ = GroupLockMode::X;
                    break;
                case LockMode::SHARED:
                    if (queue.group_lock_mode_ < GroupLockMode::S)
                        queue.group_lock_mode_ = GroupLockMode::S;
                    break;
                case LockMode::INTENTION_EXCLUSIVE:
                    if (queue.group_lock_mode_ < GroupLockMode::IX)
                        queue.group_lock_mode_ = GroupLockMode::IX;
                    break;
                case LockMode::INTENTION_SHARED:
                    if (queue.group_lock_mode_ < GroupLockMode::IS)
                        queue.group_lock_mode_ = GroupLockMode::IS;
                    break;
                default:
                    break;
            }
        }
    }

    if (req_list.empty()) {
        lock_table_.erase(it);
    }

    return true;
}

/**
 * @description: 释放事务持有的所有锁
 */
void LockManager::unlock_all(Transaction* txn) {
    if (txn == nullptr) return;

    std::unique_lock<std::mutex> lock(latch_);

    // 遍历lock_table_，移除该事务的所有锁请求
    for (auto it = lock_table_.begin(); it != lock_table_.end(); ) {
        auto& queue = it->second;
        auto& req_list = queue.request_queue_;

        // 移除该事务的锁请求
        for (auto req_it = req_list.begin(); req_it != req_list.end(); ) {
            if (req_it->txn_id_ == txn->get_transaction_id()) {
                req_it = req_list.erase(req_it);
            } else {
                ++req_it;
            }
        }

        // 更新 group_lock_mode
        queue.group_lock_mode_ = GroupLockMode::NON_LOCK;
        for (auto& req : req_list) {
            if (req.granted_) {
                switch (req.lock_mode_) {
                    case LockMode::EXLUCSIVE:
                        queue.group_lock_mode_ = GroupLockMode::X;
                        break;
                    case LockMode::SHARED:
                        if (queue.group_lock_mode_ < GroupLockMode::S)
                            queue.group_lock_mode_ = GroupLockMode::S;
                        break;
                    case LockMode::INTENTION_EXCLUSIVE:
                        if (queue.group_lock_mode_ < GroupLockMode::IX)
                            queue.group_lock_mode_ = GroupLockMode::IX;
                        break;
                    case LockMode::INTENTION_SHARED:
                        if (queue.group_lock_mode_ < GroupLockMode::IS)
                            queue.group_lock_mode_ = GroupLockMode::IS;
                        break;
                    default:
                        break;
                }
            }
        }

        // 清理空队列
        if (req_list.empty()) {
            it = lock_table_.erase(it);
        } else {
            ++it;
        }
    }
}
