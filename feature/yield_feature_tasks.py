import logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)
import csv
import os

cachesim_path = "/mnt/cfs/_libCacheSim/_build/bin/cachesim"
meta_csv_path = "/mnt/cfs/_libCacheSim/feature/meta.csv"
trace_list_path = "/mnt/cfs/_libCacheSim/grid_search/trace_lists.txt"
output_dir = "/mnt/cfs/_libCacheSim/feature/collected"

# Feature collection percentages (of total requests)
collect_percentages = [0.05, 0.10, 0.20]

# Cache size ratios to cover
cache_size_ratios = [0.001, 0.01, 0.1]

# Number of buckets for histograms
num_buckets = 20

# Read trace list
trace_paths = []
with open(trace_list_path, 'r') as f:
    for line in f:
        line = line.strip()
        if line and not line.startswith('TODO'):
            trace_paths.append(line)

logger.info(f"Loaded {len(trace_paths)} trace paths from trace_lists.txt")

# Read meta.csv to get request counts (indexed by trace basename)
trace_meta = {}
with open(meta_csv_path, 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        trace_meta[row['trace']] = int(row['num_requests'])

logger.info(f"Loaded metadata for {len(trace_meta)} traces from meta.csv")

# Generate tasks
for cache_size_ratio in cache_size_ratios:
    for trace_path in trace_paths:
        trace_name = os.path.basename(trace_path)

        # Get request count from meta.csv
        if trace_name not in trace_meta:
            logger.warning(f"Skipping {trace_name}: not found in meta.csv")
            continue

        num_requests = trace_meta[trace_name]

        for pct in collect_percentages:
            collect_reqs = int(num_requests * pct)

            # Minimum collect_reqs to get stable features
            if collect_reqs < 10000:
                collect_reqs = 10000

            pct_str = f"{int(pct*100)}pct"
            cache_str = f"c{cache_size_ratio}"
            output_file = os.path.join(output_dir, f"{trace_name.replace('.oracleGeneral.zst', '')}_{cache_str}_{pct_str}.csv")

            # s4fifo command with default parameters + feature collection
            cmd = f'{cachesim_path} {trace_path} oracleGeneral s4fifo {cache_size_ratio} ' \
                  f'-e "collect-features=true,feature-collect-reqs={collect_reqs},feature-num-buckets={num_buckets},dump-file={output_file}" ' \
                  f'--ignore-obj-size 1'

            # Format: shell:priority:retry:timeout:command
            print(f'shell:1:1:1:{cmd}')
