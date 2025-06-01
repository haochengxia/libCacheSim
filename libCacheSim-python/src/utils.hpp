// Copied from libCacheSim/bin/cli_reader_utils.c
#pragma once

#include "libCacheSim/reader.h"

static inline void cal_working_set_size(reader_t *reader, int64_t *wss_obj,
                          int64_t *wss_byte) {
  reset_reader(reader);
  request_t *req = new_request();
  GHashTable *obj_table = g_hash_table_new(g_direct_hash, g_direct_equal);
  *wss_obj = 0;
  *wss_byte = 0;

  // sample the object space in case there are too many objects
  // which can cause a crash
  int scaling_factor = 1;
  if (reader->file_size > 5 * GiB) {
    scaling_factor = 101;
  } else if (reader->file_size > 1 * GiB) {
    scaling_factor = 11;
  }

  int64_t n_req = 0;
  INFO("calculating working set size...\n");
  while (read_one_req(reader, req) == 0) {
    n_req += 1;
    if (n_req % 2000000 == 0) {
      DEBUG("processed %ld requests, %lld objects, %lld bytes\n", (long)n_req,
            (long long)*wss_obj, (long long)*wss_byte);
    }
    if (scaling_factor > 1 && req->obj_id % scaling_factor != 0) {
      continue;
    }

    if (g_hash_table_contains(obj_table, (gconstpointer)req->obj_id)) {
      continue;
    }

    g_hash_table_add(obj_table, (gpointer)req->obj_id);

    *wss_obj += 1;
    *wss_byte += req->obj_size;
  }
  *wss_obj *= scaling_factor;
  *wss_byte *= scaling_factor;

  if (scaling_factor > 1) {
    INFO(
        "estimated working set size (%.2f sample ratio): %lld object %lld "
        "byte\n",
        1.0 / scaling_factor, (long long)*wss_obj, (long long)*wss_byte);
  } else {
    INFO("working set size: %lld object %lld byte\n", (long long)*wss_obj,
         (long long)*wss_byte);
  }

  g_hash_table_destroy(obj_table);
  free_request(req);
  reset_reader(reader);
}