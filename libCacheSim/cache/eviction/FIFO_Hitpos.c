//
//  first in first out
//
//
//  FIFO.c
//  libCacheSim
//
//  Created by Juncheng on 12/4/18.
//  Copyright © 2018 Juncheng. All rights reserved.
//

#include "../../dataStructure/hashtable/hashtable.h"
#include "../../include/libCacheSim/evictionAlgo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FIFO_Hitpos_params {
  cache_obj_t *q_head;
  cache_obj_t *q_tail;

  // uint32_t* num_pos_hit; // pos -> the number of hit
} FIFO_Hitpos_params_t;

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************

static void FIFO_Hitpos_parse_params(cache_t *cache,
                              const char *cache_specific_params);
static void FIFO_Hitpos_free(cache_t *cache);
static bool FIFO_Hitpos_get(cache_t *cache, const request_t *req);
static cache_obj_t *FIFO_Hitpos_find(cache_t *cache, const request_t *req,
                              const bool update_cache);
static cache_obj_t *FIFO_Hitpos_insert(cache_t *cache, const request_t *req);
static cache_obj_t *FIFO_Hitpos_to_evict(cache_t *cache, const request_t *req);
static void FIFO_Hitpos_evict(cache_t *cache, const request_t *req);
static bool FIFO_Hitpos_remove(cache_t *cache, const obj_id_t obj_id);

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ****                       init, free, get                         ****
// ***********************************************************************

/**
 * @brief initialize a ARC cache
 *
 * @param ccache_params some common cache parameters
 * @param cache_specific_params ARC specific parameters, should be NULL
 */
cache_t *FIFO_Hitpos_init(const common_cache_params_t ccache_params,
                   const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("FIFO", ccache_params, cache_specific_params);
  cache->cache_init = FIFO_Hitpos_init;
  cache->cache_free = FIFO_Hitpos_free;
  cache->get = FIFO_Hitpos_get;
  cache->find = FIFO_Hitpos_find;
  cache->insert = FIFO_Hitpos_insert;
  cache->evict = FIFO_Hitpos_evict;
  cache->remove = FIFO_Hitpos_remove;
  cache->to_evict = FIFO_Hitpos_to_evict;
  cache->get_occupied_byte = cache_get_occupied_byte_default;
  cache->get_n_obj = cache_get_n_obj_default;
  cache->can_insert = cache_can_insert_default;
  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(FIFO_Hitpos_params_t));
  FIFO_Hitpos_params_t *params = (FIFO_Hitpos_params_t *)cache->eviction_params;
  params->q_head = NULL;
  params->q_tail = NULL;
  // params->num_pos_hit = malloc(ccache_params.cache_size * sizeof(uint32_t));
  // memset(params->num_pos_hit, 0, ccache_params.cache_size * sizeof(uint32_t));
  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void FIFO_Hitpos_free(cache_t *cache) {
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
static bool FIFO_Hitpos_get(cache_t *cache, const request_t *req) {
  return cache_get_base(cache, req);
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
static cache_obj_t *FIFO_Hitpos_find(cache_t *cache, const request_t *req,
                               const bool update_cache) {
  FIFO_Hitpos_params_t *params = (FIFO_Hitpos_params_t *)cache->eviction_params;
  cache_obj_t* obj = cache_find_base(cache, req, update_cache);
  bool hit = (obj != NULL);
  // if (hit) // params->num_pos_hit[obj->position]++;
  // printf("pos hit: %u\n", obj->position);
  return obj;
}

/**
 * @brief insert an object into the cache,
 * update the hash table and cache metadata
 * this function assumes the cache has enough space
 * and eviction is not part of this function
 *
 * @param cache
 * @param req
 * @return the inserted object
 */
static cache_obj_t *FIFO_Hitpos_insert(cache_t *cache, const request_t *req) {
  FIFO_Hitpos_params_t *params = (FIFO_Hitpos_params_t *)cache->eviction_params;
  cache_obj_t *obj = cache_insert_base(cache, req);
  obj->position = 0;
  cache_obj_t* cursor = params->q_head;
  while (cursor != NULL) {
    cursor->position++;
    cursor = cursor->queue.next;
  }
  prepend_obj_to_head(&params->q_head, &params->q_tail, obj);
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
static cache_obj_t *FIFO_Hitpos_to_evict(cache_t *cache, const request_t *req) {
  FIFO_Hitpos_params_t *params = (FIFO_Hitpos_params_t *)cache->eviction_params;
  return params->q_tail;
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
static void FIFO_Hitpos_evict(cache_t *cache, const request_t *req) {
  FIFO_Hitpos_params_t *params = (FIFO_Hitpos_params_t *)cache->eviction_params;
  cache_obj_t *obj_to_evict = params->q_tail;
  DEBUG_ASSERT(params->q_tail != NULL);

  // we can simply call remove_obj_from_list here, but for the best performance,
  // we chose to do it manually
  // remove_obj_from_list(&params->q_head, &params->q_tail, obj);

  // TODO: check it, just remove tail, so, do not need to update pos
  params->q_tail = params->q_tail->queue.prev;
  if (likely(params->q_tail != NULL)) {
    params->q_tail->queue.next = NULL;
  } else {
    /* cache->n_obj has not been updated */
    DEBUG_ASSERT(cache->n_obj == 1);
    params->q_head = NULL;
  }

  cache_evict_base(cache, obj_to_evict, true);
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
static bool FIFO_Hitpos_remove(cache_t *cache, const obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    return false;
  }

  printf("hit pos %u\n", obj->position);

  FIFO_Hitpos_params_t *params = (FIFO_Hitpos_params_t *)cache->eviction_params;

  // update position
  cache_obj_t *local_obj = obj;
  while (local_obj->queue.next != NULL) {
    local_obj = local_obj->queue.next;
    assert(local_obj->position >= 1);
    if (local_obj->position < 1) printf("Error!\n");
    local_obj->position--;
  }
  remove_obj_from_list(&params->q_head, &params->q_tail, obj);
  cache_remove_obj_base(cache, obj, true);

  return true;
}

#ifdef __cplusplus
}
#endif
