//
// S4FIFO_base.c - Minimal S4FIFO for miss ratio comparison only
//
// No feature collection, no phase management. Performance-sensitive.
//
// Modified by Haocheng at 08/23/2025
//

#include "S4FIFO.h"

#ifdef __cplusplus
extern "C" {
#endif

static const char *DEFAULT_CACHE_PARAMS = S4FIFO_BASE_DEFAULT_PARAMS;

// ***********************************************************************
// ****                   function declarations                       ****
// ***********************************************************************
static void S4FIFO_base_free(cache_t *cache);
static bool S4FIFO_base_get(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFO_base_find(cache_t *cache, const request_t *req,
                                     const bool update_cache);
static cache_obj_t *S4FIFO_base_insert(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFO_base_to_evict(cache_t *cache, const request_t *req);
static void S4FIFO_base_evict(cache_t *cache, const request_t *req);
static void S4FIFO_base_parse_params(cache_t *cache,
                                     const char *cache_specific_params);
static void S4FIFO_base_evict_small(cache_t *cache, const request_t *req);
static void S4FIFO_base_evict_main(cache_t *cache, const request_t *req);

// ***********************************************************************
// ****                   end user facing functions                   ****
// ***********************************************************************

cache_t *S4FIFO_base_init(const common_cache_params_t ccache_params,
                          const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("S4FIFO-base", ccache_params, cache_specific_params);
  cache->cache_init = S4FIFO_base_init;
  cache->cache_free = S4FIFO_base_free;
  cache->get = S4FIFO_base_get;
  cache->find = S4FIFO_base_find;
  cache->insert = S4FIFO_base_insert;
  cache->evict = S4FIFO_base_evict;
  cache->remove = S4FIFO_shared_remove;
  cache->to_evict = S4FIFO_base_to_evict;
  cache->get_n_obj = S4FIFO_shared_get_n_obj;
  cache->get_occupied_byte = S4FIFO_shared_get_occupied_byte;
  cache->can_insert = S4FIFO_shared_can_insert;
  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(S4FIFO_base_params_t));
  memset(cache->eviction_params, 0, sizeof(S4FIFO_base_params_t));
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  params->req_local = new_request();
  params->hit_on_ghost = false;

  S4FIFO_base_parse_params(cache, DEFAULT_CACHE_PARAMS);
  if (cache_specific_params != NULL) {
    S4FIFO_base_parse_params(cache, cache_specific_params);
  }

  S4FIFO_shared_init_queues(params, ccache_params);

  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "S4FIFO-base-%.4lf-%d",
           params->small_size_ratio, params->move_to_main_threshold);

  return cache;
}

static void S4FIFO_base_free(cache_t *cache) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  free_request(params->req_local);
  S4FIFO_shared_free_queues(params);
  free(cache->eviction_params);
  cache_struct_free(cache);
}

static bool S4FIFO_base_get(cache_t *cache, const request_t *req) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  DEBUG_ASSERT(params->small_fifo->get_occupied_byte(params->small_fifo) +
                   params->main_fifo->get_occupied_byte(params->main_fifo) <=
               cache->cache_size);

  return cache_get_base(cache, req);
}

// ***********************************************************************
// ****                    developer facing APIs                      ****
// ***********************************************************************

static cache_obj_t *S4FIFO_base_find(cache_t *cache, const request_t *req,
                                     const bool update_cache) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;

  if (!update_cache) {
    cache_obj_t *obj = params->small_fifo->find(params->small_fifo, req, false);
    if (obj != NULL) return obj;
    obj = params->main_fifo->find(params->main_fifo, req, false);
    return obj;
  }

  params->hit_on_ghost = false;
  cache_obj_t *obj = params->small_fifo->find(params->small_fifo, req, true);
  if (obj != NULL) {
    if ((int64_t)(-obj->time_stamp + params->s_counter) >=
        (int64_t)(params->small_skip_ratio * params->small_fifo->cache_size)) {
      obj->S4FIFO.freq += 1;
    }
    return obj;
  }

  cache_obj_t *ghost_obj = NULL;
  if (params->ghost_fifo != NULL) {
    ghost_obj = params->ghost_fifo->find(params->ghost_fifo, req, false);
  }
  if (ghost_obj != NULL) {
    int64_t ghost_freq = ghost_obj->S4FIFO.freq;
    if (ghost_freq >= params->ghost_to_main_threshold) {
      params->ghost_fifo->remove(params->ghost_fifo, req->obj_id);
      params->hit_on_ghost = true;
      params->hit_on_ghost_freq = ghost_freq;
    } else {
      ghost_obj->S4FIFO.freq = ghost_freq + 1;
    }
  }

  obj = params->main_fifo->find(params->main_fifo, req, true);
  if (obj != NULL) {
    obj->S4FIFO.freq += 1;
  }

  return obj;
}

static cache_obj_t *S4FIFO_base_insert(cache_t *cache, const request_t *req) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  cache_obj_t *obj = NULL;

  cache_t *small = params->small_fifo;
  cache_t *main = params->main_fifo;

  if (params->hit_on_ghost) {
    params->hit_on_ghost = false;
    params->hit_on_ghost_freq = 0;
    obj = main->insert(main, req);
  } else {
    if (req->obj_size >= small->cache_size) {
      return NULL;
    }

    if (!params->has_evicted &&
        small->get_occupied_byte(small) >= small->cache_size) {
      obj = main->insert(main, req);
    } else {
      obj = small->insert(small, req);
      params->s_counter++;
      obj->time_stamp = params->s_counter;
    }
  }

  if (obj != NULL) {
    obj->S4FIFO.freq = 0;
  }

  return obj;
}

static cache_obj_t *S4FIFO_base_to_evict(cache_t *cache, const request_t *req) {
  assert(false);
  return NULL;
}

static void S4FIFO_base_evict_small(cache_t *cache, const request_t *req) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  cache_t *small = params->small_fifo;
  cache_t *ghost = params->ghost_fifo;
  cache_t *main = params->main_fifo;

  bool has_evicted = false;
  while (!has_evicted && small->get_occupied_byte(small) > 0) {
    cache_obj_t *obj_to_evict = small->to_evict(small, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    copy_cache_obj_to_request(params->req_local, obj_to_evict);

    if (obj_to_evict->S4FIFO.freq >= params->move_to_main_threshold) {
      main->insert(main, params->req_local);
    } else {
      if (ghost != NULL) {
        int64_t small_freq = obj_to_evict->S4FIFO.freq;
        ghost->get(ghost, params->req_local);
        cache_obj_t *ghost_obj = ghost->find(ghost, params->req_local, false);
        if (ghost_obj != NULL) {
          ghost_obj->S4FIFO.freq = small_freq;
        }
      }
      has_evicted = true;
    }

    bool removed = small->remove(small, params->req_local->obj_id);
    DEBUG_ASSERT(removed);
  }
}

static void S4FIFO_base_evict_main(cache_t *cache, const request_t *req) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  cache_t *main = params->main_fifo;

  bool has_evicted = false;
  while (!has_evicted && main->get_occupied_byte(main) > 0) {
    cache_obj_t *obj_to_evict = main->to_evict(main, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    int freq = obj_to_evict->S4FIFO.freq;
    copy_cache_obj_to_request(params->req_local, obj_to_evict);
    if (freq >= 1) {
      main->remove(main, obj_to_evict->obj_id);
      obj_to_evict = NULL;

      cache_obj_t *new_obj = main->insert(main, params->req_local);
      new_obj->S4FIFO.freq = MIN(freq, 3) - 1;
    } else {
      bool removed = main->remove(main, obj_to_evict->obj_id);
      DEBUG_ASSERT(removed);
      has_evicted = true;
    }
  }
}

static void S4FIFO_base_evict(cache_t *cache, const request_t *req) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  params->has_evicted = true;

  cache_t *small = params->small_fifo;
  cache_t *main = params->main_fifo;

  if (main->get_occupied_byte(main) > main->cache_size ||
      small->get_occupied_byte(small) == 0) {
    S4FIFO_base_evict_main(cache, req);
  } else {
    S4FIFO_base_evict_small(cache, req);
  }
}

// ***********************************************************************
// ****                    parameter handling                         ****
// ***********************************************************************

static void S4FIFO_base_parse_params(cache_t *cache,
                                     const char *cache_specific_params) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)(cache->eviction_params);

  char *params_str = strdup(cache_specific_params);
  char *old_params_str = params_str;

  while (params_str != NULL && params_str[0] != '\0') {
    char *key = strsep((char **)&params_str, "=");
    char *value = strsep((char **)&params_str, ",");

    while (params_str != NULL && *params_str == ' ') {
      params_str++;
    }

    if (S4FIFO_shared_parse_one_param(params, key, value)) {
      continue;
    } else if (strcasecmp(key, "print") == 0) {
      printf("parameters: %s\n", S4FIFO_shared_current_params(params));
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
