import pandas as pd

df = pd.read_csv('../FIFO_Pos.log', header=None, names=['first_order', 'last_order', 'hit_order'])

df.drop(columns=['last_order'], inplace=True)
df['row_number'] = df.index
df = df.set_index('hit_order', drop=False)

def calculate_range_hits(row):
    first_order = row['first_order']
    hit_order = row['hit_order']
    row_number = row['row_number']

    history_df = df.loc[df['row_number'] < row_number]

    if row_number % 1000 == 0:
        print(row_number // 1000)

    range_hits = history_df.loc[(history_df.index > first_order) & 
                                (history_df.index < hit_order)].shape[0]
    return range_hits

# def calculate_range_hits(row):
#     first_order = row['first_order']
#     hit_order = row['hit_order']
#     row_number = row['row_number']
#     history = df.query('row_number < @row_number and @first_order < hit_order')
#     range_hits = history.query('hit_order < @hit_order').shape[0]
#     if row_number % 1000 == 0:
#         print(row_number // 1000)
#     return range_hits

df['range_hits'] = df.apply(calculate_range_hits, axis=1)

df['hit_pos'] = df['hit_order'] - df['first_order'] - df['range_hits']

relative_hit_counts = df['hit_pos'].value_counts()
print("Relative position hit counts:")
print(relative_hit_counts)