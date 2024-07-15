//
//  Profiling the behavoir of S3FIFO
//
//
//  profilerS3FIFO.c
//  libCacheSim
//
//  Created by Haocheng on 7/15/24.
//  Copyright © 2024 Haocheng. All rights reserved.

#include "../dataStructure/hashtable/hashtable.h"
#include "../include/libCacheSim/evictionAlgo.h"
#include "../include/libCacheSim/profilerLRU.h"

#ifdef __cplusplus
extern "C" {
#endif

// ***********************************************************************
// ****                                                               ****
// ****          extern eviction algo function declarations           ****
// ****                                                               ****
// ***********************************************************************
extern cache_t *S3FIFO_init(const common_cache_params_t ccache_params,
                            const char *cache_specific_params);
extern void S3FIFO_free(cache_t *cache);
extern bool S3FIFO_get(cache_t *cache, const request_t *req);

// ***********************************************************************
// ****                                                               ****
// ****                  Profiler Stats Structure                     ****
// ****                                                               ****
// ***********************************************************************
typedef struct {
    int64_t fifo_hits;
    int64_t main_hits;
    int64_t ghost_hits;
    int64_t n_obj_move_to_main;
    int64_t n_obj_admit_to_fifo;
    int64_t n_obj_admit_to_main;
} S3FIFO_stats_t;

// ***********************************************************************
// ****                                                               ****
// ****                  Profiler Stats Function                      ****
// ****                                                               ****
// ***********************************************************************
double *get_s3fifo_obj_miss_ratio_curve(reader_t *reader, gint64 size) {
    return get_s3fifo_obj_miss_ratio(reader, size);
}

double *get_s3fifo_obj_miss_ratio(reader_t *reader, gint64 size) {
    double n_req = (double)get_num_of_req(reader);
    double *miss_ratio_array = g_new(double, size + 1);

    guint64 *miss_count_array = _get_s3fifo_miss_cnt(reader, size);
    assert(miss_count_array[0] == get_num_of_req(reader));

    for (gint64 i = 0; i < size + 1; i++) {
        miss_ratio_array[i] = miss_count_array[i] / n_req;
    }
    g_free(miss_count_array);
    return miss_ratio_array;
}

guint64 *_get_s3fifo_miss_cnt(reader_t *reader, gint64 size) {
    guint64 n_req = get_num_of_req(reader);
    guint64 *miss_cnt = _get_s3fifo_hit_cnt(reader, size);
    for (gint64 i = 0; i < size + 1; i++) {
        miss_cnt[i] = n_req - miss_cnt[i];
    }
    return miss_cnt;
}

static void update_s3fifo_stats(S3FIFO_stats_t *stats, cache_t *cache, const request_t *req) {
    if (cache_get_base(cache, req)) {
        S3FIFO_params_t *params = (S3FIFO_params_t *)cache->eviction_params;
        if (params->hit_on_ghost) {
            stats->ghost_hits++;
        } else if (params->fifo->find(params->fifo, req, false)) {
            stats->fifo_hits++;
        } else if (params->main_cache->find(params->main_cache, req, false)) {
            stats->main_hits++;
        }
    } else {
        S3FIFO_params_t *params = (S3FIFO_params_t *)cache->eviction_params;
        if (params->hit_on_ghost) {
            stats->n_obj_admit_to_main++;
        } else {
            stats->n_obj_admit_to_fifo++;
        }
    }
}

guint64 *_get_s3fifo_hit_cnt(reader_t *reader, gint64 size) {
    guint64 ts = 0;
    bool cache_hit;
    guint64 *hit_count_array = g_new0(guint64, size + 1);
    request_t *req = new_request();

    // init cache
    common_cache_params_t ccache_params;
    ccache_params.cache_size = size;
    cache_t *cache = S3FIFO_init(ccache_params, DEFAULT_CACHE_PARAMS);
    S3FIFO_stats_t stats = {0};

    read_one_req(reader, req);
    while (req->valid) {
        update_s3fifo_stats(&stats, cache, req);

        if (cache_get_base(cache, req)) {
            if (req->stack_dist + 1 <= size) {
                hit_count_array[req->stack_dist + 1] += 1;
            }
        }

        read_one_req(reader, req);
        ts++;
    }

    for (gint64 i = 1; i < size + 1; i++) {
        hit_count_array[i] = hit_count_array[i] + hit_count_array[i - 1];
    }

    printf("Objects admitted to FIFO: %ld\n", stats.n_obj_admit_to_fifo);
    printf("Objects admitted to Main: %ld\n", stats.n_obj_admit_to_main);
    printf("Objects moved to Main: %ld\n", stats.n_obj_move_to_main);
    printf("FIFO Hits: %ld\n", stats.fifo_hits);
    printf("Main Hits: %ld\n", stats.main_hits);
    printf("Ghost Hits: %ld\n", stats.ghost_hits);

    free_request(req);
    S3FIFO_free(cache);
    reset_reader(reader);
    return hit_count_array;
}

#ifdef __cplusplus
}
#endif
