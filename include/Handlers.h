#pragma once

#include <string>

#include "http/Router.h"

namespace KamaCache {
template <typename Key, typename Value> class KHashLfuCache;
}

class DBPool;
class ConsistentHash;

struct BusinessContext {
  DBPool *dbPool;
  KamaCache::KHashLfuCache<std::string, std::string> *cache;
  ConsistentHash *hashRing;
};

void registerBusinessRoutes(Router &router, BusinessContext *ctx);
