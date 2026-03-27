#include "dataStructure/hashtable/hashtable.h"
#include "libCacheSim/evictionAlgo.h"

#define S4FIFO_init S4FIFOv8_init
#define S4FIFO_ALGO_NAME "S4FIFOv8"
#define S4FIFO_CACHE_NAME_PREFIX "S4FIFOv8"
#define S4FIFO_UPDATE_QUEUE_SIZES_ON_ADJUSTMENT 0
#define S4FIFO_KEEP_GHOST_FREQ_ON_PROMOTION 0

#include "S4FIFO.c"
