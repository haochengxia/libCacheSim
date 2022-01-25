//
//  binaryReader.h
//  libCacheSim
//
//  Created by Juncheng on 2/28/17.
//  Copyright © 2017 Juncheng. All rights reserved.
//



#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <inttypes.h>
#include <stdbool.h>
  
#include "../../include/libCacheSim/const.h"
#include "../../include/libCacheSim/reader.h"

typedef struct {
  int32_t obj_id_field;                  // the beginning bytes in the struct
  uint32_t obj_id_len;                   // the size of obj_id
  char obj_id_type;

  int32_t op_field;
  uint32_t op_len;
  char op_type;

  int32_t real_time_field;
  uint32_t real_time_len;
  char real_time_type;

  int32_t obj_size_field;
  uint32_t obj_size_len;
  char obj_size_type;

  int32_t ttl_field;
  uint32_t ttl_len;
  char ttl_type;

//  int32_t extra_field1;
//  uint32_t extra_len1;
//  char extra_type1;

//  int32_t extra_field2;
//  uint32_t extra_len2;
//  char extra_type2;

  char fmt[MAX_BIN_FMT_STR_LEN];
  uint32_t num_of_fields;
} binary_params_t;



/* function to setup binary reader */
int binaryReader_setup(reader_t *const reader);

int binary_read_one_req(reader_t *reader, request_t *req);


#ifdef __cplusplus
}
#endif
