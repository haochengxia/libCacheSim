//
// S4FIFO.c - Main S4FIFO with feature collection and periodic prediction
//
// Structure:
//   S4FIFO.h           - shared base params + utility helpers
//   S4FIFO_features.h  - feature collection (histograms, feature vector)
//   S4FIFO.c           - this file (main algorithm with learning support)
//   S4FIFO_base.c      - minimal variant (no feature collection)
//   S4FIFO_verify.c    - verification variant (exact hit position tracking)
//
// Modified by Haocheng at 08/23/2025
// Feature collection added at 12/06/2025
// Periodic prediction support added at 05/11/2026
//

#include "S4FIFO.h"
#include "S4FIFO_features.h"

#ifdef __cplusplus
extern "C" {
#endif

// S4FIFO extended params: base + feature collection + phase control
typedef struct {
  S4FIFO_base_params_t base;

  bool collect_features;
  int32_t feature_num_buckets;
  S4FIFO_feature_collector_t *feature_collector;
  S4FIFO_feature_vector_t last_features;
  char dump_file_path[256];

  S4FIFO_phase_t current_phase;
  int64_t feature_collect_reqs;
  int64_t prediction_interval;
  int64_t phase_start_n_req;
  int64_t warmup_end_n_req;

  S4FIFO_phase_callback_t phase_callback;
  void *callback_user_data;
} S4FIFO_params_t;

static const char *DEFAULT_CACHE_PARAMS =
    "small-size-ratio=0.10,ghost-size-ratio=0.90,move-to-main-threshold=2,"
    "small-skip-ratio=0,ghost-to-main-threshold=0,feature-collect-reqs=10000,"
    "feature-num-buckets=16,prediction-interval=0";

// ***********************************************************************
// ****                   function declarations                       ****
// ***********************************************************************
static void S4FIFO_free(cache_t *cache);
static bool S4FIFO_get(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFO_find(cache_t *cache, const request_t *req,
                                const bool update_cache);
static cache_obj_t *S4FIFO_insert(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFO_to_evict(cache_t *cache, const request_t *req);
static void S4FIFO_evict(cache_t *cache, const request_t *req);
static void S4FIFO_parse_params(cache_t *cache,
                                const char *cache_specific_params);
static void S4FIFO_evict_small(cache_t *cache, const request_t *req);
static void S4FIFO_evict_main(cache_t *cache, const request_t *req);

// ***********************************************************************
// ****                   end user facing functions                   ****
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
  cache->remove = S4FIFO_shared_remove;
  cache->to_evict = S4FIFO_to_evict;
  cache->get_n_obj = S4FIFO_shared_get_n_obj;
  cache->get_occupied_byte = S4FIFO_shared_get_occupied_byte;
  cache->can_insert = S4FIFO_shared_can_insert;
  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(S4FIFO_params_t));
  memset(cache->eviction_params, 0, sizeof(S4FIFO_params_t));
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->base.req_local = new_request();
  params->base.hit_on_ghost = false;

  S4FIFO_parse_params(cache, DEFAULT_CACHE_PARAMS);
  if (cache_specific_params != NULL) {
    S4FIFO_parse_params(cache, cache_specific_params);
  }

  S4FIFO_shared_init_queues(&params->base, ccache_params);

  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "S4FIFO-%.4lf-%d",
           params->base.small_size_ratio, params->base.move_to_main_threshold);

  params->current_phase = S4FIFO_PHASE_WARMUP;
  params->warmup_end_n_req = 0;
  params->phase_start_n_req = 0;
  params->phase_callback = NULL;
  params->callback_user_data = NULL;

  if (params->collect_features) {
    params->feature_collector = malloc(sizeof(S4FIFO_feature_collector_t));
    feature_collector_init(params->feature_collector,
                           ccache_params.cache_size,
                           params->base.small_fifo->cache_size,
                           params->base.main_fifo->cache_size,
                           params->base.ghost_fifo
                               ? params->base.ghost_fifo->cache_size
                               : 0,
                           params->feature_num_buckets);
  }

  return cache;
}

static void S4FIFO_free(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;

  if (params->feature_collector != NULL && params->dump_file_path[0] != '\0') {
    FILE *fp = fopen(params->dump_file_path, "w");
    if (fp != NULL) {
      feature_collector_get_features(
          params->feature_collector, &params->last_features,
          params->base.small_fifo->get_occupied_byte(params->base.small_fifo),
          params->base.main_fifo->get_occupied_byte(params->base.main_fifo),
          params->base.ghost_fifo
              ? params->base.ghost_fifo->get_occupied_byte(
                    params->base.ghost_fifo)
              : 0);
      feature_vector_print(&params->last_features, fp);
      fclose(fp);
    }
  }

  free_request(params->base.req_local);
  S4FIFO_shared_free_queues(&params->base);
  if (params->feature_collector != NULL) {
    free(params->feature_collector);
  }
  free(cache->eviction_params);
  cache_struct_free(cache);
}

// ***********************************************************************
// ****                       phase management                        ****
// ***********************************************************************

static void S4FIFO_transition_phase(cache_t *cache, S4FIFO_params_t *params,
                                    S4FIFO_phase_t new_phase) {
  S4FIFO_phase_t old_phase = params->current_phase;
  params->current_phase = new_phase;
  params->phase_start_n_req = cache->n_req;

  if (params->phase_callback != NULL) {
    params->phase_callback(cache, old_phase, new_phase);
  }

  if (params->feature_collector != NULL &&
      new_phase == S4FIFO_PHASE_PREDICTION) {
    feature_collector_get_features(
        params->feature_collector, &params->last_features,
        params->base.small_fifo->get_occupied_byte(params->base.small_fifo),
        params->base.main_fifo->get_occupied_byte(params->base.main_fifo),
        params->base.ghost_fifo
            ? params->base.ghost_fifo->get_occupied_byte(params->base.ghost_fifo)
            : 0);
  }

  if (params->feature_collector != NULL &&
      new_phase == S4FIFO_PHASE_FEATURE_COLLECT) {
    feature_collector_reset(params->feature_collector,
                            cache->cache_size,
                            params->base.small_fifo->cache_size,
                            params->base.main_fifo->cache_size,
                            params->base.ghost_fifo
                                ? params->base.ghost_fifo->cache_size
                                : 0,
                            params->feature_num_buckets);
  }
}

static void S4FIFO_update_phase(cache_t *cache, S4FIFO_params_t *params) {
  if (params->feature_collect_reqs <= 0) return;

  int64_t n_req = cache->n_req;
  S4FIFO_phase_t phase = params->current_phase;

  if (phase == S4FIFO_PHASE_WARMUP) {
    if (cache->get_occupied_byte(cache) >= cache->cache_size) {
      params->warmup_end_n_req = n_req;
      S4FIFO_transition_phase(cache, params, S4FIFO_PHASE_FEATURE_COLLECT);
    }
  } else if (phase == S4FIFO_PHASE_FEATURE_COLLECT) {
    if ((n_req - params->phase_start_n_req) >= params->feature_collect_reqs) {
      S4FIFO_transition_phase(cache, params, S4FIFO_PHASE_PREDICTION);
    }
  } else if (phase == S4FIFO_PHASE_PREDICTION) {
    if (params->prediction_interval > 0 &&
        (n_req - params->phase_start_n_req) >= params->prediction_interval) {
      S4FIFO_transition_phase(cache, params, S4FIFO_PHASE_FEATURE_COLLECT);
    }
  }
}

// ***********************************************************************
// ****                         S4FIFO_get                            ****
// ***********************************************************************

static bool S4FIFO_get(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  DEBUG_ASSERT(params->base.small_fifo->get_occupied_byte(params->base.small_fifo) +
                   params->base.main_fifo->get_occupied_byte(params->base.main_fifo) <=
               cache->cache_size);

  S4FIFO_update_phase(cache, params);

  if (params->feature_collector != NULL) {
    feature_collector_record_request(params->feature_collector, cache->n_req);
  }

  bool cache_hit = cache_get_base(cache, req);

  if (!cache_hit && params->feature_collector != NULL &&
      params->current_phase == S4FIFO_PHASE_FEATURE_COLLECT) {
    feature_collector_record_miss(params->feature_collector);
  }

  return cache_hit;
}

// ***********************************************************************
// ****                    developer facing APIs                      ****
// ***********************************************************************

static cache_obj_t *S4FIFO_find(cache_t *cache, const request_t *req,
                                const bool update_cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;

  if (!update_cache) {
    cache_obj_t *obj = params->base.small_fifo->find(params->base.small_fifo, req, false);
    if (obj != NULL) return obj;
    obj = params->base.main_fifo->find(params->base.main_fifo, req, false);
    return obj;
  }

  params->base.hit_on_ghost = false;
  S4FIFO_feature_collector_t *fc = params->feature_collector;
  bool collecting = (fc != NULL && params->current_phase == S4FIFO_PHASE_FEATURE_COLLECT);

  cache_obj_t *obj = params->base.small_fifo->find(params->base.small_fifo, req, true);
  if (obj != NULL) {
    if ((int64_t)(-obj->time_stamp + params->base.s_counter) >=
        (int64_t)(params->base.small_skip_ratio * params->base.small_fifo->cache_size)) {
      obj->S4FIFO.freq += 1;
    }
    if (collecting) {
      feature_collector_record_hit_small(fc, obj->S4FIFO.insertion_time);
      feature_collector_record_repeat(fc);
    }
    return obj;
  }

  cache_obj_t *ghost_obj = NULL;
  if (params->base.ghost_fifo != NULL) {
    ghost_obj = params->base.ghost_fifo->find(params->base.ghost_fifo, req, false);
  }
  if (ghost_obj != NULL) {
    int64_t ghost_freq = ghost_obj->S4FIFO.freq;
    if (ghost_freq >= params->base.ghost_to_main_threshold) {
      if (collecting) {
        feature_collector_record_hit_ghost(fc, ghost_obj->S4FIFO.insertion_time,
                                           ghost_obj->S4FIFO.insert_bucket);
        feature_collector_record_ghost_removal(fc);
      }
      params->base.ghost_fifo->remove(params->base.ghost_fifo, req->obj_id);
      params->base.hit_on_ghost = true;
      params->base.hit_on_ghost_freq = ghost_freq;
    } else {
      ghost_obj->S4FIFO.freq = ghost_freq + 1;
    }
  }

  obj = params->base.main_fifo->find(params->base.main_fifo, req, true);
  if (obj != NULL) {
    obj->S4FIFO.freq += 1;
    if (collecting) {
      feature_collector_record_hit_main(fc, obj->S4FIFO.insertion_time);
      feature_collector_record_repeat(fc);
    }
  }

  return obj;
}

static cache_obj_t *S4FIFO_insert(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  S4FIFO_feature_collector_t *fc = params->feature_collector;
  cache_obj_t *obj = NULL;

  cache_t *small = params->base.small_fifo;
  cache_t *main = params->base.main_fifo;

  if (params->base.hit_on_ghost) {
    params->base.hit_on_ghost = false;
    params->base.hit_on_ghost_freq = 0;
    obj = main->insert(main, req);
    if (fc != NULL && obj != NULL) {
      obj->S4FIFO.insertion_time = fc->main_insert_counter;
      obj->S4FIFO.insert_bucket =
          (int32_t)feature_collector_record_insert_main(fc);
    }
  } else {
    if (req->obj_size >= small->cache_size) {
      return NULL;
    }

    if (!params->base.has_evicted &&
        small->get_occupied_byte(small) >= small->cache_size) {
      obj = main->insert(main, req);
      if (fc != NULL && obj != NULL) {
        obj->S4FIFO.insertion_time = fc->main_insert_counter;
        obj->S4FIFO.insert_bucket =
            (int32_t)feature_collector_record_insert_main(fc);
      }
    } else {
      obj = small->insert(small, req);
      params->base.s_counter++;
      obj->time_stamp = params->base.s_counter;
      if (fc != NULL && obj != NULL) {
        obj->S4FIFO.insertion_time = fc->small_insert_counter;
        obj->S4FIFO.insert_bucket =
            (int32_t)feature_collector_record_insert_small(fc);
        if (params->current_phase == S4FIFO_PHASE_FEATURE_COLLECT) {
          feature_collector_record_unique(fc);
        }
      }
    }
  }

  if (obj != NULL) {
    obj->S4FIFO.freq = 0;
  }

  return obj;
}

static cache_obj_t *S4FIFO_to_evict(cache_t *cache, const request_t *req) {
  assert(false);
  return NULL;
}

static void S4FIFO_evict_small(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  cache_t *small = params->base.small_fifo;
  cache_t *ghost = params->base.ghost_fifo;
  cache_t *main = params->base.main_fifo;
  S4FIFO_feature_collector_t *fc = params->feature_collector;

  bool has_evicted = false;
  while (!has_evicted && small->get_occupied_byte(small) > 0) {
    cache_obj_t *obj_to_evict = small->to_evict(small, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    copy_cache_obj_to_request(params->base.req_local, obj_to_evict);

    if (obj_to_evict->S4FIFO.freq >= params->base.move_to_main_threshold) {
      cache_obj_t *main_obj = main->insert(main, params->base.req_local);
      if (fc != NULL && main_obj != NULL) {
        main_obj->S4FIFO.insertion_time = fc->main_insert_counter;
        main_obj->S4FIFO.insert_bucket =
            (int32_t)feature_collector_record_insert_main(fc);
      }
    } else {
      if (ghost != NULL) {
        int64_t small_freq = obj_to_evict->S4FIFO.freq;
        ghost->get(ghost, params->base.req_local);
        cache_obj_t *ghost_obj = ghost->find(ghost, params->base.req_local, false);
        if (ghost_obj != NULL) {
          ghost_obj->S4FIFO.freq = small_freq;
          if (fc != NULL) {
            ghost_obj->S4FIFO.insertion_time = fc->ghost_insert_counter;
            ghost_obj->S4FIFO.insert_bucket =
                (int32_t)feature_collector_record_insert_ghost(fc);
          }
        }
      }
      has_evicted = true;
      if (fc != NULL && params->current_phase == S4FIFO_PHASE_FEATURE_COLLECT) {
        feature_collector_record_onehit(fc);
      }
    }

    bool removed = small->remove(small, params->base.req_local->obj_id);
    DEBUG_ASSERT(removed);
  }
}

static void S4FIFO_evict_main(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  S4FIFO_feature_collector_t *fc = params->feature_collector;
  cache_t *main = params->base.main_fifo;

  bool has_evicted = false;
  while (!has_evicted && main->get_occupied_byte(main) > 0) {
    cache_obj_t *obj_to_evict = main->to_evict(main, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    int freq = obj_to_evict->S4FIFO.freq;
    copy_cache_obj_to_request(params->base.req_local, obj_to_evict);
    if (freq >= 1) {
      main->remove(main, obj_to_evict->obj_id);
      obj_to_evict = NULL;

      cache_obj_t *new_obj = main->insert(main, params->base.req_local);
      new_obj->S4FIFO.freq = MIN(freq, 3) - 1;
      if (fc != NULL) {
        new_obj->S4FIFO.insertion_time = fc->main_insert_counter;
        new_obj->S4FIFO.insert_bucket =
            (int32_t)feature_collector_record_insert_main(fc);
      }
    } else {
      bool removed = main->remove(main, obj_to_evict->obj_id);
      DEBUG_ASSERT(removed);
      has_evicted = true;
    }
  }
}

static void S4FIFO_evict(cache_t *cache, const request_t *req) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->base.has_evicted = true;

  cache_t *small = params->base.small_fifo;
  cache_t *main = params->base.main_fifo;

  if (main->get_occupied_byte(main) > main->cache_size ||
      small->get_occupied_byte(small) == 0) {
    S4FIFO_evict_main(cache, req);
  } else {
    S4FIFO_evict_small(cache, req);
  }
}

// ***********************************************************************
// ****                    parameter handling                         ****
// ***********************************************************************

static void S4FIFO_parse_params(cache_t *cache,
                                const char *cache_specific_params) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)(cache->eviction_params);

  char *params_str = strdup(cache_specific_params);
  char *old_params_str = params_str;

  while (params_str != NULL && params_str[0] != '\0') {
    char *key = strsep((char **)&params_str, "=");
    char *value = strsep((char **)&params_str, ",");

    while (params_str != NULL && *params_str == ' ') {
      params_str++;
    }

    if (S4FIFO_shared_parse_one_param(&params->base, key, value)) {
      continue;
    } else if (strcasecmp(key, "feature-collect-reqs") == 0) {
      params->feature_collect_reqs = strtoll(value, NULL, 10);
    } else if (strcasecmp(key, "feature-num-buckets") == 0) {
      params->feature_num_buckets = atoi(value);
    } else if (strcasecmp(key, "prediction-interval") == 0) {
      params->prediction_interval = strtoll(value, NULL, 10);
    } else if (strcasecmp(key, "collect-features") == 0) {
      params->collect_features =
          (strcasecmp(value, "true") == 0 || atoi(value) == 1);
    } else if (strcasecmp(key, "dump-file") == 0) {
      strncpy(params->dump_file_path, value,
              sizeof(params->dump_file_path) - 1);
    } else if (strcasecmp(key, "print") == 0) {
      printf("parameters: %s\n",
             S4FIFO_shared_current_params(&params->base));
      exit(0);
    } else {
      ERROR("%s does not have parameter %s\n", cache->cache_name, key);
      exit(1);
    }
  }

  free(old_params_str);
}

// ***********************************************************************
// ****               Three-phase simulation API                      ****
// ***********************************************************************

void S4FIFO_set_feature_collect_reqs(cache_t *cache, int64_t n_reqs) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->feature_collect_reqs = n_reqs;
}

void S4FIFO_set_phase_callback(cache_t *cache,
                               S4FIFO_phase_callback_t callback,
                               void *user_data) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  params->phase_callback = callback;
  params->callback_user_data = user_data;
}

S4FIFO_phase_t S4FIFO_get_current_phase(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  return params->current_phase;
}

void S4FIFO_set_phase(cache_t *cache, S4FIFO_phase_t phase) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  S4FIFO_phase_t old_phase = params->current_phase;
  params->current_phase = phase;
  if (params->phase_callback != NULL && old_phase != phase) {
    params->phase_callback(cache, old_phase, phase);
  }
}

const char *S4FIFO_get_phase_name(S4FIFO_phase_t phase) {
  switch (phase) {
    case S4FIFO_PHASE_WARMUP:
      return "WARMUP";
    case S4FIFO_PHASE_FEATURE_COLLECT:
      return "FEATURE_COLLECT";
    case S4FIFO_PHASE_PREDICTION:
      return "PREDICTION";
    default:
      return "UNKNOWN";
  }
}

// ***********************************************************************
// ****               Feature collection API                          ****
// ***********************************************************************

void S4FIFO_enable_feature_collection(cache_t *cache, int64_t window_size,
                                      int32_t num_buckets) {
  (void)window_size;
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  if (params->feature_collector == NULL) {
    params->feature_collector = malloc(sizeof(S4FIFO_feature_collector_t));
    feature_collector_init(params->feature_collector, cache->cache_size,
                           params->base.small_fifo->cache_size,
                           params->base.main_fifo->cache_size,
                           params->base.ghost_fifo
                               ? params->base.ghost_fifo->cache_size
                               : 0,
                           num_buckets > 0 ? num_buckets
                                           : params->feature_num_buckets);
  }
  params->collect_features = true;
}

void S4FIFO_disable_feature_collection(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  if (params->feature_collector != NULL) {
    free(params->feature_collector);
    params->feature_collector = NULL;
  }
  params->collect_features = false;
}

bool S4FIFO_get_features(cache_t *cache, S4FIFO_feature_vector_t *fv) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  if (params->feature_collector == NULL) return false;

  feature_collector_get_features(
      params->feature_collector, fv,
      params->base.small_fifo->get_occupied_byte(params->base.small_fifo),
      params->base.main_fifo->get_occupied_byte(params->base.main_fifo),
      params->base.ghost_fifo
          ? params->base.ghost_fifo->get_occupied_byte(params->base.ghost_fifo)
          : 0);
  return true;
}

const S4FIFO_feature_vector_t *S4FIFO_get_last_features(cache_t *cache) {
  S4FIFO_params_t *params = (S4FIFO_params_t *)cache->eviction_params;
  return &params->last_features;
}

void S4FIFO_print_features(cache_t *cache, FILE *fp) {
  S4FIFO_feature_vector_t fv;
  if (S4FIFO_get_features(cache, &fv)) {
    feature_vector_print(&fv, fp);
  } else {
    fprintf(fp, "Feature collection not enabled\n");
  }
}

#ifdef __cplusplus
}
#endif
