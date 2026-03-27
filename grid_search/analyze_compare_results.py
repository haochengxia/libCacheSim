#!/usr/bin/env python3

import argparse
import csv
from collections import Counter, defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def iter_records(path):
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            idx = line.find("vtime_id=")
            if idx == -1:
                continue
            s = line[idx:].strip()
            parts = s.split(", ")
            yield (
                int(parts[0].split("=")[1]),
                int(parts[1].split("=")[1]),
                int(parts[2].split("=")[1]),
            )


def analyze(files, bin_size):
    names = [Path(f).stem for f in files]
    streams = [iter_records(f) for f in files]

    total = [0, 0, 0]
    hits = [0, 0, 0]
    cumulative_hits = [[], [], []]
    outcome_counter = Counter()

    bin_total = []
    bin_hits = [[], [], []]
    by_vtime = defaultdict(lambda: [0, 0, 0, 0])
    obj_patterns = defaultdict(Counter)

    current_bin_total = 0
    current_bin_hits = [0, 0, 0]

    for req_idx, rows in enumerate(zip(*streams), start=1):
        vtimes = [r[0] for r in rows]
        obj_ids = [r[1] for r in rows]
        hs = [r[2] for r in rows]
        if len(set(vtimes)) != 1 or len(set(obj_ids)) != 1:
            raise ValueError(f"Misaligned rows at request {req_idx}: {rows}")

        vtime = vtimes[0]
        obj_id = obj_ids[0]
        key = tuple(hs)

        current_bin_total += 1
        for i, h in enumerate(hs):
            total[i] += 1
            hits[i] += h
            current_bin_hits[i] += h
            cumulative_hits[i].append(hits[i] / total[i])

        outcome_counter[key] += 1
        by_vtime[vtime][0] += 1
        by_vtime[vtime][1] += hs[0]
        by_vtime[vtime][2] += hs[1]
        by_vtime[vtime][3] += hs[2]
        obj_patterns[obj_id][key] += 1

        if current_bin_total == bin_size:
            bin_total.append(current_bin_total)
            for i in range(3):
                bin_hits[i].append(current_bin_hits[i] / current_bin_total)
            current_bin_total = 0
            current_bin_hits = [0, 0, 0]

    if current_bin_total:
        bin_total.append(current_bin_total)
        for i in range(3):
            bin_hits[i].append(current_bin_hits[i] / current_bin_total)

    object_rows = []
    for obj_id, counter in obj_patterns.items():
        total_req = sum(counter.values())
        lose_vs_2 = counter[(1, 1, 0)] + counter[(0, 1, 0)]
        lose_vs_1 = counter[(1, 0, 0)] + counter[(1, 1, 0)]
        gain_vs_2 = counter[(1, 0, 1)] + counter[(0, 0, 1)]
        gain_vs_1 = counter[(0, 0, 1)] + counter[(0, 1, 1)]
        object_rows.append(
            {
                "obj_id": obj_id,
                "requests": total_req,
                "lose_vs_result2": lose_vs_2,
                "lose_vs_result1": lose_vs_1,
                "gain_vs_result2": gain_vs_2,
                "gain_vs_result1": gain_vs_1,
                "patterns": dict(counter),
            }
        )

    vtime_rows = []
    for vtime, vals in by_vtime.items():
        reqs, h1, h2, h3 = vals
        vtime_rows.append(
            {
                "vtime_id": vtime,
                "requests": reqs,
                "result1_hits": h1,
                "result2_hits": h2,
                "result3_hits": h3,
                "result3_minus_result1": h3 - h1,
                "result3_minus_result2": h3 - h2,
            }
        )

    return {
        "names": names,
        "total": total,
        "hits": hits,
        "cumulative_hits": cumulative_hits,
        "bin_hits": bin_hits,
        "bin_total": bin_total,
        "outcome_counter": outcome_counter,
        "object_rows": object_rows,
        "vtime_rows": vtime_rows,
    }


def write_csvs(output_dir, data):
    output_dir.mkdir(parents=True, exist_ok=True)

    summary_path = output_dir / "summary.csv"
    with open(summary_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["result", "requests", "hits", "hit_rate"])
        for name, total, hits in zip(data["names"], data["total"], data["hits"]):
            writer.writerow([name, total, hits, hits / total if total else 0])

    objects_path = output_dir / "top_object_diffs.csv"
    top_objects = sorted(
        data["object_rows"],
        key=lambda row: (row["lose_vs_result2"], row["lose_vs_result1"], row["requests"]),
        reverse=True,
    )
    with open(objects_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "obj_id",
                "requests",
                "lose_vs_result2",
                "lose_vs_result1",
                "gain_vs_result2",
                "gain_vs_result1",
                "patterns",
            ]
        )
        for row in top_objects[:200]:
            writer.writerow(
                [
                    row["obj_id"],
                    row["requests"],
                    row["lose_vs_result2"],
                    row["lose_vs_result1"],
                    row["gain_vs_result2"],
                    row["gain_vs_result1"],
                    row["patterns"],
                ]
            )

    vtime_path = output_dir / "worst_vtimes.csv"
    worst_vtimes = sorted(
        data["vtime_rows"],
        key=lambda row: (row["result3_minus_result2"], row["result3_minus_result1"]),
    )
    with open(vtime_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "vtime_id",
                "requests",
                "result1_hits",
                "result2_hits",
                "result3_hits",
                "result3_minus_result1",
                "result3_minus_result2",
            ]
        )
        for row in worst_vtimes[:200]:
            writer.writerow(
                [
                    row["vtime_id"],
                    row["requests"],
                    row["result1_hits"],
                    row["result2_hits"],
                    row["result3_hits"],
                    row["result3_minus_result1"],
                    row["result3_minus_result2"],
                ]
            )


def plot(output_dir, data, bin_size):
    output_dir.mkdir(parents=True, exist_ok=True)

    fig, axes = plt.subplots(2, 2, figsize=(16, 10), constrained_layout=True)
    ax1, ax2, ax3, ax4 = axes.flat
    colors = ["#4C78A8", "#54A24B", "#E45756"]

    x = np.arange(1, data["total"][0] + 1)
    for i, name in enumerate(data["names"]):
        ax1.plot(x, data["cumulative_hits"][i], label=name, linewidth=1.2, color=colors[i])
    ax1.set_title("Cumulative Hit Rate After Adjustment")
    ax1.set_xlabel("Post-adjustment Request Index")
    ax1.set_ylabel("Hit Rate")
    ax1.grid(alpha=0.25)
    ax1.legend()

    bx = np.arange(1, len(data["bin_total"]) + 1) * bin_size
    for i, name in enumerate(data["names"]):
        ax2.plot(bx, data["bin_hits"][i], label=name, linewidth=1.5, color=colors[i])
    ax2.set_title(f"Binned Hit Rate After Adjustment ({bin_size} requests/bin)")
    ax2.set_xlabel("Post-adjustment Request Index")
    ax2.set_ylabel("Hit Rate")
    ax2.grid(alpha=0.25)

    labels = ["111", "110", "000", "010", "101", "011", "001", "100"]
    counts = [data["outcome_counter"][tuple(int(c) for c in label)] for label in labels]
    ax3.bar(labels, counts, color="#72B7B2")
    ax3.set_title("Per-request Outcome Pattern Counts")
    ax3.set_xlabel("(result1, result2, result3)")
    ax3.set_ylabel("Requests")
    ax3.grid(axis="y", alpha=0.25)

    top_objects = sorted(
        [row for row in data["object_rows"] if row["lose_vs_result2"] > 0],
        key=lambda row: (row["lose_vs_result2"], row["requests"]),
        reverse=True,
    )[:15]
    obj_labels = [str(row["obj_id"])[-8:] for row in top_objects][::-1]
    lose2 = [row["lose_vs_result2"] for row in top_objects][::-1]
    lose1 = [row["lose_vs_result1"] for row in top_objects][::-1]
    ax4.barh(obj_labels, lose2, color="#E45756", label="lose vs result2")
    ax4.barh(obj_labels, lose1, color="#4C78A8", alpha=0.5, label="lose vs result1")
    ax4.set_title("Top Objects Lost By result3")
    ax4.set_xlabel("Hit Loss Count")
    ax4.set_ylabel("Object ID suffix")
    ax4.grid(axis="x", alpha=0.25)
    ax4.legend()

    fig.suptitle("S4FIFO Compare Analysis (Post-adjustment Only)", fontsize=16)
    fig.savefig(output_dir / "compare_analysis.png", dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="Analyze compare.sh result*.txt files.")
    parser.add_argument("--result1", default="result1.txt")
    parser.add_argument("--result2", default="result2.txt")
    parser.add_argument("--result3", default="result3.txt")
    parser.add_argument("--bin-size", type=int, default=10000)
    parser.add_argument("--output-dir", default="grid_search/analysis_output")
    args = parser.parse_args()

    files = [args.result1, args.result2, args.result3]
    data = analyze(files, args.bin_size)
    output_dir = Path(args.output_dir)
    write_csvs(output_dir, data)
    plot(output_dir, data, args.bin_size)

    print("Summary")
    for name, total, hits in zip(data["names"], data["total"], data["hits"]):
        print(f"{name}: requests={total}, hits={hits}, hit_rate={hits / total:.6f}")

    print("\nMost common patterns")
    for pattern, count in data["outcome_counter"].most_common(8):
        print(f"{pattern}: {count}")

    print("\nWorst vtime windows for result3 vs result2")
    for row in sorted(
        data["vtime_rows"],
        key=lambda r: (r["result3_minus_result2"], r["result3_minus_result1"]),
    )[:10]:
        print(row)

    print("\nTop objects lost by result3 vs result2")
    for row in sorted(
        data["object_rows"],
        key=lambda r: (r["lose_vs_result2"], r["lose_vs_result1"], r["requests"]),
        reverse=True,
    )[:15]:
        print(
            {
                "obj_id": row["obj_id"],
                "requests": row["requests"],
                "lose_vs_result2": row["lose_vs_result2"],
                "lose_vs_result1": row["lose_vs_result1"],
                "patterns": row["patterns"],
            }
        )

    print(f"\nArtifacts written to: {output_dir}")


if __name__ == "__main__":
    main()
