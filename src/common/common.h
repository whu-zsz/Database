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

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "defs.h"
#include "record/rm_defs.h"


struct TabCol {
    std::string tab_name;
    std::string col_name;

    friend bool operator<(const TabCol &x, const TabCol &y) {
        return std::make_pair(x.tab_name, x.col_name) < std::make_pair(y.tab_name, y.col_name);
    }
};

struct Value {
    ColType type;  // type of value
    union {
        int int_val;        // int value
        float float_val;    // float value
        int64_t bigint_val; // bigint value (8 bytes)
    };
    std::string str_val;  // string value

    std::shared_ptr<RmRecord> raw;  // raw record buffer

    void set_int(int int_val_) {
        type = TYPE_INT;
        int_val = int_val_;
    }

    void set_float(float float_val_) {
        type = TYPE_FLOAT;
        float_val = float_val_;
    }

    void set_str(std::string str_val_) {
        type = TYPE_STRING;
        str_val = std::move(str_val_);
    }

    void set_bigint(int64_t bigint_val_) {
        type = TYPE_BIGINT;
        bigint_val = bigint_val_;
    }

    void set_datetime(int64_t datetime_val_) {
        type = TYPE_DATETIME;
        bigint_val = datetime_val_;
    }

    void init_raw(int len) {
        assert(raw == nullptr);
        raw = std::make_shared<RmRecord>(len);
        if (type == TYPE_INT) {
            if (len == sizeof(int64_t)) {
                // INT -> BIGINT promotion (when column is BIGINT)
                *(int64_t *)(raw->data) = (int64_t)int_val;
            } else {
                assert(len == sizeof(int));
                *(int *)(raw->data) = int_val;
            }
        } else if (type == TYPE_BIGINT) {
            assert(len == sizeof(int64_t));
            *(int64_t *)(raw->data) = bigint_val;
        } else if (type == TYPE_DATETIME) {
            assert(len == sizeof(int64_t));
            *(int64_t *)(raw->data) = bigint_val;
        } else if (type == TYPE_FLOAT) {
            assert(len == sizeof(float));
            *(float *)(raw->data) = float_val;
        } else if (type == TYPE_STRING) {
            if (len < (int)str_val.size()) {
                throw StringOverflowError();
            }
            memset(raw->data, 0, len);
            memcpy(raw->data, str_val.c_str(), str_val.size());
        }
    }
};

// DATETIME validation and conversion
// Format: 'YYYY-MM-DD HH:MM:SS' (19 chars)
// Stored as int64_t: YYYYMMDDHHMMSS
inline bool is_valid_datetime(const std::string& s, int64_t& result) {
    if (s.length() != 19) return false;
    if (s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':' || s[16] != ':') return false;

    // Parse components
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    for (int i = 0; i < 4; i++) {
        if (!isdigit(s[i])) return false;
        year = year * 10 + (s[i] - '0');
    }
    for (int i = 5; i < 7; i++) {
        if (!isdigit(s[i])) return false;
        month = month * 10 + (s[i] - '0');
    }
    for (int i = 8; i < 10; i++) {
        if (!isdigit(s[i])) return false;
        day = day * 10 + (s[i] - '0');
    }
    for (int i = 11; i < 13; i++) {
        if (!isdigit(s[i])) return false;
        hour = hour * 10 + (s[i] - '0');
    }
    for (int i = 14; i < 16; i++) {
        if (!isdigit(s[i])) return false;
        minute = minute * 10 + (s[i] - '0');
    }
    for (int i = 17; i < 19; i++) {
        if (!isdigit(s[i])) return false;
        second = second * 10 + (s[i] - '0');
    }

    // Validate range
    if (year < 1000 || year > 9999) return false;
    if (month < 1 || month > 12) return false;
    if (hour > 23 || minute > 59 || second > 59) return false;

    // Days per month
    int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    // Leap year
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        days_in_month[1] = 29;
    }
    if (day < 1 || day > days_in_month[month - 1]) return false;

    // Encode as YYYYMMDDHHMMSS
    result = (int64_t)year * 10000000000LL + (int64_t)month * 100000000LL +
             (int64_t)day * 1000000LL + (int64_t)hour * 10000LL +
             (int64_t)minute * 100LL + (int64_t)second;
    return true;
}

// Convert int64_t datetime back to string
inline std::string datetime_to_str(int64_t val) {
    int second = val % 100; val /= 100;
    int minute = val % 100; val /= 100;
    int hour = val % 100; val /= 100;
    int day = val % 100; val /= 100;
    int month = val % 100; val /= 100;
    int year = val;

    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
    return std::string(buf);
}

enum CompOp { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE };

struct Condition {
    TabCol lhs_col;   // left-hand side column
    CompOp op;        // comparison operator
    bool is_rhs_val;  // true if right-hand side is a value (not a column)
    TabCol rhs_col;   // right-hand side column
    Value rhs_val;    // right-hand side value
};

struct SetClause {
    TabCol lhs;
    Value rhs;
};