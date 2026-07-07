# RMDB 数据库系统实现 - 完整实践指南 (Claude.md)

> 本指南整合了《课设指导》、《RMDB项目结构》、《测试说明文档》和《框架图》，为 RMDB 填空式开发提供一站式参考。适用于 2026 年三小学期《数据库系统实现》课程。

---

## 0. 当前实现进度

### 已完成模块（题目一～四基础查询全部通过）

| 模块 | 已完成内容 |
|:---|:---|
| **Storage** | `DiskManager`: read/write_page, create/destroy/open/close_file |
| | `BufferPoolManager`: fetch/new/unpin/flush/delete/flush_all page + find_victim_page |
| | `LRUReplacer`: victim (尾部淘汰), pin (移除), unpin (首部插入, 重复 unpin 为 no-op) |
| **Record** | `RmFileHandle`: get/insert/delete/update record + fetch/create/release page handle |
| | `RmScan`: next/is_end/rid, bitmap 驱动的顺序扫描 |
| **System** | `SmManager`: open_db (加载 meta + 打开文件句柄), close_db (刷盘 + 清理), drop_table (删索引 + 删数据文件), create_index, drop_index |
| **Execution** | `SeqScanExecutor`: RmScan + check_conds (INT/FLOAT/STRING 6 种比较符), `rid_` 在 beginTuple/nextTuple 中设置 |
| | `InsertExecutor`: 框架已实现 (写记录 + 同步索引) |
| | `DeleteExecutor`: 遍历 rids_ → 删除索引条目 → delete_record |
| | `UpdateExecutor`: 遍历 rids_ → 旧索引删 → 构造新记录 → update_record → 新索引插 (init_raw 只调用一次, 不在行循环内) |
| | `ProjectionExecutor`: 委托子节点, Next() 中按 sel_idxs_ 投影裁剪 |
| | `NestedLoopJoinExecutor`: 双循环 + check_current_match (跨表字段查找 + 条件判断), 支持笛卡尔积/等值/非等值连接 |
| **Transaction** | `begin`: 新建 Transaction, 分配 txn_id/start_ts, 状态 GROWING, 加入 txn_map |
| | `commit`/`abort`: 设置 COMMITTED/ABORTED 状态 (保留在 txn_map 以便 SetTransaction 检测并建新事务) |
| **Analyze** | 补全 `UpdateStmt` 的 WHERE 条件提取 + SET 子句转换 |
| **Portal** | UPDATE 匹配 0 行时 throw InternalError 输出 `failure` |
| **Test** | `src/test/CMakeLists.txt` 添加 query_test 目标, `query_test_basic.py` 路径修复 |

### 待实现模块

| 优先级 | 模块 | 涉及内容 |
|:---|:---|:---|
| 1 | B+ 树索引核心 | `IxIndexHandle`: lower_bound, insert_entry, delete_entry, split, coalesce, redistribute 等 |
| 2 | BIGINT/DATETIME | 类型校验, 范围/格式检查 |
| 3 | Aggregate + OrderBy | SUM/MAX/MIN/COUNT, 多字段排序, LIMIT |
| 4 | BlockNLJ | 块嵌套循环连接 |
| 5 | LockManager | 2PL + No-Wait 死锁预防, 表级/行级锁 |
| 6 | Log + Recovery | WAL, Analyze/Redo/Undo |

### 关键踩坑记录

- **LRUReplacer::unpin** — 重复 unpin 同一 frame 应为 no-op，不能将其移到首部（否则测试 1 victim 顺序错误）
- **DiskManager::create/destroy_file** — 除 `path2fd_` 检查外还需 `is_file()` 检查（文件创建后即关闭，不在 path2fd_ 中）
- **DiskManager::open_file** — 文件不存在时抛 `FileNotFoundError`（非 `UnixError`），测试代码根据异常类型判断
- **UpdateExecutor::init_raw** — 必须在行循环外调用一次；循环内每行都调会导致第二行 assert raw==nullptr 崩溃
- **SeqScanExecutor::rid_** — Portal 收 rids 时不调用 Next()，必须在 beginTuple/nextTuple 中设置 rid_
- **TransactionManager** — commit/abort 不能从 txn_map 删除事务（下条 SQL 用旧 txn_id 查询会 assertion 失败），只改状态

---

## 1. 项目概述

- **目标**：基于 RMDB 框架，补全存储管理、查询执行、事务、并发控制、故障恢复等核心模块，实现一个简单的 DBMS。
- **开发环境**：Ubuntu 20.04 (x86_64)，CMake ≥ 3.16，GCC ≥ 7.1（支持 C++17），依赖 `flex`、`bison`、`libreadline-dev`。
- **提交方式**：在项目根目录打包整个项目为压缩包，上传希冀平台自动评测。重试次数 **3 次**。
- **截止时间**：2026-07-05 23:59（非弘毅班，即三暑课程开始前）。
- **评分构成**：实践题目 90% + 实验报告 10%（报告描述实现思路，不要贴大段代码）。
- **组队要求**：1~3 人，2026-06-22 23:59 前完成组队。

---

## 2. 系统整体架构与数据流（基于框架图）
[ SQL 字符串 ]
↓
[ Parser 语法解析 ] → 生成 AST (抽象语法树)
↓
[ Analyzer 语义分析 ] → 语义检查，转换为 Query (逻辑计划树)
↓
[ Optimizer 查询优化 ] → plan_query() 生成物理计划 Plan (算子树)
↓
[ Portal 执行入口 ] → start() 构建算子树，run() 驱动执行
↓
[ Executor 执行器 ] (火山模型，每个算子提供 Next()，每次返回一个元组)
↓
┌──────┴──────┬──────────┬──────────┐
│ │ │ │
[ Storage ] [ Transaction ] [ Log ] [ Index ]
│ │ (2PL锁) │ (WAL) │ (B+树)
│ │ │ │
└───────────┴────────────┴──────────┘
↓
[ 磁盘文件 ]
(.db数据文件, .meta元数据, .log日志文件)

### 各模块职责速查

| 模块 | 核心类/文件 | 主要职责 |
| :--- | :--- | :--- |
| **Parser** | `src/parser/` (flex/bison) | 词法/语法分析，生成 AST（定义在 `ast.h`）。 |
| **Analyzer** | `src/analyze/` | 检查表/字段是否存在、类型匹配，生成 `Query`。 |
| **Optimizer** | `src/optimizer/` | 逻辑优化（谓词下推等），生成物理 `Plan`。 |
| **Executor** | `src/execution/` | 火山模型算子：SeqScan, Insert, Update, Delete, Join, Aggregation, OrderBy 等。 |
| **Storage** | `src/storage/`, `src/record/` | DiskManager, BufferPoolManager, LRUReplacer, RmFileHandle。 |
| **Transaction** | `src/transaction/` | TransactionManager, LockManager（2PL + no-wait）。 |
| **Log** | `src/recovery/` | LogManager（WAL）, RecoveryManager（Analyze/Redo/Undo）。 |
| **Index** | `src/index/` | B+树实现（IxFileHdr, IxIndexHandle, IxScan）。 |
| **System** | `src/system/` | 元数据管理（DbMeta, TabMeta, ColMeta），DDL 执行。 |

---

## 3. 核心模块实现要点与测试对应关系

### 3.1 存储管理 (Storage Management)
> **对应测试**：题目二（建表、增删改查、连接查询）的基础。

- **DiskManager**：
  - 实现 `read_page(fd, page_id, data)` / `write_page(fd, page_id, data)`。
  - **关键**：逻辑页号转物理偏移：`offset = page_id * PAGE_SIZE`。
- **BufferPoolManager**：
  - `FetchPage`：从磁盘读入缓存，增加 `pin_count`。
  - `NewPage`：分配新页。
  - `UnpinPage`：减少 pin_count，若为 0 则交给 Replacer。
  - **淘汰策略**：空闲帧优先；若无空闲帧，调用 `replacer_->Victim()` 淘汰，脏页需先 `FlushPage`。
- **LRUReplacer**：
  - `Victim`：返回 LRU 列表头部（最近最少使用）。
  - `Pin`：从 LRU 列表移除（正在使用不可淘汰）。
  - `Unpin`：加入 LRU 列表尾部。
- **Record Manager (RmFileHandle)**：
  - **文件头**：`RmFileHdr`（record_size, num_pages, first_free_page_no, bitmap_size）。
  - **页面布局**：`| page_lsn (4B) | RmPageHdr (num_records, next_free_page_no) | bitmap | slots |`。
  - **操作**：利用 Bitmap 分配/释放 slot；`RmScan` 顺序遍历所有页的有效 slot。

### 3.2 元数据管理 (System)
- **DbMeta**：管理 `<database>.meta` 文件，维护 `std::map<std::string, TabMeta>`。
- **TabMeta / ColMeta**：存储表名、字段名、类型（INT/FLOAT/STRING）、长度、偏移量。
- **DDL 支持**：实现 `create table` 和 `drop table` 的元数据持久化。

### 3.3 索引 (Index) —— B+树
> **对应测试**：题目五（唯一索引）、题目十一（恢复时索引一致性）。

- **IxFileHdr**：根页号、阶数、键类型/总长、首/尾叶子页号。
- **IxNodeHandle / IxIndexHandle**：
  - 实现 `InsertEntry(key, rid)`、`DeleteEntry(key, rid)`、`Search(key)`（返回 RID 列表）。
  - 节点分裂/合并需正确维护父节点指针。
- **唯一索引约束**：
  - 插入/更新时，若违反唯一性，必须返回 `failure`（测试点3）。
  - **性能验证**：建立索引后的查询耗时必须 ≤ 建立前的 70%，否则视为未使用索引（测试点4、5）。

### 3.4 查询处理与执行 (Query Execution)
> **对应测试**：题目二（基础查询）、题目三（BIGINT）、题目四（时间类型）、题目六（聚合）、题目七（排序）、题目八（块嵌套循环）。

- **火山模型**：所有算子继承 `AbstractExecutor`，实现 `Next(Tuple *out)`。
- **必须实现的算子**：
  - `SeqScanExecutor`（全表扫描）。
  - `InsertExecutor` / `DeleteExecutor` / `UpdateExecutor`（**注意**：增删改必须同步维护索引）。
  - `FilterExecutor`（条件过滤）。
  - `ProjectExecutor`（投影）。
  - `NestedLoopJoinExecutor`（支持等值与非等值）。
  - **BlockNestedLoopJoinExecutor**（题目八，块嵌套循环，不是普通 NLJ）。
  - `AggregateExecutor`（支持 SUM, MAX, MIN, COUNT）。
  - `OrderByExecutor`（支持单/多字段，ASC/DESC，LIMIT）。
- **额外数据类型校验**：
  - **BIGINT**（题目三）：8 字节有符号范围，超限返回 `failure`。
  - **DATETIME**（题目四）：严格校验 `YYYY-MM-DD HH:MM:SS`（考虑闰年、每月天数、时/分/秒范围），非法返回 `failure`。

### 3.5 事务与并发控制 (Transaction & CC)
> **对应测试**：题目九（事务提交/回滚）、题目十（可串行化隔离级别）。

- **TransactionManager**：
  - 支持显式事务（`BEGIN` / `COMMIT` / `ABORT`）和隐式自动提交模式。
  - `Abort` 需借助 UNDO 日志回滚。
- **LockManager（核心难点）**：
  - 实现表级锁（S/X/IS/IX）和行级锁（S/X）。
  - **两阶段封锁（2PL）**：扩展阶段（Growing）只加锁，收缩阶段（Shrinking）只解锁。
  - **死锁预防（No-Wait）**：若请求的锁与现有锁冲突，立即返回失败并回滚事务，不等待。
  - 维护全局 `lock_table`（LockDataId → LockRequestQueue）。

### 3.6 故障恢复 (Failure Recovery)
> **对应测试**：题目十一（系统故障恢复，含多线程和索引）。

- **LogManager（WAL 原则）**：
  - 日志类型：`BEGIN`, `COMMIT`, `ABORT`, `INSERT`, `DELETE`, `UPDATE`。
  - 每条日志含 `lsn`（全局递增）、`txn_id`、`prev_lsn`、数据（旧值/新值/RID）。
  - **关键**：数据页刷盘前，必须保证其 `page_lsn_` ≤ `persist_lsn_`（即对应日志已落盘）。
- **RecoveryManager**：
  - `Analyze()`：扫描日志，分出 `undo_list`（未提交）和 `redo_list`（已提交）。
  - `Redo()`：重做已提交事务（幂等）。
  - `Undo()`：回滚未提交事务（逆操作）。

---

## 4. 测试题目速查表

| 题目 | 测试内容 | 关键实现细节与输出要求 |
| :--- | :--- | :--- |
| **题目二** | 建表、插入/更新/删除、条件查询、连接查询 | 支持 `SHOW TABLES`；浮点数输出保留 6 位小数（如 `90.500000`）；支持多表连接（笛卡尔积+等值条件）。 |
| **题目三** | BIGINT 类型 | 范围 `[-9223372036854775808, 9223372036854775807]`，超出返回 `failure`。 |
| **题目四** | DATETIME 类型 | 格式必须为 `'YYYY-MM-DD HH:MM:SS'`；严格校验日期（闰年、大小月）、时间（0-23, 0-59, 0-59），非法返回 `failure`。 |
| **题目五** | 唯一索引 | 支持 `CREATE INDEX` / `DROP INDEX` / `SHOW INDEX`；**性能**：索引查询耗时 ≤ 无索引查询的 70%；**约束**：重复插入返回 `failure`。 |
| **题目六** | 聚合函数 | 实现 `SUM`, `MAX`, `MIN`, `COUNT`（含 `COUNT(*)`）；支持 `AS` 别名。 |
| **题目七** | ORDER BY + LIMIT | 支持单/多字段排序，`ASC`/`DESC`，`LIMIT` 限制行数。 |
| **题目八** | 块嵌套循环连接 | 实现 **Block Nested Loop Join**（不是简单的双重循环），测试等值与非等值连接。 |
| **题目九** | 事务控制语句 | `BEGIN` 开启，`COMMIT` 持久化，`ABORT` 回滚（包含有索引和无索引场景）。 |
| **题目十** | 可串行化隔离级别 | 使用 2PL + No-Wait 避免脏读、不可重复读、幻读、丢失更新等五种异常。 |
| **题目十一**| 系统故障恢复 | WAL 协议；`crash` 后重启，执行 Analyze/Redo/Undo；恢复后数据和索引保持一致（含多线程并发场景）。 |

---

## 5. 推荐开发顺序（迭代式）

1. **第 1 步：存储层**（最底层）
   - 实现 DiskManager → BufferPoolManager → LRUReplacer。
   - 编写单元测试验证页面的读写和缓存淘汰。
2. **第 2 步：记录管理**
   - 实现 RmFileHandle（含 Bitmap 分配算法）和 RmScan。
   - 验证记录的插入、删除、更新和遍历。
3. **第 3 步：元数据与 DDL**
   - 实现 DbMeta 的加载与持久化，完成 `create table` 和 `drop table`。
4. **第 4 步：基础查询执行**
   - 对接 Parser/Analyzer/Optimizer，实现 `SeqScanExecutor` 和 `FilterExecutor`，支持 `SELECT ... WHERE ...`。
5. **第 5 步：完整 DML**
   - 实现 `InsertExecutor` / `DeleteExecutor` / `UpdateExecutor`（先不考虑索引和事务）。
6. **第 6 步：索引**
   - 实现 B+树（IxIndexHandle），支持索引扫描（`IndexScanExecutor`），并维护 DML 时的索引同步。
7. **第 7 步：事务与锁**
   - 实现 TransactionManager 和 LockManager（2PL + No-Wait）。
8. **第 8 步：日志与恢复**
   - 实现 LogManager（WAL）和 RecoveryManager（Analyze/Redo/Undo）。
9. **第 9 步：高级功能**
   - 依次完成聚合函数（Agg）、排序（OrderBy）、块嵌套循环连接（BNLJ）、BIGINT 和时间类型。
10. **第 10 步：集成调试与性能优化**
    - 针对希冀平台的测试点逐个排查，注意浮点数精度、唯一索引报错信息、死锁回滚逻辑。

---

## 6. 常见陷阱与注意事项

- **WAL 顺序铁律**：**先刷日志（FlushLog），再刷数据页**。否则掉电会丢数据。
- **Pin/Unpin 配对**：`FetchPage` 后一定要在合适的时机 `UnpinPage`，否则 Replacer 无法淘汰页面，导致缓冲池泄漏。
- **唯一索引报错**：插入/更新违反唯一约束时，必须输出 `failure`（小写），而不是抛异常退出。
- **时间校验**：不要只判断字符串长度，要严格校验 `2月30日`、`4月31日`、闰年 `2月29日` 以及 `25:00:00` 等非法值。
- **死锁处理**：No-Wait 算法下，检测到冲突应立即释放当前事务持有的所有锁并回滚，不能阻塞等待。
- **恢复时的索引重建**：Undo/Redo 不仅要恢复记录数据，B+树索引也必须通过 Insert/Delete 日志同步恢复，否则测试点会失败。

---

## 7. 调试与性能分析

- **调试工具**：GDB（推荐 `gdb --args ./rmdb_client ...`），配合 `LOG_DEBUG` 宏输出关键变量。
- **性能对比**：题目五要求索引查询耗时 <= 70% 无索引耗时。如果达不到，检查 B+树查找逻辑是否走索引（使用 `IxScan`），不要回退到全表扫描。
- **并发测试**：使用 `std::thread` 模拟多连接，检查锁表是否出现死锁或数据竞争。

---

## 8. 参考资料

- [GDB Tutorial (CMU)](https://www.cs.cmu.edu/~gilpin/tutorial/)
- [CMU 15-445 Spring 2026 官网](https://15445.courses.cs.cmu.edu/spring2026/)
- [CMU15-445 22Fall 通关记录（知乎）](https://www.zhihu.com/column/c_1605901992903004160)
- 课程群文件：`测试说明文档.pdf`、`框架图.pdf` 和项目源码。

---

> **最后预祝大家顺利完成三小数据库系统实现课设！遇到问题多和组员讨论，善用 GDB 调试，尽早开始，避免临近截止日期扎堆提交。**