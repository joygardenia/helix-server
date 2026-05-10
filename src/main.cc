#include <sstream>
#include <string>
#include <sys/stat.h>

#include "AsyncLogging.h"
#include "DBPool.h"
#include "LFU.h"
#include "Logger.h"
#include "TcpServer.h"
#include "memoryPool.h"

static const off_t kRollSize = 1 * 1024 * 1024;

// ─── 全局资源 ────────────────────────────────────────────────────────────────
static DBPool *g_dbPool = nullptr;
// LFU：key=路由字符串, value=响应 body（读多写少的物品目录）
static KamaCache::KLfuCache<std::string, std::string> *g_cache = nullptr;

// ─── HTTP 工具 ───────────────────────────────────────────────────────────────
static std::string httpResp(int code, const std::string &body,
                            const std::string &ct = "application/json") {
  std::string status = (code == 200)   ? "200 OK"
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

static void parseRequest(const std::string &raw, std::string &method,
                         std::string &path, std::string &body) {
  size_t p1 = raw.find(' ');
  size_t p2 = raw.find(' ', p1 + 1);
  if (p1 == std::string::npos || p2 == std::string::npos)
    return;
  method = raw.substr(0, p1);
  path = raw.substr(p1 + 1, p2 - p1 - 1);
  size_t hend = raw.find("\r\n\r\n");
  if (hend != std::string::npos)
    body = raw.substr(hend + 4);
}

static bool jsonGetInt(const std::string &json, const std::string &key,
                       int &val) {
  std::string pat = "\"" + key + "\"";
  size_t pos = json.find(pat);
  if (pos == std::string::npos)
    return false;
  pos = json.find(':', pos);
  if (pos == std::string::npos)
    return false;
  val = std::stoi(json.substr(pos + 1));
  return true;
}

// 解析 "key":"value" 中的简单字符串（不做完整 JSON 转义处理，够用演示）
static bool jsonGetString(const std::string &json, const std::string &key,
                          std::string &val) {
  std::string pat = "\"" + key + "\"";
  size_t pos = json.find(pat);
  if (pos == std::string::npos)
    return false;
  pos = json.find(':', pos);
  if (pos == std::string::npos)
    return false;
  pos = json.find('"', pos);
  if (pos == std::string::npos)
    return false;
  size_t end = json.find('"', pos + 1);
  if (end == std::string::npos)
    return false;
  val = json.substr(pos + 1, end - pos - 1);
  return !val.empty();
}
//  • 缓存粒度：缓存的是 整条接口响应 JSON，属于 response
//  cache；改表后若不走失效逻辑，会读到旧目录（你当前只在别处按需失效，物品表若后台改需remove
//  或 TTL）。 • 为何适合
//  LFU：目录读多、行数少、全服共享，和「每用户一条」的数据相比更适合进程内缓存。

// ─── GET /items
// 物品目录（LFU）────────────────────────────────────────────────
static std::string handleGetItems() {
  // EchoServer::onMessage 里在 method == "GET" && path == "/items"
  // 时调用它，所以缓存键用 "GET/items" 与路由一致。
  const std::string cacheKey = "GET/items";
  std::string cached;
  if (g_cache->get(cacheKey, cached))
    return httpResp(200, cached);

  auto db = g_dbPool->get();
  MYSQL_RES *res =
      db->query("SELECT id,name,description FROM item_defs ORDER BY id");
  if (!res)
    return httpResp(500, "{\"error\":\"db error\"}");
  // 手拼 JSON 数组
  std::ostringstream body;
  body << "[";
  MYSQL_ROW row;
  bool first = true;
  while ((row = mysql_fetch_row(res))) {
    if (!first)
      body << ",";
    first = false;
    const char *n = row[1] ? row[1] : "";
    const char *d = row[2] ? row[2] : "";
    body << "{\"id\":" << row[0] << ",\"name\":\"" << db->escape(n) << "\""
         << ",\"description\":\"" << db->escape(d) << "\"}";
  }
  body << "]";
  mysql_free_result(res);

  std::string bodyStr = body.str();
  g_cache->put(cacheKey, bodyStr);
  return httpResp(200, bodyStr);
}

// ─── POST /players 创建角色 body: {"name":"Alice"} ───────────────────────────
static std::string handlePostPlayer(const std::string &body) {
  std::string name;
  if (!jsonGetString(body, "name", name))
    return httpResp(400, "{\"error\":\"invalid params, need name\"}");

  auto db = g_dbPool->get();
  if (!db->begin())
    return httpResp(500, "{\"error\":\"begin failed\"}");

  std::ostringstream ins;
  ins << "INSERT INTO players(name) VALUES('" << db->escape(name) << "')";
  if (!db->execute(ins.str())) {
    db->rollback();
    return httpResp(400, "{\"error\":\"insert failed, duplicate name?\"}");
  }

  uint64_t pid = db->lastInsertId();
  std::ostringstream inv1, inv2;
  inv1 << "INSERT INTO inventory(player_id,item_id,qty) VALUES(" << pid
       << ",1,3)";
  inv2 << "INSERT INTO inventory(player_id,item_id,qty) VALUES(" << pid
       << ",2,1)";
  if (!db->execute(inv1.str()) || !db->execute(inv2.str())) {
    db->rollback();
    return httpResp(500, "{\"error\":\"starter items failed\"}");
  }
  if (!db->commit()) {
    db->rollback();
    return httpResp(500, "{\"error\":\"commit failed\"}");
  }

  std::ostringstream resp;
  resp << "{\"player_id\":" << pid << ",\"name\":\"" << db->escape(name)
       << "\"}";
  return httpResp(200, resp.str());
}

// ─── GET /players/:id ────────────────────────────────────────────────────────
static std::string handleGetPlayer(int player_id) {
  auto db = g_dbPool->get();

  std::ostringstream qp;
  qp << "SELECT id,name FROM players WHERE id=" << player_id;
  MYSQL_RES *pr = db->query(qp.str());
  if (!pr || mysql_num_rows(pr) == 0) {
    if (pr)
      mysql_free_result(pr);
    return httpResp(404, "{\"error\":\"player not found\"}");
  }
  MYSQL_ROW prow = mysql_fetch_row(pr);
  std::string pname = prow[1] ? prow[1] : "";
  mysql_free_result(pr);

  std::ostringstream qi;
  qi << "SELECT i.item_id,d.name,i.qty FROM inventory i "
     << "JOIN item_defs d ON d.id=i.item_id WHERE i.player_id=" << player_id;
  MYSQL_RES *ir = db->query(qi.str());
  if (!ir)
    return httpResp(500, "{\"error\":\"db error\"}");

  std::ostringstream body;
  body << "{\"id\":" << player_id << ",\"name\":\"" << db->escape(pname) << "\""
       << ",\"inventory\":[";
  MYSQL_ROW row;
  bool first = true;
  while ((row = mysql_fetch_row(ir))) {
    if (!first)
      body << ",";
    first = false;
    body << "{\"item_id\":" << row[0] << ",\"name\":\"" << db->escape(row[1])
         << "\""
         << ",\"qty\":" << row[2] << "}";
  }
  body << "]}";
  mysql_free_result(ir);
  return httpResp(200, body.str());
}

// ─── POST /inventory/use 消耗道具（事务 +
// 行锁）─────────────────────────────── body:
// {"player_id":1,"item_id":1,"qty":2}
static std::string handlePostInventoryUse(const std::string &body) {
  int player_id = 0, item_id = 0, qty = 0;
  if (!jsonGetInt(body, "player_id", player_id) ||
      !jsonGetInt(body, "item_id", item_id) || !jsonGetInt(body, "qty", qty) ||
      qty <= 0)
    return httpResp(400, "{\"error\":\"invalid params\"}");

  auto db = g_dbPool->get();
  if (!db->begin())
    return httpResp(500, "{\"error\":\"begin failed\"}");

  std::ostringstream qss;
  qss << "SELECT qty FROM inventory WHERE player_id=" << player_id
      << " AND item_id=" << item_id << " FOR UPDATE";
  MYSQL_RES *res = db->query(qss.str());
  if (!res || mysql_num_rows(res) == 0) {
    if (res)
      mysql_free_result(res);
    db->rollback();
    return httpResp(404, "{\"error\":\"no such stack\"}");
  }
  MYSQL_ROW row = mysql_fetch_row(res);
  int stock = std::stoi(row[0]);
  mysql_free_result(res);

  if (stock < qty) {
    db->rollback();
    return httpResp(400, "{\"error\":\"insufficient qty\"}");
  }

  std::ostringstream upd;
  upd << "UPDATE inventory SET qty=qty-" << qty
      << " WHERE player_id=" << player_id << " AND item_id=" << item_id;
  if (!db->execute(upd.str())) {
    db->rollback();
    return httpResp(500, "{\"error\":\"update failed\"}");
  }

  std::ostringstream audit;
  audit << "INSERT INTO audit_log(player_id,action,detail) VALUES(" << player_id
        << ",'use_item','item_id=" << item_id << ",qty=" << qty << "')";
  if (!db->execute(audit.str())) {
    db->rollback();
    return httpResp(500, "{\"error\":\"audit failed\"}");
  }

  if (!db->commit()) {
    db->rollback();
    return httpResp(500, "{\"error\":\"commit failed\"}");
  }

  std::ostringstream resp;
  resp << "{\"ok\":true,\"player_id\":" << player_id
       << ",\"item_id\":" << item_id << ",\"used\":" << qty << "}";
  return httpResp(200, resp.str());
}

// ─── EchoServer ──────────────────────────────────────────────────────────────
class EchoServer {
public:
  EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
      : server_(loop, addr, name), loop_(loop) {
    server_.setConnectionCallback(
        std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&EchoServer::onMessage, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
    server_.setThreadNum(3);
  }
  void start() { server_.start(); }

private:
  void onConnection(const TcpConnectionPtr &conn) {
    if (conn->connected())
      LOG_INFO << "Connection UP :" << conn->peerAddress().toIpPort().c_str();
    else
      LOG_INFO << "Connection DOWN :" << conn->peerAddress().toIpPort().c_str();
  }

  void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp) {
    std::string raw = buf->retrieveAllAsString();

    std::string method, path, body;
    parseRequest(raw, method, path, body);

    std::string resp;
    if (method == "GET" && path == "/items") {
      resp = handleGetItems();
    } else if (method == "POST" && path == "/players") {
      resp = handlePostPlayer(body);
    } else if (method == "GET" && path.size() > 9 &&
               path.substr(0, 9) == "/players/") {
      int id = std::stoi(path.substr(9));
      resp = handleGetPlayer(id);
    } else if (method == "POST" && path == "/inventory/use") {
      resp = handlePostInventoryUse(body);
    } else {
      resp = httpResp(404, "{\"error\":\"not found\"}");
    }

    conn->send(resp);
  }

  TcpServer server_;
  EventLoop *loop_;
};

// ─── 日志 ────────────────────────────────────────────────────────────────────
AsyncLogging *g_asyncLog = nullptr;
void asyncLog(const char *msg, int len) {
  if (g_asyncLog)
    g_asyncLog->append(msg, len);
  fwrite(msg, 1, len, stdout);
}

// ─── main ────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
  mkdir("logs", 0755);
  std::ostringstream logPath;
  logPath << "logs/" << ::basename(argv[0]);
  AsyncLogging log(logPath.str(), kRollSize);
  g_asyncLog = &log;
  Logger::setOutput(asyncLog);
  log.start();

  memoryPool::HashBucket::initMemoryPool();

  KamaCache::KLfuCache<std::string, std::string> lfu(128);
  g_cache = &lfu;

  // 默认连接 game_db；账号密码与 db/init.sql 中 GRANT 一致
  DBPool dbPool("127.0.0.1", "takeaway", "takeaway123", "game_db", 3306, 8);
  g_dbPool = &dbPool;

  EventLoop loop;
  InetAddress addr(8080);
  EchoServer server(&loop, addr, "GameServer");
  server.start();

  std::cout << "=== Game HTTP Server on :8080 (db=game_db) ===" << std::endl;
  loop.loop();
  log.stop();
}
