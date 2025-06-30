from __future__ import annotations

from ._libcachesim import (
    Cache,
    Reader,
    Request,
    __doc__,
    __version__,
    open_trace,
    process_trace,
    process_trace_python_hook,
)
from .eviction import (
    ARC,
    FIFO,
    LRB,
    LRU,
    S3FIFO,
    Clock,
    Sieve,
    ThreeLCache,
    TinyLFU,
    TwoQ,
    PythonHookCachePolicy,
)

from .const import HF_CACHE_DIR
from .dataset import get_trace_file_lists, get_trace_file_path

__all__ = [
    "ARC",
    "FIFO",
    "LRB",
    "LRU",
    "S3FIFO",
    "Cache",
    "Clock",
    "Reader",
    "Request",
    "S4FIFO",
    "Sieve",
    "ThreeLCache",
    "TinyLFU",
    "TraceType",
    "TwoQ",
    "PythonHookCachePolicy",
    "__doc__",
    "__version__",
    "open_trace",
    "process_trace",
    "process_trace_python_hook",
    # TODO(haocheng): add more eviction policies
]
