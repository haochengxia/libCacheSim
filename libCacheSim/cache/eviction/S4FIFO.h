//
// S4FIFO.h - Shared types, base parameters, and utility functions
//
// This header defines the common types used by all S4FIFO variants:
//   - S4FIFO (main, with feature collection and periodic prediction)
//   - S4FIFO-base (minimal, for miss ratio comparison only)
//   - S4FIFO-verify (with exact hit position tracking)
//
// Pattern: each variant embeds S4FIFO_base_params_t as its first struct
// member so that the shared utility functions work via pointer casting.
//

#pragma once

#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/evictionAlgo.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Phase enumeration (used by S4FIFO with feature collection)
// ============================================================================

typedef enum {
  S4FIFO_PHASE_WARMUP = 0,
  S4FIFO_PHASE_FEATURE_COLLECT,
  S4FIFO_PHASE_PREDICTION,
} S4FIFO_phase_t;

// Callback function type for phase transition
typedef void (*S4FIFO_phase_callback_t)(void *cache, S4FIFO_phase_t old_phase,
                                        S4FIFO_phase_t new_phase);

// ============================================================================
// Base S4FIFO parameters (shared by all variants)
// ============================================================================

typedef struct {
  cache_t *small_fifo;
  cache_t *ghost_fifo;
  cache_t *main_fifo;
  bool hit_on_ghost;

  int hit_on_ghost_freq;
  int move_to_main_threshold;
  double small_size_ratio;
  double ghost_size_ratio;
  double small_skip_ratio;
  int ghost_to_main_threshold;

  bool has_evicted;
  request_t *req_local;
  int64_t s_counter;
} S4FIFO_base_params_t;

// ============================================================================
// Default parameter string
// ============================================================================

#define S4FIFO_BASE_DEFAULT_PARAMS                              \
  "small-size-ratio=0.10,ghost-size-ratio=0.90,"               \
  "move-to-main-threshold=2,small-skip-ratio=0,"               \
  "ghost-to-main-threshold=0"

// ============================================================================
// Shared utility functions
// ============================================================================

static inline int64_t S4FIFO_shared_get_occupied_byte(const cache_t *cache) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  return params->small_fifo->get_occupied_byte(params->small_fifo) +
         params->main_fifo->get_occupied_byte(params->main_fifo);
}

static inline int64_t S4FIFO_shared_get_n_obj(const cache_t *cache) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  return params->small_fifo->get_n_obj(params->small_fifo) +
         params->main_fifo->get_n_obj(params->main_fifo);
}

static inline bool S4FIFO_shared_can_insert(cache_t *cache,
                                            const request_t *req) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  return req->obj_size <= params->small_fifo->cache_size &&
         cache_can_insert_default(cache, req);
}

static inline bool S4FIFO_shared_remove(cache_t *cache, obj_id_t obj_id) {
  S4FIFO_base_params_t *params =
      (S4FIFO_base_params_t *)cache->eviction_params;
  bool removed = false;
  removed = removed || params->small_fifo->remove(params->small_fifo, obj_id);
  removed = removed ||
            (params->ghost_fifo &&
             params->ghost_fifo->remove(params->ghost_fifo, obj_id));
  removed = removed || params->main_fifo->remove(params->main_fifo, obj_id);
  return removed;
}

// ============================================================================
// Shared helpers: queue init, free, param parsing
// ============================================================================

static inline void S4FIFO_shared_init_queues(
    S4FIFO_base_params_t *params,
    const common_cache_params_t ccache_params) {
  int64_t small_fifo_size =
      (int64_t)ccache_params.cache_size * params->small_size_ratio;
  int64_t main_fifo_size = ccache_params.cache_size - small_fifo_size;
  int64_t ghost_fifo_size =
      (int64_t)(ccache_params.cache_size * params->ghost_size_ratio);

  common_cache_params_t local = ccache_params;
  local.cache_size = small_fifo_size;
  params->small_fifo = FIFO_init(local, NULL);
  params->has_evicted = false;

  if (ghost_fifo_size > 0) {
    local.cache_size = ghost_fifo_size;
    params->ghost_fifo = FIFO_init(local, NULL);
    snprintf(params->ghost_fifo->cache_name, CACHE_NAME_ARRAY_LEN,
             "FIFO-ghost");
  } else {
    params->ghost_fifo = NULL;
  }

  local.cache_size = main_fifo_size;
  params->main_fifo = FIFO_init(local, NULL);
  params->s_counter = 0;
}

static inline void S4FIFO_shared_free_queues(S4FIFO_base_params_t *params) {
  params->small_fifo->cache_free(params->small_fifo);
  if (params->ghost_fifo != NULL) {
    params->ghost_fifo->cache_free(params->ghost_fifo);
  }
  params->main_fifo->cache_free(params->main_fifo);
}

/**
 * @brief Parse one base parameter. Returns true if handled.
 */
static inline bool S4FIFO_shared_parse_one_param(S4FIFO_base_params_t *params,
                                                  const char *key,
                                                  const char *value) {
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
  } else {
    return false;
  }
  return true;
}

/**
 * @brief Shared param parser loop. Calls parse_one for each key=value pair.
 *        Unknown keys trigger ERROR.
 */
static inline void S4FIFO_shared_parse_params(
    S4FIFO_base_params_t *params, cache_t *cache,
    const char *cache_specific_params) {
  char *params_str = strdup(cache_specific_params);
  char *old_params_str = params_str;

  while (params_str != NULL && params_str[0] != '\0') {
    char *key = strsep((char **)&params_str, "=");
    char *value = strsep((char **)&params_str, ",");

    while (params_str != NULL && *params_str == ' ') {
      params_str++;
    }

    if (!S4FIFO_shared_parse_one_param(params, key, value)) {
      if (strcasecmp(key, "print") == 0) {
        printf("parameters: small-size-ratio=%.4lf,ghost-size-ratio=%.4lf,"
               "move-to-main-threshold=%d,small-skip-ratio=%.4lf,"
               "ghost-to-main-threshold=%d\n",
               params->small_size_ratio, params->ghost_size_ratio,
               params->move_to_main_threshold, params->small_skip_ratio,
               params->ghost_to_main_threshold);
        free(old_params_str);
        exit(0);
      }
      ERROR("%s does not have parameter %s\n", cache->cache_name, key);
      free(old_params_str);
      exit(1);
    }
  }

  free(old_params_str);
}

static inline const char *S4FIFO_shared_current_params(
    S4FIFO_base_params_t *params) {
  static __thread char buf[256];
  snprintf(buf, sizeof(buf),
           "small-size-ratio=%.4lf,ghost-size-ratio=%.4lf,"
           "move-to-main-threshold=%d,small-skip-ratio=%.4lf,"
           "ghost-to-main-threshold=%d",
           params->small_size_ratio, params->ghost_size_ratio,
           params->move_to_main_threshold, params->small_skip_ratio,
           params->ghost_to_main_threshold);
  return buf;
}

#ifdef __cplusplus
}
#endif
