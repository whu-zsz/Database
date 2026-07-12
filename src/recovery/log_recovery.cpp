/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#include "log_recovery.h"
#include "record/rm_file_handle.h"
#include "record/bitmap.h"
#include <cstring>
#include <set>
#include <vector>

static LogRecord* deserialize_log(const char* data) {
    LogType type = *reinterpret_cast<const LogType*>(data);
    switch (type) {
        case LogType::begin:    { auto* r = new BeginLogRecord(); r->deserialize(data); return r; }
        case LogType::commit:   { auto* r = new CommitLogRecord(); r->deserialize(data); return r; }
        case LogType::ABORT:    { auto* r = new AbortLogRecord(); r->deserialize(data); return r; }
        case LogType::INSERT:   { auto* r = new InsertLogRecord(); r->deserialize(data); return r; }
        case LogType::DELETE:   { auto* r = new DeleteLogRecord(); r->deserialize(data); return r; }
        case LogType::UPDATE:   { auto* r = new UpdateLogRecord(); r->deserialize(data); return r; }
        default: return nullptr;
    }
}

// 读取整个日志文件到一个缓冲区
static std::vector<char> read_all_logs(DiskManager *dm) {
    std::vector<char> all_log;
    std::vector<char> buf(LOG_BUFFER_SIZE);
    int offset = 0, bytes_read;
    while ((bytes_read = dm->read_log(buf.data(), LOG_BUFFER_SIZE, offset)) > 0) {
        all_log.insert(all_log.end(), buf.data(), buf.data() + bytes_read);
        offset += bytes_read;
    }
    return all_log;
}

// 在完整的日志缓冲区上扫描事务状态
static void scan_log_status(const std::vector<char> &log_data, std::set<txn_id_t> &committed, std::set<txn_id_t> &active) {
    int pos = 0;
    int total = log_data.size();
    while (pos + LOG_HEADER_SIZE <= total) {
        LogRecord* rec = deserialize_log(log_data.data() + pos);
        if (rec == nullptr) break;
        if (rec->log_tot_len_ <= 0 || pos + (int)rec->log_tot_len_ > total) {
            delete rec;
            break;
        }
        uint32_t rec_len = rec->log_tot_len_;
        switch (rec->log_type_) {
            case LogType::begin:   active.insert(rec->log_tid_); break;
            case LogType::commit:  active.erase(rec->log_tid_); committed.insert(rec->log_tid_); break;
            case LogType::ABORT:   active.erase(rec->log_tid_); break;
            default: break;
        }
        delete rec;
        pos += rec_len;
    }
}

// 检查 rid 处是否有记录，手动 unpin 避免 is_record 泄漏 pin
// page 不存在时返回 false（不抛异常），让调用方创建页面后重试
static bool check_record(RmFileHandle *fh, const Rid &rid, BufferPoolManager *bpm) {
    try {
        auto ph = fh->fetch_page_handle(rid.page_no);
        bool ok = Bitmap::is_set(ph.bitmap, rid.slot_no);
        bpm->unpin_page(ph.page->get_page_id(), false);
        return ok;
    } catch (PageNotExistError &) {
        return false;
    }
}

// 确保页面存在：catch PageNotExistError 后循环创建
static bool ensure_page_and_insert(RmFileHandle *fh, const Rid &rid, char *buf, BufferPoolManager *bpm) {
    try {
        fh->insert_record(rid, buf);
        return true;
    } catch (PageNotExistError &) {
        int need = rid.page_no + 1;
        while (fh->get_file_hdr().num_pages < need) {
            auto ph = fh->create_new_page_handle();
            bpm->unpin_page(ph.page->get_page_id(), true);
        }
        fh->insert_record(rid, buf);
        return true;
    } catch (std::exception &) { return false; }
}

void RecoveryManager::analyze() {
    auto log_data = read_all_logs(disk_manager_);
    std::set<txn_id_t> committed, active;
    scan_log_status(log_data, committed, active);
}

void RecoveryManager::redo() {
    auto log_data = read_all_logs(disk_manager_);
    std::set<txn_id_t> committed, active;
    scan_log_status(log_data, committed, active);
    if (committed.empty()) return;

    int pos = 0;
    int total = log_data.size();
    while (pos + LOG_HEADER_SIZE <= total) {
        LogRecord* rec = deserialize_log(log_data.data() + pos);
        if (rec == nullptr) break;
        if (rec->log_tot_len_ <= 0 || pos + (int)rec->log_tot_len_ > total) {
            delete rec;
            break;
        }
        uint32_t rec_len = rec->log_tot_len_;

        if (committed.count(rec->log_tid_)) {
            try {
                // 只恢复数据，索引在 undo 完成后统一重建
                if (rec->log_type_ == LogType::INSERT) {
                    auto* ir = static_cast<InsertLogRecord*>(rec);
                    std::string tname(ir->table_name_, ir->table_name_size_);
                    auto it = sm_manager_->fhs_.find(tname);
                    if (it != sm_manager_->fhs_.end()) {
                        auto fh = it->second.get();
                        if (!check_record(fh, ir->rid_, buffer_pool_manager_))
                            ensure_page_and_insert(fh, ir->rid_, ir->insert_value_.data, buffer_pool_manager_);
                    }
                } else if (rec->log_type_ == LogType::DELETE) {
                    auto* dr = static_cast<DeleteLogRecord*>(rec);
                    std::string tname(dr->table_name_, dr->table_name_size_);
                    auto it = sm_manager_->fhs_.find(tname);
                    if (it != sm_manager_->fhs_.end()) {
                        auto fh = it->second.get();
                        if (check_record(fh, dr->rid_, buffer_pool_manager_))
                            fh->delete_record(dr->rid_, nullptr);
                    }
                } else if (rec->log_type_ == LogType::UPDATE) {
                    auto* ur = static_cast<UpdateLogRecord*>(rec);
                    std::string tname(ur->table_name_, ur->table_name_size_);
                    auto it = sm_manager_->fhs_.find(tname);
                    if (it != sm_manager_->fhs_.end()) {
                        auto fh = it->second.get();
                        if (check_record(fh, ur->rid_, buffer_pool_manager_))
                            fh->update_record(ur->rid_, ur->new_value_.data, nullptr);
                    }
                }
            } catch (std::exception &) { /* skip failed operation */ }
        }

        delete rec;
        pos += rec_len;
    }
}

void RecoveryManager::undo() {
    auto log_data = read_all_logs(disk_manager_);
    std::set<txn_id_t> committed, active;
    scan_log_status(log_data, committed, active);
    if (active.empty()) return;

    std::vector<LogRecord*> undo_logs;
    {
        int pos = 0;
        int total = log_data.size();
        while (pos + LOG_HEADER_SIZE <= total) {
            LogRecord* rec = deserialize_log(log_data.data() + pos);
            if (rec == nullptr) break;
            if (rec->log_tot_len_ <= 0 || pos + (int)rec->log_tot_len_ > total) {
                delete rec;
                break;
            }
            uint32_t rec_len = rec->log_tot_len_;
            if (active.count(rec->log_tid_)) {
                if (rec->log_type_ == LogType::INSERT ||
                    rec->log_type_ == LogType::DELETE ||
                    rec->log_type_ == LogType::UPDATE) {
                    undo_logs.push_back(rec);
                } else {
                    delete rec;
                }
            } else {
                delete rec;
            }
            pos += rec_len;
        }
    }

    for (auto it = undo_logs.rbegin(); it != undo_logs.rend(); ++it) {
        LogRecord* rec = *it;
        try {
            // 只恢复数据，索引最后统一重建
            if (rec->log_type_ == LogType::INSERT) {
                auto* ir = static_cast<InsertLogRecord*>(rec);
                std::string tname(ir->table_name_, ir->table_name_size_);
                auto fh_it = sm_manager_->fhs_.find(tname);
                if (fh_it != sm_manager_->fhs_.end()) {
                    auto fh = fh_it->second.get();
                    if (check_record(fh, ir->rid_, buffer_pool_manager_))
                        fh->delete_record(ir->rid_, nullptr);
                }
            } else if (rec->log_type_ == LogType::DELETE) {
                auto* dr = static_cast<DeleteLogRecord*>(rec);
                std::string tname(dr->table_name_, dr->table_name_size_);
                auto fh_it = sm_manager_->fhs_.find(tname);
                if (fh_it != sm_manager_->fhs_.end()) {
                    auto fh = fh_it->second.get();
                    if (!check_record(fh, dr->rid_, buffer_pool_manager_))
                        ensure_page_and_insert(fh, dr->rid_, dr->old_value_.data, buffer_pool_manager_);
                }
            } else if (rec->log_type_ == LogType::UPDATE) {
                auto* ur = static_cast<UpdateLogRecord*>(rec);
                std::string tname(ur->table_name_, ur->table_name_size_);
                auto fh_it = sm_manager_->fhs_.find(tname);
                if (fh_it != sm_manager_->fhs_.end()) {
                    auto fh = fh_it->second.get();
                    if (check_record(fh, ur->rid_, buffer_pool_manager_))
                        fh->update_record(ur->rid_, ur->old_value_.data, nullptr);
                }
            }
        } catch (std::exception &) { /* skip */ }
    }

    for (auto* rec : undo_logs) delete rec;
}
