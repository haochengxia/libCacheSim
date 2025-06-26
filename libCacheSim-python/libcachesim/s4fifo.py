"""
This module implements the S4FIFO cache replacement policy.

It is a online learned S3FIFO policy that uses a simple heuristic to decide the
hyperparameters.
"""

import time
from collections import defaultdict
from enum import Enum

class Phase(Enum):
    WARMUP = 1
    FEATURE_COLLECTION = 2
    ONLINE_PREDICTION = 3

class S4FIFO:
    """
    S4FIFO cache replacement policy with online learning capabilities.
    """

    def __init__(self, cache_size: int, feature_collection_requests: int = 10000,
                 warmup_threshold: float = 0.95):
        """
        Initialize the S4FIFO policy with the given cache size.

        :param cache_size: The size of the cache.
        :param feature_collection_requests: Number of requests for feature collection phase.
        :param warmup_threshold: Threshold for cache fullness to end warmup phase.
        """
        self.cache_size = cache_size
        self.cache = []
        self.access_count = 0

        # Phase management
        self.current_phase = Phase.WARMUP
        self.warmup_threshold = warmup_threshold
        self.feature_collection_requests = feature_collection_requests
        self.feature_collection_count = 0

        # Feature collection
        self.features = {
            'hit_rate': 0.0,
            'miss_rate': 0.0,
            'access_pattern_entropy': 0.0,
            'temporal_locality': 0.0,
            'working_set_size_estimate': 0.0
        }
        self.hit_count = 0
        self.miss_count = 0
        self.access_history = []
        self.item_frequencies = defaultdict(int)
        self.last_access_times = {}

        # Hyperparameters (can be adjusted by model prediction)
        self.eviction_ratio = 0.1  # Fraction of cache to evict when full
        self.frequency_threshold = 2  # Minimum frequency for item promotion

    def warmup(self, items):
        """
        Warm up the cache with a list of items.

        :param items: A list of items to warm up the cache.
        """
        for item in items:
            self.access(item)

    def observe(self, item):
        """
        Observe an item without affecting cache state (for prediction/analysis).

        :param item: The item to observe.
        :return: Whether the item would be a hit or miss.
        """
        return item in self.cache

    def _check_phase_transition(self):
        """Check if we need to transition to the next phase."""
        if self.current_phase == Phase.WARMUP:
            # Transition to feature collection when cache is sufficiently full
            cache_fullness = len(self.cache) / self.cache_size
            if cache_fullness >= self.warmup_threshold:
                self.current_phase = Phase.FEATURE_COLLECTION
                self.feature_collection_count = 0
                print(f"Phase transition: WARMUP -> FEATURE_COLLECTION (cache {cache_fullness:.2%} full)")

        elif self.current_phase == Phase.FEATURE_COLLECTION:
            # Transition to online prediction after collecting enough features
            if self.feature_collection_count >= self.feature_collection_requests:
                self.current_phase = Phase.ONLINE_PREDICTION
                self._extract_features()
                self._predict_hyperparameters()
                print(f"Phase transition: FEATURE_COLLECTION -> ONLINE_PREDICTION")
                print(f"Extracted features: {self.features}")

    def _extract_features(self):
        """Extract features from the collected data during feature collection phase."""
        total_requests = self.hit_count + self.miss_count
        if total_requests > 0:
            self.features['hit_rate'] = self.hit_count / total_requests
            self.features['miss_rate'] = self.miss_count / total_requests

        # Calculate access pattern entropy
        if self.access_history:
            from collections import Counter
            import math
            access_counts = Counter(self.access_history)
            total_accesses = len(self.access_history)
            entropy = 0
            for count in access_counts.values():
                p = count / total_accesses
                if p > 0:
                    entropy -= p * math.log2(p)
            self.features['access_pattern_entropy'] = entropy

        # Estimate working set size
        unique_items = len(set(self.access_history[-1000:]))  # Last 1000 accesses
        self.features['working_set_size_estimate'] = unique_items / self.cache_size

        # Calculate temporal locality
        if len(self.access_history) > 1:
            reaccesses = 0
            window_size = min(100, len(self.access_history))
            for i in range(len(self.access_history) - window_size, len(self.access_history)):
                if self.access_history[i] in self.access_history[max(0, i-window_size):i]:
                    reaccesses += 1
            self.features['temporal_locality'] = reaccesses / window_size if window_size > 0 else 0

    def _predict_hyperparameters(self):
        """
        Use extracted features to predict optimal hyperparameters.
        This is a simple heuristic - in practice, you would use a trained ML model.
        """
        hit_rate = self.features['hit_rate']
        entropy = self.features['access_pattern_entropy']
        temporal_locality = self.features['temporal_locality']

        # Simple heuristic-based hyperparameter adjustment
        if hit_rate < 0.3:  # Low hit rate - more aggressive eviction
            self.eviction_ratio = 0.2
            self.frequency_threshold = 1
        elif hit_rate > 0.7:  # High hit rate - conservative eviction
            self.eviction_ratio = 0.05
            self.frequency_threshold = 3
        else:  # Medium hit rate - balanced approach
            self.eviction_ratio = 0.1
            self.frequency_threshold = 2

        # Adjust based on temporal locality
        if temporal_locality > 0.5:
            self.frequency_threshold = max(1, self.frequency_threshold - 1)

        print(f"Updated hyperparameters: eviction_ratio={self.eviction_ratio}, "
              f"frequency_threshold={self.frequency_threshold}")

    def _collect_features(self, item, is_hit):
        """Collect features during the feature collection phase."""
        if self.current_phase == Phase.FEATURE_COLLECTION:
            self.access_history.append(item)
            self.item_frequencies[item] += 1
            self.last_access_times[item] = self.access_count

            if is_hit:
                self.hit_count += 1
            else:
                self.miss_count += 1

            self.feature_collection_count += 1


    def access(self, item):
        """
        Access an item in the cache with phase-aware behavior.

        :param item: The item to access.
        :return: True if hit, False if miss.
        """
        self.access_count += 1
        is_hit = item in self.cache

        # Check for phase transitions
        self._check_phase_transition()

        # Collect features if in feature collection phase
        self._collect_features(item, is_hit)

        if not is_hit:
            # Cache miss - need to add item
            if len(self.cache) >= self.cache_size:
                self._evict_items()
            self.cache.append(item)
        else:
            # Cache hit - move item to end (LRU behavior)
            self.cache.remove(item)
            self.cache.append(item)

        return is_hit

    def _evict_items(self):
        """
        Evict items from cache based on current hyperparameters and phase.
        """
        if self.current_phase == Phase.WARMUP:
            # Simple FIFO eviction during warmup
            self.cache.pop(0)
        elif self.current_phase == Phase.FEATURE_COLLECTION:
            # FIFO eviction but collect statistics
            self.cache.pop(0)
        else:  # ONLINE_PREDICTION phase
            # Use learned hyperparameters for more sophisticated eviction
            num_to_evict = max(1, int(self.cache_size * self.eviction_ratio))

            # Remove items with low frequency first
            if hasattr(self, 'item_frequencies') and self.item_frequencies:
                # Sort by frequency and recency
                items_with_scores = []
                for i, item in enumerate(self.cache[:num_to_evict * 2]):  # Consider more items
                    freq = self.item_frequencies.get(item, 1)
                    recency_score = (len(self.cache) - i) / len(self.cache)  # Higher for more recent
                    combined_score = freq * 0.7 + recency_score * 0.3
                    items_with_scores.append((combined_score, item))

                # Sort by score (ascending - lower scores evicted first)
                items_with_scores.sort()

                # Evict lowest scoring items
                for _, item in items_with_scores[:num_to_evict]:
                    if item in self.cache:
                        self.cache.remove(item)
            else:
                # Fallback to simple FIFO
                for _ in range(num_to_evict):
                    if self.cache:
                        self.cache.pop(0)

    def get_cache_info(self):
        """
        Get information about current cache state and phase.

        :return: Dictionary with cache information.
        """
        total_requests = self.hit_count + self.miss_count
        current_hit_rate = self.hit_count / total_requests if total_requests > 0 else 0

        return {
            'phase': self.current_phase.name,
            'cache_size': len(self.cache),
            'max_cache_size': self.cache_size,
            'total_requests': self.access_count,
            'current_hit_rate': current_hit_rate,
            'feature_collection_progress': self.feature_collection_count / self.feature_collection_requests if self.current_phase == Phase.FEATURE_COLLECTION else 1.0,
            'hyperparameters': {
                'eviction_ratio': self.eviction_ratio,
                'frequency_threshold': self.frequency_threshold
            }
        }
