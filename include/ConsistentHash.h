#pragma once

#include <map>
#include <string>
#include <vector>
#include <functional>

class ConsistentHash {
public:
    ConsistentHash(int replicas = 100) : replicas_(replicas) {}

    void addNode(const std::string& node) {
        for (int i = 0; i < replicas_; ++i) {
            size_t hash = std::hash<std::string>{}(node + "#" + std::to_string(i));
            ring_[hash] = node;
        }
    }

    void removeNode(const std::string& node) {
        for (int i = 0; i < replicas_; ++i) {
            size_t hash = std::hash<std::string>{}(node + "#" + std::to_string(i));
            ring_.erase(hash);
        }
    }

    std::string getNode(const std::string& key) {
        if (ring_.empty()) return "";
        size_t hash = std::hash<std::string>{}(key);
        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) {
            it = ring_.begin();
        }
        return it->second;
    }

private:
    int replicas_;
    std::map<size_t, std::string> ring_;
};
