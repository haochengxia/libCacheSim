import pytest
import os

from libcachesim import Cache, Reader, Request, create_cache, open_trace, TraceType
from libcachesim import (
    FIFO, ARC, Clock, LRB, LRU, S3FIFO, Sieve, ThreeLCache,
    TinyLFU, TwoQ,
)

from tests.utils import get_reference_data


@pytest.mark.parametrize("eviction_algo", [
    FIFO,
    ARC,
    Clock,
    LRB,
    LRU,
    S3FIFO,
    Sieve,
    ThreeLCache,
    TinyLFU,
    TwoQ,
])
@pytest.mark.parametrize("cache_size_ratio", [0.01, 0.1])
def test_eviction_algo(eviction_algo, cache_size_ratio, mock_reader):
    # create a cache with the eviction policy
    cache = eviction_algo(cache_size=int(mock_reader.get_wss()*cache_size_ratio))
    req_count = 0
    miss_count = 0
    for req in mock_reader:
        hit = cache.get(req)
        if not hit:
            miss_count += 1
        req_count += 1
    
    miss_ratio = miss_count / req_count
    reference_miss_ratio = get_reference_data(eviction_algo.__name__, cache_size_ratio)
    if reference_miss_ratio is None:
        pytest.skip(f"No reference data for {eviction_algo.__name__} with cache size ratio {cache_size_ratio}")
    assert abs(miss_ratio - reference_miss_ratio) < 0.01, f"Miss ratio {miss_ratio} is not close to reference {reference_miss_ratio}"


@pytest.mark.parametrize("eviction_algo", [
    "FIFO",
    "ARC",
    "Clock",
    "LRB",
    "LRU",
    "S3FIFO",
    "Sieve",
    "3LCache",
    "TinyLFU",
    "TwoQ",
])
@pytest.mark.parametrize("cache_size_ratio", [0.01, 0.1])
def test_eviction_algo_generic(eviction_algo, cache_size_ratio, mock_reader):
    cache = create_cache(eviction_algo=eviction_algo,
                cache_size=int(mock_reader.get_wss()*cache_size_ratio),
                eviction_params="",
                consider_obj_metadata=False)
    req_count = 0
    miss_count = 0
    for req in mock_reader:
        hit = cache.get(req)
        if not hit:
            miss_count += 1
        req_count += 1

    miss_ratio = miss_count / req_count
    reference_miss_ratio = get_reference_data(eviction_algo, cache_size_ratio)
    if reference_miss_ratio is None:
        pytest.skip(f"No reference data for {eviction_algo} with cache size ratio {cache_size_ratio}")
    assert abs(miss_ratio - reference_miss_ratio) < 0.01, f"Miss ratio {miss_ratio} is not close to reference {reference_miss_ratio}"
