//
// S4FIFO Feature Collection Module (Simplified)
// One-time O(1) feature extraction for learning-based cache replacement
//
// Simplified by Haocheng at 12/06/2025
// - Removed EMA and window-based logic
// - One-time collection after warmup for specified number of requests
// - Keeps histogram and ghost hole adjustment
//

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define HIT_POS_DEFAULT_NUM_BUCKETS 20
#define HIT_POS_MAX_BUCKETS 64

// ============================================================================
// Bucketed Hit Position Tracker
// Tracks hit positions in O(1) using insertion counter difference
// ============================================================================

typedef struct {
  // Bucket configuration
  int32_t num_buckets;
  int64_t bucket_size;  // positions per bucket

  // Hit position histogram (raw counts)
  int64_t hit_counts[HIT_POS_MAX_BUCKETS];
  int64_t total_hits;

  // For ghost queue: track middle removals (holes)
  bool track_middle_removal;
  int64_t removal_counters[HIT_POS_MAX_BUCKETS];
  int64_t total_removals;
  int64_t current_bucket;
} bucketed_hit_pos_tracker_t;

/**
 * @brief Initialize the hit position tracker
 */
static inline void hit_pos_tracker_init(bucketed_hit_pos_tracker_t *tracker,
                                        int64_t expected_max_pos,
                                        int32_t num_buckets,
                                        bool track_middle_removal) {
  memset(tracker, 0, sizeof(bucketed_hit_pos_tracker_t));
  if (num_buckets <= 0) num_buckets = HIT_POS_DEFAULT_NUM_BUCKETS;
  if (num_buckets > HIT_POS_MAX_BUCKETS) num_buckets = HIT_POS_MAX_BUCKETS;
  tracker->num_buckets = num_buckets;
  tracker->bucket_size = (expected_max_pos + num_buckets - 1) / num_buckets;
  if (tracker->bucket_size < 1) tracker->bucket_size = 1;
  tracker->track_middle_removal = track_middle_removal;
}

/**
 * @brief Record an insertion and return bucket index
 * When bucket advances, clear the new bucket's removal counter (circular buffer)
 */
static inline int64_t hit_pos_tracker_record_insert(
    bucketed_hit_pos_tracker_t *tracker, int64_t insert_counter) {
  int64_t new_bucket = (insert_counter / tracker->bucket_size) % tracker->num_buckets;

  // If bucket changed, clear counters for buckets we've wrapped past
  if (tracker->track_middle_removal && new_bucket != tracker->current_bucket) {
    // Clear all buckets from current+1 to new_bucket (inclusive)
    int64_t b = (tracker->current_bucket + 1) % tracker->num_buckets;
    while (b != new_bucket) {
      tracker->removal_counters[b] = 0;
      b = (b + 1) % tracker->num_buckets;
    }
    tracker->removal_counters[new_bucket] = 0;  // Clear the new bucket too
  }

  tracker->current_bucket = new_bucket;
  return new_bucket;
}

/**
 * @brief Record a middle removal (ghost queue only)
 */
static inline void hit_pos_tracker_record_removal(
    bucketed_hit_pos_tracker_t *tracker) {
  if (!tracker->track_middle_removal) return;
  tracker->removal_counters[tracker->current_bucket]++;
  tracker->total_removals++;
}

/**
 * @brief Estimate holes between insert_bucket and current position
 */
static inline int64_t hit_pos_tracker_estimate_holes(
    bucketed_hit_pos_tracker_t *tracker, int64_t insert_bucket) {
  if (!tracker->track_middle_removal) return 0;
  int64_t holes = 0;
  int64_t bucket = insert_bucket;
  while (bucket != tracker->current_bucket) {
    holes += tracker->removal_counters[bucket];
    bucket = (bucket + 1) % tracker->num_buckets;
  }
  holes += tracker->removal_counters[tracker->current_bucket];
  return holes;
}

/**
 * @brief Record a hit and update histogram
 */
static inline void hit_pos_tracker_record_hit(
    bucketed_hit_pos_tracker_t *tracker, int64_t insert_time,
    int64_t insert_bucket, int64_t current_counter) {
  int64_t raw_position = current_counter - insert_time;
  int64_t holes = hit_pos_tracker_estimate_holes(tracker, insert_bucket);
  int64_t adjusted_position = raw_position - holes;
  if (adjusted_position < 0) adjusted_position = 0;

  int64_t pos_bucket = adjusted_position / tracker->bucket_size;
  if (pos_bucket >= tracker->num_buckets) {
    pos_bucket = tracker->num_buckets - 1;
  }
  tracker->hit_counts[pos_bucket]++;
  tracker->total_hits++;
}

/**
 * @brief Get normalized histogram
 */
static inline void hit_pos_tracker_get_histogram(
    const bucketed_hit_pos_tracker_t *tracker, double *out) {
  if (tracker->total_hits == 0) {
    memset(out, 0, tracker->num_buckets * sizeof(double));
    return;
  }
  double inv_total = 1.0 / (double)tracker->total_hits;
  for (int i = 0; i < tracker->num_buckets; i++) {
    out[i] = (double)tracker->hit_counts[i] * inv_total;
  }
}

// ============================================================================
// S4FIFO Feature Collector (Simplified)
// ============================================================================

typedef struct {
  int64_t cache_capacity;
  int32_t num_buckets;

  // Per-queue insertion counters
  int64_t small_insert_counter;
  int64_t main_insert_counter;
  int64_t ghost_insert_counter;

  // Hit position trackers
  bucketed_hit_pos_tracker_t small_tracker;
  bucketed_hit_pos_tracker_t main_tracker;
  bucketed_hit_pos_tracker_t ghost_tracker;

  // Simple hit counters (not window-based)
  int64_t total_hits_small;
  int64_t total_hits_main;
  int64_t total_hits_ghost;
  int64_t total_requests;
  int64_t total_unique;
  int64_t total_misses;      // cache misses
  int64_t onehit_count;      // objects evicted with freq < threshold (one-hit wonders)
} S4FIFO_feature_collector_t;

/**
 * @brief Initialize the feature collector
 */
static inline void feature_collector_init(S4FIFO_feature_collector_t *fc,
                                          int64_t cache_capacity,
                                          int64_t small_size,
                                          int64_t main_size,
                                          int64_t ghost_size,
                                          int32_t num_buckets) {
  memset(fc, 0, sizeof(S4FIFO_feature_collector_t));
  fc->cache_capacity = cache_capacity;
  fc->num_buckets = num_buckets > 0 ? num_buckets : HIT_POS_DEFAULT_NUM_BUCKETS;
  if (fc->num_buckets > HIT_POS_MAX_BUCKETS) fc->num_buckets = HIT_POS_MAX_BUCKETS;

  hit_pos_tracker_init(&fc->small_tracker, small_size, fc->num_buckets, false);
  hit_pos_tracker_init(&fc->main_tracker, main_size, fc->num_buckets, false);
  hit_pos_tracker_init(&fc->ghost_tracker, ghost_size, fc->num_buckets, true);
}

// ============================================================================
// Hit recording functions
// ============================================================================

static inline void feature_collector_record_hit_small(
    S4FIFO_feature_collector_t *fc, int64_t insert_time) {
  fc->total_hits_small++;
  hit_pos_tracker_record_hit(&fc->small_tracker, insert_time, 0,
                             fc->small_insert_counter);
}

static inline void feature_collector_record_hit_main(
    S4FIFO_feature_collector_t *fc, int64_t insert_time) {
  fc->total_hits_main++;
  hit_pos_tracker_record_hit(&fc->main_tracker, insert_time, 0,
                             fc->main_insert_counter);
}

static inline void feature_collector_record_hit_ghost(
    S4FIFO_feature_collector_t *fc, int64_t insert_time, int64_t insert_bucket) {
  fc->total_hits_ghost++;
  hit_pos_tracker_record_hit(&fc->ghost_tracker, insert_time, insert_bucket,
                             fc->ghost_insert_counter);
}

static inline int64_t feature_collector_record_insert_small(
    S4FIFO_feature_collector_t *fc) {
  int64_t bucket = hit_pos_tracker_record_insert(&fc->small_tracker,
                                                  fc->small_insert_counter);
  fc->small_insert_counter++;
  return bucket;
}

static inline int64_t feature_collector_record_insert_main(
    S4FIFO_feature_collector_t *fc) {
  int64_t bucket = hit_pos_tracker_record_insert(&fc->main_tracker,
                                                  fc->main_insert_counter);
  fc->main_insert_counter++;
  return bucket;
}

static inline int64_t feature_collector_record_insert_ghost(
    S4FIFO_feature_collector_t *fc) {
  int64_t bucket = hit_pos_tracker_record_insert(&fc->ghost_tracker,
                                                  fc->ghost_insert_counter);
  fc->ghost_insert_counter++;
  return bucket;
}

static inline void feature_collector_record_ghost_removal(
    S4FIFO_feature_collector_t *fc) {
  hit_pos_tracker_record_removal(&fc->ghost_tracker);
}

static inline void feature_collector_record_unique(
    S4FIFO_feature_collector_t *fc) {
  fc->total_unique++;
}

static inline void feature_collector_record_request(
    S4FIFO_feature_collector_t *fc, int64_t current_req) {
  (void)current_req;  // unused in simplified version
  fc->total_requests++;
}

static inline void feature_collector_record_repeat(
    S4FIFO_feature_collector_t *fc) {
  (void)fc;  // no-op in simplified version
}

static inline void feature_collector_record_miss(
    S4FIFO_feature_collector_t *fc) {
  fc->total_misses++;
}

static inline void feature_collector_record_onehit(
    S4FIFO_feature_collector_t *fc) {
  fc->onehit_count++;
}

// ============================================================================
// Feature Vector (Simplified)
// ============================================================================

typedef struct {
  int32_t num_buckets;
  double log_cache_capacity;

  // Hit ratios by queue (spatial distribution)
  double hit_ratio_small;   // H_s: fraction of hits in Small FIFO
  double hit_ratio_main;    // H_m: fraction of hits in Main FIFO
  double hit_ratio_ghost;   // H_g: ghost hit ratio (frequency signal)

  // Workload proxies
  double unique_ratio;      // ρ_unique: unique_objs / total_requests
  double onehit_ratio;      // ρ_onehit: one-hit wonders / total_unique

  // Absolute values
  int64_t total_requests;
  int64_t total_hits;
  int64_t total_misses;
  int64_t hits_small;
  int64_t hits_main;
  int64_t hits_ghost;

  // Hit position histograms
  double hist_small[HIT_POS_MAX_BUCKETS];
  double hist_main[HIT_POS_MAX_BUCKETS];
  double hist_ghost[HIT_POS_MAX_BUCKETS];
} S4FIFO_feature_vector_t;

/**
 * @brief Extract feature vector from collector
 */
static inline void feature_collector_get_features(
    S4FIFO_feature_collector_t *fc, S4FIFO_feature_vector_t *fv,
    int64_t small_size, int64_t main_size, int64_t ghost_size) {
  (void)small_size; (void)main_size; (void)ghost_size;

  fv->num_buckets = fc->num_buckets;
  fv->log_cache_capacity = fc->cache_capacity > 0 ?
      log10((double)fc->cache_capacity) : 0.0;

  // Absolute values
  fv->total_requests = fc->total_requests;
  fv->hits_small = fc->total_hits_small;
  fv->hits_main = fc->total_hits_main;
  fv->hits_ghost = fc->total_hits_ghost;
  fv->total_hits = fc->total_hits_small + fc->total_hits_main + fc->total_hits_ghost;
  fv->total_misses = fc->total_misses;

  // Hit ratios by queue
  if (fv->total_hits > 0) {
    fv->hit_ratio_small = (double)fc->total_hits_small / fv->total_hits;
    fv->hit_ratio_main = (double)fc->total_hits_main / fv->total_hits;
    fv->hit_ratio_ghost = (double)fc->total_hits_ghost / fv->total_hits;
  } else {
    fv->hit_ratio_small = 0.0;
    fv->hit_ratio_main = 0.0;
    fv->hit_ratio_ghost = 0.0;
  }

  // Workload proxies
  fv->unique_ratio = fc->total_requests > 0 ?
      (double)fc->total_unique / fc->total_requests : 0.0;
  fv->onehit_ratio = fc->total_unique > 0 ?
      (double)fc->onehit_count / fc->total_unique : 0.0;

  // Histograms
  hit_pos_tracker_get_histogram(&fc->small_tracker, fv->hist_small);
  hit_pos_tracker_get_histogram(&fc->main_tracker, fv->hist_main);
  hit_pos_tracker_get_histogram(&fc->ghost_tracker, fv->hist_ghost);
}

/**
 * @brief Print feature vector
 */
static inline void feature_vector_print(const S4FIFO_feature_vector_t *fv,
                                        FILE *fp) {
  // Physical context and hit ratios
  fprintf(fp, "log_C=%.2f H_s=%.3f H_m=%.3f H_g=%.3f\n",
          fv->log_cache_capacity, fv->hit_ratio_small,
          fv->hit_ratio_main, fv->hit_ratio_ghost);

  // Workload proxies
  fprintf(fp, "rho_unique=%.4f rho_onehit=%.4f\n",
          fv->unique_ratio, fv->onehit_ratio);

  // Absolute values
  fprintf(fp, "total_reqs=%ld total_hits=%ld total_misses=%ld\n",
          fv->total_requests, fv->total_hits, fv->total_misses);
  fprintf(fp, "hits_small=%ld hits_main=%ld hits_ghost=%ld\n",
          fv->hits_small, fv->hits_main, fv->hits_ghost);

  // Histograms
  fprintf(fp, "hist_small[%d]: ", fv->num_buckets);
  for (int i = 0; i < fv->num_buckets; i++) {
    fprintf(fp, "%.3f ", fv->hist_small[i]);
  }
  fprintf(fp, "\n");

  fprintf(fp, "hist_main[%d]:  ", fv->num_buckets);
  for (int i = 0; i < fv->num_buckets; i++) {
    fprintf(fp, "%.3f ", fv->hist_main[i]);
  }
  fprintf(fp, "\n");

  fprintf(fp, "hist_ghost[%d]: ", fv->num_buckets);
  for (int i = 0; i < fv->num_buckets; i++) {
    fprintf(fp, "%.3f ", fv->hist_ghost[i]);
  }
  fprintf(fp, "\n");
}

#ifdef __cplusplus
}
#endif
