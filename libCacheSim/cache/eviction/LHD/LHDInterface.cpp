

#include <assert.h>

#include "../../../dataStructure/hashtable/hashtable.h"
#include "lhd.hpp"
#include "repl.hpp"

using namespace repl;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void *LHD_cache;

  int associativity;
  int admission;

} LHD_params_t;

// ***********************************************************************
// ****                                                               ****
// ****                   function declarations                       ****
// ****                                                               ****
// ***********************************************************************

static void LHD_free(cache_t *cache);
static bool LHD_get(cache_t *cache, const request_t *req);

static cache_obj_t *LHD_find(cache_t *cache, const request_t *req,
                             const bool update_cache);
static cache_obj_t *LHD_insert(cache_t *cache, const request_t *req);
static void LHD_evict(cache_t *cache, const request_t *req);
static bool LHD_remove(cache_t *cache, const obj_id_t obj_id);

// ***********************************************************************
// ****                                                               ****
// ****                   end user facing functions                   ****
// ****                                                               ****
// ****                       init, free, get                         ****
// ***********************************************************************

/**
 * @brief initialize the cache
 *
 * @param ccache_params some common cache parameters
 * @param cache_specific_params cache specific parameters, see parse_params
 * function or use -e "print" with the cachesim binary
 */
cache_t *LHD_init(const common_cache_params_t ccache_params,
                  const char *cache_specific_params) {
#ifdef SUPPORT_TTL
  if (ccache_params.default_ttl < 30 * 86400) {
    ERROR("LHD does not support expiration\n");
    abort();
  }
#endif

  cache_t *cache = cache_struct_init("LHD", ccache_params);
  cache->cache_init = LHD_init;
  cache->cache_free = LHD_free;
  cache->get = LHD_get;
  cache->find = LHD_find;
  cache->insert = LHD_insert;
  cache->evict = LHD_evict;
  cache->remove = LHD_remove;
  cache->to_evict = NULL;
  cache->init_params = cache_specific_params;
  if (cache_specific_params != NULL) {
    ERROR("%s does not support any parameters, but got %s\n", cache->cache_name,
          cache_specific_params);
    abort();
  }

  if (ccache_params.consider_obj_metadata) {
    cache->obj_md_size = 8 * 3 + 1;  // two age, one time stamp
  } else {
    cache->obj_md_size = 0;
  }

  auto *params = my_malloc(LHD_params_t);
  memset(params, 0, sizeof(LHD_params_t));
  cache->eviction_params = params;

  if (params->admission == 0) {
    params->admission = 8;
  }
  if (params->associativity == 0) {
    params->associativity = 32;
  }

  params->LHD_cache = static_cast<void *>(
      new LHD(params->associativity, params->admission, cache));

  return cache;
}

/**
 * free resources used by this cache
 *
 * @param cache
 */
static void LHD_free(cache_t *cache) {
  auto *params = static_cast<LHD_params_t *>(cache->eviction_params);
  auto *lhd = static_cast<repl::LHD *>(params->LHD_cache);
  delete lhd;
  my_free(sizeof(LHD_params_t), params);
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
static bool LHD_get(cache_t *cache, const request_t *req) {
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
static cache_obj_t *LHD_find(cache_t *cache, const request_t *req,
                             const bool update_cache) {
  auto *params = static_cast<LHD_params_t *>(cache->eviction_params);
  auto *lhd = static_cast<repl::LHD *>(params->LHD_cache);

  auto id = repl::candidate_t::make(req);
  auto itr = lhd->sizeMap.find(id);
  bool hit = (itr != lhd->sizeMap.end());

  if (!hit) {
    return NULL;
  }

  if (update_cache) {
    if (itr->second != req->obj_size) {
      cache->occupied_byte -= itr->second;
      cache->occupied_byte += req->obj_size;
      itr->second = req->obj_size;
    }
    lhd->update(id, req);
  }

  return reinterpret_cast<cache_obj_t *>(0x1);
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
static cache_obj_t *LHD_insert(cache_t *cache, const request_t *req) {
  auto *params = static_cast<LHD_params_t *>(cache->eviction_params);
  auto *lhd = static_cast<repl::LHD *>(params->LHD_cache);
  auto id = repl::candidate_t::make(req);

#if defined(TRACK_EVICTION_R_AGE) || defined(TRACK_EVICTION_V_AGE)
  id.create_time = CURR_TIME(cache, req);
#endif

  lhd->sizeMap[id] = req->obj_size;
  lhd->update(id, req);

  cache->occupied_byte += req->obj_size + cache->obj_md_size;
  cache->n_obj += 1;

  return NULL;
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
static void LHD_evict(cache_t *cache, const request_t *req) {
  auto *params = static_cast<LHD_params_t *>(cache->eviction_params);
  auto *lhd = static_cast<repl::LHD *>(params->LHD_cache);

  repl::candidate_t victim = lhd->rank(req);
  auto victimItr = lhd->sizeMap.find(victim);
  if (victimItr == lhd->sizeMap.end()) {
    std::cerr << "Couldn't find victim: " << victim << std::endl;
  }
  assert(victimItr != lhd->sizeMap.end());

  DEBUG_ASSERT(cache->occupied_byte >= victimItr->second);
  cache->occupied_byte -= (victimItr->second + cache->obj_md_size);
  cache->n_obj -= 1;

#if defined(TRACK_EVICTION_R_AGE) || defined(TRACK_EVICTION_V_AGE)
  record_eviction_age(cache, CURR_TIME(cache, req) - victim.create_time);
#endif

  lhd->replaced(victim);
  lhd->sizeMap.erase(victimItr);
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
static bool LHD_remove(cache_t *cache, const obj_id_t obj_id) {
  auto *params = static_cast<LHD_params_t *>(cache->eviction_params);
  auto *lhd = static_cast<repl::LHD *>(params->LHD_cache);
  repl::candidate_t id{DEFAULT_APP_ID, (int64_t)obj_id};

  auto itr = lhd->sizeMap.find(id);
  if (itr == lhd->sizeMap.end()) {
    return false;
  }

  cache->occupied_byte -= (itr->second + cache->obj_md_size);
  cache->n_obj -= 1;
  lhd->sizeMap.erase(itr);
  auto idx = lhd->indices[id];
  lhd->indices.erase(id);
  lhd->tags[idx] = lhd->tags.back();
  lhd->tags.pop_back();

  if (idx < lhd->tags.size()) {
    lhd->indices[lhd->tags[idx].id] = idx;
  }

  return true;
}

#ifdef __cplusplus
}
#endif
