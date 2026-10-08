#include "http/Router.h"

void Router::addRoute(const std::string& method, const std::string& path,
                      Handler handler) {
  exactRoutes_[method + " " + path] = handler;
}

void Router::addPrefixRoute(const std::string& method,
                            const std::string& prefix, Handler handler) {
  PrefixRoute route = {method + " " + prefix, prefix, handler};
  prefixRoutes_.push_back(route);
}

bool Router::route(const HttpRequest& req, HttpResponse* resp) const {
  const std::string method = std::string(req.methodString()) + " ";
  const std::string methodPath = method + req.path();
  const std::unordered_map<std::string, Handler>::const_iterator exact =
      exactRoutes_.find(methodPath);
  if (exact != exactRoutes_.end()) {
    exact->second(req, resp);
    return true;
  }

  const PrefixRoute* best = 0;
  for (std::vector<PrefixRoute>::const_iterator it = prefixRoutes_.begin();
       it != prefixRoutes_.end(); ++it) {
    if (it->key.compare(0, method.size(), method) != 0)
      continue;
    if (req.path().size() < it->prefix.size() ||
        req.path().compare(0, it->prefix.size(), it->prefix) != 0)
      continue;
    if (best == 0 || it->prefix.size() > best->prefix.size())
      best = &*it;
  }
  if (best == 0)
    return false;
  best->handler(req, resp);
  return true;
}
