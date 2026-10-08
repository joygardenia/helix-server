#!/bin/zsh

# 分布式集群数据库写入同步压测脚本 (针对 zsh 优化)
NODES=("192.168.100.1:8080" "192.168.100.2:8081" "192.168.100.3:8082")

echo "==========================================="
echo "正在执行分布式数据库随机写入压测 [3 节点同时写入]..."
echo "==========================================="

for NODE in $NODES; do
    PORT=$(echo $NODE | cut -d: -f2)
    echo "启动对节点 $NODE 的写入压测..."
    # 增加 --latency 参数
    wrk -t4 -c50 -d10s -s scripts/post_random_player.lua --latency "http://$NODE/players" > "write_results_$PORT.txt" &
done

wait

echo -e "\n==========================================="
echo "数据库写入压测结果汇总："
echo "==========================================="

for NODE in $NODES; do
    PORT=$(echo $NODE | cut -d: -f2)
    QPS=$(grep "Requests/sec" "write_results_$PORT.txt" | awk '{print $2}')
    LATENCY=$(grep "99%" "write_results_$PORT.txt" | awk '{print $2}')
    echo "节点 $NODE | 写入 QPS: $QPS | P99 Latency: $LATENCY"
done

rm write_results_*.txt
