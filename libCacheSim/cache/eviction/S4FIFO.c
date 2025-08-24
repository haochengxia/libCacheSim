//
// This version (S4FIFO.c) is for cache miss ratio comparison only.
// Since we need different combinations of parameters to achieve the best miss ratio.
// And the number of combinations is large, therefore, it is performance-sensitive.
// It will not gather other statistics information like hit position or others.
//
//  S4FIFO.c
//  libCacheSim
//
//  Modified by Haocheng at 08/23/2025

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
  int hit_on_ghost_freq; // frequency of the object in ghost fifo

  int move_to_main_threshold;
  double small_size_ratio;
  double ghost_size_ratio;
  double small_skip_ratio;
  int ghost_to_main_threshold;

  bool has_evicted;
  request_t *req_local;

  int64_t timer; // is used for small skip logic
} S4FIFO_params_t;

static const char *DEFAULT_CACHE_PARAMS =
    "small-size-ratio=0.10,ghost-size-ratio=0.90,move-to-main-threshold=2,small-skip-ratio=0,ghost-to-main-threshold=0";

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************
static void S4FIFO_free(cache_t *cache);
static bool S4FIFO_get(cache_t *cache, const request_t *req);

static cache_obj_t *S4FIFO_find(cache_t *cache, const request_t *req,
                                const bool update_cache);
static cache_obj_t *S4FIFO_insert(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFO_to_evict(cache_t *cache, const request_t *req);
static void S4FIFO_evict(cache_t *cache, const request_t *req);
static bool S4FIFO_remove(cache_t *cache, const obj_id_t obj_id);
static inline int64_t S4FIFO_get_occupied_byte(const cache_t *cache);
static inline int64_t S4FIFO_get_n_obj(const cache_t *cache);
static inline bool S4FIFO_can_insert(cache_t *cache, const request_t *req);
static void S4FIFO_parse_params(cache_t *cache,
                                const char *cache_specific_params);

static void S4FIFO_evict_small(cache_t *cache, const request_t *req);
static void S4FIFO_evict_main(cache_t *cache, const request_t *req);

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ***********************************************************************

cache_t *S4FIFO_init(const common_cache_params_t ccache_params,
                     const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("S4FIFO", ccache_params, cache_specific_params);
  cache->cache_init = S4FIFO_init;
  cache->cache_free = S4FIFO_free;
  cache->get = S4FIFO_get;
  cache->find = S4FIFO_find;
  cache->insert = S4FIFO_insert;
  cache->evict = S4FIFO_evict;
  cache->remove = S4FIFO_remove;
  cache->to_evict = S4FIFO_to_evict;
  cache->get_n_obj = S4FIFO_get_n_obj;
  cache->get_occupied_byte = S4FIFO_get_occupied_byte;
  cache->can_insert = S4FIFO_can_insert;

  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(S4FIFO_params_t));
  memset(cache->eviction_params, 0, sizeof(S4FIFO_params_t));
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->req_local = new_request();
  params->hit_on_ghost = false;

  S4FIFO_parse_params(cache, DEFAULT_CACHE_PARAMS);
  if (cache_specific_params != NULL) {
    S4FIFO_parse_params(cache, cache_specific_params);
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

  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "S4FIFO-%.4lf-%d",
           params->small_size_ratio, params->move_to_main_threshold);

  /* S4FIFO: initialize the timer */
  params->timer = 0;

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void S4FIFO_free(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
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
static bool S4FIFO_get(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  DEBUG_ASSERT(params->small_fifo->get_occupied_byte(params->small_fifo) +
                   params->main_fifo->get_occupied_byte(params->main_fifo) <=
               cache->cache_size);

  bool cache_hit = cache_get_base(cache, req);

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
static cache_obj_t *S4FIFO_find(cache_t *cache, const request_t *req,
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
    if ((int64_t)(-obj->time_stamp + params->timer) >=
        (int64_t)(params->small_skip_ratio * params->small_fifo->cache_size)) {
      obj->S4FIFO.freq += 1;
    }
    return obj;
  }

  // New logic:
  // if the obj find in the ghost fifo, check the freq > thres then added to main
  // otherwise just add freq
  if (params->ghost_fifo != NULL && params->ghost_fifo->find(
          params->ghost_fifo, req, false) != NULL) {
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
static cache_obj_t *S4FIFO_insert(cache_t *cache, const request_t *req) {
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
      params->timer++; // only increase timer when insert into small fifo
      obj->time_stamp = params->timer;
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
static cache_obj_t *S4FIFO_to_evict(cache_t *cache, const request_t *req) {
  assert(false);
  return NULL;
}

static void S4FIFO_evict_small(cache_t *cache, const request_t *req) {
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

static void S4FIFO_evict_main(cache_t *cache, const request_t *req) {
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
static void S4FIFO_evict(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->has_evicted = true;

  cache_t *small = params->small_fifo;
  cache_t *main = params->main_fifo;

  if (main->get_occupied_byte(main) > main->cache_size ||
      small->get_occupied_byte(small) == 0) {
    S4FIFO_evict_main(cache, req);
  } else {
    S4FIFO_evict_small(cache, req);
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
static bool S4FIFO_remove(cache_t *cache, const obj_id_t obj_id) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  bool removed = false;
  removed = removed || params->small_fifo->remove(params->small_fifo, obj_id);
  removed = removed || (params->ghost_fifo &&
                        params->ghost_fifo->remove(params->ghost_fifo, obj_id));
  removed = removed || params->main_fifo->remove(params->main_fifo, obj_id);

  return removed;
}

static inline int64_t S4FIFO_get_occupied_byte(const cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  return params->small_fifo->get_occupied_byte(params->small_fifo) +
         params->main_fifo->get_occupied_byte(params->main_fifo);
}

static inline int64_t S4FIFO_get_n_obj(const cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  return params->small_fifo->get_n_obj(params->small_fifo) +
         params->main_fifo->get_n_obj(params->main_fifo);
}

static inline bool S4FIFO_can_insert(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;

  return req->obj_size <= params->small_fifo->cache_size &&
         cache_can_insert_default(cache, req);
}

// ***********************************************************************
// ****                                                               ****
// ****                parameter set up functions                     ****
// ****                                                               ****
// ***********************************************************************
static const char *S4FIFO_current_params(S4FIFO_params_t *params) {
  static __thread char params_str[128];
  snprintf(params_str, 128,
           "small-size-ratio=%.4lf,ghost-size-ratio=%.4lf,move-to-main-"
           "threshold=%d\n",
           params->small_size_ratio, params->ghost_size_ratio,
           params->move_to_main_threshold);
  return params_str;
}

static void S4FIFO_parse_params(cache_t *cache,
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
    } else if (strcasecmp(key, "print") == 0) {
      printf("parameters: %s\n", S4FIFO_current_params(params));
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
