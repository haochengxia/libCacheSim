#define _GNU_SOURCE

#include "mix.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "libCacheSim/logging.h"

#ifdef __cplusplus
extern "C" {
#endif

static char *trim_space(char *str) {
  while (*str != '\0' && isspace((unsigned char)*str)) {
    str += 1;
  }

  size_t len = strlen(str);
  while (len > 0 && isspace((unsigned char)str[len - 1])) {
    str[len - 1] = '\0';
    len -= 1;
  }

  return str;
}

static trace_type_e detect_mix_subtrace_type(const char *trace_path) {
  if (strcasestr(trace_path, ".oracleGeneral") != NULL) {
    return ORACLE_GENERAL_TRACE;
  } else if (strcasestr(trace_path, ".lcs") != NULL) {
    return LCS_TRACE;
  } else if (strcasestr(trace_path, ".vscsi") != NULL) {
    return VSCSI_TRACE;
  } else if (strcasestr(trace_path, ".twrNS") != NULL) {
    return TWRNS_TRACE;
  } else if (strcasestr(trace_path, ".twr") != NULL) {
    return TWR_TRACE;
  } else if (strcasestr(trace_path, "oracleSysTwrNS") != NULL) {
    return ORACLE_SYS_TWRNS_TRACE;
  } else if (strcasestr(trace_path, ".csv") != NULL) {
    return CSV_TRACE;
  } else if (strcasestr(trace_path, ".txt") != NULL) {
    return PLAIN_TXT_TRACE;
  } else if (strcasestr(trace_path, ".bin") != NULL) {
    return BIN_TRACE;
  }

  return UNKNOWN_TRACE;
}

void mix_setup_reader(reader_t *reader) {
  char *trace_paths = strdup(reader->trace_path);
  char *trace_paths_head = trace_paths;
  char *trace_paths_copy = NULL;
  char *token = NULL;
  size_t n_readers = 0;

  while ((token = strsep(&trace_paths, ",")) != NULL) {
    token = trim_space(token);
    if (token[0] != '\0') {
      n_readers += 1;
    }
  }

  if (n_readers == 0) {
    free(trace_paths_head);
    ERROR("mix trace reader needs at least one trace path\n");
    exit(1);
  }

  mix_reader_params_t *params = malloc(sizeof(mix_reader_params_t));
  memset(params, 0, sizeof(mix_reader_params_t));
  params->readers = malloc(sizeof(reader_t *) * n_readers);
  memset(params->readers, 0, sizeof(reader_t *) * n_readers);
  params->n_readers = n_readers;

  trace_paths_copy = strdup(reader->trace_path);
  trace_paths = trace_paths_copy;
  size_t reader_idx = 0;
  trace_format_e trace_format = INVALID_TRACE_FORMAT;
  bool all_same_format = true;
  bool all_numeric_obj_id = true;
  bool all_obj_id_num_set = true;
  size_t item_size = 0;
  bool all_same_item_size = true;
  int64_t n_total_req = 0;

  while ((token = strsep(&trace_paths, ",")) != NULL) {
    reader_init_param_t child_init = reader->init_params;
    reader_t *child_reader = NULL;
    trace_type_e child_trace_type;

    token = trim_space(token);
    if (token[0] == '\0') {
      continue;
    }

    child_trace_type = detect_mix_subtrace_type(token);
    if (child_trace_type == UNKNOWN_TRACE) {
      free(trace_paths_head);
      free(trace_paths_copy);
      ERROR(
          "mix trace reader cannot detect the trace type of %s, please use a "
          "supported extension such as .oracleGeneral/.lcs/.csv/.txt\n",
          token);
      exit(1);
    }

    child_init.sampler = NULL;
    child_init.cap_at_n_req = -1;
    child_reader = setup_reader(token, child_trace_type, &child_init);
    params->readers[reader_idx] = child_reader;

    if (reader_idx == 0) {
      trace_format = child_reader->trace_format;
      item_size = child_reader->item_size;
    } else {
      if (child_reader->trace_format != trace_format) {
        all_same_format = false;
      }
      if (child_reader->item_size != item_size) {
        all_same_item_size = false;
      }
    }

    all_numeric_obj_id = all_numeric_obj_id && child_reader->obj_id_is_num;
    all_obj_id_num_set = all_obj_id_num_set && child_reader->obj_id_is_num_set;
    reader->file_size += child_reader->file_size;

    if (child_reader->n_total_req > 0 && n_total_req >= 0) {
      n_total_req += child_reader->n_total_req;
    } else {
      n_total_req = 0;
    }

    reader_idx += 1;
  }

  free(trace_paths_head);
  free(trace_paths_copy);

  reader->reader_params = params;
  reader->trace_format = all_same_format ? trace_format : INVALID_TRACE_FORMAT;
  reader->item_size =
      (all_same_format && all_same_item_size) ? item_size : 0;
  reader->obj_id_is_num = all_numeric_obj_id;
  reader->obj_id_is_num_set = all_obj_id_num_set;
  reader->n_total_req = n_total_req;
  reader->mapped_file = NULL;
  reader->mmap_offset = 0;
}

int mix_read_one_req(reader_t *reader, request_t *req) {
  mix_reader_params_t *params = reader->reader_params;

  while (params->current_reader_idx < params->n_readers) {
    reader_t *child_reader = params->readers[params->current_reader_idx];
    if (read_one_req(child_reader, req) == 0) {
      return 0;
    }
    params->current_reader_idx += 1;
  }

  req->valid = false;
  return 1;
}

void mix_reset_reader(reader_t *reader) {
  mix_reader_params_t *params = reader->reader_params;

  params->current_reader_idx = 0;
  for (size_t i = 0; i < params->n_readers; i++) {
    reset_reader(params->readers[i]);
  }
}

int64_t mix_get_num_of_req(reader_t *reader) {
  if (reader->n_total_req > 0) {
    return reader->n_total_req;
  }

  mix_reader_params_t *params = reader->reader_params;
  int64_t n_total_req = 0;
  for (size_t i = 0; i < params->n_readers; i++) {
    n_total_req += get_num_of_req(params->readers[i]);
  }

  reader->n_total_req = n_total_req;
  return n_total_req;
}

void mix_close_reader(reader_t *reader) {
  mix_reader_params_t *params = reader->reader_params;

  if (params == NULL) {
    return;
  }

  for (size_t i = 0; i < params->n_readers; i++) {
    close_reader(params->readers[i]);
  }

  free(params->readers);
  free(params);
  reader->reader_params = NULL;
}

#ifdef __cplusplus
}
#endif