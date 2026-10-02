#pragma once

#include <string>
#include <vector>

class EtcdClient {
public:
    EtcdClient(const std::string& host, int port);

    // 注册服务，带租约（TTL秒）
    bool registerService(const std::string& key, const std::string& value, int ttl);

    // 撤销租约/注销服务
    void deregisterService(const std::string& key);

    // 获取所有服务（按前缀）
    std::vector<std::pair<std::string, std::string>> getServices(const std::string& prefix);

    // 续约
    bool keepAlive();

private:
    std::string baseUrl_;
    std::string leaseId_;
    std::string registeredKey_;

    std::string base64Encode(const std::string& in);
    std::string base64Decode(const std::string& in);
    std::string executeCurl(const std::string& url, const std::string& data);
};
