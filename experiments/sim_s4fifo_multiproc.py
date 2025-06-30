import multiprocessing as mp
from itertools import product
import os
import sys
from typing import Tuple, Dict, Any
import pandas as pd

# 添加项目根目录到路径
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from libcachesim import get_trace_file_path, open_trace, TraceType
from libcachesim.eviction import S4FIFO, S3FIFO, EvictionPolicy, LRU, ARC, TwoQ, ThreeLCache, TinyLFU, LRB


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


def sim_s3fifo(trace_name: str, cache_size_ratio: float = 0.01, fifo_size_ratio: float = 0.1,
               ghost_size_ratio: float = 0.9, move_to_main_threshold: int = 2):
    """S3FIFO模拟函数"""
    try:
        trace_path = get_trace_file_path(trace_name)
        reader = open_trace(trace_path, type=TraceType.ORACLE_GENERAL_TRACE, ignore_obj_size=True)
        cache = S3FIFO(cache_size=int(reader.get_wss()*cache_size_ratio),
                      fifo_size_ratio=fifo_size_ratio,
                      ghost_size_ratio=ghost_size_ratio,
                      move_to_main_threshold=move_to_main_threshold)
        miss_ratio = cache.process_trace(reader)
        return miss_ratio
    except Exception as e:
        print(f"Error in sim_s3fifo for {trace_name}: {e}")
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
    # 参数网格
    cache_size_ratio_grid = [0.01, 0.1]
    small_skip_ratio_grid = [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1]
    move_to_main_threshold_grid = [1, 2]
    fifo_size_ratio_grid = [0.01, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.99]
    additional_ghost_size_ratio_grid = [0, 1]

    # 获取trace文件列表
    trace_names = '''cdn2,2021_cdn2/1M/cf_allcolo.ns10.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1014.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns102.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1025.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns10406.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1042.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns105.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns10508.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1051.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1055.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1076.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1081.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1095.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns11.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1103.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns111.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1133.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1152.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns116.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1161.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1171.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1173.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1174.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1175.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1176.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1177.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1178.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1179.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1180.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1181.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1182.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1183.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1184.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1185.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1189.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1190.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1192.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1193.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1194.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns12.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1200.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1211.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns122.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1228.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns123.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1230.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1235.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1236.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1242.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1243.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1247.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1250.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1268.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1273.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns128.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns13.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1304.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1322.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1333.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns134.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns135.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1359.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1363.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1371.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1376.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1378.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1392.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns14.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns141.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1413.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1419.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1435.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1436.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1437.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1440.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1446.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1447.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1476.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1485.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns15.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1502.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1519.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1522.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1536.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1543.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1554.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns15541.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1555.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1557.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1568.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1569.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1572.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1597.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns16.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1628.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1639.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1648.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1660.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1669.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns168.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns169.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1698.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1699.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1700.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1701.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1702.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1703.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1708.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1715.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns173.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1739.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns174.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns175.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1774.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns178.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1782.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1797.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns181.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns183.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1832.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1836.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1837.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1844.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1846.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1849.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns185.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1876.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1880.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns189.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1892.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1899.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1907.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1909.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1910.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns192.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1924.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns193.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1934.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1957.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1964.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1984.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns199.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1995.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1996.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns1998.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2021.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2028.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns203.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2034.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns206.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2066.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2096.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2106.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2113.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2115.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2117.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2135.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2170.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2181.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns219.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2192.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2208.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2243.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns225.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2251.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2258.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns226.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2272.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2280.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2281.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2284.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2290.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2297.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2302.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns231.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns233.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2335.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2338.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2340.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns236.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns24.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns241.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns242.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2501.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2526.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2533.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2539.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns256.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2561.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns257.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2570.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2589.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns261.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2637.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2638.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2647.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2652.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns2654.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns267.oracleGeneral.zst
cdn2,2021_cdn2/1M/cf_allcolo.ns270.oracleGeneral.zst'''.split('\n')

    # 调试模式：只使用一个trace文件
    trace_names = '''cdn2,2021_cdn2/1M/cf_allcolo.ns10.oracleGeneral.zst'''.split('\n')

    # 处理trace名称
    trace_names = [name.split(',')[1] for name in trace_names if name.strip()]

    print(f"开始多进程S4FIFO实验...")
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
    num_processes = min(mp.cpu_count(), 8)  # 限制最大进程数
    print(f"使用 {num_processes} 个进程")

    with mp.Pool(processes=num_processes) as pool:
        s4fifo_results = pool.map(run_s4fifo_experiment, s4fifo_args)

    # 保存S4FIFO结果
    s4fifo_df = pd.DataFrame(s4fifo_results)
    s4fifo_df.to_csv('s4fifo_results_multiproc.csv', index=False)
    print(f"S4FIFO结果已保存到 s4fifo_results_multiproc.csv")

    # 生成基线实验参数组合
    baseline_args = []
    for eviction_policy in [LRU, ARC, TwoQ, ThreeLCache, TinyLFU, LRB]:
        for trace_name in trace_names:
            baseline_args.append((trace_name, eviction_policy))

    print(f"开始多进程基线实验...")
    print(f"基线实验数量: {len(baseline_args)}")

    # 使用多进程运行基线实验
    with mp.Pool(processes=num_processes) as pool:
        baseline_results = pool.map(run_baseline_experiment, baseline_args)

    # 保存基线结果
    baseline_df = pd.DataFrame(baseline_results)
    baseline_df.to_csv('baseline_results_multiproc.csv', index=False)
    print(f"基线结果已保存到 baseline_results_multiproc.csv")

    print("所有实验完成！")


if __name__ == "__main__":
    # 设置多进程启动方法
    mp.set_start_method('spawn', force=True)
    main()
