# RMDB 数据库系统实现

> 基于 RMDB 框架的填空式开发项目，实现一个简易 DBMS 的核心模块。
> 课程：数据库系统实现（2026 三小学期）

## 当前进度

### ✅ 已完成

#### 1. 存储引擎 (Storage Engine)

| 文件 | 状态 | 说明 |
|---|---|---|
| [src/storage/disk_manager.cpp](src/storage/disk_manager.cpp) | ✅ | 磁盘页读写 (`read_page`/`write_page`)，文件创建/打开/关闭/删除 |
| [src/storage/buffer_pool_manager.cpp](src/storage/buffer_pool_manager.cpp) | ✅ | 缓冲池管理：`fetch_page`、`new_page`、`unpin_page`、`flush_page`、`delete_page`、`flush_all_pages` |
| [src/replacer/lru_replacer.cpp](src/replacer/lru_replacer.cpp) | ✅ | LRU 淘汰策略：`victim`（取尾部）、`pin`（移除）、`unpin`（插入首部） |

#### 2. 记录管理 (Record Management)

| 文件 | 状态 | 说明 |
|---|---|---|
| [src/record/rm_file_handle.cpp](src/record/rm_file_handle.cpp) | ✅ | 记录增删改查：`get_record`、`insert_record`、`delete_record`、`update_record`，空闲页链维护 |
| [src/record/rm_scan.cpp](src/record/rm_scan.cpp) | ✅ | 全表顺序扫描，bitmap 驱动的记录迭代器 |

### ❌ 待实现

按照推荐开发顺序排列：

| 优先级 | 模块 | 涉及文件 |
|---|---|---|
| 1 | 元数据管理 | `src/system/sm_manager.cpp`（`open_db`, `close_db`, `drop_table`, `create_index`, `drop_index`） |
| 2 | 查询执行器 | `src/execution/`（SeqScan, Insert, Delete, Update, Filter, Project, NLJ, BNLJ, Aggregate, OrderBy, IndexScan） |
| 3 | B+ 树索引 | `src/index/ix_index_handle.cpp`（`lower_bound`, `insert_entry`, `delete_entry`, `split`, `coalesce` 等 15+ 函数） |
| 4 | 事务与并发控制 | `src/transaction/`（TransactionManager, LockManager: 2PL + No-Wait） |
| 5 | 日志与恢复 | `src/recovery/`（LogManager: WAL, RecoveryManager: Analyze/Redo/Undo） |
| 6 | 数据类型扩展 | BIGINT、DATETIME 类型校验 |

## 项目结构

```
rmdb/
├── src/
│   ├── analyze/      # 语义分析
│   ├── common/       # 公共定义 (config, context)
│   ├── execution/    # 执行器 (火山模型算子)
│   ├── index/        # B+ 树索引
│   ├── optimizer/    # 查询优化器
│   ├── parser/       # 词法/语法分析 (flex/bison)
│   ├── record/       # 记录管理 ✅
│   ├── recovery/     # 日志与故障恢复
│   ├── replacer/     # 缓存淘汰策略 ✅
│   ├── storage/      # 磁盘与缓冲池管理 ✅
│   ├── system/       # 元数据管理
│   └── transaction/  # 事务与并发控制
├── deps/             # 依赖 (googletest)
├── build/            # 构建产物
├── CLAUDE.md         # 完整项目指南
└── README.md         # 本文件
```

## 构建

```bash
cd build
cmake ..
make
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
                    │  BufferPoolManager   │  ← 缓冲池 (65536 帧), page_table 映射
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
              │  磁盘文件    │  (.db, .meta, .log)
              └─────────────┘
```
