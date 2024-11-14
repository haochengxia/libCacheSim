#include <stdio.h>
#include "../../dataStructure/hashtable/hashtable.h"
#include "../../include/libCacheSim/evictionAlgo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  cache_obj_t *q_head;
  cache_obj_t *q_tail;
  uint64_t counter;
  FILE *log_file;
} FIFO_Pos_params_t;

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************

static void FIFO_Pos_parse_params(cache_t *cache,
                              const char *cache_specific_params);
static void FIFO_Pos_free(cache_t *cache);
static bool FIFO_Pos_get(cache_t *cache, const request_t *req);
static cache_obj_t *FIFO_Pos_find(cache_t *cache, const request_t *req,
                              const bool update_cache);
static cache_obj_t *FIFO_Pos_insert(cache_t *cache, const request_t *req);
static cache_obj_t *FIFO_Pos_to_evict(cache_t *cache, const request_t *req);
static void FIFO_Pos_evict(cache_t *cache, const request_t *req);
static bool FIFO_Pos_remove(cache_t *cache, const obj_id_t obj_id);

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
cache_t *FIFO_Pos_init(const common_cache_params_t ccache_params,
                   const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("FIFO_Pos", ccache_params, cache_specific_params);
  cache->cache_init = FIFO_Pos_init;
  cache->cache_free = FIFO_Pos_free;
  cache->get = FIFO_Pos_get;
  cache->find = FIFO_Pos_find;
  cache->insert = FIFO_Pos_insert;
  cache->evict = FIFO_Pos_evict;
  cache->remove = FIFO_Pos_remove;
  cache->to_evict = FIFO_Pos_to_evict;
  cache->get_occupied_byte = cache_get_occupied_byte_default;
  cache->get_n_obj = cache_get_n_obj_default;
  cache->can_insert = cache_can_insert_default;
  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(FIFO_Pos_params_t));
  FIFO_Pos_params_t *params = (FIFO_Pos_params_t *)cache->eviction_params;
  params->q_head = NULL;
  params->q_tail = NULL;
  params->counter = 0;
  params->log_file = fopen("FIFO_Pos.log", "w+");

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void FIFO_Pos_free(cache_t *cache) {
  FIFO_Pos_params_t *params = (FIFO_Pos_params_t *)cache->eviction_params;
  fclose(params->log_file);
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
static bool FIFO_Pos_get(cache_t *cache, const request_t *req) {
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
static cache_obj_t *FIFO_Pos_find(cache_t *cache, const request_t *req,
                               const bool update_cache) {
  return cache_find_base(cache, req, update_cache);
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
static cache_obj_t *FIFO_Pos_insert(cache_t *cache, const request_t *req) {
  FIFO_Pos_params_t *params = (FIFO_Pos_params_t *)cache->eviction_params;
  cache_obj_t *obj = cache_insert_base(cache, req);
  obj->unique_order = params->counter++;
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
static cache_obj_t *FIFO_Pos_to_evict(cache_t *cache, const request_t *req) {
  FIFO_Pos_params_t *params = (FIFO_Pos_params_t *)cache->eviction_params;
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
static void FIFO_Pos_evict(cache_t *cache, const request_t *req) {
  FIFO_Pos_params_t *params = (FIFO_Pos_params_t *)cache->eviction_params;
  cache_obj_t *obj_to_evict = params->q_tail;
  DEBUG_ASSERT(params->q_tail != NULL);

  // we can simply call remove_obj_from_list here, but for the best performance,
  // we chose to do it manually
  // remove_obj_from_list(&params->q_head, &params->q_tail, obj);

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
static bool FIFO_Pos_remove(cache_t *cache, const obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    return false;
  }

  // ghost hit
  FIFO_Pos_params_t* params = (FIFO_Pos_params_t *)cache->eviction_params;
  FILE *ofs = params->log_file;
  fprintf(ofs, "%ld,%ld,%ld\n", params->q_tail->unique_order, params->q_head->unique_order, obj->unique_order);  // tail_order, head_order, hit_order 

  remove_obj_from_list(&params->q_head, &params->q_tail, obj);
  cache_remove_obj_base(cache, obj, true);

  return true;
}

#ifdef __cplusplus
}
#endif
