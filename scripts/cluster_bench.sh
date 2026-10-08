#!/bin/bash

# 分布式集群同步压测脚本
NODES=("192.168.100.1:8080" "192.168.100.2:8081" "192.168.100.3:8082")

echo "==========================================="
echo "正在对分布式集群 [${#NODES[@]} 个节点] 发起同步压力测试..."
echo "==========================================="

for NODE in "${NODES[@]}"; do
    IP=$(echo $NODE | cut -d: -f1)
    PORT=$(echo $NODE | cut -d: -f2)
    
    echo "启动对节点 $NODE 的后台压测..."
    # 显式增加 --latency 参数，否则 grep 不到 99% 的行
    wrk -t4 -c100 -d10s --latency "http://$NODE/discover?key=bench" > "results_$PORT.txt" &
done

# 等待所有后台 wrk 进程结束
wait

echo -e "\n==========================================="
echo "压测结果汇总 (P99 需 wrk 输出支持):"
echo "==========================================="

for NODE in "${NODES[@]}"; do
    PORT=$(echo $NODE | cut -d: -f2)
    # 使用更鲁棒的 awk 解析方式
    QPS=$(grep "Requests/sec" "results_$PORT.txt" | awk '{print $2}')
    LATENCY=$(grep "99%" "results_$PORT.txt" | awk '{print $2}')
    if [ -z "$LATENCY" ]; then LATENCY="N/A (check --latency)"; fi
    echo "节点 $NODE | QPS: $QPS | P99 Latency: $LATENCY"
done

# 清理临时文件
rm results_*.txt
