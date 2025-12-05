import logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)
import json
import os

cachesim_path = "/mnt/cfs/_libCacheSim/_build/bin/cachesim"
trace_list_path = "/mnt/cfs/_libCacheSim/grid_search/trace_lists.txt"
grid_config_path = "/mnt/cfs/_libCacheSim/grid_search/conf_2.json"

with open(trace_list_path, 'r') as f:
    data_path_list = f.readlines()
    # remove todo
    data_path_list = [line.strip() for line in data_path_list if not "TODO" in line]

logger.info(f"Loaded {len(data_path_list)} trace files")

with open(grid_config_path, 'r') as f:
    grid_config = json.load(f)

# Extract parameter values from config
ghost_to_main_thresholds = [float(k) if '.' in k else int(k) for k in grid_config.get("ghost_to_main_thresh", {}).keys()]
small_skip_ratios = [float(k) for k in grid_config.get("small_skip_ratio", {}).keys()]
ghost_size_ratios = [float(k) for k in grid_config.get("ghost_size_ratio", {}).keys()]
small_size_ratios = [float(k) for k in grid_config.get("small_size_ratio", {}).keys()]
move_to_main_thresholds = [int(k) for k in grid_config.get("move_to_main_thresh", {}).keys()]

# Define cache size ratios (not in config file)
cache_size_ratios = [0.001, 0.01, 0.1]

# Generate all combinations
for cache_size_ratio in cache_size_ratios:
    for trace_file in data_path_list:
        for small_size_ratio in small_size_ratios:
            for ghost_size_ratio in ghost_size_ratios:
                for move_to_main_threshold in move_to_main_thresholds:
                    for ghost_to_main_threshold in ghost_to_main_thresholds:
                        for small_skip_ratio in small_skip_ratios:
                            print(f'shell:1:1:1:{cachesim_path} {trace_file} oracleGeneral s4fifo {str(cache_size_ratio)} -e "small-size-ratio={str(small_size_ratio)},ghost-size-ratio={str(ghost_size_ratio)},move-to-main-threshold={str(move_to_main_threshold)},ghost-to-main-threshold={str(ghost_to_main_threshold)},small-skip-ratio={str(small_skip_ratio)}" --ignore-obj-size 1 -o dummy > /mnt/cfs/results/s4fifo_{os.path.basename(trace_file)}_c{str(cache_size_ratio)}_s{str(small_size_ratio)}_g{str(ghost_size_ratio)}_m{str(move_to_main_threshold)}_t{str(ghost_to_main_threshold)}_k{str(small_skip_ratio)}.log')
