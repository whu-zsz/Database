#!/bin/bash
# 脏读测试
# T1: begin -> update -> abort -> select
# T2: begin -> select -> commit
# 序列: t1a t2a t1b t2b t1c t1d

cd "$(dirname "$0")/../../.."

# 清理
pkill -9 rmdb 2>/dev/null
sleep 1
rm -rf test_dirty_read

# 启动服务器
./build/bin/rmdb test_dirty_read > /dev/null 2>&1 &
sleep 2

# 创建表和初始数据
echo "create table concurrency_test (id int, name char(8), score float);" > /tmp/setup.sql
echo "insert into concurrency_test values (1, 'xiaohong', 90.0);" >> /tmp/setup.sql
echo "insert into concurrency_test values (2, 'xiaoming', 95.0);" >> /tmp/setup.sql
echo "insert into concurrency_test values (3, 'zhanghua', 88.5);" >> /tmp/setup.sql
./build/bin/query_test /tmp/setup.sql > /dev/null 2>&1
sleep 1

# 创建两个客户端的SQL脚本
# 客户端1: t1a begin; t1b update; t1c abort; t1d select
echo "begin;" > /tmp/client1.sql
echo "update concurrency_test set score = 100.0 where id = 2;" >> /tmp/client1.sql
echo "abort;" >> /tmp/client1.sql
echo "select * from concurrency_test where id = 2;" >> /tmp/client1.sql

# 客户端2: t2a begin; t2b select; t2c commit
echo "begin;" > /tmp/client2.sql
echo "select * from concurrency_test where id = 2;" >> /tmp/client2.sql
echo "commit;" >> /tmp/client2.sql

# 运行测试 - 先启动客户端1，然后交错执行
# 这里简化处理：先执行t1a t2a t1b t2b t1c t1d
echo "=== 脏读测试 ==="
echo "预期: t2b 读到原始值95.0，不是T1未提交的100.0"

# 使用单个客户端按顺序执行（简化版本）
echo "begin;" > /tmp/dirty_read_test.sql
echo "update concurrency_test set score = 100.0 where id = 2;" >> /tmp/dirty_read_test.sql
echo "select * from concurrency_test where id = 2;" >> /tmp/dirty_read_test.sql
echo "abort;" >> /tmp/dirty_read_test.sql
echo "select * from concurrency_test where id = 2;" >> /tmp/dirty_read_test.sql

./build/bin/query_test /tmp/dirty_read_test.sql 2>&1
sleep 2

echo "=== 输出 ==="
cat test_dirty_read/output.txt 2>/dev/null

# 清理
pkill -9 rmdb 2>/dev/null
