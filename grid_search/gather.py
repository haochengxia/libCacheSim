RESULT_DIR = "/mnt/cfs/results"
GATHER_DIR = "/mnt/cfs/_libCacheSim/grid_search"
import glob
import os
import tempfile
from multiprocessing import Pool

def process_log_file_batch(args):
    """Process a batch of log files and write to a temporary file"""
    batch_files, temp_file = args
    results = []

    for fname in batch_files:
        try:
            with open(fname) as infile:
                line = infile.readline()
                if line == "":
                    continue

                # c s g m t k
                # s4fifo_tencentBlock.ns12623.oracleGeneral.zst_c0.001_s0.01_g0.5_m1_t0_k1.0.log
                params = fname.split('.zst_')[-1].split('_')
                trace_name = fname.split('/')[-1].split('.zst_')[0]
                cache_size_ratio = params[0][1:]
                small_size_ratio = params[1][1:]
                ghost_size_ratio = params[2][1:]
                move_to_main_threshold = params[3][1:]
                ghost_to_main_threshold = params[4][1:]
                small_skip_ratio = params[5][1:].split('.log')[0]

                # /mnt/cfs/oracleReuse/tencentBlock/tencentBlock.ns12623.oracleGeneral.zst S4FIFO-0.0100-1 cache size      907,          2393392 req, miss ratio 0.7036, throughput 3.13 MQPS
                cache_size = line.split(',')[0].split('cache size')[-1].strip()
                total_requests = line.split(',')[1].split('req')[0].strip()
                miss_ratio = line.split(',')[2].split('miss ratio')[-1].strip()
                throughput = line.split(',')[3].split('throughput')[-1].strip()

                results.append(f"{trace_name},{cache_size_ratio},{small_size_ratio},{ghost_size_ratio},{move_to_main_threshold},{ghost_to_main_threshold},{small_skip_ratio},{cache_size},{total_requests},{miss_ratio},{throughput}\n")
        except Exception as e:
            print(f"Error processing {fname}: {e}")

    # Write batch results to temporary file
    with open(temp_file, 'w') as f:
        f.writelines(results)

    return len(results), temp_file

def gather_logs():
    log_files = glob.glob(f"{RESULT_DIR}/s4fifo_*.log")
    print(f"Found {len(log_files)} log files to process")

    # Split files into 20 batches
    num_processes = 20
    batch_size = (len(log_files) + num_processes - 1) // num_processes
    batches = []

    for i in range(num_processes):
        start = i * batch_size
        end = min((i + 1) * batch_size, len(log_files))
        if start >= len(log_files):
            break

        # Create temporary file for each batch
        temp_fd, temp_file = tempfile.mkstemp(prefix=f"gather_{i}_", suffix=".csv", dir=GATHER_DIR)
        os.close(temp_fd)
        batches.append((log_files[start:end], temp_file))

    # Process batches in parallel
    with Pool(processes=num_processes) as pool:
        batch_results = pool.map(process_log_file_batch, batches)

    # Merge all temporary files into final CSV
    total_processed = sum(count for count, _ in batch_results)
    print(f"Successfully processed {total_processed} files")

    with open(f"{GATHER_DIR}/gathered_results.csv", 'w') as outfile:
        # write header
        outfile.write("trace_name,cache_size_ratio,small_size_ratio,ghost_size_ratio,move_to_main_threshold,ghost_to_main_threshold,small_skip_ratio,cache_size,total_requests,miss_ratio,throughput\n")

        # Merge all temporary files
        for count, temp_file in batch_results:
            if count > 0 and os.path.exists(temp_file):
                with open(temp_file, 'r') as f:
                    outfile.write(f.read())
                # Clean up temporary file
                os.remove(temp_file)

    print(f"Results written to {GATHER_DIR}/gathered_results.csv")

if __name__ == "__main__":
    gather_logs()
