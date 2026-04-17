import pandas as pd
import matplotlib.pyplot as plt

# =========================
# 1. 读数据（你可以改成读CSV）
# =========================

data = [
    # pair 1
    {"pair": "10+13", "type": "trace1", "miss": 0.4999, "thpt": 2.00},
    {"pair": "10+13", "type": "trace2", "miss": 0.6284, "thpt": 1.81},
    {"pair": "10+13", "type": "mix",    "miss": 0.6099, "thpt": 2.09},

    # pair 2
    {"pair": "10+3", "type": "trace1", "miss": 0.4999, "thpt": 2.00},
    {"pair": "10+3", "type": "trace2", "miss": 0.0072, "thpt": 5.89},
    {"pair": "10+3", "type": "mix",    "miss": 0.0752, "thpt": 5.32},

    # pair 3
    {"pair": "13+3", "type": "trace1", "miss": 0.6284, "thpt": 1.81},
    {"pair": "13+3", "type": "trace2", "miss": 0.0072, "thpt": 5.89},
    {"pair": "13+3", "type": "mix",    "miss": 0.3098, "thpt": 3.83},
]

df = pd.DataFrame(data)

# 如果你想直接读CSV，用这个替换上面 data：
# df = pd.read_csv("your_file.csv")

pairs = df["pair"].unique()
types = ["trace1", "trace2", "mix"]

# =========================
# 2. 图1：Miss Ratio 对比
# =========================

def plot_miss_ratio(df):
    x = range(len(pairs))
    width = 0.25

    plt.figure()

    for i, t in enumerate(types):
        vals = [
            df[(df["pair"] == p) & (df["type"] == t)]["miss"].values[0]
            for p in pairs
        ]
        plt.bar([j + i * width for j in x], vals, width=width, label=t)

    plt.xticks([j + width for j in x], pairs)
    plt.ylabel("Miss Ratio")
    plt.title("Miss Ratio: Single vs Mixed Workloads")
    plt.legend()

    plt.tight_layout()
    plt.savefig("miss_ratio.png", dpi=300)
    plt.close()


# =========================
# 3. 图2：Throughput 对比
# =========================

def plot_throughput(df):
    x = range(len(pairs))
    width = 0.25

    plt.figure()

    for i, t in enumerate(types):
        vals = [
            df[(df["pair"] == p) & (df["type"] == t)]["thpt"].values[0]
            for p in pairs
        ]
        plt.bar([j + i * width for j in x], vals, width=width, label=t)

    plt.xticks([j + width for j in x], pairs)
    plt.ylabel("Throughput (MQPS)")
    plt.title("Throughput: Single vs Mixed Workloads")
    plt.legend()

    plt.tight_layout()
    plt.savefig("throughput.png", dpi=300)
    plt.close()


# =========================
# 4. 图3：Cache Pollution（核心图）
# =========================

def plot_pollution(df):
    pollution = []

    for p in pairs:
        sub = df[df["pair"] == p]
        mix = sub[sub["type"] == "mix"]["miss"].values[0]
        best = sub[sub["type"] != "mix"]["miss"].min()
        pollution.append(mix - best)

    plt.figure()
    plt.bar(pairs, pollution)

    plt.ylabel("Miss Ratio Increase")
    plt.title("Cache Pollution Effect (vs Best Single Trace)")

    plt.tight_layout()
    plt.savefig("pollution.png", dpi=300)
    plt.close()


# =========================
# 5. 图4：Pareto（Miss vs Throughput）
# =========================

def plot_pareto(df):
    plt.figure()

    for t in types:
        sub = df[df["type"] == t]
        plt.scatter(sub["miss"], sub["thpt"], label=t)

    for _, row in df.iterrows():
        label = f"{row['pair']}-{row['type']}"
        plt.text(row["miss"], row["thpt"], label, fontsize=8)

    plt.xlabel("Miss Ratio")
    plt.ylabel("Throughput (MQPS)")
    plt.title("Trade-off: Miss Ratio vs Throughput")
    plt.legend()

    plt.tight_layout()
    plt.savefig("pareto.png", dpi=300)
    plt.close()


# =========================
# 6. 主函数
# =========================

def main():
    plot_miss_ratio(df)
    plot_throughput(df)
    plot_pollution(df)
    plot_pareto(df)

    print("All plots saved:")
    print(" - miss_ratio.png")
    print(" - throughput.png")
    print(" - pollution.png  (MOST IMPORTANT)")
    print(" - pareto.png")


if __name__ == "__main__":
    main()
