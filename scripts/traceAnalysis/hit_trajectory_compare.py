"""
Plot post-adjustment hit/miss trajectories for compare.sh outputs.

Each input file is expected to contain lines like:
vtime_id=47655, obj_id=51808988207054848, hit=1

The x-axis uses post-adjustment request order in the result file.

Default mode focuses on the three non-clear-all result files and draws
pairwise diff panels to reduce overlap in the difference view.
"""

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def iter_records(path):
    post_adjust_req_id = 0
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            idx = line.find("vtime_id=")
            if idx == -1:
                continue
            post_adjust_req_id += 1
            parts = line[idx:].strip().split(", ")
            yield (
                post_adjust_req_id,
                int(parts[0].split("=")[1]),
                int(parts[1].split("=")[1]),
                int(parts[2].split("=")[1]),
            )


def load_aligned(result_paths):
    rows = []
    streams = [iter_records(path) for path in result_paths]
    for recs in zip(*streams):
        req_ids = [r[0] for r in recs]
        vtimes = [r[1] for r in recs]
        obj_ids = [r[2] for r in recs]
        hits = [r[3] for r in recs]
        if len(set(req_ids)) != 1 or len(set(vtimes)) != 1 or len(set(obj_ids)) != 1:
            raise ValueError(f"Misaligned records: {recs}")
        rows.append((req_ids[0], vtimes[0], obj_ids[0], hits))
    return rows


def sample_rows(rows, max_points):
    if len(rows) <= max_points:
        return rows
    step = max(1, len(rows) // max_points)
    sampled = rows[::step]
    if sampled[-1][0] != rows[-1][0]:
        sampled.append(rows[-1])
    return sampled


def build_rank_map(rows):
    first_seen = {}
    for req_id, _vtime, obj_id, _hits in rows:
        if obj_id not in first_seen:
            first_seen[obj_id] = req_id
    sorted_obj = sorted(first_seen.items(), key=lambda kv: (kv[1], kv[0]))
    return {obj_id: idx for idx, (obj_id, _req_id) in enumerate(sorted_obj)}


def scatter_hits(ax, req_ids, yvals, hits, title):
    hits = np.asarray(hits)
    req_ids = np.asarray(req_ids)
    yvals = np.asarray(yvals)

    miss_mask = hits == 0
    hit_mask = ~miss_mask

    ax.scatter(req_ids[miss_mask], yvals[miss_mask], s=1.5, c="#d62728", alpha=0.65, label="miss")
    ax.scatter(req_ids[hit_mask], yvals[hit_mask], s=1.5, c="#1f77b4", alpha=0.65, label="hit")
    ax.set_title(title)
    ax.set_ylabel("Object Rank")
    ax.grid(alpha=0.15)


def scatter_pairwise_diff(ax, req_ids, yvals, lhs_hits, rhs_hits, lhs_name, rhs_name):
    req_ids = np.asarray(req_ids)
    yvals = np.asarray(yvals)
    lhs_hits = np.asarray(lhs_hits)
    rhs_hits = np.asarray(rhs_hits)

    lhs_only = (lhs_hits == 1) & (rhs_hits == 0)
    rhs_only = (lhs_hits == 0) & (rhs_hits == 1)

    if np.any(lhs_only):
        ax.scatter(
            req_ids[lhs_only],
            yvals[lhs_only],
            s=1.8,
            c="#b22222",
            alpha=0.75,
            label=f"{lhs_name} hit, {rhs_name} miss",
        )
    if np.any(rhs_only):
        ax.scatter(
            req_ids[rhs_only],
            yvals[rhs_only],
            s=1.8,
            c="#1f77b4",
            alpha=0.75,
            label=f"{lhs_name} miss, {rhs_name} hit",
        )

    ax.set_title(f"{lhs_name} vs {rhs_name}")
    ax.set_xlabel("Post-adjustment Request ID")
    ax.set_ylabel("Object Rank")
    ax.grid(alpha=0.15)
    ax.legend(loc="upper right", markerscale=4, frameon=False)


def scatter_diff_four(ax, req_ids, yvals, h3, h4):
    req_ids = np.asarray(req_ids)
    yvals = np.asarray(yvals)
    h3 = np.asarray(h3)
    h4 = np.asarray(h4)

    patterns = [
        (((h3 == 1) & (h4 == 0)), "#b22222", "result3 hit, clear-all miss"),
        (((h3 == 0) & (h4 == 1)), "#1f77b4", "result3 miss, clear-all hit"),
    ]

    for mask, color, label in patterns:
        if np.any(mask):
            ax.scatter(req_ids[mask], yvals[mask], s=1.8, c=color, alpha=0.75, label=label)

    ax.set_title("result3 vs result4(clear-all)")
    ax.set_xlabel("Post-adjustment Request ID")
    ax.set_ylabel("Object Rank")
    ax.grid(alpha=0.15)
    ax.legend(loc="upper right", markerscale=4, frameon=False)


def summarize(rows):
    stats = defaultdict(int)
    by_obj_loss = defaultdict(int)
    by_vtime_loss = defaultdict(int)
    for _req_id, vtime, obj_id, hits in rows:
        h1, h2, h3 = hits[:3]
        stats[tuple(hits)] += 1
        if h2 == 1 and h3 == 0:
            by_obj_loss[obj_id] += 1
            by_vtime_loss[vtime] += 1
    return stats, by_obj_loss, by_vtime_loss


def plot_compare(rows, output_path, max_points, has_result4):
    sampled = sample_rows(rows, max_points)
    rank_map = build_rank_map(rows)

    req_ids = np.array([row[0] for row in sampled])
    yvals = np.array([rank_map[row[2]] for row in sampled])
    h1 = np.array([row[3][0] for row in sampled])
    h2 = np.array([row[3][1] for row in sampled])
    h3 = np.array([row[3][2] for row in sampled])
    if has_result4:
        h4 = np.array([row[3][3] for row in sampled])
        fig, axes = plt.subplots(4, 2, figsize=(18, 22), sharex=True, constrained_layout=True)
        axes = axes.flat
        scatter_hits(axes[0], req_ids, yvals, h1, "result1: default")
        scatter_hits(axes[1], req_ids, yvals, h2, "result2: best")
        scatter_hits(axes[2], req_ids, yvals, h3, "result3: default -> best")
        scatter_hits(axes[3], req_ids, yvals, h4, "result4: clear-all")
        scatter_pairwise_diff(axes[4], req_ids, yvals, h1, h2, "result1", "result2")
        scatter_pairwise_diff(axes[5], req_ids, yvals, h1, h3, "result1", "result3")
        scatter_pairwise_diff(axes[6], req_ids, yvals, h2, h3, "result2", "result3")
        scatter_diff_four(axes[7], req_ids, yvals, h3, h4)
    else:
        fig, axes = plt.subplots(3, 2, figsize=(18, 16), sharex=True, constrained_layout=True)
        axes = axes.flat
        scatter_hits(axes[0], req_ids, yvals, h1, "result1: default")
        scatter_hits(axes[1], req_ids, yvals, h2, "result2: best")
        scatter_hits(axes[2], req_ids, yvals, h3, "result3: default -> best")
        scatter_pairwise_diff(axes[3], req_ids, yvals, h1, h2, "result1", "result2")
        scatter_pairwise_diff(axes[4], req_ids, yvals, h1, h3, "result1", "result3")
        scatter_pairwise_diff(axes[5], req_ids, yvals, h2, h3, "result2", "result3")
    for ax in axes[: min(4, len(axes))]:
        handles, labels = ax.get_legend_handles_labels()
        if handles:
            ax.legend(loc="upper right", markerscale=4, frameon=False)
    fig.suptitle("Hit/Miss Trajectory Comparison", fontsize=16)
    fig.savefig(output_path, dpi=220)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--result1", default="result1.txt")
    parser.add_argument("--result2", default="result2.txt")
    parser.add_argument("--result3", default="result3.txt")
    parser.add_argument("--result4", default="")
    parser.add_argument(
        "--output",
        default="grid_search/analysis_output/hit_trajectory_compare.png",
    )
    parser.add_argument(
        "--max-points",
        type=int,
        default=300000,
        help="sample requests down to at most this many points for plotting",
    )
    args = parser.parse_args()

    result_paths = [args.result1, args.result2, args.result3]
    has_result4 = bool(args.result4)
    if has_result4:
        result_paths.append(args.result4)
    rows = load_aligned(result_paths)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    plot_compare(rows, output_path, args.max_points, has_result4)

    stats, by_obj_loss, by_vtime_loss = summarize(rows)
    print("Saved figure:", output_path)
    print("Pattern counts:")
    for key, val in sorted(stats.items(), key=lambda kv: kv[1], reverse=True)[:10]:
        print(key, val)

    print("\nTop objects where result2 hit but result3 missed:")
    for obj_id, cnt in sorted(by_obj_loss.items(), key=lambda kv: kv[1], reverse=True)[:15]:
        print(obj_id, cnt)

    print("\nTop vtimes where result2 hit but result3 missed:")
    for vtime, cnt in sorted(by_vtime_loss.items(), key=lambda kv: kv[1], reverse=True)[:15]:
        print(vtime, cnt)


if __name__ == "__main__":
    main()
