# RMDB 数据库系统实现

> 基于 RMDB 框架的填空式开发项目，实现一个简易 DBMS 的核心模块。
> 课程：数据库系统实现（2026 三小学期）

## 当前进度

### ✅ 已完成

#### 1. 存储引擎 (Storage Engine)

| 文件 | 说明 |
|---|---|
| [src/storage/disk_manager.cpp](src/storage/disk_manager.cpp) | 磁盘页读写、文件创建/打开/关闭/删除 |
| [src/storage/buffer_pool_manager.cpp](src/storage/buffer_pool_manager.cpp) | 缓冲池管理：`fetch_page`、`new_page`、`unpin_page`、`flush_page`、`delete_page`、`flush_all_pages` |
| [src/replacer/lru_replacer.cpp](src/replacer/lru_replacer.cpp) | LRU 淘汰策略：`victim`、`pin`、`unpin` |

#### 2. 记录管理 (Record Management)

| 文件 | 说明 |
|---|---|
| [src/record/rm_file_handle.cpp](src/record/rm_file_handle.cpp) | 记录增删改查 + 空闲页链 + Bitmap 管理 |
| [src/record/rm_scan.cpp](src/record/rm_scan.cpp) | 全表顺序扫描迭代器 |

#### 3. 元数据与 DDL (System Management)

| 文件 | 说明 |
|---|---|
| [src/system/sm_manager.cpp](src/system/sm_manager.cpp) | `open_db`、`close_db`、`drop_table`、`create_index`、`drop_index` |

#### 4. 查询执行器 (Query Executors)

| 文件 | 说明 |
|---|---|
| [src/execution/executor_seq_scan.h](src/execution/executor_seq_scan.h) | 全表扫描 + WHERE 条件过滤（INT/FLOAT/STRING，6 种比较符） |
| [src/execution/executor_insert.h](src/execution/executor_insert.h) | 插入记录 + 同步索引 |
| [src/execution/executor_delete.h](src/execution/executor_delete.h) | 删除记录 + 同步索引 |
| [src/execution/executor_update.h](src/execution/executor_update.h) | 更新记录 + 索引同步（先删后插） |
| [src/execution/executor_projection.h](src/execution/executor_projection.h) | 列投影 |
| [src/execution/executor_nestedloop_join.h](src/execution/executor_nestedloop_join.h) | 嵌套循环连接（支持等值/非等值/笛卡尔积） |
| [src/execution/execution_sort.h](src/execution/execution_sort.h) | 多列 ORDER BY + LIMIT（INT/BIGINT/FLOAT/DATETIME/STRING, ASC/DESC） |
| [src/execution/executor_aggregate.h](src/execution/executor_aggregate.h) | 聚合函数 SUM/MAX/MIN/COUNT，支持 COUNT(*)，AS 别名 |
| [src/execution/executor_block_nestedloop_join.h](src/execution/executor_block_nestedloop_join.h) | 块嵌套循环连接，`JOIN_BUFFER_SIZE` 可配，对非等值连接自动选用 |

#### 8. 语法扩展 (题目六～八: 聚合/ORDER BY+LIMIT/BNLJ)

| 文件 | 说明 |
|---|---|
| [src/parser/ast.h](src/parser/ast.h) | `OrderBy` 支持多列排序键 |
| [src/parser/yacc.y](src/parser/yacc.y) | `order_clause` 递归解析多列, `opt_limit_clause` 规则 |
| [src/optimizer/plan.h](src/optimizer/plan.h) | `SortPlan` 支持多排序键 |
| [src/optimizer/planner.cpp](src/optimizer/planner.cpp) | `generate_sort_plan` 遍历所有排序键 |
| [src/execution/execution_sort.h](src/execution/execution_sort.h) | `SortExecutor` 多列排序实现 |
| [src/portal.h](src/portal.h) | 多列排序 + Aggregate + BNLJ wired |
| [src/common/config.h](src/common/config.h) | `JOIN_BUFFER_SIZE` BNLJ 缓冲区大小 |

#### 9. BNLJ 块嵌套循环连接

| 文件 | 说明 |
|---|---|
| [src/execution/executor_block_nestedloop_join.h](src/execution/executor_block_nestedloop_join.h) | BNLJ 执行器，`JOIN_BUFFER_SIZE` 字节 → 元组数动态计算 |
| [src/optimizer/plan.h](src/optimizer/plan.h) | PlanTag 新增 `T_BlockNestLoop` |
| [src/optimizer/planner.cpp](src/optimizer/planner.cpp) | `choose_join_type()` 检测非等值条件 → 自动选 BNLJ |
| [src/common/config.h](src/common/config.h) | `JOIN_BUFFER_SIZE = 1MB`（平台 2GB 限制内可调） |

#### 5. 数据类型扩展 (已完成 题目三/四)

| 文件 | 说明 |
|---|---|
| [src/common/common.h](src/common/common.h) | `is_valid_datetime()` 严格校验 + `datetime_to_str()` 输出, BIGINT range check |
| [src/execution/execution_manager.cpp](src/execution/execution_manager.cpp) | BIGINT/DATETIME 输出格式化 |
| [src/execution/executor_insert.h](src/execution/executor_insert.h) | INT→BIGINT 提升, STRING→DATETIME 转换校验 |
| [src/execution/executor_update.h](src/execution/executor_update.h) | UPDATE 中的类型转换 |
| [src/execution/executor_seq_scan.h](src/execution/executor_seq_scan.h) | BIGINT/DATETIME 条件比较 (`cmp_bigint`) |
| [src/parser/lex.l](src/parser/lex.l) | BIGINT 溢出检测 (`g_bigint_overflow`) |
| [src/rmdb.cpp](src/rmdb.cpp) | BIGINT overflow → `failure` 输出 |

#### 10. 日志与故障恢复 (题目十一)

| 文件 | 说明 |
|---|---|
| [src/recovery/log_manager.h](src/recovery/log_manager.h) | 6 种 LogRecord 类型（Begin/Commit/Abort/Insert/Delete/Update） |
| [src/recovery/log_manager.cpp](src/recovery/log_manager.cpp) | `add_log_to_buffer` + `flush_log_to_disk`, WAL 协议 |
| [src/recovery/log_recovery.cpp](src/recovery/log_recovery.cpp) | `analyze` + `redo`(只重做已提交) + `undo`(逆序回滚) |
| [src/transaction/transaction_manager.cpp](src/transaction/transaction_manager.cpp) | begin/commit/abort 写日志 |
| [src/execution/executor_insert.h](src/execution/executor_insert.h) | INSERT 写 InsertLogRecord |
| [src/execution/executor_delete.h](src/execution/executor_delete.h) | DELETE 写 DeleteLogRecord（保存旧值） |
| [src/execution/executor_update.h](src/execution/executor_update.h) | UPDATE 写 UpdateLogRecord（保存新旧值） |
| [src/system/sm_manager.cpp](src/system/sm_manager.cpp) | `rebuild_indexes` 恢复后全量重建索引 |
| [src/storage/disk_manager.cpp](src/storage/disk_manager.cpp) | `read_log` assert→安全返回（防止不完整日志 abort） |
| [src/rmdb.cpp](src/rmdb.cpp) | 恢复阶段多层 try-catch + catch-all 防护 |

### ❌ 待实现

| 优先级 | 模块 | 涉及文件 |
|---|---|---|
| 1 | B+ 树索引核心算法 | [src/index/ix_index_handle.cpp](src/index/ix_index_handle.cpp) |
| 2 | LockManager | 2PL + No-Wait 死锁预防 |

## 项目结构

```
rmdb/
├── src/
│   ├── analyze/      # 语义分析 ✅ (UpdateStmt 已补全)
│   ├── common/       # 公共定义 (config, context, Value, Condition)
│   ├── execution/    # 执行器 (火山模型算子) ✅ 9 个算子
│   ├── index/        # B+ 树索引 (框架完成，核心算法待实现)
│   ├── optimizer/    # 查询优化器 (框架完成)
│   ├── parser/       # 词法/语法分析 (flex/bison)
│   ├── record/       # 记录管理 ✅
│   ├── recovery/     # 日志与故障恢复 ✅
│   ├── replacer/     # 缓存淘汰策略 ✅
│   ├── storage/      # 磁盘与缓冲池管理 ✅
│   ├── system/       # 元数据管理 ✅
│   ├── test/         # 测试 ✅ (query_test 已配置)
│   └── transaction/  # 事务基础 ✅ (锁定/恢复待实现)
├── deps/             # 依赖 (googletest)
├── build/            # 构建产物 (gitignore 排除)
├── CLAUDE.md         # 完整项目指南
└── README.md         # 本文件
```

## 构建与测试

```bash
cd build
cmake ..
make -j4

# 单元测试
./bin/unit_test

# 本地 SQL 测试（题目一到四）
python3 src/test/query/query_test_basic.py
```

要求：Ubuntu ≥ 20.04, CMake ≥ 3.16, GCC ≥ 7.1 (C++17), flex, bison, libreadline-dev

## 存储层架构

```
                    ┌──────────────────────┐
                    │   RmFileHandle       │  ← 记录增删改查, bitmap 管理
                    │   RmScan             │  ← 全表顺序扫描
                    └──────────┬───────────┘
                               │ fetch_page / new_page / unpin_page
                    ┌──────────▼───────────┐
                    │  BufferPoolManager   │  ← 缓冲池, page_table 映射
                    │  find_victim_page    │
                    └────┬──────────┬──────┘
                         │          │ victim / pin / unpin
              ┌──────────▼──┐  ┌───▼──────────┐
              │ DiskManager │  │ LRUReplacer  │  ← LRU 淘汰 (首部 MRU, 尾部 LRU)
              │ read/write  │  │ LRUlist_     │
              │ lseek+read  │  │ LRUhash_     │
              │ /write      │  └──────────────┘
              └──────┬──────┘
                     │
              ┌──────▼──────┐
              │  磁盘文件    │  (.db 数据, .meta 元数据, .log 日志)
              └─────────────┘
```

## 执行器数据流

```
SELECT: SeqScan(beginTuple过滤→Next取记录) → Projection(Next投影) → output.txt
INSERT: InsertExecutor(Next写记录+同步索引)
DELETE: SeqScan(收rids, rid_在beginTuple/nextTuple中设置)
        → DeleteExecutor(Next删记录+同步索引)
UPDATE: SeqScan(收rids) → UpdateExecutor(Next: 旧索引删→更新→新索引插)
JOIN:  SeqScan(left) + SeqScan(right) → NestedLoopJoin(双循环+条件匹配)
```
