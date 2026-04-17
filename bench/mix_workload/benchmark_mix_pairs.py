#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import itertools
import random
import re
import subprocess
import sys
from concurrent.futures import Future, ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


RESULT_RE = re.compile(
    r"cache size\s+(?P<cache_size>[^,]+),\s+"
    r"(?P<n_req>\d+) req, miss ratio (?P<miss_ratio>[0-9.]+),\s+"
    r"throughput (?P<throughput>[0-9.]+) MQPS"
)

DEFAULT_SEED = 20260417


@dataclass(frozen=True)
class BenchResult:
    n_req: int
    miss_ratio: float
    throughput_mqps: float


@dataclass(frozen=True)
class PairEntry:
    pair_id: int
    trace1: Path
    trace2: Path


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[2]
    default_output = repo_root / "bench" / "mix_workload" / "results" / (
        "mix_pairs_table.csv"
    )
    parser = argparse.ArgumentParser(
        description=(
            "Benchmark single-trace and 2-trace mixed workloads and write one CSV "
            "table containing solo and mixed miss ratios."
        )
    )
    parser.add_argument(
        "--trace-dir",
        type=Path,
        default=Path("/mnt/cfs/oracleReuse/sample"),
        help="Directory containing oracleReuse sample traces.",
    )
    parser.add_argument(
        "--trace-glob",
        default="cluster*.oracleGeneral.sample10.zst",
        help="Glob used to enumerate traces inside --trace-dir.",
    )
    parser.add_argument(
        "--num-pairs",
        type=int,
        default=1000,
        help="Number of unique 2-trace workloads to generate.",
    )
    parser.add_argument(
        "--pair-strategy",
        choices=["sample", "first"],
        default="sample",
        help="How to pick pairs from all unique unordered combinations.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=DEFAULT_SEED,
        help="Random seed used when --pair-strategy=sample.",
    )
    parser.add_argument(
        "--algo",
        default="s3fifo",
        help="Cache algorithm passed to cachesim.",
    )
    parser.add_argument(
        "--cache-size",
        default="256MB",
        help="Cache size passed to cachesim.",
    )
    parser.add_argument(
        "--single-trace-type",
        default="oracleGeneral",
        help="Trace type used for individual traces.",
    )
    parser.add_argument(
        "--mix-trace-type",
        default="mix",
        help="Trace type used for mixed pair runs.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=4,
        help="Number of concurrent cachesim processes.",
    )
    parser.add_argument(
        "--num-thread",
        type=int,
        default=1,
        help="--num-thread value passed to each cachesim process.",
    )
    parser.add_argument(
        "--cachesim-path",
        type=Path,
        default=None,
        help="Explicit path to cachesim. Defaults to _build_rel, then _build_dbg.",
    )
    parser.add_argument(
        "--output-csv",
        type=Path,
        default=default_output,
        help="Final CSV containing solo and mixed results.",
    )
    parser.add_argument(
        "--solo-csv",
        type=Path,
        default=None,
        help="Optional path for cached single-trace results.",
    )
    parser.add_argument(
        "--manifest-csv",
        type=Path,
        default=None,
        help="Optional path for the sampled pair manifest.",
    )
    parser.add_argument(
        "--resume",
        action="store_true",
        help="Resume from existing manifest/output CSV files when possible.",
    )
    return parser.parse_args()


def find_cachesim_path(repo_root: Path, explicit_path: Path | None) -> Path:
    if explicit_path is not None:
        explicit_path = explicit_path.expanduser().resolve()
        if not explicit_path.exists():
            raise FileNotFoundError(f"cachesim not found: {explicit_path}")
        return explicit_path

    candidates = [
        repo_root / "_build_rel" / "bin" / "cachesim",
        repo_root / "_build_dbg" / "bin" / "cachesim",
        repo_root / "_build" / "bin" / "cachesim",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        "cannot find cachesim; build libCacheSim first or pass --cachesim-path"
    )


def list_traces(trace_dir: Path, trace_glob: str) -> List[Path]:
    traces = sorted(path.resolve() for path in trace_dir.glob(trace_glob) if path.is_file())
    if not traces:
        raise FileNotFoundError(
            f"no traces found under {trace_dir} with pattern {trace_glob}"
        )
    return traces


def choose_pairs(
    traces: Sequence[Path], num_pairs: int, pair_strategy: str, seed: int
) -> List[Tuple[Path, Path]]:
    all_pairs = list(itertools.combinations(traces, 2))
    if num_pairs > len(all_pairs):
        raise ValueError(
            f"requested {num_pairs} pairs, but only {len(all_pairs)} unique unordered pairs exist"
        )

    if pair_strategy == "first":
        selected = all_pairs[:num_pairs]
    else:
        rng = random.Random(seed)
        indices = rng.sample(range(len(all_pairs)), num_pairs)
        selected = [all_pairs[index] for index in indices]
        selected.sort(key=lambda pair: (pair[0].name, pair[1].name))
    return selected


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def manifest_path_from_output(output_csv: Path) -> Path:
    return output_csv.with_name(output_csv.stem + ".manifest.csv")


def solo_path_from_output(output_csv: Path) -> Path:
    return output_csv.with_name(output_csv.stem + ".solo.csv")


def write_manifest(manifest_csv: Path, pairs: Sequence[Tuple[Path, Path]]) -> None:
    ensure_parent(manifest_csv)
    with manifest_csv.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["pair_id", "trace1_path", "trace2_path"])
        writer.writeheader()
        for index, (trace1, trace2) in enumerate(pairs, start=1):
            writer.writerow(
                {
                    "pair_id": index,
                    "trace1_path": str(trace1),
                    "trace2_path": str(trace2),
                }
            )


def load_manifest(manifest_csv: Path) -> List[PairEntry]:
    entries: List[PairEntry] = []
    with manifest_csv.open(newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            entries.append(
                PairEntry(
                    pair_id=int(row["pair_id"]),
                    trace1=Path(row["trace1_path"]),
                    trace2=Path(row["trace2_path"]),
                )
            )
    if not entries:
        raise ValueError(f"manifest is empty: {manifest_csv}")
    return entries


def load_solo_cache(solo_csv: Path) -> Dict[str, BenchResult]:
    if not solo_csv.exists():
        return {}

    results: Dict[str, BenchResult] = {}
    with solo_csv.open(newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            results[row["trace_path"]] = BenchResult(
                n_req=int(row["n_req"]),
                miss_ratio=float(row["miss_ratio"]),
                throughput_mqps=float(row["throughput_mqps"]),
            )
    return results


def save_solo_cache(solo_csv: Path, results: Dict[str, BenchResult]) -> None:
    ensure_parent(solo_csv)
    with solo_csv.open("w", newline="") as handle:
        fieldnames = [
            "trace_name",
            "trace_path",
            "n_req",
            "miss_ratio",
            "throughput_mqps",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for trace_path in sorted(results):
            result = results[trace_path]
            writer.writerow(
                {
                    "trace_name": Path(trace_path).name,
                    "trace_path": trace_path,
                    "n_req": result.n_req,
                    "miss_ratio": f"{result.miss_ratio:.6f}",
                    "throughput_mqps": f"{result.throughput_mqps:.4f}",
                }
            )


def load_completed_pair_ids(output_csv: Path) -> set[int]:
    if not output_csv.exists():
        return set()

    completed: set[int] = set()
    with output_csv.open(newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            completed.add(int(row["pair_id"]))
    return completed


def sort_pair_table(output_csv: Path) -> None:
    if not output_csv.exists():
        return

    with output_csv.open(newline="") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
        fieldnames = reader.fieldnames

    if not rows or fieldnames is None:
        return

    rows.sort(key=lambda row: int(row["pair_id"]))
    with output_csv.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def parse_bench_output(stdout_text: str) -> BenchResult:
    matched_result: BenchResult | None = None
    for line in stdout_text.splitlines():
        match = RESULT_RE.search(line)
        if match is None:
            continue
        matched_result = BenchResult(
            n_req=int(match.group("n_req")),
            miss_ratio=float(match.group("miss_ratio")),
            throughput_mqps=float(match.group("throughput")),
        )

    if matched_result is None:
        raise ValueError(f"failed to parse cachesim output:\n{stdout_text}")
    return matched_result


def run_cachesim(
    cachesim_path: Path,
    trace_arg: str,
    trace_type: str,
    algo: str,
    cache_size: str,
    num_thread: int,
) -> BenchResult:
    cmd = [
        str(cachesim_path),
        trace_arg,
        trace_type,
        algo,
        cache_size,
        "--num-thread",
        str(num_thread),
        "--print-head-req=false",
        "--verbose=0",
        "-o",
        "/dev/null",
    ]
    process = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError(
            "cachesim failed with exit code "
            f"{process.returncode} for trace={trace_arg}\n{process.stdout}"
        )
    return parse_bench_output(process.stdout)


def benchmark_solo_traces(
    cachesim_path: Path,
    traces: Iterable[Path],
    algo: str,
    cache_size: str,
    single_trace_type: str,
    num_thread: int,
    jobs: int,
    cached_results: Dict[str, BenchResult],
) -> Dict[str, BenchResult]:
    trace_list = sorted(traces)
    missing = [trace for trace in trace_list if str(trace) not in cached_results]
    if not missing:
        return cached_results

    print(f"[solo] benchmarking {len(missing)} uncached traces", flush=True)
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        future_map: Dict[Future[BenchResult], Path] = {}
        for trace in missing:
            future = executor.submit(
                run_cachesim,
                cachesim_path,
                str(trace),
                single_trace_type,
                algo,
                cache_size,
                num_thread,
            )
            future_map[future] = trace

        completed = 0
        total = len(future_map)
        for future in as_completed(future_map):
            trace = future_map[future]
            cached_results[str(trace)] = future.result()
            completed += 1
            print(
                f"[solo] completed {completed}/{total}: {trace.name}",
                flush=True,
            )
    return cached_results


def pair_rows_fieldnames() -> List[str]:
    return [
        "pair_id",
        "pair_strategy",
        "seed",
        "algo",
        "cache_size",
        "trace1",
        "trace2",
        "trace1_path",
        "trace2_path",
        "trace1_n_req",
        "trace1_miss_ratio",
        "trace1_throughput_mqps",
        "trace2_n_req",
        "trace2_miss_ratio",
        "trace2_throughput_mqps",
        "mix_n_req",
        "mix_miss_ratio",
        "mix_throughput_mqps",
    ]


def open_pair_writer(output_csv: Path, append: bool) -> tuple[object, csv.DictWriter]:
    ensure_parent(output_csv)
    mode = "a" if append and output_csv.exists() else "w"
    handle = output_csv.open(mode, newline="")
    writer = csv.DictWriter(handle, fieldnames=pair_rows_fieldnames())
    if mode == "w":
        writer.writeheader()
    return handle, writer


def benchmark_pairs(
    cachesim_path: Path,
    pairs: Sequence[PairEntry],
    solo_results: Dict[str, BenchResult],
    output_csv: Path,
    mix_trace_type: str,
    algo: str,
    cache_size: str,
    pair_strategy: str,
    seed: int,
    num_thread: int,
    jobs: int,
    completed_pair_ids: set[int],
) -> None:
    pending_pairs = [pair for pair in pairs if pair.pair_id not in completed_pair_ids]
    if not pending_pairs:
        print("[pair] all requested pairs are already present in the output CSV", flush=True)
        return

    print(f"[pair] benchmarking {len(pending_pairs)} remaining mixed workloads", flush=True)
    handle, writer = open_pair_writer(output_csv, append=bool(completed_pair_ids))
    try:
        with ThreadPoolExecutor(max_workers=jobs) as executor:
            future_map: Dict[Future[BenchResult], PairEntry] = {}
            for pair in pending_pairs:
                mix_arg = f"{pair.trace1},{pair.trace2}"
                future = executor.submit(
                    run_cachesim,
                    cachesim_path,
                    mix_arg,
                    mix_trace_type,
                    algo,
                    cache_size,
                    num_thread,
                )
                future_map[future] = pair

            completed = 0
            total = len(future_map)
            for future in as_completed(future_map):
                pair = future_map[future]
                mix_result = future.result()
                solo1 = solo_results[str(pair.trace1)]
                solo2 = solo_results[str(pair.trace2)]
                writer.writerow(
                    {
                        "pair_id": pair.pair_id,
                        "pair_strategy": pair_strategy,
                        "seed": seed,
                        "algo": algo,
                        "cache_size": cache_size,
                        "trace1": pair.trace1.name,
                        "trace2": pair.trace2.name,
                        "trace1_path": str(pair.trace1),
                        "trace2_path": str(pair.trace2),
                        "trace1_n_req": solo1.n_req,
                        "trace1_miss_ratio": f"{solo1.miss_ratio:.6f}",
                        "trace1_throughput_mqps": f"{solo1.throughput_mqps:.4f}",
                        "trace2_n_req": solo2.n_req,
                        "trace2_miss_ratio": f"{solo2.miss_ratio:.6f}",
                        "trace2_throughput_mqps": f"{solo2.throughput_mqps:.4f}",
                        "mix_n_req": mix_result.n_req,
                        "mix_miss_ratio": f"{mix_result.miss_ratio:.6f}",
                        "mix_throughput_mqps": f"{mix_result.throughput_mqps:.4f}",
                    }
                )
                handle.flush()
                completed += 1
                print(
                    f"[pair] completed {completed}/{total}: {pair.trace1.name} + {pair.trace2.name}",
                    flush=True,
                )
    finally:
        handle.close()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[2]
    cachesim_path = find_cachesim_path(repo_root, args.cachesim_path)
    output_csv = args.output_csv.resolve()
    solo_csv = (
        args.solo_csv.resolve() if args.solo_csv is not None else solo_path_from_output(output_csv)
    )
    manifest_csv = (
        args.manifest_csv.resolve()
        if args.manifest_csv is not None
        else manifest_path_from_output(output_csv)
    )

    traces = list_traces(args.trace_dir.resolve(), args.trace_glob)
    print(f"found {len(traces)} traces under {args.trace_dir}", flush=True)

    if args.resume and manifest_csv.exists():
        pairs = load_manifest(manifest_csv)
        print(f"loaded manifest from {manifest_csv}", flush=True)
    else:
        selected_pairs = choose_pairs(traces, args.num_pairs, args.pair_strategy, args.seed)
        write_manifest(manifest_csv, selected_pairs)
        pairs = load_manifest(manifest_csv)
        print(f"wrote manifest to {manifest_csv}", flush=True)

    selected_traces = {pair.trace1 for pair in pairs} | {pair.trace2 for pair in pairs}
    solo_results = load_solo_cache(solo_csv) if args.resume else {}
    solo_results = benchmark_solo_traces(
        cachesim_path,
        selected_traces,
        args.algo,
        args.cache_size,
        args.single_trace_type,
        args.num_thread,
        args.jobs,
        solo_results,
    )
    save_solo_cache(solo_csv, solo_results)
    print(f"wrote solo cache to {solo_csv}", flush=True)

    completed_pair_ids = load_completed_pair_ids(output_csv) if args.resume else set()
    benchmark_pairs(
        cachesim_path,
        pairs,
        solo_results,
        output_csv,
        args.mix_trace_type,
        args.algo,
        args.cache_size,
        args.pair_strategy,
        args.seed,
        args.num_thread,
        args.jobs,
        completed_pair_ids,
    )
    sort_pair_table(output_csv)
    print(f"wrote pair table to {output_csv}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())