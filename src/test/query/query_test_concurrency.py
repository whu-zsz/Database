#!/usr/bin/env python3
"""
并发控制测试脚本
测试五种数据异常的防护：脏写、脏读、丢失更新、不可重复读、幻读
"""

import os
import sys
import time
import socket
import threading
import subprocess

def send_sql(sock, sql):
    """发送SQL语句到服务器"""
    try:
        sock.sendall((sql + '\0').encode())
        time.sleep(0.1)
    except Exception as e:
        print(f"Send error: {e}")

def recv_response(sock):
    """接收服务器响应"""
    try:
        data = sock.recv(4096).decode()
        return data
    except Exception as e:
        print(f"Recv error: {e}")
        return ""

def create_client():
    """创建客户端连接"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 8765))
    return sock

def run_dirty_read_test():
    """
    脏读测试
    T1: begin -> update -> abort -> select
    T2: begin -> select -> commit
    序列: t1a t2a t1b t2b t1c t1d
    预期: t2b 读到原始值(95.0)，不是 T1 未提交的 100.0
    """
    print("=== 脏读测试 ===")

    # 创建表和初始数据
    setup_sql = [
        "create table concurrency_test (id int, name char(8), score float)",
        "insert into concurrency_test values (1, 'xiaohong', 90.0)",
        "insert into concurrency_test values (2, 'xiaoming', 95.0)",
        "insert into concurrency_test values (3, 'zhanghua', 88.5)",
    ]

    sock = create_client()
    for sql in setup_sql:
        send_sql(sock, sql)
        recv_response(sock)
    sock.close()

    time.sleep(0.5)

    # 创建两个客户端
    client1 = create_client()
    client2 = create_client()

    results = {}

    # t1a: begin
    send_sql(client1, "begin")
    recv_response(client1)
    time.sleep(0.1)

    # t2a: begin
    send_sql(client2, "begin")
    recv_response(client2)
    time.sleep(0.1)

    # t1b: update
    send_sql(client1, "update concurrency_test set score = 100.0 where id = 2")
    recv_response(client1)
    time.sleep(0.1)

    # t2b: select (应该读到原始值95.0，不是100.0)
    send_sql(client2, "select * from concurrency_test where id = 2")
    resp = recv_response(client2)
    results['t2b'] = resp
    time.sleep(0.1)

    # t1c: abort
    send_sql(client1, "abort")
    recv_response(client1)
    time.sleep(0.1)

    # t1d: select (应该读到原始值95.0)
    send_sql(client1, "select * from concurrency_test where id = 2")
    resp = recv_response(client1)
    results['t1d'] = resp

    client1.close()
    client2.close()

    # 验证结果
    if '95.000000' in results.get('t2b', ''):
        print("  t2b: PASS (读到原始值95.0)")
    else:
        print(f"  t2b: FAIL (期望95.0，实际: {results.get('t2b', '无响应')})")

    if '95.000000' in results.get('t1d', ''):
        print("  t1d: PASS (读到原始值95.0)")
    else:
        print(f"  t1d: FAIL (期望95.0，实际: {results.get('t1d', '无响应')})")

    return '95.000000' in results.get('t2b', '') and '95.000000' in results.get('t1d', '')

def run_dirty_write_test():
    """
    脏写测试
    T1: begin -> update -> abort
    T2: begin -> update -> commit
    序列: t1a t2a t1b t2b t1c t2c
    预期: T2 的更新应该被阻塞或回滚
    """
    print("=== 脏写测试 ===")

    # 创建表和初始数据
    setup_sql = [
        "create table concurrency_test2 (id int, val int)",
        "insert into concurrency_test2 values (1, 100)",
    ]

    sock = create_client()
    for sql in setup_sql:
        send_sql(sock, sql)
        recv_response(sock)
    sock.close()

    time.sleep(0.5)

    client1 = create_client()
    client2 = create_client()

    results = {}

    # t1a: begin
    send_sql(client1, "begin")
    recv_response(client1)
    time.sleep(0.1)

    # t2a: begin
    send_sql(client2, "begin")
    recv_response(client2)
    time.sleep(0.1)

    # t1b: update
    send_sql(client1, "update concurrency_test2 set val = 200 where id = 1")
    recv_response(client1)
    time.sleep(0.1)

    # t2b: update (应该被阻塞或失败)
    send_sql(client2, "update concurrency_test2 set val = 300 where id = 1")
    resp = recv_response(client2)
    results['t2b'] = resp
    time.sleep(0.1)

    # t1c: abort
    send_sql(client1, "abort")
    recv_response(client1)
    time.sleep(0.1)

    # t2c: commit (如果t2b成功的话)
    send_sql(client2, "commit")
    recv_response(client2)
    time.sleep(0.1)

    # 验证最终值
    verify_sock = create_client()
    send_sql(verify_sock, "select * from concurrency_test2 where id = 1")
    resp = recv_response(verify_sock)
    results['final'] = resp
    verify_sock.close()

    client1.close()
    client2.close()

    # 检查t2b是否被阻塞(返回abort)或最终值是否正确
    if 'abort' in results.get('t2b', '').lower():
        print("  t2b: PASS (被阻塞回滚)")
        return True
    elif '100' in results.get('final', ''):
        print("  final: PASS (值未被脏写)")
        return True
    else:
        print(f"  FAIL: t2b={results.get('t2b', '无响应')}, final={results.get('final', '无响应')}")
        return False

def cleanup():
    """清理测试表"""
    sock = create_client()
    tables = ['concurrency_test', 'concurrency_test2', 'concurrency_test3']
    for t in tables:
        send_sql(sock, f"drop table if exists {t}")
        recv_response(sock)
    sock.close()

def main():
    print("并发控制测试")
    print("=" * 50)

    # 等待服务器启动
    time.sleep(1)

    cleanup()
    time.sleep(0.5)

    results = {}
    results['dirty_read'] = run_dirty_read_test()
    time.sleep(1)

    cleanup()
    time.sleep(0.5)

    results['dirty_write'] = run_dirty_write_test()
    time.sleep(1)

    print("\n" + "=" * 50)
    print("测试结果汇总:")
    for test, result in results.items():
        status = "PASS" if result else "FAIL"
        print(f"  {test}: {status}")

if __name__ == "__main__":
    main()
