# Mix Pair Benchmark

This directory contains a batch script for benchmarking 2-trace mixed workloads.

The script:
- enumerates traces from `/mnt/cfs/oracleReuse/sample`
- picks 1000 unique unordered 2-trace pairs
- benchmarks each participating trace once by itself
- benchmarks each mixed pair through the `mix` reader
- writes one CSV table containing both solo and mixed miss ratios

Example:

```bash
python3 bench/mix_workload/benchmark_mix_pairs.py \
  --trace-dir /mnt/cfs/oracleReuse/sample \
  --num-pairs 1000 \
  --algo s3fifo \
  --cache-size 256MB \
  --jobs 4 \
  --resume
```

Useful outputs next to `--output-csv`:
- `*.manifest.csv`: the exact sampled pair list
- `*.solo.csv`: cached single-trace results reused by pair rows

If the run is interrupted, rerun with `--resume` to continue from the existing manifest and CSV files.