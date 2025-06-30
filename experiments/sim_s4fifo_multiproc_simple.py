#!/usr/bin/env python3
"""
简化版多进程S4FIFO模拟脚本
用于快速测试多进程功能
"""

import multiprocessing as mp
import os
import sys
from typing import Tuple, Dict, Any
import pandas as pd

# 添加项目根目录到路径
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from libcachesim import get_trace_file_path, open_trace, TraceType
from libcachesim.eviction import S4FIFO, LRU, ARC, TwoQ, ThreeLCache, TinyLFU, LRB


def sim_s4fifo(trace_name: str, cache_size_ratio: float = 0.01, small_size_ratio: float = 0.1,
               ghost_size_ratio: float = 0.9, move_to_main_threshold: int = 2, small_skip_ratio: float = 0):
    """S4FIFO模拟函数"""
    try:
        trace_path = get_trace_file_path(trace_name)
        reader = open_trace(trace_path, type=TraceType.ORACLE_GENERAL_TRACE, ignore_obj_size=True)
        cache = S4FIFO(cache_size=int(reader.get_wss()*cache_size_ratio),
                      small_size_ratio=small_size_ratio,
                      ghost_size_ratio=ghost_size_ratio,
                      move_to_main_threshold=move_to_main_threshold,
                      small_skip_ratio=small_skip_ratio)
        miss_ratio = cache.process_trace(reader)
        return miss_ratio
    except Exception as e:
        print(f"Error in sim_s4fifo for {trace_name}: {e}")
        return None


def sim_baseline(trace_name: str, eviction_policy, cache_size_ratio: float = 0.01):
    """基线算法模拟函数"""
    try:
        trace_path = get_trace_file_path(trace_name)
        reader = open_trace(trace_path, type=TraceType.ORACLE_GENERAL_TRACE, ignore_obj_size=True)
        cache = eviction_policy(cache_size=int(reader.get_wss()*cache_size_ratio))
        miss_ratio = cache.process_trace(reader)
        return miss_ratio
    except Exception as e:
        print(f"Error in sim_baseline for {trace_name}: {e}")
        return None


def run_s4fifo_experiment(args: Tuple) -> Dict[str, Any]:
    """运行单个S4FIFO实验"""
    trace_name, cache_size_ratio, small_skip_ratio, move_to_main_threshold, fifo_size_ratio, additional_ghost_size_ratio = args

    miss_ratio = sim_s4fifo(
        trace_name,
        cache_size_ratio=cache_size_ratio,
        small_skip_ratio=small_skip_ratio,
        ghost_size_ratio=additional_ghost_size_ratio + (1-fifo_size_ratio),
        move_to_main_threshold=move_to_main_threshold
    )

    return {
        'trace_name': trace_name,
        'cache_size_ratio': cache_size_ratio,
        'small_skip_ratio': small_skip_ratio,
        'move_to_main_threshold': move_to_main_threshold,
        'fifo_size_ratio': fifo_size_ratio,
        'additional_ghost_size_ratio': additional_ghost_size_ratio,
        'miss_ratio': miss_ratio
    }


def run_baseline_experiment(args: Tuple) -> Dict[str, Any]:
    """运行单个基线实验"""
    trace_name, eviction_policy = args

    miss_ratio = sim_baseline(trace_name, eviction_policy, cache_size_ratio=0.01)

    return {
        'trace_name': trace_name,
        'eviction_policy': eviction_policy.__name__,
        'miss_ratio': miss_ratio
    }


def main():
    # 简化的参数网格（用于快速测试）
    cache_size_ratio_grid = [0.01]  # 只测试一个缓存大小
    small_skip_ratio_grid = [0, 0.5]  # 只测试两个值
    move_to_main_threshold_grid = [2]  # 只测试一个值
    fifo_size_ratio_grid = [0.1, 0.5]  # 只测试两个值
    additional_ghost_size_ratio_grid = [0]  # 只测试一个值

    # 只使用一个trace文件进行测试
    trace_names = ['2021_cdn2/1M/cf_allcolo.ns10.oracleGeneral.zst']

    print(f"开始简化版多进程S4FIFO实验...")
    print(f"Trace文件数量: {len(trace_names)}")
    print(f"参数组合总数: {len(cache_size_ratio_grid) * len(trace_names) * len(small_skip_ratio_grid) * len(move_to_main_threshold_grid) * len(fifo_size_ratio_grid) * len(additional_ghost_size_ratio_grid)}")

    # 生成S4FIFO实验参数组合
    s4fifo_args = []
    for cache_size_ratio in cache_size_ratio_grid:
        for trace_name in trace_names:
            for small_skip_ratio in small_skip_ratio_grid:
                for move_to_main_threshold in move_to_main_threshold_grid:
                    for fifo_size_ratio in fifo_size_ratio_grid:
                        for additional_ghost_size_ratio in additional_ghost_size_ratio_grid:
                            s4fifo_args.append((
                                trace_name, cache_size_ratio, small_skip_ratio,
                                move_to_main_threshold, fifo_size_ratio, additional_ghost_size_ratio
                            ))

    # 使用多进程运行S4FIFO实验
    num_processes = min(mp.cpu_count(), 4)  # 限制最大进程数为4
    print(f"使用 {num_processes} 个进程")

    with mp.Pool(processes=num_processes) as pool:
        s4fifo_results = pool.map(run_s4fifo_experiment, s4fifo_args)

    # 保存S4FIFO结果
    s4fifo_df = pd.DataFrame(s4fifo_results)
    s4fifo_df.to_csv('s4fifo_results_multiproc_simple.csv', index=False)
    print(f"S4FIFO结果已保存到 s4fifo_results_multiproc_simple.csv")
    print("S4FIFO结果预览:")
    print(s4fifo_df)

    # 生成基线实验参数组合
    baseline_args = []
    for eviction_policy in [LRU, ARC]:  # 只测试两个基线算法
        for trace_name in trace_names:
            baseline_args.append((trace_name, eviction_policy))

    print(f"开始多进程基线实验...")
    print(f"基线实验数量: {len(baseline_args)}")

    # 使用多进程运行基线实验
    with mp.Pool(processes=num_processes) as pool:
        baseline_results = pool.map(run_baseline_experiment, baseline_args)

    # 保存基线结果
    baseline_df = pd.DataFrame(baseline_results)
    baseline_df.to_csv('baseline_results_multiproc_simple.csv', index=False)
    print(f"基线结果已保存到 baseline_results_multiproc_simple.csv")
    print("基线结果预览:")
    print(baseline_df)

    print("所有实验完成！")


if __name__ == "__main__":
    # 设置多进程启动方法
    mp.set_start_method('spawn', force=True)
    main()
