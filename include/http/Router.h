#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "HttpRequest.h"
#include "HttpResponse.h"

class Router {
public:
  using Handler = std::function<void(const HttpRequest&, HttpResponse*)>;

  void addRoute(const std::string& method, const std::string& path,
                Handler handler);
  void addPrefixRoute(const std::string& method, const std::string& prefix,
                      Handler handler);
  bool route(const HttpRequest& req, HttpResponse* resp) const;

private:
  struct PrefixRoute {
    std::string key;
    std::string prefix;
    Handler handler;
  };

  std::unordered_map<std::string, Handler> exactRoutes_;
  std::vector<PrefixRoute> prefixRoutes_;
};
