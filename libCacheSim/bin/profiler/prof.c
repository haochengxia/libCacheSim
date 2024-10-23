//
// Created by Haocheng Xia on 2024/7/18.
//


#include "../../include/libCacheSim/cache.h"
#include "../../include/libCacheSim/reader.h"
#include "../../utils/include/mymath.h"
#include "../../utils/include/mystr.h"
#include "../../utils/include/mysys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  cache_t *fifo;
  cache_t *fifo_ghost;
  cache_t *main_cache;
  bool hit_on_ghost;

  int64_t n_obj_admit_to_fifo;
  int64_t n_obj_admit_to_main;
  int64_t n_obj_move_to_main;
  int64_t n_byte_admit_to_fifo;
  int64_t n_byte_admit_to_main;
  int64_t n_byte_move_to_main;

  int move_to_main_threshold;
  double fifo_size_ratio;
  double ghost_size_ratio;
  char main_cache_type[32];

  request_t *req_local;
} S3FIFO_params_t;

/* stats for S3FIFO */
typedef struct {
  int64_t miss_cnt;

  int64_t fifo_hits;
  int64_t main_hits;
  int64_t ghost_hits;
  int64_t n_obj_move_to_main;
  int64_t n_obj_admit_to_fifo;
  int64_t n_obj_admit_to_main;
} S3FIFO_stats_t;


/***** helper *****/
static bool update_s3fifo_stats(S3FIFO_stats_t *stats, cache_t *cache, const request_t *req, S3FIFO_stats_t *windows, int w1, int w2) {
  bool hit = true;
//  bool ghost_hit = false;
  int64_t n_obj_admit_to_main_old = ((S3FIFO_params_t *)cache->eviction_params)->n_obj_admit_to_main;
  int64_t n_obj_admit_to_fifo_old = ((S3FIFO_params_t *)cache->eviction_params)->n_obj_admit_to_fifo;
  int64_t n_obj_move_to_main_old = ((S3FIFO_params_t *)cache->eviction_params)->n_obj_move_to_main;

  if (cache_get_base(cache, req)) { // deal with one req
    S3FIFO_params_t *params = (S3FIFO_params_t *)cache->eviction_params;
//    ghost_hit = params->hit_on_ghost;
    if (params->fifo->find(params->fifo, req, false)) {
      stats->fifo_hits++;
      if (w1 >= 0) windows[w1].fifo_hits++;
      if (w2 >= 0) windows[w2].fifo_hits++;
    } else if (params->main_cache->find(params->main_cache, req, false)) {
      stats->main_hits++;
      if (w1 >= 0) windows[w1].main_hits++;
      if (w2 >= 0) windows[w2].main_hits++;
    }
    


    hit = true;
  } else {
    if (w1 >= 0) windows[w1].miss_cnt++;
    if (w2 >= 0) windows[w2].miss_cnt++;
//    S3FIFO_params_t *params = (S3FIFO_params_t *)cache->eviction_params;
//    ghost_hit = params->hit_on_ghost;
    hit = false;
  }

//  if (ghost_hit) {
//    stats->ghost_hits++;
//    if (w1 >= 0) windows[w1].ghost_hits++;
//    if (w2 >= 0) windows[w2].ghost_hits++;
//  }

  int64_t n_obj_admit_to_main = ((S3FIFO_params_t *)cache->eviction_params)->n_obj_admit_to_main;
  int64_t n_obj_admit_to_fifo = ((S3FIFO_params_t *)cache->eviction_params)->n_obj_admit_to_fifo;
  int64_t n_obj_move_to_main = ((S3FIFO_params_t *)cache->eviction_params)->n_obj_move_to_main;

  if (n_obj_admit_to_main != n_obj_admit_to_main_old) {
    stats->n_obj_admit_to_main++;
    if (w1 >= 0) windows[w1].n_obj_admit_to_main++;
    if (w2 >= 0) windows[w2].n_obj_admit_to_main++;

    stats->ghost_hits = stats->n_obj_admit_to_main;
    windows[w1].ghost_hits = windows[w1].n_obj_admit_to_main;
    windows[w2].ghost_hits = windows[w2].n_obj_admit_to_main;
  }

  if (n_obj_admit_to_fifo != n_obj_admit_to_fifo_old) {
    stats->n_obj_admit_to_fifo++;
    if (w1 >= 0) windows[w1].n_obj_admit_to_fifo++;
    if (w2 >= 0) windows[w2].n_obj_admit_to_fifo++;
  }
  if (n_obj_move_to_main != n_obj_move_to_main_old) {
    stats->n_obj_move_to_main = n_obj_move_to_main;
    // bug
    int64_t delta = n_obj_move_to_main - n_obj_move_to_main_old;
    if (delta > 0) {
      
      if (w1 >= 0) windows[w1].n_obj_move_to_main += delta;
      if (w2 >= 0) windows[w2].n_obj_move_to_main += delta;
    }
  }
  return hit;
}

void profile(reader_t *reader, cache_t *cache, int report_interval,
              int warmup_sec, char *ofilepath, bool ignore_obj_size, 
              double window_ratio, double skip_ratio) {
  /* random seed */
  srand(time(NULL));
  set_rand_seed(rand());

  uint64_t n_total_req = -1;
  if (reader->is_zstd_file) {
    n_total_req = 0;
    request_t *req = new_request();
    read_one_req(reader, req);
    while (req->valid) {
      n_total_req++;
      read_one_req(reader, req);
    }
    reset_reader(reader);
  } else {
    n_total_req = reader->n_total_req;
  }

  request_t *req = new_request();
  uint64_t req_cnt = 0, miss_cnt = 0;
  uint64_t last_req_cnt = 0, last_miss_cnt = 0;

  S3FIFO_stats_t stats;
  S3FIFO_stats_t window_stats[100];

  /* init */
  int num_window = (int)(1 / (window_ratio / 2)) - 1;
  memset(&stats, 0, sizeof(S3FIFO_stats_t));
  memset(window_stats, 0, 100 * sizeof(S3FIFO_stats_t));

  read_one_req(reader, req);
  uint64_t start_ts = (uint64_t)req->clock_time;
  uint64_t last_report_ts = warmup_sec;
  
  int64_t num_request_to_skip = (int64_t)(skip_ratio * n_total_req);

  double start_time = -1;
  while (req->valid) {
    req->clock_time -= (int64_t )start_ts;
    if (req->clock_time <= warmup_sec) {
      cache->get(cache, req); // keep, will not be used in stats
      read_one_req(reader, req);
      continue;
    } else {
      if (start_time < 0) {
        start_time = gettime();
      }
    }

    req_cnt++;
    // according to the current req counter, determine data need to be record to which time window
    if (req_cnt <= num_request_to_skip) continue;

    // if (cache->get(cache, req) == false) {
    double current_percentage = ((double )req_cnt) / (double)(n_total_req);
    int w1 = -1;
    int w2 = -1;

    if (current_percentage <= window_ratio / 2) {
      w1 = 0;
    } else if (current_percentage > 1-window_ratio / 2) {
      w1 = num_window - 1;
    } else {
      // two window
      w1 = (int)(current_percentage / (window_ratio/2)) - 1;
      w2 = w1 + 1;
    }

    if (update_s3fifo_stats(&stats, cache, req, window_stats, w1, w2) == false) {
      miss_cnt++;
    }
    if (req->clock_time - last_report_ts >= report_interval &&
        req->clock_time != 0) {
      INFO(
          "%s %s %.2lf hour: %lu requests, miss ratio %.4lf, interval miss "
          "ratio "
          "%.4lf\n",
          mybasename(reader->trace_path), cache->cache_name,
          (double)req->clock_time / 3600, (unsigned long)req_cnt,
          (double)miss_cnt / req_cnt,
          (double)(miss_cnt - last_miss_cnt) / (req_cnt - last_req_cnt));
      last_miss_cnt = miss_cnt;
      last_req_cnt = req_cnt;
      last_report_ts = (int64_t)req->clock_time;
    }

    read_one_req(reader, req);
  }

  double runtime = gettime() - start_time;

  char output_str[1024];
  char size_str[8];
  if (!ignore_obj_size)
    convert_size_to_str(cache->cache_size, size_str);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  if (!ignore_obj_size) {
    snprintf(output_str, 1024,
             "%s %s cache size %8s, %16lu req, miss ratio %.4lf, throughput "
             "%.2lf MQPS\n",
             reader->trace_path, cache->cache_name, size_str,
             (unsigned long)req_cnt, (double)miss_cnt / (double)req_cnt,
             (double)req_cnt / 1000000.0 / runtime);
  } else {
    snprintf(output_str, 1024,
             "%s %s cache size %8ld, %16lu req, miss ratio %.4lf, throughput "
             "%.2lf MQPS\n",
             reader->trace_path, cache->cache_name, cache->cache_size,
             (unsigned long)req_cnt, (double)miss_cnt / (double)req_cnt,
             (double)req_cnt / 1000000.0 / runtime);
  }

  printf(
           "GLOBAL,   main hits %8ld, fifo hits %8ld, ghost hits %8ld, "
           "admit main %8ld, admit fifo %8ld, move main %8ld, "
           "total reqs %8ld\n",
           stats.main_hits, stats.fifo_hits, stats.ghost_hits,
           stats.n_obj_admit_to_main, stats.n_obj_admit_to_fifo, stats.n_obj_move_to_main,
           n_total_req);

  for (int i = 0; i < num_window; i++) {
    printf(
             "WINDOW %d, main hits %8ld, fifo hits %8ld, ghost hits %8ld, "
             "admit main %8ld, admit fifo %8ld, move main %8ld, miss cnt %8ld, "
             "\n",
             i, window_stats[i].main_hits, window_stats[i].fifo_hits, window_stats[i].ghost_hits,
             window_stats[i].n_obj_admit_to_main, window_stats[i].n_obj_admit_to_fifo, window_stats[i].n_obj_move_to_main,
             window_stats[i].miss_cnt);
  }

  S3FIFO_params_t* params = (S3FIFO_params_t*)(cache->eviction_params);
  printf(
      "VALIDATE,  "
      "admit main %8ld, admit fifo %8ld, move main %8ld, "
      "total reqs %8ld, req_cnt %8ld\n",
      params->n_obj_admit_to_main, params->n_obj_admit_to_fifo, params->n_obj_move_to_main,
      n_total_req, req_cnt);

#pragma GCC diagnostic pop
  printf("%s", output_str);

  FILE *output_file = fopen(ofilepath, "a");
  if (output_file == NULL) {
    ERROR("cannot open file %s %s\n", ofilepath, strerror(errno));
    exit(1);
  }
  fprintf(output_file, "%s\n", output_str);
  fclose(output_file);

#if defined(TRACK_EVICTION_V_AGE)
  while (cache->get_occupied_byte(cache) > 0) {
    cache->evict(cache, req);
  }

#endif
  free_request(req);
  cache->cache_free(cache);
}

#ifdef __cplusplus
}
#endif
