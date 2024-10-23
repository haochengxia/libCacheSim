//
// Created by Haocheng Xia on 7/15/24.
//

#include "../cli_reader_utils.h"
#include "internal.h"

#include "../../include/libCacheSim/cache.h"
#include "../../include/libCacheSim/reader.h"
#include "../../include/libCacheSim/simulator.h"
#include "../../utils/include/mystr.h"
#include "../../utils/include/mysys.h"



int main(int argc, char *argv[]) {
  struct arguments args;
  parse_cmd(argc, argv, &args);

  // run simulation of S3FIFO and record stats
  if (args.n_cache_size * args.n_eviction_algo == 1) {
    profile(args.reader, args.caches[0], args.report_interval, args.warmup_sec,
            args.ofilepath, args.ignore_obj_size, args.window_ratio, args.skip_ratio);

    free_arg(&args);
    return 0;
  } else {
    ERROR("Only support S3FIFO and one cache size");
  }


//  TraceAnalyzer *stat = new TraceAnalyzer(
//      args.reader, args.ofilepath, args.analysis_option, args.analysis_param);
//  stat->run();

  // } else if (strcasecmp(args.task, "hotOS23") == 0) {
  //   args.analysis_option.popularity_decay = true;
  //   args.analysis_option.prob_at_age = true;
  //   args.analysis_option.lifetime = false;

  //   args.analysis_param.time_window = 300;

//  ofstream ofs("traceStat", ios::out | ios::app);
//  ofs << *stat << endl;
//  ofs.close();
//  cout << *stat;
//
//  delete stat;
//
//  close_reader(args.reader);

  return 0;
}
//
//// main.c
//#include <stdio.h>
//#include "../../include/libCacheSim/reader.h"
//#include "../../include/libCacheSim/profilerLRU.h"
//
//// 声明分析函数
//extern double *get_s3fifo_obj_miss_ratio_curve(reader_t *reader, gint64 size);
//
//int main(int argc, char *argv[]) {
//    if (argc != 3) {
//        fprintf(stderr, "Usage: %s <trace_file> <cache_size>\n", argv[0]);
//        return 1;
//    }
//
//    // 从命令行参数中获取跟踪文件路径和缓存大小
//    const char *trace_file = argv[1];
//    gint64 cache_size = atoll(argv[2]);
//
//    // 打开跟踪文件
//    reader_t *reader = open_trace_reader(trace_file);
//    if (reader == NULL) {
//        fprintf(stderr, "Error opening trace file: %s\n", trace_file);
//        return 1;
//    }
//
//    // 获取S3FIFO算法的失误率曲线
//    double *miss_ratio_curve = get_s3fifo_obj_miss_ratio_curve(reader, cache_size);
//    if (miss_ratio_curve == NULL) {
//        fprintf(stderr, "Error computing miss ratio curve\n");
//        close_trace_reader(reader);
//        return 1;
//    }
//
//    // 打印失误率曲线
//    printf("Cache Size: %lld\n", cache_size);
//    for (gint64 i = 0; i <= cache_size; i++) {
//        printf("Cache Size %lld: Miss Ratio: %.6f\n", i, miss_ratio_curve[i]);
//    }
//
//    // 释放资源
//    g_free(miss_ratio_curve);
//    close_trace_reader(reader);
//
//    return 0;
//}
