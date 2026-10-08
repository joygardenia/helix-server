#include "Handlers.h"

#include <sstream>
#include <string>

#include "ConsistentHash.h"
#include "DBPool.h"
#include "LFU.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"

namespace {

bool jsonGetInt(const std::string &json, const std::string &key, int &val) {
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

bool jsonGetString(const std::string &json, const std::string &key,
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

void reply(HttpResponse *resp, HttpResponse::HttpStatusCode code,
           const std::string &body) {
  resp->setStatusCode(code);
  resp->setBody(body);
}

void handleGetItems(BusinessContext *ctx, HttpResponse *resp) {
  const std::string cacheKey = "GET/items";
  std::string cached;
  if (ctx->cache && ctx->cache->get(cacheKey, cached)) {
    reply(resp, HttpResponse::k200Ok, cached);
    return;
  }
  if (!ctx->dbPool) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"db pool not init\"}");
    return;
  }
  auto db = ctx->dbPool->get();
  MYSQL_RES *res =
      db->query("SELECT id,name,description FROM item_defs ORDER BY id");
  if (!res) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"db error\"}");
    return;
  }
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
  if (ctx->cache)
    ctx->cache->put(cacheKey, bodyStr);
  reply(resp, HttpResponse::k200Ok, bodyStr);
}

void handlePostPlayer(BusinessContext *ctx, const std::string &body,
                      HttpResponse *resp) {
  std::string name;
  if (!jsonGetString(body, "name", name)) {
    reply(resp, HttpResponse::k400BadRequest,
          "{\"error\":\"invalid params, need name\"}");
    return;
  }
  if (!ctx->dbPool) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"db pool not init\"}");
    return;
  }
  auto db = ctx->dbPool->get();
  if (!db->begin()) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"begin failed\"}");
    return;
  }
  std::ostringstream ins;
  ins << "INSERT INTO players(name) VALUES('" << db->escape(name) << "')";
  if (!db->execute(ins.str())) {
    db->rollback();
    reply(resp, HttpResponse::k400BadRequest,
          "{\"error\":\"insert failed, duplicate name?\"}");
    return;
  }
  uint64_t pid = db->lastInsertId();
  std::ostringstream inv1, inv2;
  inv1 << "INSERT INTO inventory(player_id,item_id,qty) VALUES(" << pid
       << ",1,3)";
  inv2 << "INSERT INTO inventory(player_id,item_id,qty) VALUES(" << pid
       << ",2,1)";
  if (!db->execute(inv1.str()) || !db->execute(inv2.str())) {
    db->rollback();
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"starter items failed\"}");
    return;
  }
  if (!db->commit()) {
    db->rollback();
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"commit failed\"}");
    return;
  }
  std::ostringstream out;
  out << "{\"player_id\":" << pid << ",\"name\":\"" << db->escape(name)
      << "\"}";
  reply(resp, HttpResponse::k200Ok, out.str());
}

void handleGetPlayer(BusinessContext *ctx, const HttpRequest &req,
                     HttpResponse *resp) {
  int playerId = 0;
  try {
    playerId = std::stoi(req.path().substr(9));
  } catch (...) {
    reply(resp, HttpResponse::k400BadRequest,
          "{\"error\":\"invalid player id\"}");
    return;
  }
  if (!ctx->dbPool) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"db pool not init\"}");
    return;
  }
  auto db = ctx->dbPool->get();

  std::ostringstream qp;
  qp << "SELECT id,name FROM players WHERE id=" << playerId;
  MYSQL_RES *pr = db->query(qp.str());
  if (!pr || mysql_num_rows(pr) == 0) {
    if (pr)
      mysql_free_result(pr);
    reply(resp, HttpResponse::k404NotFound, "{\"error\":\"player not found\"}");
    return;
  }
  MYSQL_ROW prow = mysql_fetch_row(pr);
  std::string pname = prow[1] ? prow[1] : "";
  mysql_free_result(pr);

  std::ostringstream qi;
  qi << "SELECT i.item_id,d.name,i.qty FROM inventory i "
     << "JOIN item_defs d ON d.id=i.item_id WHERE i.player_id=" << playerId;
  MYSQL_RES *ir = db->query(qi.str());
  if (!ir) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"db error\"}");
    return;
  }

  std::ostringstream out;
  out << "{\"id\":" << playerId << ",\"name\":\"" << db->escape(pname)
      << "\",\"inventory\":[";
  MYSQL_ROW row;
  bool first = true;
  while ((row = mysql_fetch_row(ir))) {
    if (!first)
      out << ",";
    first = false;
    out << "{\"item_id\":" << row[0] << ",\"name\":\"" << db->escape(row[1])
        << "\""
        << ",\"qty\":" << row[2] << "}";
  }
  out << "]}";
  mysql_free_result(ir);
  reply(resp, HttpResponse::k200Ok, out.str());
}

void handlePostInventoryUse(BusinessContext *ctx, const std::string &body,
                            HttpResponse *resp) {
  int playerId = 0, itemId = 0, qty = 0;
  if (!jsonGetInt(body, "player_id", playerId) ||
      !jsonGetInt(body, "item_id", itemId) || !jsonGetInt(body, "qty", qty) ||
      qty <= 0) {
    reply(resp, HttpResponse::k400BadRequest, "{\"error\":\"invalid params\"}");
    return;
  }
  if (!ctx->dbPool) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"db pool not init\"}");
    return;
  }
  auto db = ctx->dbPool->get();
  if (!db->begin()) {
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"begin failed\"}");
    return;
  }
  std::ostringstream qss;
  qss << "SELECT qty FROM inventory WHERE player_id=" << playerId
      << " AND item_id=" << itemId << " FOR UPDATE";
  MYSQL_RES *res = db->query(qss.str());
  if (!res || mysql_num_rows(res) == 0) {
    if (res)
      mysql_free_result(res);
    db->rollback();
    reply(resp, HttpResponse::k404NotFound, "{\"error\":\"no such stack\"}");
    return;
  }
  MYSQL_ROW row = mysql_fetch_row(res);
  int stock = std::stoi(row[0]);
  mysql_free_result(res);
  if (stock < qty) {
    db->rollback();
    reply(resp, HttpResponse::k400BadRequest,
          "{\"error\":\"insufficient qty\"}");
    return;
  }
  std::ostringstream upd;
  upd << "UPDATE inventory SET qty=qty-" << qty
      << " WHERE player_id=" << playerId << " AND item_id=" << itemId;
  if (!db->execute(upd.str())) {
    db->rollback();
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"update failed\"}");
    return;
  }
  std::ostringstream audit;
  audit << "INSERT INTO audit_log(player_id,action,detail) VALUES(" << playerId
        << ",'use_item','item_id=" << itemId << ",qty=" << qty << "')";
  if (!db->execute(audit.str())) {
    db->rollback();
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"audit failed\"}");
    return;
  }
  if (!db->commit()) {
    db->rollback();
    reply(resp, HttpResponse::k500InternalServerError,
          "{\"error\":\"commit failed\"}");
    return;
  }
  std::ostringstream out;
  out << "{\"ok\":true,\"player_id\":" << playerId << ",\"item_id\":" << itemId
      << ",\"used\":" << qty << "}";
  reply(resp, HttpResponse::k200Ok, out.str());
}

void handleDiscover(BusinessContext *ctx, const std::string &path,
                    HttpResponse *resp) {
  size_t pos = path.find("key=");
  if (pos == std::string::npos) {
    reply(resp, HttpResponse::k400BadRequest, "{\"error\":\"missing key\"}");
    return;
  }
  std::string key = path.substr(pos + 4);
  if (ctx->hashRing) {
    std::string node = ctx->hashRing->getNode(key);
    reply(resp, HttpResponse::k200Ok,
          "{\"key\":\"" + key + "\",\"node\":\"" + node + "\"}");
    return;
  }
  reply(resp, HttpResponse::k500InternalServerError,
        "{\"error\":\"hash ring not init\"}");
}

}

void registerBusinessRoutes(Router &router, BusinessContext *ctx) {
  router.addRoute("GET", "/items",
                  [ctx](const HttpRequest &, HttpResponse *resp) {
                    handleGetItems(ctx, resp);
                  });
  router.addRoute("POST", "/players",
                  [ctx](const HttpRequest &req, HttpResponse *resp) {
                    handlePostPlayer(ctx, req.body(), resp);
                  });
  router.addPrefixRoute("GET", "/players/",
                        [ctx](const HttpRequest &req, HttpResponse *resp) {
                          handleGetPlayer(ctx, req, resp);
                        });
  router.addRoute("POST", "/inventory/use",
                  [ctx](const HttpRequest &req, HttpResponse *resp) {
                    handlePostInventoryUse(ctx, req.body(), resp);
                  });
  router.addPrefixRoute("GET", "/discover",
                        [ctx](const HttpRequest &req, HttpResponse *resp) {
                          handleDiscover(ctx, req.path(), resp);
                        });
}
