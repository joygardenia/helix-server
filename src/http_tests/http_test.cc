#include "Buffer.h"
#include "http/HttpContext.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "http/Router.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

static void feed(Buffer &buf, const std::string &s) {
  buf.append(s.c_str(), s.size());
}

static void testSplitHeader() {
  HttpContext ctx;
  Buffer buf;
  feed(buf, "GET /items HTTP/1.1\r\nHost: x");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kIncomplete);
  feed(buf, "\r\n\r\n");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kComplete);
  CHECK(ctx.request().method() == HttpRequest::kGet);
  CHECK(ctx.request().path() == "/items");
}

static void testSplitBody() {
  HttpContext ctx;
  Buffer buf;
  feed(buf, "POST /players HTTP/1.1\r\nContent-Length: 12\r\n\r\n{\"name\":\"a");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kIncomplete);
  feed(buf, "\"}");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kComplete);
  CHECK(ctx.request().body() == "{\"name\":\"a\"}");
}

static void testTwoRequestsOneRead() {
  HttpContext ctx;
  Buffer buf;
  feed(buf, "GET /a HTTP/1.1\r\n\r\nGET /b HTTP/1.1\r\n\r\n");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kComplete);
  CHECK(ctx.request().path() == "/a");
  ctx.reset();
  CHECK(ctx.parseRequest(&buf) == HttpContext::kComplete);
  CHECK(ctx.request().path() == "/b");
}

static void testPostWithoutBody() {
  HttpContext ctx;
  Buffer buf;
  feed(buf, "POST /players HTTP/1.1\r\nHost: x\r\n\r\n");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kBadRequest);
  CHECK(ctx.errorCode() == 411);
}

static void testInvalidContentLength() {
  HttpContext ctx;
  Buffer buf;
  feed(buf, "POST /players HTTP/1.1\r\nContent-Length: abc\r\n\r\n");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kBadRequest);
  CHECK(ctx.errorCode() == 411);
}

static void testMalformedRequestLine() {
  HttpContext ctx;
  Buffer buf;
  feed(buf, "GETONLY\r\n\r\n");
  CHECK(ctx.parseRequest(&buf) == HttpContext::kBadRequest);

  HttpContext ctx2;
  Buffer buf2;
  feed(buf2, "GET /x NOTHTTP\r\n\r\n");
  CHECK(ctx2.parseRequest(&buf2) == HttpContext::kBadRequest);
}

static void testOversized() {
  HttpContext ctx(64);
  Buffer buf;
  std::string big = "GET /" + std::string(200, 'a') + " HTTP/1.1\r\n\r\n";
  feed(buf, big);
  CHECK(ctx.parseRequest(&buf) == HttpContext::kTooLarge);
  CHECK(ctx.errorCode() == 413);
}

static void testRouteMatchMiss() {
  Router router;
  bool hit = false;
  router.addRoute("GET", "/items",
                  [&hit](const HttpRequest &, HttpResponse *resp) {
                    hit = true;
                    resp->setStatusCode(HttpResponse::k200Ok);
                    resp->setBody("ok");
                  });
  router.addPrefixRoute("GET", "/players/",
                        [](const HttpRequest &, HttpResponse *resp) {
                          resp->setStatusCode(HttpResponse::k200Ok);
                          resp->setBody("player");
                        });

  HttpRequest req;
  req.setMethod(HttpRequest::kGet);
  req.setPath("/items");
  HttpResponse resp(false);
  CHECK(router.route(req, &resp));
  CHECK(hit);
  CHECK(resp.toString().find("HTTP/1.1 200 OK") == 0);

  HttpRequest prefixReq;
  prefixReq.setMethod(HttpRequest::kGet);
  prefixReq.setPath("/players/42");
  HttpResponse prefixResp(false);
  CHECK(router.route(prefixReq, &prefixResp));
  CHECK(prefixResp.body() == "player");

  HttpRequest miss;
  miss.setMethod(HttpRequest::kGet);
  miss.setPath("/nope");
  HttpResponse missResp(false);
  CHECK(!router.route(miss, &missResp));
}

static void testResponseSerialize() {
  HttpResponse resp(false);
  resp.setStatusCode(HttpResponse::k200Ok);
  resp.setContentType("application/json");
  resp.setBody("{\"ok\":true}");
  std::string s = resp.toString();
  CHECK(s.find("HTTP/1.1 200 OK\r\n") == 0);
  CHECK(s.find("Content-Length: 11\r\n") != std::string::npos);
  CHECK(s.find("Connection: keep-alive\r\n") != std::string::npos);
  CHECK(s.find("{\"ok\":true}") != std::string::npos);
}

static void testResponseErrorStatus() {
  HttpResponse resp(true);
  resp.setStatusCode(HttpResponse::k503ServiceUnavailable);
  std::string s = resp.toString();
  CHECK(s.find("HTTP/1.1 503 Service Unavailable") == 0);
  CHECK(s.find("Connection: close\r\n") != std::string::npos);
}

int main() {
  testSplitHeader();
  testSplitBody();
  testTwoRequestsOneRead();
  testPostWithoutBody();
  testInvalidContentLength();
  testMalformedRequestLine();
  testOversized();
  testRouteMatchMiss();
  testResponseSerialize();
  testResponseErrorStatus();

  if (g_failures == 0)
    std::printf("http_test: ALL PASS\n");
  else
    std::printf("http_test: %d FAILURES\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
