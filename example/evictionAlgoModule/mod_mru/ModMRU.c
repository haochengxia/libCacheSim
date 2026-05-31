#include "libCacheSim/evictionAlgo.h"

cache_t *ModMRU_init(const common_cache_params_t ccache_params,
                     const char *cache_specific_params) {
  cache_t *cache = MRU_init(ccache_params, cache_specific_params);
  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "ModMRU");
  return cache;
}
