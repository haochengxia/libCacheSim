import pandas as pd
from joblib import Parallel, delayed

# 读取数据
df = pd.read_csv('../FIFO_Pos.log', header=None, names=['first_order', 'last_order', 'hit_order'])
df.drop(columns=['last_order'], inplace=True)
df['row_number'] = df.index
df = df.set_index('hit_order', drop=False)

print(df.query('first_order == hit_order').shape)

# # 定义函数计算每行的 range_hits
# def calculate_range_hits(row):
#     first_order = row['first_order']
#     hit_order = row['hit_order']
#     row_number = row['row_number']
    
#     # # 获取当前行之前的历史数据
#     # history_df = df.loc[df['row_number'] < row_number]
#     range_hits = df.query('row_number < @row_number and @first_order < hit_order < @hit_order').shape[0]
    
#     if row_number % 1000 == 0:
#         print(f"Processing row {row_number // 1000}k...")
    
#     # # 计算 (first_order, hit_order) 范围内的历史命中次数
#     # range_hits = history_df.loc[(history_df.index > first_order) & 
#     #                             (history_df.index < hit_order)].shape[0]
#     return range_hits

# # 使用并行化计算 range_hits
# df['range_hits'] = Parallel(n_jobs=-1)(delayed(calculate_range_hits)(row) for _, row in df.iterrows())

# # 计算 hit_pos
# df['hit_pos'] = df['hit_order'] - df['first_order'] - df['range_hits']

# # 统计每个相对位置的命中次数
# relative_hit_counts = df['hit_pos'].value_counts()
# relative_hit_counts_sorted = relative_hit_counts.sort_index()
# print("Relative position hit counts:")
# print(relative_hit_counts_sorted)