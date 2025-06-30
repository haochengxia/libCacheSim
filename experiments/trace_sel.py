"""
Trace selection
"""

from libcachesim import get_trace_file_lists
import pandas as pd


trace_set_names = ["alibabaBlock", "cloudphysics", "metaCDN", "metaKV", "metaStorage", "msr", "tencentBlock", "tencentPhoto", "twitter", "wiki", "cdn1", "cdn2"]  # fiu and systor are not available


def select_traces():
    df = pd.DataFrame(columns=["trace_set_name", "trace_file_name"])
    trace_set_files = []
    for trace_set_name in trace_set_names:
        try:
            trace_set_files.append(get_trace_file_lists(trace_set_name))
            current_trace_set_files = trace_set_files[-1]
            current_trace_set_files = [trace_file_name for trace_file_name in current_trace_set_files if ("1K" not in trace_file_name and "10K" not in trace_file_name and "100K" not in trace_file_name)]
            for trace_file_name in current_trace_set_files[:200]:
                new_row_df = pd.DataFrame([{"trace_set_name": trace_set_name, "trace_file_name": trace_file_name}])
                df = pd.concat([df, new_row_df], ignore_index=True)
            print(f"Trace set {trace_set_name} has {len(current_trace_set_files)} traces, selected {len(current_trace_set_files[:200])} traces")
        except FileNotFoundError:
            print(f"Trace set {trace_set_name} not found")
            continue
    df.to_csv("trace_set_files.csv", index=False)
    return trace_set_files


if __name__ == "__main__":
    select_traces()
