#include <sstream>
#include <string>
#include <sys/stat.h>

#include "AsyncLogging.h"
#include "ConsistentHash.h"
#include "DBPool.h"
#include "EtcdClient.h"
#include "LFU.h"
#include "Logger.h"
#include "TcpServer.h"
#include "memoryPool.h"

static const off_t kRollSize = 1 * 1024 * 1024;

// ─── 全局资源 ────────────────────────────────────────────────────────────────
static DBPool *g_dbPool = nullptr;
// LFU 分片缓存：key=路由字符串, value=响应 body（读多写少的物品目录）
static KamaCache::KHashLfuCache<std::string, std::string> *g_cache = nullptr;

static EtcdClient *g_etcd = nullptr;
static ConsistentHash *g_hashRing = nullptr;

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

static std::string handleGetItems() {
  const std::string cacheKey = "GET/items";
  std::string cached;
  if (g_cache->get(cacheKey, cached))
    return httpResp(200, cached);

  if (!g_dbPool)
    return httpResp(500, "{\"error\":\"db pool not init\"}");

  auto db = g_dbPool->get();
  MYSQL_RES *res =
      db->query("SELECT id,name,description FROM item_defs ORDER BY id");
  if (!res)
    return httpResp(500, "{\"error\":\"db error\"}");

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

static std::string handlePostPlayer(const std::string &body) {
  std::string name;
  if (!jsonGetString(body, "name", name))
    return httpResp(400, "{\"error\":\"invalid params, need name\"}");

  if (!g_dbPool)
    return httpResp(500, "{\"error\":\"db pool not init\"}");

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

static std::string handleGetPlayer(int player_id) {
  if (!g_dbPool)
    return httpResp(500, "{\"error\":\"db pool not init\"}");

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

static std::string handleDiscover(const std::string &path) {
  size_t pos = path.find("key=");
  if (pos == std::string::npos)
    return httpResp(400, "{\"error\":\"missing key\"}");
  std::string key = path.substr(pos + 4);
  if (g_hashRing) {
    std::string node = g_hashRing->getNode(key);
    return httpResp(200, "{\"key\":\"" + key + "\",\"node\":\"" + node + "\"}");
  }
  return httpResp(500, "{\"error\":\"hash ring not init\"}");
}

static std::string handlePostInventoryUse(const std::string &body) {
  int player_id = 0, item_id = 0, qty = 0;
  if (!jsonGetInt(body, "player_id", player_id) ||
      !jsonGetInt(body, "item_id", item_id) || !jsonGetInt(body, "qty", qty) ||
      qty <= 0)
    return httpResp(400, "{\"error\":\"invalid params\"}");

  if (!g_dbPool)
    return httpResp(500, "{\"error\":\"db pool not init\"}");

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
    server_.setIdleSeconds(60);
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
      try {
        int id = std::stoi(path.substr(9));
        resp = handleGetPlayer(id);
      } catch (...) {
        resp = httpResp(400, "{\"error\":\"invalid player id\"}");
      }
    } else if (method == "POST" && path == "/inventory/use") {
      resp = handlePostInventoryUse(body);
    } else if (method == "GET" && path.size() >= 9 &&
               path.substr(0, 9) == "/discover") {
      resp = handleDiscover(path);
    } else {
      resp = httpResp(404, "{\"error\":\"not found\"}");
    }

    conn->send(resp);
  }

  TcpServer server_;
  EventLoop *loop_;
};

AsyncLogging *g_asyncLog = nullptr;
void asyncLog(const char *msg, int len) {
  if (g_asyncLog)
    g_asyncLog->append(msg, len);
  fwrite(msg, 1, len, stdout);
}

int main(int argc, char *argv[]) {
  int port = 8080;
  std::string nodeName = "node1";
  std::string etcdHost = "127.0.0.1";
  std::string dbHost = "127.0.0.1";

  if (argc > 1)
    nodeName = argv[1];
  if (argc > 2)
    port = std::stoi(argv[2]);
  if (argc > 3)
    etcdHost = argv[3];
  if (argc > 4)
    dbHost = argv[4];

  mkdir("logs", 0755);
  std::ostringstream logPath;
  logPath << "logs/" << nodeName;
  AsyncLogging log(logPath.str(), kRollSize);
  g_asyncLog = &log;
  Logger::setOutput(asyncLog);
  log.start();

  memoryPool::HashBucket::initMemoryPool();

  KamaCache::KHashLfuCache<std::string, std::string> lfu(128, 8);
  g_cache = &lfu;

  ConsistentHash hashRing(100);
  hashRing.addNode("node1");
  hashRing.addNode("node2");
  hashRing.addNode("node3");
  g_hashRing = &hashRing;

  EventLoop loop;

  EtcdClient etcd(etcdHost, 2379);
  g_etcd = &etcd;
  bool etcdOk = etcd.registerService("/game/" + nodeName,
                                     "127.0.0.1:" + std::to_string(port), 10);
  if (etcdOk) {
    LOG_INFO << "Service registered to etcd: /game/" << nodeName;
  } else {
    LOG_ERROR << "Etcd registration failed";
  }

  try {
    g_dbPool = new DBPool(dbHost.c_str(), "takeaway", "takeaway123", "game_db",
                          3306, 8);
    LOG_INFO << "DBPool initialized successfully";
  } catch (const std::exception &e) {
    LOG_ERROR << "DBPool init failed: " << e.what();
    g_dbPool = nullptr;
  }

  if (etcdOk) {
    LOG_INFO << "Starting etcd keepalive timer...";
    EtcdClient *etcdPtr = g_etcd;
    loop.runEvery(5.0, [etcdPtr]() {
      if (etcdPtr) {
        etcdPtr->keepAlive();
      }
    });
  }

  LOG_INFO << "Starting TcpServer on port: " << port;
  // InetAddress 构造函数参数顺序是 (port, ip)
  InetAddress addr(port, "0.0.0.0");
  EchoServer server(&loop, addr, "GameServer");
  server.start();

  LOG_INFO << "=== Game HTTP Server [" << nodeName << "] on :" << port
           << " (db=game_db) ===";

  loop.loop();
  log.stop();
}
