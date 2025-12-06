//
// S4FIFO Feature Collection Module
// Lightweight O(1) feature extraction for learning-based cache replacement
//
// Created by Haocheng at 12/06/2025
//
// Key Design:
// - Bucketed Hit Position Tracking for all queues (small, main, ghost)
// - Ghost queue has special middle-removal adjustment
// - All operations are O(1)
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

// Default number of buckets for position tracking (20 supports 5% precision)
// Can be overridden at init time
#define HIT_POS_DEFAULT_NUM_BUCKETS 20

// Maximum number of buckets (for static array allocation)
#define HIT_POS_MAX_BUCKETS 64

// Feature collection window size (in requests)
#define FEATURE_WINDOW_SIZE 10000

// ============================================================================
// Bucketed Hit Position Tracker
// Tracks hit positions in O(1) using time-based bucketing
//
// For FIFO queues: position = (current_time - insert_time)
// For ghost queue: position = (current_time - insert_time) - holes
// ============================================================================

typedef struct {
  // Circular buffer of counters per time bucket
  int64_t insert_counters[HIT_POS_MAX_BUCKETS];   // insertions per bucket
  int64_t removal_counters[HIT_POS_MAX_BUCKETS];  // removals per bucket (for
                                                  // ghost middle-removal)

  // Bucket configuration
  int32_t num_buckets;       // actual number of buckets used
  int64_t bucket_size;       // time units (requests) per bucket
  int64_t current_bucket;    // current active bucket index
  int64_t bucket_start_time; // time when current bucket started

  // Statistics accumulators
  int64_t total_hits;             // total hits recorded
  int64_t total_position_sum;     // sum of all hit positions (for mean)
  int64_t hit_pos_histogram[HIT_POS_MAX_BUCKETS];  // histogram of hit positions

  // For ghost queue: track middle removals
  bool track_middle_removal;  // true for ghost queue
  int64_t total_removals;     // total middle removals (holes)
} bucketed_hit_pos_tracker_t;

/**
 * @brief Initialize the hit position tracker
 *
 * @param tracker pointer to tracker
 * @param window_size total tracking window size
 * @param num_buckets number of buckets (0 = use default)
 * @param track_middle_removal true for ghost queue (has holes)
 */
static inline void hit_pos_tracker_init(bucketed_hit_pos_tracker_t *tracker,
                                        int64_t window_size, int32_t num_buckets,
                                        bool track_middle_removal) {
  memset(tracker, 0, sizeof(bucketed_hit_pos_tracker_t));

  // Set number of buckets with bounds checking
  if (num_buckets <= 0) {
    tracker->num_buckets = HIT_POS_DEFAULT_NUM_BUCKETS;
  } else if (num_buckets > HIT_POS_MAX_BUCKETS) {
    tracker->num_buckets = HIT_POS_MAX_BUCKETS;
  } else {
    tracker->num_buckets = num_buckets;
  }

  tracker->bucket_size =
      (window_size + tracker->num_buckets - 1) / tracker->num_buckets;
  if (tracker->bucket_size < 1) tracker->bucket_size = 1;
  tracker->track_middle_removal = track_middle_removal;
}

/**
 * @brief Advance time and handle bucket rotation
 */
static inline void hit_pos_tracker_advance(bucketed_hit_pos_tracker_t *tracker,
                                           int64_t current_time) {
  while (current_time >= tracker->bucket_start_time + tracker->bucket_size) {
    tracker->bucket_start_time += tracker->bucket_size;
    tracker->current_bucket =
        (tracker->current_bucket + 1) % tracker->num_buckets;

    // Clear the new bucket (circular reuse)
    tracker->insert_counters[tracker->current_bucket] = 0;
    tracker->removal_counters[tracker->current_bucket] = 0;
  }
}

/**
 * @brief Record an insertion (object enters the queue)
 *
 * @param tracker pointer to tracker
 * @param current_time current request number
 * @return bucket index for this insertion (store with object)
 */
static inline int64_t
hit_pos_tracker_record_insert(bucketed_hit_pos_tracker_t *tracker,
                              int64_t current_time) {
  hit_pos_tracker_advance(tracker, current_time);
  tracker->insert_counters[tracker->current_bucket]++;
  return tracker->current_bucket;
}

/**
 * @brief Record a middle removal (object removed before tail eviction)
 * Only used for ghost queue when object is promoted back to main
 *
 * @param tracker pointer to tracker
 * @param current_time current request number
 */
static inline void
hit_pos_tracker_record_removal(bucketed_hit_pos_tracker_t *tracker,
                               int64_t current_time) {
  if (!tracker->track_middle_removal) return;
  hit_pos_tracker_advance(tracker, current_time);
  tracker->removal_counters[tracker->current_bucket]++;
  tracker->total_removals++;
}

/**
 * @brief Estimate number of holes (middle removals) in a time interval
 * Used for ghost queue to adjust position estimate
 *
 * @param tracker pointer to tracker
 * @param insert_bucket bucket index when object was inserted
 * @param current_time current request number
 * @return estimated number of holes
 */
static inline int64_t
hit_pos_tracker_estimate_holes(bucketed_hit_pos_tracker_t *tracker,
                               int64_t insert_bucket, int64_t current_time) {
  if (!tracker->track_middle_removal) return 0;

  hit_pos_tracker_advance(tracker, current_time);
  int64_t holes = 0;

  // Sum removal counters from insert_bucket to current_bucket
  int64_t bucket = insert_bucket;
  while (bucket != tracker->current_bucket) {
    holes += tracker->removal_counters[bucket];
    bucket = (bucket + 1) % tracker->num_buckets;
  }
  // Add partial current bucket (proportional estimate)
  // For simplicity, add full current bucket
  holes += tracker->removal_counters[tracker->current_bucket];

  return holes;
}

/**
 * @brief Record a hit and compute position
 *
 * For FIFO queues: position ≈ objects_between_insert_and_now
 * For ghost queue: position ≈ objects_between - holes (middle removals)
 *
 * @param tracker pointer to tracker
 * @param insert_time time when object was inserted
 * @param insert_bucket bucket when object was inserted (for ghost)
 * @param current_time current request number
 * @return estimated position in queue
 */
static inline int64_t hit_pos_tracker_record_hit(
    bucketed_hit_pos_tracker_t *tracker, int64_t insert_time,
    int64_t insert_bucket, int64_t current_time) {
  hit_pos_tracker_advance(tracker, current_time);

  // Raw position estimate: time difference
  int64_t raw_position = current_time - insert_time;

  // For ghost queue: adjust for holes
  int64_t holes = 0;
  if (tracker->track_middle_removal) {
    holes = hit_pos_tracker_estimate_holes(tracker, insert_bucket, current_time);
  }

  int64_t adjusted_position = raw_position - holes;
  if (adjusted_position < 0) adjusted_position = 0;

  // Update statistics
  tracker->total_hits++;
  tracker->total_position_sum += adjusted_position;

  // Update histogram (bucket by position)
  int64_t pos_bucket = adjusted_position / tracker->bucket_size;
  if (pos_bucket >= tracker->num_buckets) {
    pos_bucket = tracker->num_buckets - 1;
  }
  tracker->hit_pos_histogram[pos_bucket]++;

  return adjusted_position;
}

/**
 * @brief Get hit position histogram as normalized distribution
 * This gives the hit ratio distribution across position buckets
 *
 * @param tracker pointer to tracker
 * @param out output array (must have at least num_buckets elements)
 * @return number of buckets written
 */
static inline int hit_pos_tracker_get_histogram(
    const bucketed_hit_pos_tracker_t *tracker, double *out) {
  if (tracker->total_hits == 0) {
    for (int i = 0; i < tracker->num_buckets; i++) {
      out[i] = 0.0;
    }
  } else {
    double inv_total = 1.0 / (double)tracker->total_hits;
    for (int i = 0; i < tracker->num_buckets; i++) {
      out[i] = (double)tracker->hit_pos_histogram[i] * inv_total;
    }
  }
  return tracker->num_buckets;
}

/**
 * @brief Get mean hit position
 */
static inline double
hit_pos_tracker_get_mean(const bucketed_hit_pos_tracker_t *tracker) {
  if (tracker->total_hits == 0) return 0.0;
  return (double)tracker->total_position_sum / (double)tracker->total_hits;
}

/**
 * @brief Distribution statistics for richer feature representation
 */
typedef struct {
  double mean;       // 均值
  double std;        // 标准差
  double p25;        // 25th percentile
  double p50;        // 50th percentile (median)
  double p75;        // 75th percentile
  double skewness;   // 偏度: >0 右偏(多数靠前), <0 左偏(多数靠后)
  double entropy;    // 熵: 高=均匀分布, 低=集中分布
  double head_ratio; // 前1/4区域的命中比例
  double tail_ratio; // 后1/4区域的命中比例
} hit_pos_stats_t;

/**
 * @brief Compute comprehensive statistics from histogram
 * More informative than just mean
 */
static inline void hit_pos_tracker_get_stats(
    const bucketed_hit_pos_tracker_t *tracker, hit_pos_stats_t *stats) {
  memset(stats, 0, sizeof(hit_pos_stats_t));

  if (tracker->total_hits == 0) return;

  int n = tracker->num_buckets;
  double total = (double)tracker->total_hits;
  double inv_total = 1.0 / total;

  // Compute normalized histogram and cumulative distribution
  double hist[HIT_POS_MAX_BUCKETS];
  double cdf[HIT_POS_MAX_BUCKETS];
  double cumsum = 0.0;

  for (int i = 0; i < n; i++) {
    hist[i] = (double)tracker->hit_pos_histogram[i] * inv_total;
    cumsum += hist[i];
    cdf[i] = cumsum;
  }

  // Mean (bucket index, normalized to [0,1])
  double mean = 0.0;
  for (int i = 0; i < n; i++) {
    mean += hist[i] * ((double)i + 0.5) / n;
  }
  stats->mean = mean;

  // Standard deviation
  double var = 0.0;
  for (int i = 0; i < n; i++) {
    double pos = ((double)i + 0.5) / n;
    var += hist[i] * (pos - mean) * (pos - mean);
  }
  stats->std = sqrt(var);

  // Percentiles (from CDF)
  stats->p25 = 0.0;
  stats->p50 = 0.0;
  stats->p75 = 0.0;
  for (int i = 0; i < n; i++) {
    double pos = ((double)i + 1.0) / n;
    if (stats->p25 == 0.0 && cdf[i] >= 0.25) stats->p25 = pos;
    if (stats->p50 == 0.0 && cdf[i] >= 0.50) stats->p50 = pos;
    if (stats->p75 == 0.0 && cdf[i] >= 0.75) stats->p75 = pos;
  }

  // Skewness (third moment)
  if (stats->std > 1e-10) {
    double skew = 0.0;
    for (int i = 0; i < n; i++) {
      double pos = ((double)i + 0.5) / n;
      double z = (pos - mean) / stats->std;
      skew += hist[i] * z * z * z;
    }
    stats->skewness = skew;
  }

  // Entropy (information content)
  double entropy = 0.0;
  for (int i = 0; i < n; i++) {
    if (hist[i] > 1e-10) {
      entropy -= hist[i] * log2(hist[i]);
    }
  }
  // Normalize by max entropy (uniform distribution)
  stats->entropy = entropy / log2((double)n);

  // Head/tail ratios (first and last quarter)
  int quarter = n / 4;
  if (quarter < 1) quarter = 1;

  stats->head_ratio = 0.0;
  for (int i = 0; i < quarter; i++) {
    stats->head_ratio += hist[i];
  }

  stats->tail_ratio = 0.0;
  for (int i = n - quarter; i < n; i++) {
    stats->tail_ratio += hist[i];
  }
}

/**
 * @brief Reset statistics (keep bucket structure)
 */
static inline void
hit_pos_tracker_reset_stats(bucketed_hit_pos_tracker_t *tracker) {
  tracker->total_hits = 0;
  tracker->total_position_sum = 0;
  memset(tracker->hit_pos_histogram, 0, sizeof(tracker->hit_pos_histogram));
}

// ============================================================================
// S4FIFO Feature Collector
// Combines multiple hit position trackers for comprehensive features
// ============================================================================

typedef struct {
  // Physical context
  int64_t cache_capacity;

  // Configuration
  int32_t num_buckets;  // number of buckets for hit position histogram

  // Hit position trackers for each queue
  bucketed_hit_pos_tracker_t small_tracker;  // small FIFO (no middle removal)
  bucketed_hit_pos_tracker_t main_tracker;   // main FIFO (no middle removal)
  bucketed_hit_pos_tracker_t ghost_tracker;  // ghost queue (with middle
                                             // removal)

  // Window-based workload statistics
  int64_t window_size;
  int64_t window_start_req;
  int64_t window_total_reqs;
  int64_t window_unique_objs;
  int64_t window_onehit_count;

  // Hit counters by region
  int64_t window_hits_small;
  int64_t window_hits_main;
  int64_t window_hits_ghost;
  int64_t window_total_hits;
  int64_t window_misses;

  // EMA for smooth features
  double ema_alpha;
  double ema_unique_ratio;
  double ema_onehit_ratio;
  double ema_hit_small;
  double ema_hit_main;
  double ema_hit_ghost;

  // EMA for hit position statistics (richer than just mean)
  hit_pos_stats_t ema_stats_small;
  hit_pos_stats_t ema_stats_main;
  hit_pos_stats_t ema_stats_ghost;

  // EMA for hit position histograms (vectors)
  double ema_hist_small[HIT_POS_MAX_BUCKETS];
  double ema_hist_main[HIT_POS_MAX_BUCKETS];
  double ema_hist_ghost[HIT_POS_MAX_BUCKETS];
} S4FIFO_feature_collector_t;

/**
 * @brief Initialize the feature collector
 *
 * @param fc pointer to feature collector
 * @param cache_capacity cache capacity in bytes
 * @param window_size window size in requests (0 = use default)
 * @param num_buckets number of buckets for histograms (0 = use default)
 */
static inline void feature_collector_init(S4FIFO_feature_collector_t *fc,
                                          int64_t cache_capacity,
                                          int64_t window_size,
                                          int32_t num_buckets) {
  memset(fc, 0, sizeof(S4FIFO_feature_collector_t));
  fc->cache_capacity = cache_capacity;
  fc->window_size = window_size > 0 ? window_size : FEATURE_WINDOW_SIZE;
  fc->num_buckets = num_buckets > 0 ? num_buckets : HIT_POS_DEFAULT_NUM_BUCKETS;
  if (fc->num_buckets > HIT_POS_MAX_BUCKETS) {
    fc->num_buckets = HIT_POS_MAX_BUCKETS;
  }
  fc->ema_alpha = 0.1;

  // Initialize trackers: ghost has middle removal, others don't
  hit_pos_tracker_init(&fc->small_tracker, fc->window_size, fc->num_buckets, false);
  hit_pos_tracker_init(&fc->main_tracker, fc->window_size, fc->num_buckets, false);
  hit_pos_tracker_init(&fc->ghost_tracker, fc->window_size, fc->num_buckets, true);
}

/**
 * @brief Update EMAs and reset window counters
 */
static inline void feature_collector_update_emas(S4FIFO_feature_collector_t *fc) {
  double alpha = fc->ema_alpha;
  double one_minus_alpha = 1.0 - alpha;

  if (fc->window_total_reqs > 0) {
    double unique_ratio =
        (double)fc->window_unique_objs / fc->window_total_reqs;
    double onehit_ratio =
        fc->window_unique_objs > 0
            ? (double)fc->window_onehit_count / fc->window_unique_objs
            : 0.0;

    fc->ema_unique_ratio = alpha * unique_ratio + one_minus_alpha * fc->ema_unique_ratio;
    fc->ema_onehit_ratio = alpha * onehit_ratio + one_minus_alpha * fc->ema_onehit_ratio;

    if (fc->window_total_hits > 0) {
      fc->ema_hit_small =
          alpha * ((double)fc->window_hits_small / fc->window_total_hits) +
          one_minus_alpha * fc->ema_hit_small;
      fc->ema_hit_main =
          alpha * ((double)fc->window_hits_main / fc->window_total_hits) +
          one_minus_alpha * fc->ema_hit_main;
      fc->ema_hit_ghost =
          alpha * ((double)fc->window_hits_ghost / fc->window_total_hits) +
          one_minus_alpha * fc->ema_hit_ghost;
    }

    // Update hit position statistics EMAs (richer than mean)
    hit_pos_stats_t stats_temp;

    hit_pos_tracker_get_stats(&fc->small_tracker, &stats_temp);
    fc->ema_stats_small.mean = alpha * stats_temp.mean + one_minus_alpha * fc->ema_stats_small.mean;
    fc->ema_stats_small.std = alpha * stats_temp.std + one_minus_alpha * fc->ema_stats_small.std;
    fc->ema_stats_small.p25 = alpha * stats_temp.p25 + one_minus_alpha * fc->ema_stats_small.p25;
    fc->ema_stats_small.p50 = alpha * stats_temp.p50 + one_minus_alpha * fc->ema_stats_small.p50;
    fc->ema_stats_small.p75 = alpha * stats_temp.p75 + one_minus_alpha * fc->ema_stats_small.p75;
    fc->ema_stats_small.skewness = alpha * stats_temp.skewness + one_minus_alpha * fc->ema_stats_small.skewness;
    fc->ema_stats_small.entropy = alpha * stats_temp.entropy + one_minus_alpha * fc->ema_stats_small.entropy;
    fc->ema_stats_small.head_ratio = alpha * stats_temp.head_ratio + one_minus_alpha * fc->ema_stats_small.head_ratio;
    fc->ema_stats_small.tail_ratio = alpha * stats_temp.tail_ratio + one_minus_alpha * fc->ema_stats_small.tail_ratio;

    hit_pos_tracker_get_stats(&fc->main_tracker, &stats_temp);
    fc->ema_stats_main.mean = alpha * stats_temp.mean + one_minus_alpha * fc->ema_stats_main.mean;
    fc->ema_stats_main.std = alpha * stats_temp.std + one_minus_alpha * fc->ema_stats_main.std;
    fc->ema_stats_main.p25 = alpha * stats_temp.p25 + one_minus_alpha * fc->ema_stats_main.p25;
    fc->ema_stats_main.p50 = alpha * stats_temp.p50 + one_minus_alpha * fc->ema_stats_main.p50;
    fc->ema_stats_main.p75 = alpha * stats_temp.p75 + one_minus_alpha * fc->ema_stats_main.p75;
    fc->ema_stats_main.skewness = alpha * stats_temp.skewness + one_minus_alpha * fc->ema_stats_main.skewness;
    fc->ema_stats_main.entropy = alpha * stats_temp.entropy + one_minus_alpha * fc->ema_stats_main.entropy;
    fc->ema_stats_main.head_ratio = alpha * stats_temp.head_ratio + one_minus_alpha * fc->ema_stats_main.head_ratio;
    fc->ema_stats_main.tail_ratio = alpha * stats_temp.tail_ratio + one_minus_alpha * fc->ema_stats_main.tail_ratio;

    hit_pos_tracker_get_stats(&fc->ghost_tracker, &stats_temp);
    fc->ema_stats_ghost.mean = alpha * stats_temp.mean + one_minus_alpha * fc->ema_stats_ghost.mean;
    fc->ema_stats_ghost.std = alpha * stats_temp.std + one_minus_alpha * fc->ema_stats_ghost.std;
    fc->ema_stats_ghost.p25 = alpha * stats_temp.p25 + one_minus_alpha * fc->ema_stats_ghost.p25;
    fc->ema_stats_ghost.p50 = alpha * stats_temp.p50 + one_minus_alpha * fc->ema_stats_ghost.p50;
    fc->ema_stats_ghost.p75 = alpha * stats_temp.p75 + one_minus_alpha * fc->ema_stats_ghost.p75;
    fc->ema_stats_ghost.skewness = alpha * stats_temp.skewness + one_minus_alpha * fc->ema_stats_ghost.skewness;
    fc->ema_stats_ghost.entropy = alpha * stats_temp.entropy + one_minus_alpha * fc->ema_stats_ghost.entropy;
    fc->ema_stats_ghost.head_ratio = alpha * stats_temp.head_ratio + one_minus_alpha * fc->ema_stats_ghost.head_ratio;
    fc->ema_stats_ghost.tail_ratio = alpha * stats_temp.tail_ratio + one_minus_alpha * fc->ema_stats_ghost.tail_ratio;

    // Update histogram EMAs (hit position distributions)
    double hist_temp[HIT_POS_MAX_BUCKETS];
    int n = fc->num_buckets;

    hit_pos_tracker_get_histogram(&fc->small_tracker, hist_temp);
    for (int i = 0; i < n; i++) {
      fc->ema_hist_small[i] = alpha * hist_temp[i] + one_minus_alpha * fc->ema_hist_small[i];
    }

    hit_pos_tracker_get_histogram(&fc->main_tracker, hist_temp);
    for (int i = 0; i < n; i++) {
      fc->ema_hist_main[i] = alpha * hist_temp[i] + one_minus_alpha * fc->ema_hist_main[i];
    }

    hit_pos_tracker_get_histogram(&fc->ghost_tracker, hist_temp);
    for (int i = 0; i < n; i++) {
      fc->ema_hist_ghost[i] = alpha * hist_temp[i] + one_minus_alpha * fc->ema_hist_ghost[i];
    }
  }
}

/**
 * @brief Reset window counters
 */
static inline void
feature_collector_reset_window(S4FIFO_feature_collector_t *fc) {
  feature_collector_update_emas(fc);

  fc->window_total_reqs = 0;
  fc->window_unique_objs = 0;
  fc->window_onehit_count = 0;
  fc->window_hits_small = 0;
  fc->window_hits_main = 0;
  fc->window_hits_ghost = 0;
  fc->window_total_hits = 0;
  fc->window_misses = 0;

  hit_pos_tracker_reset_stats(&fc->small_tracker);
  hit_pos_tracker_reset_stats(&fc->main_tracker);
  hit_pos_tracker_reset_stats(&fc->ghost_tracker);
}

/**
 * @brief Check and handle window boundary
 */
static inline void feature_collector_check_window(
    S4FIFO_feature_collector_t *fc, int64_t current_req) {
  if (current_req >= fc->window_start_req + fc->window_size) {
    feature_collector_reset_window(fc);
    fc->window_start_req = current_req;
  }
}

/**
 * @brief Record request (called on every request)
 */
static inline void feature_collector_record_request(
    S4FIFO_feature_collector_t *fc, int64_t current_req) {
  feature_collector_check_window(fc, current_req);
  fc->window_total_reqs++;
}

/**
 * @brief Record a first access (new unique object in window)
 */
static inline void
feature_collector_record_unique(S4FIFO_feature_collector_t *fc) {
  fc->window_unique_objs++;
  fc->window_onehit_count++;
}

/**
 * @brief Record a repeat access (object accessed again)
 */
static inline void
feature_collector_record_repeat(S4FIFO_feature_collector_t *fc) {
  if (fc->window_onehit_count > 0) {
    fc->window_onehit_count--;
  }
}

/**
 * @brief Record cache miss
 */
static inline void
feature_collector_record_miss(S4FIFO_feature_collector_t *fc) {
  fc->window_misses++;
}

// ============================================================================
// Hit recording functions - return position for external use
// ============================================================================

/**
 * @brief Record hit in small FIFO
 * @return estimated position
 */
static inline int64_t feature_collector_record_hit_small(
    S4FIFO_feature_collector_t *fc, int64_t insert_time, int64_t current_time) {
  fc->window_hits_small++;
  fc->window_total_hits++;
  // Small FIFO doesn't need bucket tracking for holes
  return hit_pos_tracker_record_hit(&fc->small_tracker, insert_time, 0,
                                    current_time);
}

/**
 * @brief Record hit in main FIFO
 * @return estimated position
 */
static inline int64_t feature_collector_record_hit_main(
    S4FIFO_feature_collector_t *fc, int64_t insert_time, int64_t current_time) {
  fc->window_hits_main++;
  fc->window_total_hits++;
  return hit_pos_tracker_record_hit(&fc->main_tracker, insert_time, 0,
                                    current_time);
}

/**
 * @brief Record hit in ghost queue (with hole adjustment)
 * @param insert_bucket bucket index when inserted to ghost
 * @return estimated position (adjusted for holes)
 */
static inline int64_t feature_collector_record_hit_ghost(
    S4FIFO_feature_collector_t *fc, int64_t insert_time, int64_t insert_bucket,
    int64_t current_time) {
  fc->window_hits_ghost++;
  fc->window_total_hits++;
  return hit_pos_tracker_record_hit(&fc->ghost_tracker, insert_time,
                                    insert_bucket, current_time);
}

/**
 * @brief Record insertion to small FIFO
 * @return bucket index (store with object for later hit tracking)
 */
static inline int64_t
feature_collector_record_insert_small(S4FIFO_feature_collector_t *fc,
                                      int64_t current_time) {
  return hit_pos_tracker_record_insert(&fc->small_tracker, current_time);
}

/**
 * @brief Record insertion to main FIFO
 */
static inline int64_t
feature_collector_record_insert_main(S4FIFO_feature_collector_t *fc,
                                     int64_t current_time) {
  return hit_pos_tracker_record_insert(&fc->main_tracker, current_time);
}

/**
 * @brief Record insertion to ghost queue
 * @return bucket index (MUST store with object for hole adjustment)
 */
static inline int64_t
feature_collector_record_insert_ghost(S4FIFO_feature_collector_t *fc,
                                      int64_t current_time) {
  return hit_pos_tracker_record_insert(&fc->ghost_tracker, current_time);
}

/**
 * @brief Record middle removal from ghost queue (object promoted to main)
 * This creates a "hole" that affects position estimates
 */
static inline void
feature_collector_record_ghost_removal(S4FIFO_feature_collector_t *fc,
                                       int64_t current_time) {
  hit_pos_tracker_record_removal(&fc->ghost_tracker, current_time);
}

// ============================================================================
// Feature Vector
// ============================================================================

typedef struct {
  // Number of buckets used in histograms
  int32_t num_buckets;

  // Physical context
  double log_cache_capacity;  // log10(C)

  // Workload proxies
  double unique_ratio;  // ρ_unique
  double onehit_ratio;  // ρ_onehit

  // Spatial distribution (scalar hit ratios - sum = 1)
  double hit_ratio_small;  // H_S
  double hit_ratio_main;   // H_M
  double hit_ratio_ghost;  // H_G

  // Partition sizes (commented out - fixed during feature collection)
  // double small_size_ratio;
  // double main_size_ratio;
  // double ghost_size_ratio;

  // Rich distribution statistics for each queue (normalized)
  hit_pos_dist_stats_t stats_small;  // distribution stats for small FIFO
  hit_pos_dist_stats_t stats_main;   // distribution stats for main FIFO
  hit_pos_dist_stats_t stats_ghost;  // distribution stats for ghost (hole-adjusted)

  // Hit position distributions (vectors, each sums to 1)
  // These show WHERE in each queue the hits occur
  double hist_small[HIT_POS_MAX_BUCKETS];  // hit position dist in small
  double hist_main[HIT_POS_MAX_BUCKETS];   // hit position dist in main
  double hist_ghost[HIT_POS_MAX_BUCKETS];  // hit position dist in ghost
} S4FIFO_feature_vector_t;

/**
 * @brief Extract current feature vector (scalars + histograms)
 */
static inline void feature_collector_get_features(
    S4FIFO_feature_collector_t *fc, S4FIFO_feature_vector_t *fv,
    int64_t small_size, int64_t main_size, int64_t ghost_size) {
  fv->num_buckets = fc->num_buckets;

  // Physical context
  fv->log_cache_capacity =
      fc->cache_capacity > 0 ? log10((double)fc->cache_capacity) : 0.0;

  // EMA-smoothed scalar features
  fv->unique_ratio = fc->ema_unique_ratio;
  fv->onehit_ratio = fc->ema_onehit_ratio;
  fv->hit_ratio_small = fc->ema_hit_small;
  fv->hit_ratio_main = fc->ema_hit_main;
  fv->hit_ratio_ghost = fc->ema_hit_ghost;

  // Partition sizes (commented out - fixed during feature collection)
  // double capacity = (double)fc->cache_capacity;
  // if (capacity > 0) {
  //   fv->small_size_ratio = (double)small_size / capacity;
  //   fv->main_size_ratio = (double)main_size / capacity;
  //   fv->ghost_size_ratio = (double)ghost_size / capacity;
  // } else {
  //   fv->small_size_ratio = 0.0;
  //   fv->main_size_ratio = 0.0;
  //   fv->ghost_size_ratio = 0.0;
  // }
  (void)small_size; (void)main_size; (void)ghost_size;  // suppress unused warnings

  // Copy EMA-smoothed distribution statistics (already normalized)
  fv->stats_small = fc->ema_stats_small;
  fv->stats_main = fc->ema_stats_main;
  fv->stats_ghost = fc->ema_stats_ghost;

  // Copy EMA-smoothed histograms
  memcpy(fv->hist_small, fc->ema_hist_small,
         fc->num_buckets * sizeof(double));
  memcpy(fv->hist_main, fc->ema_hist_main,
         fc->num_buckets * sizeof(double));
  memcpy(fv->hist_ghost, fc->ema_hist_ghost,
         fc->num_buckets * sizeof(double));
}

/**
 * @brief Get number of scalar features
 * Layout: 6 base scalars + 3 queues * 9 stats each = 33 total
 * (size ratios are commented out as they are fixed during collection)
 */
#define FEATURE_VECTOR_NUM_SCALARS 33

/**
 * @brief Get scalar features as flat array (without histograms)
 * @return number of scalar features (36)
 */
static inline int feature_vector_scalars_to_array(
    const S4FIFO_feature_vector_t *fv, double *out) {
  int idx = 0;

  // Base scalars (6, size ratios commented out)
  out[idx++] = fv->log_cache_capacity;
  out[idx++] = fv->unique_ratio;
  out[idx++] = fv->onehit_ratio;
  out[idx++] = fv->hit_ratio_small;
  out[idx++] = fv->hit_ratio_main;
  out[idx++] = fv->hit_ratio_ghost;
  // out[idx++] = fv->small_size_ratio;  // fixed during collection
  // out[idx++] = fv->main_size_ratio;   // fixed during collection
  // out[idx++] = fv->ghost_size_ratio;  // fixed during collection

  // Stats for small queue (9)
  out[idx++] = fv->stats_small.mean;
  out[idx++] = fv->stats_small.std;
  out[idx++] = fv->stats_small.skewness;
  out[idx++] = fv->stats_small.entropy;
  out[idx++] = fv->stats_small.p25;
  out[idx++] = fv->stats_small.p50;
  out[idx++] = fv->stats_small.p75;
  out[idx++] = fv->stats_small.head_ratio;
  out[idx++] = fv->stats_small.tail_ratio;

  // Stats for main queue (9)
  out[idx++] = fv->stats_main.mean;
  out[idx++] = fv->stats_main.std;
  out[idx++] = fv->stats_main.skewness;
  out[idx++] = fv->stats_main.entropy;
  out[idx++] = fv->stats_main.p25;
  out[idx++] = fv->stats_main.p50;
  out[idx++] = fv->stats_main.p75;
  out[idx++] = fv->stats_main.head_ratio;
  out[idx++] = fv->stats_main.tail_ratio;

  // Stats for ghost queue (9)
  out[idx++] = fv->stats_ghost.mean;
  out[idx++] = fv->stats_ghost.std;
  out[idx++] = fv->stats_ghost.skewness;
  out[idx++] = fv->stats_ghost.entropy;
  out[idx++] = fv->stats_ghost.p25;
  out[idx++] = fv->stats_ghost.p50;
  out[idx++] = fv->stats_ghost.p75;
  out[idx++] = fv->stats_ghost.head_ratio;
  out[idx++] = fv->stats_ghost.tail_ratio;

  return idx;  // 36
}

/**
 * @brief Get full feature vector as flat array (scalars + histograms)
 * Layout: [36 scalars] + [num_buckets for small] + [num_buckets for main] +
 *         [num_buckets for ghost]
 * @return total number of features (36 + 3*num_buckets)
 */
static inline int feature_vector_to_array(const S4FIFO_feature_vector_t *fv,
                                          double *out) {
  // First copy all scalars using the helper function
  int idx = feature_vector_scalars_to_array(fv, out);

  // Histogram vectors
  for (int i = 0; i < fv->num_buckets; i++) {
    out[idx++] = fv->hist_small[i];
  }
  for (int i = 0; i < fv->num_buckets; i++) {
    out[idx++] = fv->hist_main[i];
  }
  for (int i = 0; i < fv->num_buckets; i++) {
    out[idx++] = fv->hist_ghost[i];
  }

  return idx;  // 36 + 3 * num_buckets
}

/**
 * @brief Get total number of features in the feature vector
 */
static inline int feature_vector_get_size(const S4FIFO_feature_vector_t *fv) {
  return FEATURE_VECTOR_NUM_SCALARS + 3 * fv->num_buckets;
}

/**
 * @brief Helper to print stats struct
 */
static inline void print_stats(const hit_pos_dist_stats_t *s, const char *name,
                               FILE *fp) {
  fprintf(fp,
          "  %s: mean=%.3f std=%.3f skew=%.3f entropy=%.3f "
          "p25=%.3f p50=%.3f p75=%.3f head=%.3f tail=%.3f\n",
          name, s->mean, s->std, s->skewness, s->entropy, s->p25, s->p50,
          s->p75, s->head_ratio, s->tail_ratio);
}

/**
 * @brief Print feature vector (scalars only)
 */
static inline void feature_vector_print(const S4FIFO_feature_vector_t *fv,
                                        FILE *fp) {
  fprintf(fp,
          "Base: log_C=%.2f unique=%.3f onehit=%.3f "
          "H_s=%.3f H_m=%.3f H_g=%.3f\n",
          fv->log_cache_capacity, fv->unique_ratio, fv->onehit_ratio,
          fv->hit_ratio_small, fv->hit_ratio_main, fv->hit_ratio_ghost);
  print_stats(&fv->stats_small, "small", fp);
  print_stats(&fv->stats_main, "main ", fp);
  print_stats(&fv->stats_ghost, "ghost", fp);
}

/**
 * @brief Print full feature vector including histograms
 */
static inline void feature_vector_print_full(const S4FIFO_feature_vector_t *fv,
                                             FILE *fp) {
  feature_vector_print(fv, fp);

  // Print histograms
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
