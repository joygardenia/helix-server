#include <string>
#include <sstream>
#include <sys/stat.h>

#include "AsyncLogging.h"
#include "DBPool.h"
#include "LFU.h"
#include "Logger.h"
#include "TcpServer.h"
#include "memoryPool.h"

static const off_t kRollSize = 1 * 1024 * 1024;

// ─── 全局资源 ────────────────────────────────────────────────────────────────
static DBPool* g_dbPool = nullptr;
// LFU 缓存：key=路由字符串, value=响应 body
static KamaCache::KLfuCache<std::string, std::string>* g_cache = nullptr;

// ─── HTTP 工具 ───────────────────────────────────────────────────────────────
static std::string httpResp(int code, const std::string& body,
                            const std::string& ct = "application/json") {
    std::string status = (code == 200) ? "200 OK"
                       : (code == 400) ? "400 Bad Request"
                       : (code == 404) ? "404 Not Found"
                                       : "500 Internal Server Error";
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << ct << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: keep-alive\r\n\r\n"
        << body;
    return oss.str();
}

// 从 HTTP 请求中解析 method / path / body
static void parseRequest(const std::string& raw,
                         std::string& method, std::string& path, std::string& body) {
    size_t p1 = raw.find(' ');
    size_t p2 = raw.find(' ', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) return;
    method = raw.substr(0, p1);
    path   = raw.substr(p1 + 1, p2 - p1 - 1);
    // body 在 \r\n\r\n 之后
    size_t hend = raw.find("\r\n\r\n");
    if (hend != std::string::npos) body = raw.substr(hend + 4);
}

// 从 JSON body 中取 "key":数字 （简单解析，不引入第三方库）
static bool jsonGetInt(const std::string& json, const std::string& key, int& val) {
    std::string pat = "\"" + key + "\"";
    size_t pos = json.find(pat);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    val = std::stoi(json.substr(pos + 1));
    return true;
}

// ─── Handler: GET /products ──────────────────────────────────────────────────
// 商品列表，接入 LFU 缓存（读多写少）
static std::string handleGetProducts() {
    const std::string cacheKey = "GET/products";
    std::string cached;
    if (g_cache->get(cacheKey, cached))
        return httpResp(200, cached);

    auto db = g_dbPool->get();
    MYSQL_RES* res = db->query("SELECT id,name,price,stock FROM products");
    if (!res) return httpResp(500, "{\"error\":\"db error\"}");

    std::ostringstream body;
    body << "[";
    MYSQL_ROW row;
    bool first = true;
    while ((row = mysql_fetch_row(res))) {
        if (!first) body << ",";
        first = false;
        body << "{\"id\":" << row[0]
             << ",\"name\":\"" << row[1] << "\""
             << ",\"price\":" << row[2]
             << ",\"stock\":" << row[3] << "}";
    }
    body << "]";
    mysql_free_result(res);

    std::string bodyStr = body.str();
    g_cache->put(cacheKey, bodyStr);
    return httpResp(200, bodyStr);
}

// ─── Handler: POST /orders ───────────────────────────────────────────────────
// 下单：事务保证扣库存 + 创建订单原子性，防超卖
// 请求 body: {"user_id":1,"product_id":1,"qty":2}
static std::string handlePostOrder(const std::string& body) {
    int user_id = 0, product_id = 0, qty = 0;
    if (!jsonGetInt(body, "user_id",    user_id)    ||
        !jsonGetInt(body, "product_id", product_id) ||
        !jsonGetInt(body, "qty",        qty)        || qty <= 0)
        return httpResp(400, "{\"error\":\"invalid params\"}");

    auto db = g_dbPool->get();
    if (!db->begin()) return httpResp(500, "{\"error\":\"begin failed\"}");

    // 1. 查库存（SELECT ... FOR UPDATE 行锁，防并发超卖）
    std::ostringstream qss;
    qss << "SELECT price,stock FROM products WHERE id=" << product_id << " FOR UPDATE";
    MYSQL_RES* res = db->query(qss.str());
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        db->rollback();
        return httpResp(404, "{\"error\":\"product not found\"}");
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    double price  = std::stod(row[0]);
    int    stock  = std::stoi(row[1]);
    mysql_free_result(res);

    if (stock < qty) {
        db->rollback();
        return httpResp(400, "{\"error\":\"insufficient stock\"}");
    }

    // 2. 扣库存
    std::ostringstream upd;
    upd << "UPDATE products SET stock=stock-" << qty
        << " WHERE id=" << product_id;
    if (!db->execute(upd.str())) {
        db->rollback();
        return httpResp(500, "{\"error\":\"update stock failed\"}");
    }

    // 3. 创建订单
    double total = price * qty;
    std::ostringstream ins;
    ins << "INSERT INTO orders(user_id,total,status) VALUES("
        << user_id << "," << total << ",0)";
    if (!db->execute(ins.str())) {
        db->rollback();
        return httpResp(500, "{\"error\":\"insert order failed\"}");
    }
    uint64_t order_id = db->lastInsertId();

    // 4. 插入订单明细
    std::ostringstream item;
    item << "INSERT INTO order_items(order_id,product_id,qty,price) VALUES("
         << order_id << "," << product_id << "," << qty << "," << price << ")";
    if (!db->execute(item.str())) {
        db->rollback();
        return httpResp(500, "{\"error\":\"insert order_item failed\"}");
    }

    if (!db->commit()) {
        db->rollback();
        return httpResp(500, "{\"error\":\"commit failed\"}");
    }

    // 下单成功后使商品缓存失效
    g_cache->remove("GET/products");

    std::ostringstream resp;
    resp << "{\"order_id\":" << order_id << ",\"total\":" << total << "}";
    return httpResp(200, resp.str());
}

// ─── Handler: GET /orders/:id ────────────────────────────────────────────────
static std::string handleGetOrder(int order_id) {
    auto db = g_dbPool->get();

    std::ostringstream q;
    q << "SELECT o.id,o.user_id,o.total,o.status,"
      << "oi.product_id,oi.qty,oi.price "
      << "FROM orders o JOIN order_items oi ON o.id=oi.order_id "
      << "WHERE o.id=" << order_id;
    MYSQL_RES* res = db->query(q.str());
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return httpResp(404, "{\"error\":\"order not found\"}");
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    std::ostringstream body;
    body << "{\"id\":"      << row[0]
         << ",\"user_id\":" << row[1]
         << ",\"total\":"   << row[2]
         << ",\"status\":"  << row[3]
         << ",\"items\":[";
    bool first = true;
    do {
        if (!first) body << ",";
        first = false;
        body << "{\"product_id\":" << row[4]
             << ",\"qty\":"        << row[5]
             << ",\"price\":"      << row[6] << "}";
    } while ((row = mysql_fetch_row(res)));
    body << "]}";
    mysql_free_result(res);

    return httpResp(200, body.str());
}

// ─── EchoServer ──────────────────────────────────────────────────────────────
class EchoServer {
public:
    EchoServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
        : server_(loop, addr, name), loop_(loop)
    {
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this,
                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        server_.setThreadNum(3);
    }
    void start() { server_.start(); }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected())
            LOG_INFO << "Connection UP :"   << conn->peerAddress().toIpPort().c_str();
        else
            LOG_INFO << "Connection DOWN :" << conn->peerAddress().toIpPort().c_str();
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp) {
        std::string raw = buf->retrieveAllAsString();

        std::string method, path, body;
        parseRequest(raw, method, path, body);

        std::string resp;
        if (method == "GET" && path == "/products") {
            resp = handleGetProducts();
        } else if (method == "POST" && path == "/orders") {
            resp = handlePostOrder(body);
        } else if (method == "GET" && path.substr(0, 8) == "/orders/") {
            int id = std::stoi(path.substr(8));
            resp = handleGetOrder(id);
        } else {
            resp = httpResp(404, "{\"error\":\"not found\"}");
        }

        conn->send(resp);
    }

    TcpServer server_;
    EventLoop* loop_;
};

// ─── 日志 ────────────────────────────────────────────────────────────────────
AsyncLogging* g_asyncLog = nullptr;
void asyncLog(const char* msg, int len) {
    if (g_asyncLog) g_asyncLog->append(msg, len);
    fwrite(msg, 1, len, stdout);
}

// ─── main ────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // 日志
    mkdir("logs", 0755);
    std::ostringstream logPath;
    logPath << "logs/" << ::basename(argv[0]);
    AsyncLogging log(logPath.str(), kRollSize);
    g_asyncLog = &log;
    Logger::setOutput(asyncLog);
    log.start();

    // 内存池
    memoryPool::HashBucket::initMemoryPool();

    // LFU 缓存（容量 128 条）
    KamaCache::KLfuCache<std::string, std::string> lfu(128);
    g_cache = &lfu;

    // 数据库连接池（8 条连接）
    DBPool dbPool("127.0.0.1", "takeaway", "takeaway123", "takeaway", 3306, 8);
    g_dbPool = &dbPool;

    // 网络
    EventLoop loop;
    InetAddress addr(8080);
    EchoServer server(&loop, addr, "TakeawayServer");
    server.start();

    std::cout << "=== Takeaway Server started on :8080 ===" << std::endl;
    loop.loop();
    log.stop();
}
