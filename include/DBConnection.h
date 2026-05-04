#pragma once

#include <mysql/mysql.h>
#include <stdexcept>
#include <string>

// 封装单条 MySQL 连接：连接、查询、事务
class DBConnection {
public:
    DBConnection(const char* host, const char* user,
                 const char* passwd, const char* db, unsigned int port = 3306)
    {
        conn_ = mysql_init(nullptr);
        if (!conn_) throw std::runtime_error("mysql_init failed");
        if (!mysql_real_connect(conn_, host, user, passwd, db, port, nullptr, 0))
            throw std::runtime_error(mysql_error(conn_));
        // 自动重连
        bool reconnect = true;
        mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);
    }

    ~DBConnection() { if (conn_) mysql_close(conn_); }

    // 执行无结果集的 SQL（INSERT/UPDATE/DELETE）
    bool execute(const std::string& sql) {
        return mysql_query(conn_, sql.c_str()) == 0;
    }

    // 执行有结果集的 SQL，调用方负责 mysql_free_result
    MYSQL_RES* query(const std::string& sql) {
        if (mysql_query(conn_, sql.c_str()) != 0) return nullptr;
        return mysql_store_result(conn_);
    }

    bool begin()    { return mysql_query(conn_, "BEGIN") == 0; }
    bool commit()   { return mysql_query(conn_, "COMMIT") == 0; }
    bool rollback() { return mysql_query(conn_, "ROLLBACK") == 0; }

    // 最后一次 INSERT 的自增 ID
    uint64_t lastInsertId() { return mysql_insert_id(conn_); }

    const char* lastError() { return mysql_error(conn_); }

    // 转义字符串，防 SQL 注入
    std::string escape(const std::string& s) {
        std::string buf(s.size() * 2 + 1, '\0');
        unsigned long len = mysql_real_escape_string(conn_, &buf[0], s.c_str(), s.size());
        buf.resize(len);
        return buf;
    }

    DBConnection(const DBConnection&) = delete;
    DBConnection& operator=(const DBConnection&) = delete;

private:
    MYSQL* conn_;
};
