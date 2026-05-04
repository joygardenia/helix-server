#pragma once

#include "DBConnection.h"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

// 连接池：预建 N 条连接，取用归还，线程安全
class DBPool {
public:
    DBPool(const char* host, const char* user,
           const char* passwd, const char* db,
           unsigned int port = 3306, int poolSize = 8)
    {
        for (int i = 0; i < poolSize; ++i)
            pool_.push(std::unique_ptr<DBConnection>(
                new DBConnection(host, user, passwd, db, port)));
    }

    // RAII guard：析构时自动归还连接
    struct Guard {
        Guard(DBPool& p, std::unique_ptr<DBConnection> c)
            : pool_(&p), conn_(std::move(c)) {}
        // 支持移动
        Guard(Guard&& o) : pool_(o.pool_), conn_(std::move(o.conn_)) { o.pool_ = nullptr; }
        ~Guard() { if (pool_ && conn_) pool_->put(std::move(conn_)); }
        DBConnection* operator->() { return conn_.get(); }
        DBConnection& operator*()  { return *conn_; }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    private:
        DBPool* pool_;
        std::unique_ptr<DBConnection> conn_;
    };

    // 阻塞直到拿到连接
    Guard get() {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this]{ return !pool_.empty(); });
        auto conn = std::move(pool_.front());
        pool_.pop();
        return Guard(*this, std::move(conn));
    }

private:
    void put(std::unique_ptr<DBConnection> c) {
        std::lock_guard<std::mutex> lk(mu_);
        pool_.push(std::move(c));
        cv_.notify_one();
    }

    std::queue<std::unique_ptr<DBConnection>> pool_;
    std::mutex mu_;
    std::condition_variable cv_;
};
