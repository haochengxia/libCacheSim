

#include "../../dataStructure/hashtable/hashtable.h"
#include "../../include/libCacheSim/cache.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  cache_obj_t *q_head;
  cache_obj_t *q_tail;

  cache_obj_t *pointer;
  int64_t n_scanned_obj;
  int64_t n_written_obj;
  double wrap_around_ratio;
} MyClock_params_t;

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************

static void MyClock_parse_params(cache_t *cache,
                                 const char *cache_specific_params);
static void MyClock_free(cache_t *cache);
static bool MyClock_get(cache_t *cache, const request_t *req);
static cache_obj_t *MyClock_find(cache_t *cache, const request_t *req,
                                 const bool update_cache);
static cache_obj_t *MyClock_insert(cache_t *cache, const request_t *req);
static cache_obj_t *MyClock_to_evict(cache_t *cache, const request_t *req);
static void MyClock_evict(cache_t *cache, const request_t *req);
static bool MyClock_remove(cache_t *cache, const obj_id_t obj_id);

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ****                       init, free, get                         ****
// ***********************************************************************

/**
 * @brief initialize cache
 *
 * @param ccache_params some common cache parameters
 * @param cache_specific_params cache specific parameters, see parse_params
 * function or use -e "print" with the cachesim binary
 */
cache_t *MyClock_init(const common_cache_params_t ccache_params,
                      const char *cache_specific_params) {
  cache_t *cache = cache_struct_init("MyClock", ccache_params);
  cache->cache_init = MyClock_init;
  cache->cache_free = MyClock_free;
  cache->get = MyClock_get;
  cache->find = MyClock_find;
  cache->insert = MyClock_insert;
  cache->evict = MyClock_evict;
  cache->remove = MyClock_remove;
  cache->to_evict = MyClock_to_evict;
  cache->init_params = cache_specific_params;

  if (ccache_params.consider_obj_metadata) {
    cache->obj_md_size = 1;
  } else {
    cache->obj_md_size = 0;
  }

  if (cache_specific_params != NULL) {
    ERROR("Clock does not support any parameters, but got %s\n",
          cache_specific_params);
    abort();
  }

  cache->eviction_params = my_malloc(MyClock_params_t);
  MyClock_params_t *params = (MyClock_params_t *)cache->eviction_params;
  params->pointer = NULL;
  params->q_head = NULL;
  params->q_tail = NULL;

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void MyClock_free(cache_t *cache) {
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

static bool MyClock_get(cache_t *cache, const request_t *req) {
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
static cache_obj_t *MyClock_find(cache_t *cache, const request_t *req,
                                 const bool update_cache) {
  cache_obj_t *cache_obj = cache_find_base(cache, req, update_cache);
  if (cache_obj != NULL && update_cache) cache_obj->clock.visited = true;

  return cache_obj;
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
static cache_obj_t *MyClock_insert(cache_t *cache, const request_t *req) {
  MyClock_params_t *params = cache->eviction_params;
  cache_obj_t *obj = cache_insert_base(cache, req);
  prepend_obj_to_head(&params->q_head, &params->q_tail, obj);
  obj->clock.visited = false;
  params->n_written_obj++;

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
static cache_obj_t *MyClock_to_evict(cache_t *cache, const request_t *req) {
  MyClock_params_t *params = cache->eviction_params;
  cache_obj_t *pointer = params->pointer;

  /* if we have run one full around or first eviction */
  if (pointer == NULL) pointer = params->q_tail;

  /* find the first untouched */
  while (pointer != NULL && pointer->clock.visited) {
    params->n_scanned_obj++;
    pointer->clock.visited = false;
    pointer = pointer->queue.prev;
  }

  /* if we have finished one around, start from the tail */
  if (pointer == NULL) {
    // printf("%ld %ld\n", params->n_scanned_obj, params->n_written_obj);
    pointer = params->q_tail;
    while (pointer != NULL && pointer->clock.visited) {
      pointer->clock.visited = false;
      pointer = pointer->queue.prev;
    }
  }

  params->n_scanned_obj++;
  return pointer;
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
static void MyClock_evict(cache_t *cache, const request_t *req) {
  MyClock_params_t *params = cache->eviction_params;
  cache_obj_t *moving_pointer = MyClock_to_evict(cache, req);

  params->pointer = moving_pointer->queue.prev;
  remove_obj_from_list(&params->q_head, &params->q_tail, moving_pointer);
  cache_evict_base(cache, moving_pointer, true);
}

static void MyClock_remove_obj(cache_t *cache, cache_obj_t *obj_to_remove) {
  DEBUG_ASSERT(obj_to_remove != NULL);
  MyClock_params_t *params = cache->eviction_params;
  remove_obj_from_list(&params->q_head, &params->q_tail, obj_to_remove);
  cache_remove_obj_base(cache, obj_to_remove, true);
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
static bool MyClock_remove(cache_t *cache, const obj_id_t obj_id) {
  cache_obj_t *obj = hashtable_find_obj_id(cache->hashtable, obj_id);
  if (obj == NULL) {
    return false;
  }
  MyClock_remove_obj(cache, obj);

  return true;
}

#ifdef __cplusplus
}
#endif
