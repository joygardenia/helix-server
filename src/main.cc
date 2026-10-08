#include <sstream>
#include <string>
#include <sys/stat.h>

#include "AsyncLogging.h"
#include "ConsistentHash.h"
#include "DBPool.h"
#include "EtcdClient.h"
#include "Handlers.h"
#include "HttpServer.h"
#include "LFU.h"
#include "Logger.h"
#include "TcpConnection.h"
#include "ThreadPool.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "memoryPool.h"

static const off_t kRollSize = 1 * 1024 * 1024;

static DBPool *g_dbPool = nullptr;
static KamaCache::KHashLfuCache<std::string, std::string> *g_cache = nullptr;
static EtcdClient *g_etcd = nullptr;
static ConsistentHash *g_hashRing = nullptr;

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

  BusinessContext business;
  business.dbPool = g_dbPool;
  business.cache = g_cache;
  business.hashRing = g_hashRing;

  ThreadPool businessPool(8, 1024);
  businessPool.start();

  LOG_INFO << "Starting TcpServer on port: " << port;
  // InetAddress 构造函数参数顺序是 (port, ip)
  InetAddress addr(port, "0.0.0.0");
  HttpServer server(&loop, addr, "GameServer", &businessPool);
  server.setThreadNum(3);
  server.setIdleSeconds(60);
  registerBusinessRoutes(server.router(), &business);
  server.router().addRoute("GET", "/stats", [&server](const HttpRequest &,
                                                     HttpResponse *resp) {
    resp->setStatusCode(HttpResponse::k200Ok);
    resp->setContentType("application/json");
    resp->setBody(server.dumpStats());
  });
  server.setConnectionCallback([](const TcpConnectionPtr &conn) {
    if (conn->connected()) {
      LOG_INFO << "Connection UP :" << conn->peerAddress().toIpPort().c_str();
    } else {
      LOG_INFO << "Connection DOWN :" << conn->peerAddress().toIpPort().c_str();
    }
  });
  server.start();

  LOG_INFO << "=== Game HTTP Server [" << nodeName << "] on :" << port
           << " (db=game_db) ===";

  loop.loop();
  businessPool.stop();
  log.stop();
}
