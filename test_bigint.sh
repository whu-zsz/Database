#!/bin/bash

# 测试 BIGINT 功能
# 使用方法: ./test_bigint.sh

set -e

echo "=== BIGINT 测试脚本 ==="

# 清理旧进程
pkill -9 rmdb 2>/dev/null || true
sleep 1

# 清理旧数据库
cd /Users/lnm/Downloads/WHU_database_2026/Database/build
rm -rf storage_bigint_test 2>/dev/null || true

echo "1. 启动服务器..."
./bin/rmdb storage_bigint_test &
SERVER_PID=$!
sleep 3

echo "2. 运行测试客户端..."
cd /Users/lnm/Downloads/WHU_database_2026/Database/build
./bin/query_test ../src/test/query/query_sql/storage_test6.sql

echo "3. 检查输出文件..."
if [ -f "storage_bigint_test/output.txt" ]; then
    echo "=== output.txt 内容 ==="
    cat storage_bigint_test/output.txt
    echo "========================"
else
    echo "错误: output.txt 未生成!"
    echo "检查 storage_bigint_test 目录:"
    ls -la storage_bigint_test/
fi

echo "4. 停止服务器..."
kill $SERVER_PID 2>/dev/null || true

echo "=== 测试完成 ==="
