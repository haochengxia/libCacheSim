import logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)
import json
import os

cachesim_path = "/mnt/cfs/_libCacheSim/_build/bin/cachesim"
trace_list_path = "/mnt/cfs/_libCacheSim/grid_search/trace_lists.txt"

with open(trace_list_path, 'r') as f:
    data_path_list = f.readlines()
    # remove todo
    data_path_list = [line.strip() for line in data_path_list if not "TODO" in line]

logger.info(f"Loaded {len(data_path_list)} trace files")

cache_size_ratios = [0.001, 0.003, 0.01, 0.03, 0.1, 0.3]

# Generate all combinations
for cache_size_ratio in cache_size_ratios:
    for trace_file in data_path_list:
        print(f'shell:1:1:1:{cachesim_path} {trace_file} oracleGeneral lecar {str(cache_size_ratio)} --ignore-obj-size 1 -o dummy > /mnt/cfs/results/lecar_{os.path.basename(trace_file)}_c{str(cache_size_ratio)}.log 2>&1')

# /mnt/cfs/_libCacheSim/_build/bin/cachesim /mnt/cfs/oracleReuse/tencentBlock/tencentBlock.ns9995.oracleGeneral.zst oracleGeneral lecar 0.001 --ignore-obj-size 1 -o dummy > /mnt/cfs/results/lecar_tencentBlock.ns9995.oracleGeneral.zst_c0.001.log 2>&1
