from huggingface_hub import hf_hub_download, list_repo_files

from .const import DATASET_NAME, HF_CACHE_DIR


def get_trace_file_lists(trace_set_name: str) -> list[str]:
    """
    Get the list of trace files in the dataset.
    """
    repo_files = list_repo_files(repo_id=DATASET_NAME, repo_type="dataset")
    # Apply keyword filter
    keyword = trace_set_name.lower()
    return [file for file in repo_files if keyword in file.lower()]

def get_trace_file_path(trace_file_name: str) -> str:
    """
    Get the localpath of a trace file in the dataset. Note, it will download the file to the cache directory.
    """
    return hf_hub_download(
        repo_id=DATASET_NAME, repo_type="dataset", filename=trace_file_name, cache_dir=HF_CACHE_DIR)
