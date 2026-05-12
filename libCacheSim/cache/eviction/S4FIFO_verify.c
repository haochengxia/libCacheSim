//
// S4FIFO_verify.c - Verification variant with exact hit position tracking
//
// Tracks the EXACT hit position for each object in every queue.
// Used for validating the approximate feature collection in S4FIFO.c.
//

#include "S4FIFO.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VERIFY_MAX_BINS 1024

typedef struct {
  int64_t bins[VERIFY_MAX_BINS];
  int64_t total_hits;
  int64_t max_position;
  int64_t bin_size;
  int n_buckets;
} hit_pos_histogram_t;

typedef struct {
  S4FIFO_base_params_t base;

  bool dump_hit_pos;
  FILE *dump_file;
  char dump_file_path[256];

  hit_pos_histogram_t small_hist;
  hit_pos_histogram_t main_hist;
  hit_pos_histogram_t ghost_hist;

  bool warmed_up;
  int64_t collect_req;
  int64_t collect_counter;
  bool collection_active;
  int n_buckets;
} S4FIFO_verify_params_t;

static const char *DEFAULT_CACHE_PARAMS =
    "small-size-ratio=0.10,ghost-size-ratio=0.90,move-to-main-threshold=2,"
    "small-skip-ratio=0,ghost-to-main-threshold=0,dump-hit-pos=false,"
    "collect-req=0,n-buckets=1024";

// ***********************************************************************
// ****                   function declarations                       ****
// ***********************************************************************
static void S4FIFO_verify_free(cache_t *cache);
static bool S4FIFO_verify_get(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFO_verify_find(cache_t *cache, const request_t *req,
                                       const bool update_cache);
static cache_obj_t *S4FIFO_verify_insert(cache_t *cache, const request_t *req);
static cache_obj_t *S4FIFO_verify_to_evict(cache_t *cache, const request_t *req);
static void S4FIFO_verify_evict(cache_t *cache, const request_t *req);
static void S4FIFO_verify_parse_params(cache_t *cache,
                                       const char *cache_specific_params);
static void S4FIFO_verify_evict_small(cache_t *cache, const request_t *req);
static void S4FIFO_verify_evict_main(cache_t *cache, const request_t *req);

// ***********************************************************************
// ****                   Helper functions                            ****
// ***********************************************************************

static inline int64_t find_exact_position_in_fifo(cache_t *fifo_cache,
                                                  obj_id_t obj_id) {
  FIFO_params_t *fifo_params = (FIFO_params_t *)fifo_cache->eviction_params;
  cache_obj_t *curr = fifo_params->q_head;
  int64_t position = 0;

  while (curr != NULL) {
    if (curr->obj_id == obj_id) {
      return position;
    }
    curr = curr->queue.next;
    position++;
  }
  return -1;
}

static inline void hit_pos_histogram_init(hit_pos_histogram_t *hist,
                                          int64_t expected_max_pos,
                                          int n_buckets) {
  memset(hist, 0, sizeof(hit_pos_histogram_t));
  if (n_buckets <= 0) n_buckets = VERIFY_MAX_BINS;
  if (n_buckets > VERIFY_MAX_BINS) n_buckets = VERIFY_MAX_BINS;
  hist->n_buckets = n_buckets;
  hist->bin_size = (expected_max_pos + n_buckets - 1) / n_buckets;
  if (hist->bin_size < 1) hist->bin_size = 1;
}

static inline void hit_pos_histogram_record(hit_pos_histogram_t *hist,
                                            int64_t position) {
  hist->total_hits++;
  if (position > hist->max_position) {
    hist->max_position = position;
  }

  int64_t bin = position / hist->bin_size;
  if (bin >= hist->n_buckets) {
    bin = hist->n_buckets - 1;
  }
  hist->bins[bin]++;
}

static void hit_pos_histogram_dump(const hit_pos_histogram_t *hist,
                                   const char *queue_name, FILE *fp) {
  if (hist->total_hits == 0) {
    fprintf(fp, "# %s: no hits\n", queue_name);
    return;
  }

  fprintf(fp, "# %s hit position distribution (total_hits=%ld, max_pos=%ld, "
          "bin_size=%ld, n_buckets=%d)\n",
          queue_name, hist->total_hits, hist->max_position,
          hist->bin_size, hist->n_buckets);
  fprintf(fp, "# bin_start,bin_end,count,ratio\n");

  for (int i = 0; i < hist->n_buckets; i++) {
    int64_t bin_start = i * hist->bin_size;
    int64_t bin_end = (i + 1) * hist->bin_size - 1;
    double ratio = (double)hist->bins[i] / hist->total_hits;
    fprintf(fp, "%s,%ld,%ld,%ld,%.6f\n",
            queue_name, bin_start, bin_end, hist->bins[i], ratio);
  }
}

// ***********************************************************************
// ****                   end user facing functions                   ****
// ***********************************************************************

cache_t *S4FIFO_verify_init(const common_cache_params_t ccache_params,
                            const char *cache_specific_params) {
  cache_t *cache =
      cache_struct_init("S4FIFO-verify", ccache_params, cache_specific_params);
  cache->cache_init = S4FIFO_verify_init;
  cache->cache_free = S4FIFO_verify_free;
  cache->get = S4FIFO_verify_get;
  cache->find = S4FIFO_verify_find;
  cache->insert = S4FIFO_verify_insert;
  cache->evict = S4FIFO_verify_evict;
  cache->remove = S4FIFO_shared_remove;
  cache->to_evict = S4FIFO_verify_to_evict;
  cache->get_n_obj = S4FIFO_shared_get_n_obj;
  cache->get_occupied_byte = S4FIFO_shared_get_occupied_byte;
  cache->can_insert = S4FIFO_shared_can_insert;
  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(S4FIFO_verify_params_t));
  memset(cache->eviction_params, 0, sizeof(S4FIFO_verify_params_t));
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  params->base.req_local = new_request();
  params->base.hit_on_ghost = false;

  S4FIFO_verify_parse_params(cache, DEFAULT_CACHE_PARAMS);
  if (cache_specific_params != NULL) {
    S4FIFO_verify_parse_params(cache, cache_specific_params);
  }

  S4FIFO_shared_init_queues(&params->base, ccache_params);

  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "S4FIFO-verify-%.4lf-%d",
           params->base.small_size_ratio, params->base.move_to_main_threshold);

  hit_pos_histogram_init(&params->small_hist,
                         params->base.small_fifo->cache_size, params->n_buckets);
  hit_pos_histogram_init(&params->main_hist,
                         params->base.main_fifo->cache_size, params->n_buckets);
  hit_pos_histogram_init(&params->ghost_hist,
                         params->base.ghost_fifo ? params->base.ghost_fifo->cache_size : 0,
                         params->n_buckets);

  params->warmed_up = false;
  params->collect_counter = 0;
  params->collection_active = false;

  if (params->dump_hit_pos && params->dump_file_path[0] != '\0') {
    params->dump_file = fopen(params->dump_file_path, "w");
    if (params->dump_file == NULL) {
      fprintf(stderr, "Warning: cannot open dump file %s, using stdout\n",
              params->dump_file_path);
      params->dump_file = stdout;
    }
  } else {
    params->dump_file = stdout;
  }

  return cache;
}

static void S4FIFO_verify_free(cache_t *cache) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;

  if (params->dump_hit_pos) {
    FILE *fp = params->dump_file ? params->dump_file : stdout;
    fprintf(fp, "\n# ========== FINAL HIT POSITION HISTOGRAMS ==========\n");
    hit_pos_histogram_dump(&params->small_hist, "small", fp);
    hit_pos_histogram_dump(&params->main_hist, "main", fp);
    hit_pos_histogram_dump(&params->ghost_hist, "ghost", fp);

    fprintf(fp, "\n# ========== SUMMARY ==========\n");
    fprintf(fp, "# small: total_hits=%ld, max_pos=%ld\n",
            params->small_hist.total_hits, params->small_hist.max_position);
    fprintf(fp, "# main:  total_hits=%ld, max_pos=%ld\n",
            params->main_hist.total_hits, params->main_hist.max_position);
    fprintf(fp, "# ghost: total_hits=%ld, max_pos=%ld\n",
            params->ghost_hist.total_hits, params->ghost_hist.max_position);

    if (params->dump_file != NULL && params->dump_file != stdout) {
      fclose(params->dump_file);
    }
  }

  free_request(params->base.req_local);
  S4FIFO_shared_free_queues(&params->base);
  free(cache->eviction_params);
  cache_struct_free(cache);
}

static bool S4FIFO_verify_get(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  DEBUG_ASSERT(params->base.small_fifo->get_occupied_byte(params->base.small_fifo) +
                   params->base.main_fifo->get_occupied_byte(params->base.main_fifo) <=
               cache->cache_size);

  if (!params->warmed_up) {
    int64_t total_occupied =
        params->base.small_fifo->get_occupied_byte(params->base.small_fifo) +
        params->base.main_fifo->get_occupied_byte(params->base.main_fifo);
    if (total_occupied >= cache->cache_size) {
      params->warmed_up = true;
      params->collection_active = true;
      if (params->dump_hit_pos && params->dump_file != NULL) {
        fprintf(params->dump_file,
                "# Warmup complete, cache full, starting collection\n");
      }
    }
  }

  if (params->collection_active && params->collect_req > 0 &&
      params->collect_counter >= params->collect_req) {
    params->collection_active = false;
  }

  if (params->collection_active) {
    params->collect_counter++;
  }

  return cache_get_base(cache, req);
}

// ***********************************************************************
// ****                    developer facing APIs                      ****
// ***********************************************************************

static cache_obj_t *S4FIFO_verify_find(cache_t *cache, const request_t *req,
                                       const bool update_cache) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;

  if (!update_cache) {
    cache_obj_t *obj =
        params->base.small_fifo->find(params->base.small_fifo, req, false);
    if (obj != NULL) return obj;
    obj = params->base.main_fifo->find(params->base.main_fifo, req, false);
    return obj;
  }

  params->base.hit_on_ghost = false;

  cache_obj_t *obj =
      params->base.small_fifo->find(params->base.small_fifo, req, true);
  if (obj != NULL) {
    if ((int64_t)(-obj->time_stamp + params->base.s_counter) >=
        (int64_t)(params->base.small_skip_ratio *
                  params->base.small_fifo->cache_size)) {
      obj->S4FIFO.freq += 1;
    }

    if (params->collection_active) {
      int64_t position =
          find_exact_position_in_fifo(params->base.small_fifo, req->obj_id);
      if (position >= 0) {
        hit_pos_histogram_record(&params->small_hist, position);
      }
    }

    return obj;
  }

  cache_obj_t *ghost_obj = NULL;
  if (params->base.ghost_fifo != NULL) {
    ghost_obj =
        params->base.ghost_fifo->find(params->base.ghost_fifo, req, false);
  }
  if (ghost_obj != NULL) {
    int64_t ghost_freq = ghost_obj->S4FIFO.freq;

    if (params->collection_active) {
      int64_t position =
          find_exact_position_in_fifo(params->base.ghost_fifo, req->obj_id);
      if (position >= 0) {
        hit_pos_histogram_record(&params->ghost_hist, position);
      }
    }

    if (ghost_freq >= params->base.ghost_to_main_threshold) {
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

    if (params->collection_active) {
      int64_t position =
          find_exact_position_in_fifo(params->base.main_fifo, req->obj_id);
      if (position >= 0) {
        hit_pos_histogram_record(&params->main_hist, position);
      }
    }
  }

  return obj;
}

static cache_obj_t *S4FIFO_verify_insert(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  cache_obj_t *obj = NULL;

  cache_t *small = params->base.small_fifo;
  cache_t *main = params->base.main_fifo;

  if (params->base.hit_on_ghost) {
    params->base.hit_on_ghost = false;
    params->base.hit_on_ghost_freq = 0;
    obj = main->insert(main, req);
  } else {
    if (req->obj_size >= small->cache_size) {
      return NULL;
    }

    if (!params->base.has_evicted &&
        small->get_occupied_byte(small) >= small->cache_size) {
      obj = main->insert(main, req);
    } else {
      obj = small->insert(small, req);
      params->base.s_counter++;
      obj->time_stamp = params->base.s_counter;
    }
  }

  if (obj != NULL) {
    obj->S4FIFO.freq = 0;
  }

  return obj;
}

static cache_obj_t *S4FIFO_verify_to_evict(cache_t *cache,
                                           const request_t *req) {
  assert(false);
  return NULL;
}

static void S4FIFO_verify_evict_small(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  cache_t *small = params->base.small_fifo;
  cache_t *ghost = params->base.ghost_fifo;
  cache_t *main = params->base.main_fifo;

  bool has_evicted = false;
  while (!has_evicted && small->get_occupied_byte(small) > 0) {
    cache_obj_t *obj_to_evict = small->to_evict(small, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    copy_cache_obj_to_request(params->base.req_local, obj_to_evict);

    if (obj_to_evict->S4FIFO.freq >= params->base.move_to_main_threshold) {
      main->insert(main, params->base.req_local);
    } else {
      if (ghost != NULL) {
        int64_t small_freq = obj_to_evict->S4FIFO.freq;
        ghost->get(ghost, params->base.req_local);
        cache_obj_t *ghost_obj = ghost->find(ghost, params->base.req_local, false);
        if (ghost_obj != NULL) {
          ghost_obj->S4FIFO.freq = small_freq;
        }
      }
      has_evicted = true;
    }

    bool removed = small->remove(small, params->base.req_local->obj_id);
    DEBUG_ASSERT(removed);
  }
}

static void S4FIFO_verify_evict_main(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
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
    } else {
      bool removed = main->remove(main, obj_to_evict->obj_id);
      DEBUG_ASSERT(removed);
      has_evicted = true;
    }
  }
}

static void S4FIFO_verify_evict(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  params->base.has_evicted = true;

  cache_t *small = params->base.small_fifo;
  cache_t *main = params->base.main_fifo;

  if (main->get_occupied_byte(main) > main->cache_size ||
      small->get_occupied_byte(small) == 0) {
    S4FIFO_verify_evict_main(cache, req);
  } else {
    S4FIFO_verify_evict_small(cache, req);
  }
}

// ***********************************************************************
// ****                    parameter handling                         ****
// ***********************************************************************

static void S4FIFO_verify_parse_params(cache_t *cache,
                                       const char *cache_specific_params) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)(cache->eviction_params);

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
    } else if (strcasecmp(key, "dump-hit-pos") == 0) {
      params->dump_hit_pos =
          (strcasecmp(value, "true") == 0 || atoi(value) == 1);
    } else if (strcasecmp(key, "dump-file") == 0) {
      strncpy(params->dump_file_path, value,
              sizeof(params->dump_file_path) - 1);
      params->dump_file_path[sizeof(params->dump_file_path) - 1] = '\0';
    } else if (strcasecmp(key, "collect-req") == 0) {
      params->collect_req = atol(value);
    } else if (strcasecmp(key, "n-buckets") == 0) {
      params->n_buckets = atoi(value);
    } else if (strcasecmp(key, "print") == 0) {
      printf("parameters: small-size-ratio=%.4lf,ghost-size-ratio=%.4lf,"
             "move-to-main-threshold=%d,dump-hit-pos=%s,collect-req=%ld,"
             "n-buckets=%d\n",
             params->base.small_size_ratio, params->base.ghost_size_ratio,
             params->base.move_to_main_threshold,
             params->dump_hit_pos ? "true" : "false",
             params->collect_req, params->n_buckets);
      free(old_params_str);
      exit(0);
    } else {
      ERROR("%s does not have parameter %s\n", cache->cache_name, key);
      free(old_params_str);
      exit(1);
    }
  }

  free(old_params_str);
}

// ***********************************************************************
// ****              API for dumping histograms                       ****
// ***********************************************************************

int64_t S4FIFO_verify_get_histogram(cache_t *cache, const char *queue_name,
                                    int64_t *bins, int64_t *total_hits) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;

  hit_pos_histogram_t *hist = NULL;
  if (strcmp(queue_name, "small") == 0) {
    hist = &params->small_hist;
  } else if (strcmp(queue_name, "main") == 0) {
    hist = &params->main_hist;
  } else if (strcmp(queue_name, "ghost") == 0) {
    hist = &params->ghost_hist;
  } else {
    return -1;
  }

  memcpy(bins, hist->bins, sizeof(hist->bins));
  *total_hits = hist->total_hits;
  return hist->bin_size;
}

void S4FIFO_verify_dump_all_histograms(cache_t *cache, const char *filepath) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;

  FILE *fp = fopen(filepath, "w");
  if (fp == NULL) {
    fprintf(stderr, "Cannot open file %s for writing\n", filepath);
    return;
  }

  fprintf(fp, "# S4FIFO-verify hit position histograms\n");
  fprintf(fp, "# Format: queue,bin_start,bin_end,count,ratio\n\n");

  hit_pos_histogram_dump(&params->small_hist, "small", fp);
  hit_pos_histogram_dump(&params->main_hist, "main", fp);
  hit_pos_histogram_dump(&params->ghost_hist, "ghost", fp);

  fclose(fp);
}

#ifdef __cplusplus
}
#endif
