from libcachesim import open_trace, TraceType, get_trace_file_path, get_trace_file_lists
from libcachesim.eviction import S3FIFO
import pandas as pd
import multiprocessing as mp
from functools import partial


def simulate(eviction_algo, trace_file_name: str, cache_size_ratio: float, ignore_obj_size: bool, eviction_algo_params: dict) -> None:
    # get the trace file path
    trace_file_path = get_trace_file_path(trace_file_name)
    # open the trace file
    reader = open_trace(trace_file_path, type=TraceType.ORACLE_GENERAL_TRACE,
        ignore_obj_size=ignore_obj_size)
    # create a cache with the eviction policy
    cache = eviction_algo(cache_size=int(reader.get_wss(ignore_obj_size=ignore_obj_size)*cache_size_ratio), **eviction_algo_params)
    # process the trace
    miss_ratio = cache.process_trace(reader)
    # return the miss ratio
    return miss_ratio


def simulate_single_trace(args):
    """单个trace文件的模拟函数，用于多进程"""
    trace_file_name, eviction_algo, cache_size_ratio, ignore_obj_size, eviction_algo_params = args
    try:
        miss_ratio = simulate(eviction_algo, trace_file_name, cache_size_ratio, ignore_obj_size, eviction_algo_params)
        return {"trace_file_name": trace_file_name, "miss_ratio": miss_ratio, "status": "success"}
    except Exception as e:
        return {"trace_file_name": trace_file_name, "miss_ratio": None, "status": f"error: {str(e)}"}


def main():
    ignore_obj_size = True
    cache_size_ratio = 0.01
    eviction_algo = S3FIFO
    eviction_algo_params = {
        "fifo_size_ratio": 0.1,
        "move_to_main_threshold": 2,
    }

    # get the trace file path
    files = get_trace_file_lists("msr")
    miss_ratios = []
    for file in files:
        trace_file_path = get_trace_file_path(file)
        # open the trace file
        reader = open_trace(trace_file_path, type=TraceType.ORACLE_GENERAL_TRACE, ignore_obj_size=ignore_obj_size)
        # create a cache with the eviction policy
        cache = eviction_algo(cache_size=int(reader.get_wss(ignore_obj_size=ignore_obj_size)*cache_size_ratio), **eviction_algo_params)
        # process the trace
        miss_ratio = cache.process_trace(reader)
        miss_ratios.append(miss_ratio)

    print(f"Miss ratio: {miss_ratios}")


def simulate_all_traces_parallel(num_processes=None):
    """使用多进程并行处理所有trace文件"""
    if num_processes is None:
        num_processes = mp.cpu_count()

    print(f"使用 {num_processes} 个进程进行并行处理...")

    # 读取trace文件列表
    df = pd.read_csv("trace_set_files.csv")

    # 准备参数
    eviction_algo = S3FIFO
    cache_size_ratio = 0.01
    ignore_obj_size = True
    eviction_algo_params = {"fifo_size_ratio": 0.1, "move_to_main_threshold": 2}

    # 准备任务参数
    tasks = []
    for index, row in df.iterrows():
        trace_file_name = str(row["trace_file_name"])
        task_args = (trace_file_name, eviction_algo, cache_size_ratio, ignore_obj_size, eviction_algo_params)
        tasks.append(task_args)

    print(f"总共需要处理 {len(tasks)} 个trace文件")

    # 使用多进程池处理
    results = []
    with mp.Pool(processes=num_processes) as pool:
        # 使用imap来显示进度
        for i, result in enumerate(pool.imap(simulate_single_trace, tasks)):
            results.append(result)
            if (i + 1) % 10 == 0:
                print(f"已处理 {i + 1}/{len(tasks)} 个文件")

    # 处理结果
    successful_results = [r for r in results if r["status"] == "success"]
    failed_results = [r for r in results if r["status"] != "success"]

    print(f"\n处理完成！")
    print(f"成功处理: {len(successful_results)} 个文件")
    print(f"失败处理: {len(failed_results)} 个文件")

    if failed_results:
        print("\n失败的文件:")
        for result in failed_results:
            print(f"  {result['trace_file_name']}: {result['status']}")

    # 提取miss ratios
    miss_ratios = [r["miss_ratio"] for r in successful_results]
    print(f"\nMiss ratios: {miss_ratios}")

    # 保存结果到CSV
    results_df = pd.DataFrame(results)
    results_df.to_csv("simulation_results.csv", index=False)
    print("结果已保存到 simulation_results.csv")

    return results


def simulate_all_traces():
    """原始的单进程版本"""
    df = pd.read_csv("trace_set_files.csv")
    miss_ratios = []
    for index, row in df.iterrows():
        trace_file_name = str(row["trace_file_name"])
        miss_ratio = simulate(S3FIFO, trace_file_name, 0.01, True, {"fifo_size_ratio": 0.1, "move_to_main_threshold": 2})
        miss_ratios.append(miss_ratio)
    print(f"Miss ratio: {miss_ratios}")


if __name__ == "__main__":
    # 使用多进程版本
    simulate_all_traces_parallel()
