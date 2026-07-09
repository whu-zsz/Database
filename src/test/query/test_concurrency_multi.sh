#!/bin/bash
# 并发测试 - 使用多个进程
# 测试脏读：T1更新后T2读取，T1回滚，验证T2读到的值

cd "$(dirname "$0")/../../.."

# 清理
pkill -9 rmdb 2>/dev/null
sleep 1
rm -rf test_conc_multi

# 启动服务器
./build/bin/rmdb test_conc_multi > /dev/null 2>&1 &
sleep 2

# 创建表和初始数据
echo "create table t1(id int, val int);" > /tmp/setup_conc.sql
echo "insert into t1 values(1, 100);" >> /tmp/setup_conc.sql
./build/bin/query_test /tmp/setup_conc.sql > /dev/null 2>&1
sleep 1

echo "=== 并发测试 ==="
echo "测试场景：T1更新id=1的值为200，T2同时读取id=1"

# 客户端1：开始事务，更新，等待，回滚
echo "begin;" > /tmp/conc_client1.sql
echo "update t1 set val = 200 where id = 1;" >> /tmp/conc_client1.sql

# 客户端2：开始事务，读取
echo "begin;" > /tmp/conc_client2.sql
echo "select * from t1 where id = 1;" >> /tmp/conc_client2.sql

# 先启动客户端1（更新）
./build/bin/query_test /tmp/conc_client1.sql > /tmp/conc_result1.txt 2>&1 &
CLIENT1_PID=$!
sleep 0.5

# 然后启动客户端2（读取）
./build/bin/query_test /tmp/conc_client2.sql > /tmp/conc_result2.txt 2>&1 &
CLIENT2_PID=$!

# 等待两个客户端完成
wait $CLIENT1_PID
wait $CLIENT2_PID

sleep 1

echo "=== 客户端1输出 ==="
cat /tmp/conc_result1.txt
echo "=== 客户端2输出 ==="
cat /tmp/conc_result2.txt
echo "=== 服务器输出 ==="
cat test_conc_multi/output.txt 2>/dev/null

# 验证：客户端2应该读到原始值100，或者被阻塞返回abort
if grep -q "100" /tmp/conc_result2.txt; then
    echo "结果: PASS (客户端2读到原始值100)"
elif grep -q "abort" /tmp/conc_result2.txt; then
    echo "结果: PASS (客户端2被阻塞回滚)"
else
    echo "结果: FAIL (客户端2读到了未提交的值200)"
fi

# 清理
pkill -9 rmdb 2>/dev/null
