#!/bin/bash

# 分布式 WebServer 压测脚本
# 使用方法: ./run_benchmarks.sh <IP> <PORT>

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <IP> <PORT>"
    exit 1
fi

IP=$1
PORT=$2
BASE_URL="http://$IP:$PORT"

echo "==========================================="
echo "开始对节点 $IP:$PORT 进行性能验证"
echo "==========================================="

# 1. 验证静态内容/缓存 (GET /items)
echo -e "\n[1/3] 压测 GET /items (验证 LFU 缓存性能)..."
wrk -t4 -c100 -d10s --latency "$BASE_URL/items"

# 2. 验证一致性哈希计算 (GET /discover)
echo -e "\n[2/3] 压测 GET /discover (验证一致性哈希计算开销)..."
wrk -t4 -c100 -d10s -s scripts/benchmark_discover.lua --latency "$BASE_URL/discover"

# 3. 验证数据库写入性能 (POST /players)
# 创建一个临时的 post 脚本
cat <<EOF > scripts/post_player.lua
wrk.method = "POST"
wrk.body   = '{"name":"bench_user"}'
wrk.headers["Content-Type"] = "application/json"
EOF

echo -e "\n[3/3] 压测 POST /players (验证数据库连接池与事务性能)..."
wrk -t4 -c50 -d10s -s scripts/post_player.lua --latency "$BASE_URL/players"

rm scripts/post_player.lua

echo -e "\n==========================================="
echo "压测完成！"
echo "==========================================="
