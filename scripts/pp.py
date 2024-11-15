import pandas as pd
import numpy as np
from joblib import Parallel, delayed
import glob
import matplotlib.pyplot as plt

# 使用 glob 获取所有符合条件的文件路径
files = glob.glob('../*.ghost.log.csv')

for file in files:
    print(f"Processing {file}")
    df = pd.read_csv(file)
    
    # 提取 cache_size 并重命名列
    cache_size = int(df.columns[1])
    df.columns = ['oldest_ts', 'latest_ts', 'hit_ts']
    df.drop(columns=['oldest_ts'], inplace=True)
    df = df[:min(100000, len(df))]
    # 增加 row_number 列并设置 hit_ts 作为索引
    df['row_number'] = df.index
    df = df.set_index('hit_ts', drop=False)
    total_k = len(df) // 1000

    # 优化后的 calculate_range_hits 函数
    def calculate_range_hits(row_number, latest_ts, hit_ts):
        # 仅筛选当前行之前的数据
        history_df = df.loc[:hit_ts]
        # 计算 range_hits，限制在 latest_ts 和 hit_ts 范围内
        range_hits = history_df.query('@hit_ts < hit_ts < @latest_ts').shape[0]
        if row_number % 1000 == 0:
            print(f"- processing row {row_number // 1000}k/{total_k}k...")
        return range_hits

    # 使用并行化计算 range_hits
    df['range_hits'] = Parallel(n_jobs=-1)(
        delayed(calculate_range_hits)(row['row_number'], row['latest_ts'], row['hit_ts'])
        for _, row in df.iterrows()
    )

    # 计算 hit_pos
    df['hit_pos'] = df['latest_ts'] - df['hit_ts'] - df['range_hits']

    # 计算相对命中位置的访问次数
    relative_hit_counts = df['hit_pos'].value_counts().sort_index()

    # 将索引除以 cache_size 归一化
    normalized_hit_pos = relative_hit_counts.index / cache_size
    hit_counts = relative_hit_counts.values

    # 绘制 PDF
    plt.figure(figsize=(12, 6))
    plt.subplot(1, 2, 1)
    plt.plot(normalized_hit_pos, hit_counts / hit_counts.sum(), label="PDF")
    plt.xlabel("Normalized Hit Position (hit_pos / cache_size)")
    plt.ylabel("Probability")
    plt.title("PDF of Access Frequency by Position")
    plt.legend()

    # 绘制 CDF
    cdf = np.cumsum(hit_counts / hit_counts.sum())
    plt.subplot(1, 2, 2)
    plt.plot(normalized_hit_pos, cdf, label="CDF", color='orange')
    plt.xlabel("Normalized Hit Position (hit_pos / cache_size)")
    plt.ylabel("Cumulative Probability")
    plt.title("CDF of Access Frequency by Position")
    plt.legend()

    # 保存和显示图表
    plt.tight_layout()
    plt.savefig(f"{file}_access_frequency.png")

    # 保存归一化的访问频率计数
    relative_hit_counts_normalized = pd.Series(hit_counts, index=normalized_hit_pos)
    relative_hit_counts_normalized.to_csv(f"{file}.count")