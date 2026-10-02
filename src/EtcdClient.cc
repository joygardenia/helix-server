#include "EtcdClient.h"
#include <iostream>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <sstream>
#include <algorithm>

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

EtcdClient::EtcdClient(const std::string& host, int port) {
    baseUrl_ = "http://" + host + ":" + std::to_string(port);
}

std::string EtcdClient::base64Encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
        if (valb == -6 || valb == -4 || valb == -2) { /* 保持状态 */ }
        if (out.size() % 4 == 0) val = 0; 
    }
    if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string EtcdClient::executeCurl(const std::string& url, const std::string& data) {
    std::string cmd = "curl -s -X POST -L \"" + url + "\" -d '" + data + "' 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "";

    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), fp)) {
        result += buffer;
    }
    pclose(fp);
    return result;
}

bool EtcdClient::registerService(const std::string& key, const std::string& value, int ttl) {
    std::string grantData = "{\"TTL\": " + std::to_string(ttl) + "}";
    std::string grantResp = executeCurl(baseUrl_ + "/v3/lease/grant", grantData);
    
    if (grantResp.empty()) return false;

    size_t idPos = grantResp.find("\"ID\":\"");
    if (idPos == std::string::npos) return false;

    size_t idEnd = grantResp.find("\"", idPos + 6);
    leaseId_ = grantResp.substr(idPos + 6, idEnd - (idPos + 6));

    std::string b64Key = base64Encode(key);
    std::string b64Val = base64Encode(value);
    std::string putData = "{\"key\": \"" + b64Key + "\", \"value\": \"" + b64Val + "\", \"lease\": \"" + leaseId_ + "\"}";
    std::string putResp = executeCurl(baseUrl_ + "/v3/kv/put", putData);
    
    if (putResp.find("\"revision\":\"") == std::string::npos) return false;

    registeredKey_ = key;
    return true;
}

bool EtcdClient::keepAlive() {
    if (leaseId_.empty()) return false;
    std::string keepData = "{\"ID\": \"" + leaseId_ + "\"}";
    std::string resp = executeCurl(baseUrl_ + "/v3/lease/keepalive", keepData);
    return resp.find(leaseId_) != std::string::npos;
}

void EtcdClient::deregisterService(const std::string& key) {
    std::string b64Key = base64Encode(key);
    std::string delData = "{\"key\": \"" + b64Key + "\"}";
    executeCurl(baseUrl_ + "/v3/kv/deleterange", delData);
}

std::vector<std::pair<std::string, std::string>> EtcdClient::getServices(const std::string& prefix) {
    std::string b64Prefix = base64Encode(prefix);
    std::string rangeEnd = prefix;
    if (!rangeEnd.empty()) {
        rangeEnd.back()++;
    }
    std::string b64End = base64Encode(rangeEnd);

    std::string rangeData = "{\"key\": \"" + b64Prefix + "\", \"range_end\": \"" + b64End + "\"}";
    std::string resp = executeCurl(baseUrl_ + "/v3/kv/range", rangeData);

    std::vector<std::pair<std::string, std::string>> services;
    return services; 
}
