#!/usr/bin/env python3
"""
Test script for the online learning S4FIFO cache replacement policy.
Demonstrates the three phases: warmup, feature collection, and online prediction.
"""

import random
import numpy as np
from libcachesim.s4fifo import S4FIFO

def generate_workload(num_requests=50000, working_set_size=1000, temporal_locality=0.7):
    """
    Generate a synthetic workload with configurable characteristics.

    :param num_requests: Total number of requests to generate
    :param working_set_size: Size of the working set
    :param temporal_locality: Probability of accessing recently accessed items
    :return: List of item IDs
    """
    workload = []
    recent_items = []

    for _ in range(num_requests):
        if random.random() < temporal_locality and recent_items:
            # Access recently used item
            item = random.choice(recent_items[-100:])  # Last 100 items
        else:
            # Access random item from working set
            item = random.randint(1, working_set_size)

        workload.append(item)
        recent_items.append(item)

        # Keep recent_items list manageable
        if len(recent_items) > 200:
            recent_items = recent_items[-100:]

    return workload

def run_experiment():
    """Run the S4FIFO online learning experiment."""

    print("=== S4FIFO Online Learning Cache Experiment ===\n")

    # Initialize cache
    cache_size = 500
    cache = S4FIFO(cache_size=cache_size,
                   feature_collection_requests=5000,
                   warmup_threshold=0.9)

    # Generate different workload patterns
    print("Generating workloads...")

    # Phase 1: High temporal locality workload (easy to cache)
    workload1 = generate_workload(num_requests=15000,
                                  working_set_size=800,
                                  temporal_locality=0.8)

    # Phase 2: Medium temporal locality workload
    workload2 = generate_workload(num_requests=15000,
                                  working_set_size=1200,
                                  temporal_locality=0.5)

    # Phase 3: Low temporal locality workload (hard to cache)
    workload3 = generate_workload(num_requests=20000,
                                  working_set_size=2000,
                                  temporal_locality=0.2)

    full_workload = workload1 + workload2 + workload3

    print(f"Generated {len(full_workload)} requests\n")

    # Run the workload
    hit_counts = []
    miss_counts = []
    phase_transitions = []

    for i, item in enumerate(full_workload):
        is_hit = cache.access(item)

        # Record statistics every 1000 requests
        if (i + 1) % 1000 == 0:
            info = cache.get_cache_info()
            hit_counts.append(info['current_hit_rate'])

            # Print progress
            if (i + 1) % 5000 == 0:
                print(f"Request {i+1:5d}: Phase={info['phase']:20s} "
                      f"Hit Rate={info['current_hit_rate']:.3f} "
                      f"Cache Size={info['cache_size']:3d}/{info['max_cache_size']}")

                if info['phase'] == 'FEATURE_COLLECTION':
                    print(f"              Feature Collection Progress: "
                          f"{info['feature_collection_progress']:.1%}")
                elif info['phase'] == 'ONLINE_PREDICTION':
                    print(f"              Hyperparameters: "
                          f"eviction_ratio={info['hyperparameters']['eviction_ratio']:.3f}, "
                          f"freq_threshold={info['hyperparameters']['frequency_threshold']}")
                print()

    # Final results
    final_info = cache.get_cache_info()
    print("\n=== Final Results ===")
    print(f"Final Phase: {final_info['phase']}")
    print(f"Total Requests: {final_info['total_requests']}")
    print(f"Final Hit Rate: {final_info['current_hit_rate']:.3f}")
    print(f"Final Cache Utilization: {final_info['cache_size']}/{final_info['max_cache_size']} "
          f"({final_info['cache_size']/final_info['max_cache_size']:.1%})")

    if hasattr(cache, 'features'):
        print(f"\nExtracted Features:")
        for feature, value in cache.features.items():
            print(f"  {feature}: {value:.3f}")

    print(f"\nFinal Hyperparameters:")
    for param, value in final_info['hyperparameters'].items():
        print(f"  {param}: {value}")

def compare_with_baseline():
    """Compare S4FIFO online learning with simple LRU."""
    print("\n=== Comparison with Baseline LRU ===\n")

    # Simple LRU implementation for comparison
    class SimpleLRU:
        def __init__(self, cache_size):
            self.cache_size = cache_size
            self.cache = []
            self.hits = 0
            self.total = 0

        def access(self, item):
            self.total += 1
            if item in self.cache:
                self.cache.remove(item)
                self.cache.append(item)
                self.hits += 1
                return True
            else:
                if len(self.cache) >= self.cache_size:
                    self.cache.pop(0)
                self.cache.append(item)
                return False

        def get_hit_rate(self):
            return self.hits / self.total if self.total > 0 else 0

    cache_size = 500
    workload = generate_workload(num_requests=30000, working_set_size=1500, temporal_locality=0.6)

    # Test S4FIFO
    s4fifo = S4FIFO(cache_size=cache_size, feature_collection_requests=3000)
    for item in workload:
        s4fifo.access(item)

    # Test LRU
    lru = SimpleLRU(cache_size)
    for item in workload:
        lru.access(item)

    print(f"S4FIFO Hit Rate: {s4fifo.get_cache_info()['current_hit_rate']:.3f}")
    print(f"LRU Hit Rate: {lru.get_hit_rate():.3f}")

    improvement = (s4fifo.get_cache_info()['current_hit_rate'] - lru.get_hit_rate()) / lru.get_hit_rate() * 100
    print(f"Improvement: {improvement:+.1f}%")

if __name__ == "__main__":
    run_experiment()
    compare_with_baseline()
