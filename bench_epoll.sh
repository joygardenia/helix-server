#!/bin/bash
# epoll 基准压测，改为 io_uring 后再跑一次对比

echo "=== Warming up ==="
wrk -t2 -c100 -d5s http://127.0.0.1:8080/products > /dev/null 2>&1

echo ""
echo "=== [1] 低并发 100 连接 ==="
wrk -t4 -c100 -d15s --latency http://127.0.0.1:8080/products

echo ""
echo "=== [2] 中并发 500 连接 ==="
wrk -t4 -c500 -d15s --latency http://127.0.0.1:8080/products

echo ""
echo "=== [3] 高并发 1000 连接 ==="
wrk -t4 -c1000 -d15s --latency http://127.0.0.1:8080/products
