#pragma once

#include "libCacheSim/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef cache_t *(*eviction_algo_init_fn_t)(
    const common_cache_params_t ccache_params,
    const char *cache_specific_params
);

int libcachesim_register_eviction_algo(const char *name,
                                       eviction_algo_init_fn_t init_fn);

eviction_algo_init_fn_t libcachesim_find_eviction_algo(const char *name);


void libcachesim_register_extra_eviction_algos(void);

#ifdef __cplusplus
}
#endif
