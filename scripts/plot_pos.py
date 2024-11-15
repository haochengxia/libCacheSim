import pandas as pd
import glob
import matplotlib

files = glob.glob('../*.ghost.log.csv')

for file in files[::-1]:
    print(f"Processing {file}")
    df = pd.read_csv(file)
# df = pd.read_csv('../FIFO_Pos.log', header=None, names=['oldest_ts', 'latest_ts', 'hit_ts'])
    df.columns = ['oldest_ts', 'latest_ts', 'hit_ts']
    df.drop(columns=['oldest_ts'], inplace=True)
    df['row_number'] = df.index
    # df = df[:min(10000, len(df))]
    df = df.set_index('hit_ts', drop=False)

    def calculate_range_hits(row):
        latest_ts = row['latest_ts']
        hit_ts = row['hit_ts']
        row_number = row['row_number']
        # range_hits = df.query('row_number < @row_number and @hit_ts < hit_ts < @latest_ts').shape[0]
        if row_number % 1000 == 0:
            print(f"Processing row {row_number // 1000}k...")
        

        history_df = df.loc[df['row_number'] < row_number]
        range_hits = history_df.loc[(history_df.index > hit_ts) & 
                                    (history_df.index < latest_ts)].shape[0]
        return range_hits

    df['range_hits'] = df.apply(calculate_range_hits, axis=1)

    df['hit_pos'] = df['latest_ts'] - df['hit_ts'] - df['range_hits']

    relative_hit_counts = df['hit_pos'].value_counts()
    relative_hit_counts_sorted = relative_hit_counts.sort_index()
    relative_hit_counts_sorted.to_csv(f"{file}.count")

    # plot
    normalized_hit_pos = relative_hit_counts_sorted.index / cache_size
    hit_counts = relative_hit_counts_sorted.values

    plt.figure(figsize=(12, 6))
    plt.subplot(1, 2, 1)
    plt.plot(normalized_hit_pos, hit_counts / hit_counts.sum(), label="PDF")
    plt.xlabel("Normalized Hit Position (hit_pos / cache_size)")
    plt.ylabel("Probability")
    plt.title("PDF of Access Frequency by Position")
    plt.legend()

    cdf = np.cumsum(hit_counts / hit_counts.sum())
    plt.subplot(1, 2, 2)
    plt.plot(normalized_hit_pos, cdf, label="CDF", color='orange')
    plt.xlabel("Normalized Hit Position (hit_pos / cache_size)")
    plt.ylabel("Cumulative Probability")
    plt.title("CDF of Access Frequency by Position")
    plt.legend()

    plt.tight_layout()
    plt.savefig(f"{file}_access_frequency.png")