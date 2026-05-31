#pragma once

#include "libCacheSim/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef cache_t *(*eviction_algo_mod_init_fn)(
    const common_cache_params_t ccache_params,
    const char *cache_specific_params);

typedef struct eviction_algo_mod_entry {
  const char *name;
  eviction_algo_mod_init_fn init_func;
} eviction_algo_mod_entry_t;

cache_t *create_cache_from_eviction_algo_module(
    const char *eviction_algo,
    common_cache_params_t ccache_params,
    const char *eviction_params);

void print_available_eviction_algo_modules(void);

#ifdef __cplusplus
}
#endif
