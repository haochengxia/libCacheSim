#!/usr/bin/env python3
"""
Aggregate results from /mnt/cfs/results directory.
Each log file contains one line with format:
<trace> <algorithm> cache size <size>, <num_req> req, miss ratio <miss_ratio>, throughput <throughput> MQPS
"""

import os
import re
import csv
from pathlib import Path
from collections import defaultdict
import argparse


def parse_log_file(filepath):
    """Parse a single log file and extract information."""
    try:
        with open(filepath, 'r') as f:
            line = f.read().strip()

        if not line:
            return None

        # Parse the log line
        # Example: /mnt/cfs/oracleReuse/systor/2016_LUN0.oracleGeneral.zst S4FIFO-0.0500-1 cache size 64548, 552023811 req, miss ratio 0.7538, throughput 1.55 MQPS

        # Extract trace path
        parts = line.split()
        if len(parts) < 10:
            return None

        trace = parts[0]
        algorithm = parts[1]

        # Extract cache size
        cache_size_match = re.search(r'cache size\s+(\d+)', line)
        cache_size = int(cache_size_match.group(1)) if cache_size_match else None

        # Extract number of requests
        req_match = re.search(r'(\d+)\s+req', line)
        num_req = int(req_match.group(1)) if req_match else None

        # Extract miss ratio
        miss_ratio_match = re.search(r'miss ratio\s+([\d.]+)', line)
        miss_ratio = float(miss_ratio_match.group(1)) if miss_ratio_match else None

        # Extract throughput
        throughput_match = re.search(r'throughput\s+([\d.]+)\s+MQPS', line)
        throughput = float(throughput_match.group(1)) if throughput_match else None

        # Parse filename to extract parameters
        filename = os.path.basename(filepath)
        # Example: s4fifo_2016_LUN0.oracleGeneral.zst_c0.001_s0.05_g0.9_m1_t0_k0.0.log
        param_match = re.search(r'_c([\d.]+)_s([\d.]+)_g([\d.]+)_m(\d+)_t(\d+)_k([\d.]+)\.log$', filename)

        if param_match:
            c_param = float(param_match.group(1))
            s_param = float(param_match.group(2))
            g_param = float(param_match.group(3))
            m_param = int(param_match.group(4))
            t_param = int(param_match.group(5))
            k_param = float(param_match.group(6))
        else:
            c_param = s_param = g_param = m_param = t_param = k_param = None

        return {
            'filename': filename,
            'trace': trace,
            'algorithm': algorithm,
            'cache_size': cache_size,
            'num_req': num_req,
            'miss_ratio': miss_ratio,
            'throughput': throughput,
            'c_param': c_param,
            's_param': s_param,
            'g_param': g_param,
            'm_param': m_param,
            't_param': t_param,
            'k_param': k_param,
        }

    except Exception as e:
        print(f"Error parsing {filepath}: {e}")
        return None


def aggregate_results(results_dir, output_file):
    """Aggregate all results from the directory."""
    results = []

    results_path = Path(results_dir)
    log_files = list(results_path.glob('*.log'))

    print(f"Found {len(log_files)} log files")

    total = len(log_files)
    processed = 0

    for log_file in log_files:
        data = parse_log_file(log_file)
        if data:
            results.append(data)

        processed += 1
        if processed % 10000 == 0:
            print(f"Processed {processed}/{total} files ({100*processed/total:.1f}%)")

    print(f"Successfully parsed {len(results)} files")

    # Write to CSV
    if results:
        fieldnames = [
            'filename', 'trace', 'algorithm', 'cache_size', 'num_req',
            'miss_ratio', 'throughput', 'c_param', 's_param', 'g_param',
            'm_param', 't_param', 'k_param'
        ]

        with open(output_file, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(results)

        print(f"Results written to {output_file}")

        # Print summary statistics
        print("\n=== Summary Statistics ===")
        print(f"Total results: {len(results)}")

        traces = set(r['trace'] for r in results)
        print(f"Unique traces: {len(traces)}")

        algorithms = set(r['algorithm'] for r in results)
        print(f"Unique algorithms: {len(algorithms)}")

        if results[0]['miss_ratio'] is not None:
            miss_ratios = [r['miss_ratio'] for r in results if r['miss_ratio'] is not None]
            if miss_ratios:
                print(f"Miss ratio range: {min(miss_ratios):.4f} - {max(miss_ratios):.4f}")
                print(f"Average miss ratio: {sum(miss_ratios)/len(miss_ratios):.4f}")


def main():
    parser = argparse.ArgumentParser(description='Aggregate S4FIFO experiment results')
    parser.add_argument('--results-dir', default='/mnt/cfs/results',
                        help='Directory containing result log files')
    parser.add_argument('--output', default='aggregated_results.csv',
                        help='Output CSV file')

    args = parser.parse_args()

    aggregate_results(args.results_dir, args.output)


if __name__ == '__main__':
    main()
