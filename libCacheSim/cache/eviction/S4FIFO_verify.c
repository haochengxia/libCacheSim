//
// S4FIFO_verify.c - Verification version with exact hit position tracking
//
// This is a reference implementation for validating feature collection.
// It tracks the EXACT hit position for each object in every queue by
// maintaining per-object position metadata.
//
// Usage:
//   - Set dump-hit-pos=true to enable hit position dumping
//   - Set dump-file=<path> to specify output file (default: stdout)
//
// Output format (CSV):
//   queue,position,queue_size,normalized_position
//
// Created by Haocheng at 12/06/2025
//

#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/evictionAlgo.h"

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of histogram bins for exact hit position distribution
#define VERIFY_MAX_BINS 1024

// Hit position histogram for one queue
typedef struct {
  int64_t bins[VERIFY_MAX_BINS];  // hit counts per position bin
  int64_t total_hits;              // total hits in this queue
  int64_t max_position;            // maximum position seen
  int64_t bin_size;                // size of each bin (in positions)
  int n_buckets;                   // actual number of buckets used
} hit_pos_histogram_t;

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

  // ==== Verification: exact hit position tracking ====
  bool dump_hit_pos;           // whether to dump hit positions
  FILE *dump_file;             // output file (NULL = stdout)
  char dump_file_path[256];    // path to dump file


  // Exact hit position histograms
  hit_pos_histogram_t small_hist;
  hit_pos_histogram_t main_hist;
  hit_pos_histogram_t ghost_hist;

  // Warmup and collection control
  bool warmed_up;             // whether cache has been filled (warmed up)
  int64_t collect_req;        // number of requests to collect after warmup (0 = unlimited)
  int64_t collect_counter;    // counter for collected requests
  bool collection_active;     // whether we are in collection phase
  int n_buckets;              // number of histogram buckets (default: VERIFY_MAX_BINS)
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
static bool S4FIFO_verify_remove(cache_t *cache, const obj_id_t obj_id);
static inline int64_t S4FIFO_verify_get_occupied_byte(const cache_t *cache);
static inline int64_t S4FIFO_verify_get_n_obj(const cache_t *cache);
static inline bool S4FIFO_verify_can_insert(cache_t *cache, const request_t *req);
static void S4FIFO_verify_parse_params(cache_t *cache,
                                       const char *cache_specific_params);

static void S4FIFO_verify_evict_small(cache_t *cache, const request_t *req);
static void S4FIFO_verify_evict_main(cache_t *cache, const request_t *req);

// ***********************************************************************
// ****                   Helper functions                            ****
// ***********************************************************************

/**
 * @brief Find the exact position of an object in a FIFO queue by traversing
 *        the linked list from head (newest) to tail (oldest).
 *        Position 0 = newest object (just inserted), Position N-1 = oldest (to be evicted)
 *
 * @param fifo_cache the FIFO cache to search
 * @param obj_id the object ID to find
 * @return position from head (0-indexed), or -1 if not found
 */
static inline int64_t find_exact_position_in_fifo(cache_t *fifo_cache,
                                                  obj_id_t obj_id) {
  FIFO_params_t *fifo_params = (FIFO_params_t *)fifo_cache->eviction_params;
  cache_obj_t *curr = fifo_params->q_head;  // Start from newest (just inserted)
  int64_t position = 0;

  while (curr != NULL) {
    if (curr->obj_id == obj_id) {
      return position;
    }
    curr = curr->queue.next;  // Move towards tail (older objects)
    position++;
  }
  return -1;  // Not found
}

static inline void hit_pos_histogram_init(hit_pos_histogram_t *hist,
                                          int64_t expected_max_pos,
                                          int n_buckets) {
  memset(hist, 0, sizeof(hit_pos_histogram_t));
  // Clamp n_buckets to valid range
  if (n_buckets <= 0) n_buckets = VERIFY_MAX_BINS;
  if (n_buckets > VERIFY_MAX_BINS) n_buckets = VERIFY_MAX_BINS;
  hist->n_buckets = n_buckets;
  // Set bin size to have reasonable resolution
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

  fprintf(fp, "# %s hit position distribution (total_hits=%ld, max_pos=%ld, bin_size=%ld, n_buckets=%d)\n",
          queue_name, hist->total_hits, hist->max_position, hist->bin_size, hist->n_buckets);
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
  cache->remove = S4FIFO_verify_remove;
  cache->to_evict = S4FIFO_verify_to_evict;
  cache->get_n_obj = S4FIFO_verify_get_n_obj;
  cache->get_occupied_byte = S4FIFO_verify_get_occupied_byte;
  cache->can_insert = S4FIFO_verify_can_insert;

  cache->obj_md_size = 0;

  cache->eviction_params = malloc(sizeof(S4FIFO_verify_params_t));
  memset(cache->eviction_params, 0, sizeof(S4FIFO_verify_params_t));
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  params->req_local = new_request();
  params->hit_on_ghost = false;

  S4FIFO_verify_parse_params(cache, DEFAULT_CACHE_PARAMS);
  if (cache_specific_params != NULL) {
    S4FIFO_verify_parse_params(cache, cache_specific_params);
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

  snprintf(cache->cache_name, CACHE_NAME_ARRAY_LEN, "S4FIFO-verify-%.4lf-%d",
           params->small_size_ratio, params->move_to_main_threshold);

  params->s_counter = 0;

  // Initialize histograms with expected queue sizes
  hit_pos_histogram_init(&params->small_hist, small_fifo_size, params->n_buckets);
  hit_pos_histogram_init(&params->main_hist, main_fifo_size, params->n_buckets);
  hit_pos_histogram_init(&params->ghost_hist, ghost_fifo_size, params->n_buckets);

  // Initialize warmup and collection state
  params->warmed_up = false;
  params->collect_counter = 0;
  params->collection_active = false;  // Will become active when cache is full

  // Open dump file if specified
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

  // Dump final histograms if enabled
  if (params->dump_hit_pos) {
    FILE *fp = params->dump_file ? params->dump_file : stdout;
    fprintf(fp, "\n# ========== FINAL HIT POSITION HISTOGRAMS ==========\n");
    hit_pos_histogram_dump(&params->small_hist, "small", fp);
    hit_pos_histogram_dump(&params->main_hist, "main", fp);
    hit_pos_histogram_dump(&params->ghost_hist, "ghost", fp);

    // Summary statistics
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

  free_request(params->req_local);
  params->small_fifo->cache_free(params->small_fifo);
  if (params->ghost_fifo != NULL) {
    params->ghost_fifo->cache_free(params->ghost_fifo);
  }
  params->main_fifo->cache_free(params->main_fifo);
  free(cache->eviction_params);
  cache_struct_free(cache);
}

static bool S4FIFO_verify_get(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  DEBUG_ASSERT(params->small_fifo->get_occupied_byte(params->small_fifo) +
                   params->main_fifo->get_occupied_byte(params->main_fifo) <=
               cache->cache_size);

  // Check if cache is warmed up (both small and main are full)
  if (!params->warmed_up) {
    int64_t total_occupied = params->small_fifo->get_occupied_byte(params->small_fifo) +
                             params->main_fifo->get_occupied_byte(params->main_fifo);
    if (total_occupied >= cache->cache_size) {
      params->warmed_up = true;
      params->collection_active = true;
      if (params->dump_hit_pos && params->dump_file != NULL) {
        fprintf(params->dump_file, "# Warmup complete, cache full, starting collection\n");
      }
    }
  }

  // Check if we should stop collecting (after collect_req requests)
  if (params->collection_active && params->collect_req > 0 &&
      params->collect_counter >= params->collect_req) {
    params->collection_active = false;
  }

  // Count requests during collection phase
  if (params->collection_active) {
    params->collect_counter++;
  }

  bool cache_hit = cache_get_base(cache, req);
  return cache_hit;
}

static cache_obj_t *S4FIFO_verify_find(cache_t *cache, const request_t *req,
                                       const bool update_cache) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;

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

  params->hit_on_ghost = false;

  // Check small FIFO
  cache_obj_t *obj = params->small_fifo->find(params->small_fifo, req, true);
  if (obj != NULL) {
    if ((int64_t)(-obj->time_stamp + params->s_counter) >=
        (int64_t)(params->small_skip_ratio * params->small_fifo->cache_size)) {
      obj->S4FIFO.freq += 1;
    }

    // Record exact hit position in small FIFO using linked list traversal
    if (params->collection_active) {
      int64_t position = find_exact_position_in_fifo(params->small_fifo, req->obj_id);
      if (position >= 0) {
        hit_pos_histogram_record(&params->small_hist, position);
      }
    }

    return obj;
  }

  // Check ghost FIFO
  if (params->ghost_fifo != NULL &&
      params->ghost_fifo->find(params->ghost_fifo, req, false) != NULL) {
    cache_obj_t *ghost_obj =
        params->ghost_fifo->find(params->ghost_fifo, req, false);
    int64_t ghost_freq = ghost_obj->S4FIFO.freq;

    // Record exact hit position in ghost queue using linked list traversal
    if (params->collection_active) {
      int64_t position = find_exact_position_in_fifo(params->ghost_fifo, req->obj_id);
      if (position >= 0) {
        hit_pos_histogram_record(&params->ghost_hist, position);
      }
    }

    if (ghost_freq >= params->ghost_to_main_threshold) {
      params->ghost_fifo->remove(params->ghost_fifo, req->obj_id);
      params->hit_on_ghost = true;
      params->hit_on_ghost_freq = ghost_freq;
    } else {
      ghost_obj->S4FIFO.freq = ghost_freq + 1;
    }
  }

  // Check main FIFO
  obj = params->main_fifo->find(params->main_fifo, req, true);
  if (obj != NULL) {
    obj->S4FIFO.freq += 1;

    // Record exact hit position in main FIFO using linked list traversal
    if (params->collection_active) {
      int64_t position = find_exact_position_in_fifo(params->main_fifo, req->obj_id);
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

static cache_obj_t *S4FIFO_verify_to_evict(cache_t *cache,
                                          const request_t *req) {
  assert(false);
  return NULL;
}

static void S4FIFO_verify_evict_small(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  cache_t *small = params->small_fifo;
  cache_t *ghost = params->ghost_fifo;
  cache_t *main = params->main_fifo;

  bool has_evicted = false;
  while (!has_evicted && small->get_occupied_byte(small) > 0) {
    cache_obj_t *obj_to_evict = small->to_evict(small, req);
    DEBUG_ASSERT(obj_to_evict != NULL);
    copy_cache_obj_to_request(params->req_local, obj_to_evict);

    if (obj_to_evict->S4FIFO.freq >= params->move_to_main_threshold) {
      cache_obj_t *main_obj = main->insert(main, params->req_local);
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

static void S4FIFO_verify_evict_main(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
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

static void S4FIFO_verify_evict(cache_t *cache, const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  params->has_evicted = true;

  cache_t *small = params->small_fifo;
  cache_t *main = params->main_fifo;

  if (main->get_occupied_byte(main) > main->cache_size ||
      small->get_occupied_byte(small) == 0) {
    S4FIFO_verify_evict_main(cache, req);
  } else {
    S4FIFO_verify_evict_small(cache, req);
  }
}

static bool S4FIFO_verify_remove(cache_t *cache, const obj_id_t obj_id) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  bool removed = false;
  removed = removed || params->small_fifo->remove(params->small_fifo, obj_id);
  removed = removed || (params->ghost_fifo &&
                        params->ghost_fifo->remove(params->ghost_fifo, obj_id));
  removed = removed || params->main_fifo->remove(params->main_fifo, obj_id);
  return removed;
}

static inline int64_t S4FIFO_verify_get_occupied_byte(const cache_t *cache) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  return params->small_fifo->get_occupied_byte(params->small_fifo) +
         params->main_fifo->get_occupied_byte(params->main_fifo);
}

static inline int64_t S4FIFO_verify_get_n_obj(const cache_t *cache) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  return params->small_fifo->get_n_obj(params->small_fifo) +
         params->main_fifo->get_n_obj(params->main_fifo);
}

static inline bool S4FIFO_verify_can_insert(cache_t *cache,
                                            const request_t *req) {
  S4FIFO_verify_params_t *params =
      (S4FIFO_verify_params_t *)cache->eviction_params;
  return req->obj_size <= params->small_fifo->cache_size &&
         cache_can_insert_default(cache, req);
}

static const char *S4FIFO_verify_current_params(S4FIFO_verify_params_t *params) {
  static __thread char params_str[512];
  snprintf(params_str, 512,
           "small-size-ratio=%.4lf,ghost-size-ratio=%.4lf,"
           "move-to-main-threshold=%d,dump-hit-pos=%s,collect-req=%ld,n-buckets=%d\n",
           params->small_size_ratio, params->ghost_size_ratio,
           params->move_to_main_threshold,
           params->dump_hit_pos ? "true" : "false",
           params->collect_req, params->n_buckets);
  return params_str;
}

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
    } else if (strcasecmp(key, "dump-hit-pos") == 0) {
      params->dump_hit_pos =
          (strcasecmp(value, "true") == 0 || atoi(value) == 1);
    } else if (strcasecmp(key, "dump-file") == 0) {
      strncpy(params->dump_file_path, value, sizeof(params->dump_file_path) - 1);
    } else if (strcasecmp(key, "collect-req") == 0) {
      params->collect_req = atol(value);
    } else if (strcasecmp(key, "n-buckets") == 0) {
      params->n_buckets = atoi(value);
    } else if (strcasecmp(key, "print") == 0) {
      printf("parameters: %s\n", S4FIFO_verify_current_params(params));
      exit(0);
    } else {
      ERROR("%s does not have parameter %s\n", cache->cache_name, key);
      exit(1);
    }
  }

  free(old_params_str);
}

// ***********************************************************************
// ****              API for dumping histograms                       ****
// ***********************************************************************

/**
 * @brief Get the exact hit position histogram for a queue
 *
 * @param cache the cache
 * @param queue_name "small", "main", or "ghost"
 * @param bins output array (caller allocated, size VERIFY_MAX_BINS)
 * @param total_hits output total hits
 * @return bin_size used
 */
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

/**
 * @brief Dump all histograms to a file
 */
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
