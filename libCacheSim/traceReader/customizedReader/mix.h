#pragma once

#include "libCacheSim/reader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  reader_t **readers;
  size_t n_readers;
  size_t current_reader_idx;
} mix_reader_params_t;

void mix_setup_reader(reader_t *reader);

int mix_read_one_req(reader_t *reader, request_t *req);

void mix_reset_reader(reader_t *reader);

int64_t mix_get_num_of_req(reader_t *reader);

void mix_close_reader(reader_t *reader);

#ifdef __cplusplus
}
#endif