//
// S4FIFOv2 adds an explicit state transition step when parameters change.
// The goal is to keep the "new parameters + old cache state" mismatch from
// leaking into subsequent requests.
//
//  S4FIFOv2.c
//  libCacheSim

#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/evictionAlgo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  cache_t *small_fifo;
  cache_t *ghost_fifo;
  cache_t *main_fifo;
  bool hit_on_ghost;

  // bool collect_features;  // whether to collect features for learning-based
  // cache
  //                        // replacement, False by default

  int hit_on_ghost_freq;  // frequency of the object in ghost fifo
  int move_to_main_threshold;
  double small_size_ratio;
  double ghost_size_ratio;
  double small_skip_ratio;
  int ghost_to_main_threshold;

  int64_t after_n_reqs;
  // new parameters for dynamic adjustment
  double ns;              // double small_size_ratio;
  double ng;              // ghost_size_ratio;
  double nk;              // small_skip_ratio;
  int ngt;                // ghost_to_main_threshold;
  int nst;                // move_to_main_threshold; aka small_to_main_threshold
  int64_t request_count;  // count the number of requests, used for dynamic
                          // adjustment

  bool has_evicted;
  bool has_adjusted;
  int transition_type;  // not used yet
  request_t *req_local;

  int64_t s_counter;  // is used for small skip logic

  // custom hit ratio recording
  int64_t miss_count_after_adjustment;
  int64_t req_count_after_adjustment;
} S4FIFO_params_t;

static const char *DEFAULT_CACHE_PARAMS =
    "small-size-ratio=0.10,ghost-size-ratio=0.90,move-to-main-threshold=2,"
    "small-skip-ratio=0,ghost-to-main-threshold=0,after-n-reqs=1000,"
    "ns=0.10,ng=0.90,nst=2,ngt=0,nk=0.10";

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************
static void S4FIFOv2_free(cache_t *cache);
static bool S4FIFOv2_get(cache_t *cache, const request_t *req);

static cache_obj_t *S4FIFOv2_find(cache_t *cache, const request_t *req,
                                const bool update_cache);
static cache_obj_t *S4FIFOv2_insert(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFOv2_to_evict(cache_t *cache, const request_t *req);
static void S4FIFOv2_evict(cache_t *cache, const request_t *req);
static bool S4FIFOv2_remove(cache_t *cache, const obj_id_t obj_id);
static inline int64_t S4FIFOv2_get_occupied_byte(const cache_t *cache);
static inline int64_t S4FIFOv2_get_n_obj(const cache_t *cache);
static inline bool S4FIFOv2_can_insert(cache_t *cache, const request_t *req);
static void S4FIFOv2_parse_params(cache_t *cache,
                                  const char *cache_specific_params);

static void S4FIFOv2_evict_small(cache_t *cache, const request_t *req);
static void S4FIFOv2_evict_main(cache_t *cache, const request_t *req);
static void S4FIFOv2_apply_adjustment(cache_t *cache);
static void S4FIFOv2_rebalance_after_adjustment(cache_t *cache);
static void S4FIFOv2_trim_fifo(cache_t *fifo, request_t *req_local);

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ***********************************************************************

cache_t *S4FIFOv2_init(const common_cache_params_t ccache_params,
                       const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("S4FIFOv2", ccache_params, cache_specific_params);
  cache->cache_init = S4FIFOv2_init;
  cache->cache_free = S4FIFOv2_free;
  cache->get = S4FIFOv2_get;
  cache->find = S4FIFOv2_find;
  cache->insert = S4FIFOv2_insert;
  cache->evict = S4FIFOv2_evict;
  cache->remove = S4FIFOv2_remove;
  cache->to_evict = S4FIFOv2_to_evict;
  cache->get_n_obj = S4FIFOv2_get_n_obj;
  cache->get_occupied_byte = S4FIFOv2_get_occupied_byte;
  cache->can_insert = S4FIFOv2_can_insert;

  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(S4FIFO_params_t));
  memset(cache->eviction_params, 0, sizeof(S4FIFO_params_t));
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->req_local = new_request();
  params->hit_on_ghost = false;

  S4FIFOv2_parse_params(cache, DEFAULT_CACHE_PARAMS);
  if (cache_specific_params != NULL) {
    S4FIFOv2_parse_params(cache, cache_specific_params);
  }

  int64_t small_fifo_size =
      (int64_t)ccache_params.cache_size * params->small_size_ratio;
  int64_t main_fifo_size = ccache_params.cache_size - small_fifo_size;
  int64_t ghost_fifo_size =
      (int64_t)(ccache_params.cache_size * params->ghost_size_ratio);

  common_cache_params_t ccache_params_local = ccache_params;
  ccache_params_local.cache_size = small_fifo_size;
  params->small_fifo = FIFO_init(ccache_params_local, NULL);
  params->has_evicted = false;
  params->has_adjusted = false;

  if (ghost_fifo_size > 0) {
    ccache_params_local.cache_size = ghost_fifo_size;
    params->ghost_fifo = FIFO_init(ccache_params_local, NULL);
    snprintf(params->ghost_fifo->cache_name, CACHE_NAME_ARRAY_LEN,
             "FIFO-ghost");
  } else {
    params->ghost_fifo = NULL;
  }

  ccache_params_local.cache_size = main_fifo_size;
  params->main_fifo = FIFO_init(ccache_params_local, NULL);

  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "S4FIFOv2-%.4lf-%d",
           params->small_size_ratio, params->move_to_main_threshold);

  /* S4FIFO: initialize the s_counter, since no obj enter small queue -> 0 */
  params->s_counter = 0;
  params->request_count = 0;

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void S4FIFOv2_free(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  // before free, dump the custom miss ratio after adjustment
  if (params->has_adjusted) {
    double miss_ratio_after_adjustment =
        (double)params->miss_count_after_adjustment /
        params->req_count_after_adjustment;
    printf("S4FIFO: miss ratio after adjustment: %.4lf\n",
           miss_ratio_after_adjustment);
  }
  free_request(params->req_local);
  params->small_fifo->cache_free(params->small_fifo);
  if (params->ghost_fifo != NULL) {
    params->ghost_fifo->cache_free(params->ghost_fifo);
  }
  params->main_fifo->cache_free(params->main_fifo);
  free(cache->eviction_params);
  cache_struct_free(cache);
}

/**
 * @brief this function is the user facing API
 * it performs the following logic
 *
 * ```
 * if obj in cache:
 *    update_metadata
 *    return true
 * else:
 *    if cache does not have enough space:
 *        evict until it has space to insert
 *    insert the object
 *    return false
 * ```
 *
 * @param cache
 * @param req
 * @return true if cache hit, false if cache miss
 */
static bool S4FIFOv2_get(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  DEBUG_ASSERT(params->small_fifo->get_occupied_byte(params->small_fifo) +
                   params->main_fifo->get_occupied_byte(params->main_fifo) <=
               cache->cache_size);

  bool cache_hit = cache_get_base(cache, req);

  // custom hit ratio recording
  if (params->has_adjusted) {
    params->req_count_after_adjustment++;
    if (!cache_hit) {
      params->miss_count_after_adjustment++;
    }
    // we can dump the virtual time id, object id, and hit/miss information for
    // each request to analyze the hit ratio in different time windows
    printf("request_id=%ld, vtime_id=%ld, obj_id=%ld, hit=%d\n",
           params->req_count_after_adjustment, req->clock_time, req->obj_id,
           cache_hit);
  }

  // Here we update the request count and check if we need to adjust the
  // parameters
  if (params->has_evicted) params->request_count++;
  if (params->request_count >= params->after_n_reqs && !params->has_adjusted) {
    S4FIFOv2_apply_adjustment(cache);
  }

  return cache_hit;
}

// ***********************************************************************
// ****                                                               ****
// ****       developer facing APIs (used by cache developer)         ****
// ****                                                               ****
// ***********************************************************************
/**
 * @brief find an object in the cache
 *
 * @param cache
 * @param req
 * @param update_cache whether to update the cache,
 *  if true, the object is promoted
 *  and if the object is expired, it is removed from the cache
 * @return the object or NULL if not found
 */
static cache_obj_t *S4FIFOv2_find(cache_t *cache, const request_t *req,
                                  const bool update_cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;

  // if update cache is false, we only check the fifo and main caches
  if (!update_cache) {
    cache_obj_t *obj = params->small_fifo->find(params->small_fifo, req, false);
    if (obj != NULL) {
      return obj;
    }
    obj = params->main_fifo->find(params->main_fifo, req, false);
    if (obj != NULL) {
      return obj;
    }
    return NULL;
  }

  /* update cache is true from now */
  params->hit_on_ghost = false;
  cache_obj_t *obj = params->small_fifo->find(params->small_fifo, req, true);
  if (obj != NULL) {
    /* S4FIFO: update the frequency */
    if ((int64_t)(-obj->time_stamp + params->s_counter) >=
        (int64_t)(params->small_skip_ratio * params->small_fifo->cache_size)) {
      obj->S4FIFO.freq += 1;
    }
    return obj;
  }

  // New logic:
  // if the obj find in the ghost fifo, check the freq > thres then added to
  // main otherwise just add freq
  if (params->ghost_fifo != NULL &&
      params->ghost_fifo->find(params->ghost_fifo, req, false) != NULL) {
    cache_obj_t *ghost_obj =
        params->ghost_fifo->find(params->ghost_fifo, req, false);
    int64_t ghost_freq = ghost_obj->S4FIFO.freq;

    if (ghost_freq >= params->ghost_to_main_threshold) {
      params->ghost_fifo->remove(params->ghost_fifo, req->obj_id);
      params->hit_on_ghost = true;
      params->hit_on_ghost_freq = ghost_freq;
    } else {
      ghost_obj->S4FIFO.freq = ghost_freq + 1;
    }
    // if object in ghost_fifo, remove will return true
  }

  obj = params->main_fifo->find(params->main_fifo, req, true);
  if (obj != NULL) {
    obj->S4FIFO.freq += 1;
  }

  return obj;
}

/**
 * @brief insert an object into the cache,
 * update the hash table and cache metadata
 * this function assumes the cache has enough space
 * eviction should be
 * performed before calling this function
 *
 * @param cache
 * @param req
 * @return the inserted object
 */
static cache_obj_t *S4FIFOv2_insert(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  cache_obj_t *obj = NULL;

  cache_t *small = params->small_fifo;
  cache_t *main = params->main_fifo;

  if (params->hit_on_ghost) {
    /* insert into main FIFO */
    params->hit_on_ghost = false;
    params->hit_on_ghost_freq = 0;
    obj = main->insert(main, req);
  } else {
    /* insert into small fifo */
    if (req->obj_size >= small->cache_size) {
      return NULL;
    }

    if (!params->has_evicted &&
        small->get_occupied_byte(small) >= small->cache_size) {
      obj = main->insert(main, req);
    } else {
      obj = small->insert(small, req);
      params
          ->s_counter++;  // only increase s_counter when insert into small fifo
      obj->time_stamp = params->s_counter;
    }
  }

  // if an object is inserted from ghost to main, we also set it to zero
  obj->S4FIFO.freq = 0;

  return obj;
}

/**
 * @brief find the object to be evicted
 * this function does not actually evict the object or update metadata
 * not all eviction algorithms support this function
 * because the eviction logic cannot be decoupled from finding eviction
 * candidate, so use assert(false) if you cannot support this function
 *
 * @param cache the cache
 * @return the object to be evicted
 */
static cache_obj_t *S4FIFOv2_to_evict(cache_t *cache, const request_t *req) {
  assert(false);
  return NULL;
}

static void S4FIFOv2_trim_fifo(cache_t *fifo, request_t *req_local) {
  while (fifo != NULL && fifo->get_occupied_byte(fifo) > fifo->cache_size) {
    cache_obj_t *obj_to_evict = fifo->to_evict(fifo, req_local);
    DEBUG_ASSERT(obj_to_evict != NULL);
    bool removed = fifo->remove(fifo, obj_to_evict->obj_id);
    DEBUG_ASSERT(removed);
  }
}

static void S4FIFOv2_rebalance_after_adjustment(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;

  while (params->small_fifo->get_occupied_byte(params->small_fifo) >
         params->small_fifo->cache_size) {
    S4FIFOv2_evict_small(cache, params->req_local);
  }

  while (params->main_fifo->get_occupied_byte(params->main_fifo) >
         params->main_fifo->cache_size) {
    S4FIFOv2_evict_main(cache, params->req_local);
  }

  S4FIFOv2_trim_fifo(params->ghost_fifo, params->req_local);
}

static void S4FIFOv2_apply_adjustment(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  int64_t old_small_size = params->small_fifo->cache_size;
  int64_t old_ghost_size =
      params->ghost_fifo != NULL ? params->ghost_fifo->cache_size : 0;

  params->move_to_main_threshold = params->nst;
  params->small_skip_ratio = params->nk;
  params->ghost_to_main_threshold = params->ngt;
  params->small_size_ratio = params->ns;
  params->ghost_size_ratio = params->ng;

  int64_t new_small_size =
      (int64_t)(cache->cache_size * params->small_size_ratio);
  int64_t new_main_size = cache->cache_size - new_small_size;
  int64_t new_ghost_size =
      (int64_t)(cache->cache_size * params->ghost_size_ratio);

  printf("original small and ghost size %ld %ld", old_small_size,
         old_ghost_size);

  params->small_fifo->cache_size = new_small_size;
  params->main_fifo->cache_size = new_main_size;
  if (params->ghost_fifo != NULL) {
    params->ghost_fifo->cache_size = new_ghost_size;
  }

  S4FIFOv2_rebalance_after_adjustment(cache);
  params->has_adjusted = true;
  params->transition_type = 1;

  printf("adjusted small and ghost size %ld %ld",
         params->small_fifo->cache_size,
         params->ghost_fifo != NULL ? params->ghost_fifo->cache_size : 0);
}

static void S4FIFOv2_evict_small(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  cache_t *small = params->small_fifo;
  cache_t *ghost = params->ghost_fifo;
  cache_t *main = params->main_fifo;

  bool has_evicted = false;
  while (!has_evicted && small->get_occupied_byte(small) > 0) {
    cache_obj_t *obj_to_evict = small->to_evict(small, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    // need to copy the object before it is evicted
    copy_cache_obj_to_request(params->req_local, obj_to_evict);

    if (obj_to_evict->S4FIFO.freq >= params->move_to_main_threshold) {
      main->insert(main, params->req_local);
    } else {
      // insert to ghost
      if (ghost != NULL) {
        int64_t small_freq = obj_to_evict->S4FIFO.freq;
        ghost->get(ghost, params->req_local);
        // let the obj inherit the freq from small fifo (exact value)
        cache_obj_t *ghost_obj = ghost->find(ghost, params->req_local, false);
        if (ghost_obj != NULL) {
          ghost_obj->S4FIFO.freq = small_freq;
        }
      }
      has_evicted = true;
    }

    // remove from small fifo, but do not update stat
    bool removed = small->remove(small, params->req_local->obj_id);
    DEBUG_ASSERT(removed);
  }
}

static void S4FIFOv2_evict_main(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  cache_t *main = params->main_fifo;

  bool has_evicted = false;
  while (!has_evicted && main->get_occupied_byte(main) > 0) {
    cache_obj_t *obj_to_evict = main->to_evict(main, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    int freq = obj_to_evict->S4FIFO.freq;
    copy_cache_obj_to_request(params->req_local, obj_to_evict);
    if (freq >= 1) {
      // we need to evict first because the object to insert has the same obj_id
      main->remove(main, obj_to_evict->obj_id);
      obj_to_evict = NULL;

      cache_obj_t *new_obj = main->insert(main, params->req_local);
      // clock with 2-bit counter
      new_obj->S4FIFO.freq = MIN(freq, 3) - 1;

    } else {
      bool removed = main->remove(main, obj_to_evict->obj_id);
      DEBUG_ASSERT(removed);

      has_evicted = true;
    }
  }
}

/**
 * @brief evict an object from the cache
 * it needs to call cache_evict_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param req not used
 * @param evicted_obj if not NULL, return the evicted object to caller
 */
static void S4FIFOv2_evict(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->has_evicted = true;

  cache_t *small = params->small_fifo;
  cache_t *main = params->main_fifo;

  if (main->get_occupied_byte(main) > main->cache_size ||
      small->get_occupied_byte(small) == 0) {
    S4FIFOv2_evict_main(cache, req);
  } else {
    S4FIFOv2_evict_small(cache, req);
  }
}

/**
 * @brief remove an object from the cache
 * this is different from cache_evict because it is used to for user trigger
 * remove, and eviction is used by the cache to make space for new objects
 *
 * it needs to call cache_remove_obj_base before returning
 * which updates some metadata such as n_obj, occupied size, and hash table
 *
 * @param cache
 * @param obj_id
 * @return true if the object is removed, false if the object is not in the
 * cache
 */
static bool S4FIFOv2_remove(cache_t *cache, const obj_id_t obj_id) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  bool removed = false;
  removed = removed || params->small_fifo->remove(params->small_fifo, obj_id);
  removed = removed || (params->ghost_fifo &&
                        params->ghost_fifo->remove(params->ghost_fifo, obj_id));
  removed = removed || params->main_fifo->remove(params->main_fifo, obj_id);

  return removed;
}

static inline int64_t S4FIFOv2_get_occupied_byte(const cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  return params->small_fifo->get_occupied_byte(params->small_fifo) +
         params->main_fifo->get_occupied_byte(params->main_fifo);
}

static inline int64_t S4FIFOv2_get_n_obj(const cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  return params->small_fifo->get_n_obj(params->small_fifo) +
         params->main_fifo->get_n_obj(params->main_fifo);
}

static inline bool S4FIFOv2_can_insert(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;

  return req->obj_size <= params->small_fifo->cache_size &&
         cache_can_insert_default(cache, req);
}

// ***********************************************************************
// ****                                                               ****
// ****                parameter set up functions                     ****
// ****                                                               ****
// ***********************************************************************
static const char *S4FIFOv2_current_params(S4FIFO_params_t *params) {
  static __thread char params_str[128];
  snprintf(params_str, 128,
           "small-size-ratio=%.4lf,ghost-size-ratio=%.4lf,move-to-main-"
           "threshold=%d\n",
           params->small_size_ratio, params->ghost_size_ratio,
           params->move_to_main_threshold);
  return params_str;
}

static void S4FIFOv2_parse_params(cache_t *cache,
                                const char *cache_specific_params) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)(cache->eviction_params);

  char *params_str = strdup(cache_specific_params);
  char *old_params_str = params_str;

  while (params_str != NULL && params_str[0] != '\0') {
    /* different parameters are separated by comma,
     * key and value are separated by = */
    char *key = strsep((char **)&params_str, "=");
    char *value = strsep((char **)&params_str, ",");

    // skip the white space
    while (params_str != NULL && *params_str == ' ') {
      params_str++;
    }

    if (strcasecmp(key, "fifo-size-ratio") == 0 ||
        strcasecmp(key, "small-size-ratio") == 0) {
      params->small_size_ratio = strtod(value, NULL);
    } else if (strcasecmp(key, "ghost-size-ratio") == 0) {
      params->ghost_size_ratio = strtod(value, NULL);
    } else if (strcasecmp(key, "move-to-main-threshold") == 0) {
      params->move_to_main_threshold = atoi(value);
    } else if (strcasecmp(key, "small-skip-ratio") == 0) {
      params->small_skip_ratio = strtod(value, NULL);
    } else if (strcasecmp(key, "ghost-to-main-threshold") == 0) {
      params->ghost_to_main_threshold = atoi(value);
    } else if (strcasecmp(key, "after-n-reqs") == 0) {
      params->after_n_reqs = atoi(value);
    } else if (strcasecmp(key, "ns") == 0) {
      params->ns = strtod(value, NULL);
    } else if (strcasecmp(key, "ng") == 0) {
      params->ng = strtod(value, NULL);
    } else if (strcasecmp(key, "nk") == 0) {
      params->nk = strtod(value, NULL);
    } else if (strcasecmp(key, "nst") == 0) {
      params->nst = atoi(value);
    } else if (strcasecmp(key, "ngt") == 0) {
      params->ngt = atoi(value);
    } else if (strcasecmp(key, "print") == 0) {
      printf("parameters: %s\n", S4FIFOv2_current_params(params));
      exit(0);
    } else {
      ERROR("%s does not have parameter %s\n", cache->cache_name, key);
      exit(1);
    }
  }

  free(old_params_str);
}

#ifdef __cplusplus
}
#endif
