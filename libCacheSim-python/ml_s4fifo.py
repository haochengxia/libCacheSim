"""
Advanced S4FIFO with machine learning model integration.
This module shows how to integrate a trained ML model for hyperparameter prediction.
"""

import pickle
import numpy as np
from sklearn.ensemble import RandomForestRegressor
from sklearn.preprocessing import StandardScaler
from collections import defaultdict, deque
import json
import os

class MLEnhancedS4FIFO:
    """
    S4FIFO with machine learning model for hyperparameter prediction.
    """

    def __init__(self, cache_size: int, feature_collection_requests: int = 10000,
                 model_path: str = None, warmup_threshold: float = 0.95):
        """
        Initialize ML-enhanced S4FIFO.

        :param cache_size: Size of the cache
        :param feature_collection_requests: Requests for feature collection
        :param model_path: Path to pre-trained ML model
        :param warmup_threshold: Cache fullness threshold for warmup completion
        """
        self.cache_size = cache_size
        self.cache = []
        self.access_count = 0

        # Phase management
        self.current_phase = "warmup"
        self.warmup_threshold = warmup_threshold
        self.feature_collection_requests = feature_collection_requests
        self.feature_collection_count = 0

        # Advanced feature collection
        self.features = {}
        self.hit_count = 0
        self.miss_count = 0
        self.access_history = deque(maxlen=10000)
        self.item_frequencies = defaultdict(int)
        self.last_access_times = {}
        self.inter_arrival_times = []
        self.unique_items_sliding_window = deque(maxlen=1000)

        # ML model components
        self.model = None
        self.scaler = StandardScaler()
        self.model_path = model_path
        self.training_data = []

        # Hyperparameters with bounds
        self.hyperparams = {
            'eviction_ratio': 0.1,  # [0.01, 0.5]
            'frequency_threshold': 2,  # [1, 10]
            'recency_weight': 0.7,  # [0.1, 0.9]
            'frequency_weight': 0.3   # [0.1, 0.9]
        }

        # Load pre-trained model if available
        if model_path and os.path.exists(model_path):
            self._load_model()

    def _extract_advanced_features(self):
        """Extract comprehensive features for ML model."""
        total_requests = self.hit_count + self.miss_count

        if total_requests == 0:
            return None

        # Basic performance metrics
        hit_rate = self.hit_count / total_requests
        miss_rate = self.miss_count / total_requests

        # Access pattern analysis
        if len(self.access_history) > 1:
            # Entropy of access pattern
            from collections import Counter
            import math
            access_counts = Counter(list(self.access_history))
            total_accesses = len(self.access_history)
            entropy = 0
            for count in access_counts.values():
                p = count / total_accesses
                if p > 0:
                    entropy -= p * math.log2(p)
        else:
            entropy = 0

        # Working set size estimation
        unique_recent = len(set(list(self.access_history)[-1000:]))
        working_set_ratio = unique_recent / self.cache_size

        # Temporal locality metrics
        reuse_distance = self._calculate_reuse_distance()
        temporal_locality = self._calculate_temporal_locality()

        # Frequency distribution metrics
        freq_stats = self._calculate_frequency_stats()

        # Cache utilization patterns
        cache_utilization = len(self.cache) / self.cache_size

        features = {
            'hit_rate': hit_rate,
            'miss_rate': miss_rate,
            'access_pattern_entropy': entropy,
            'working_set_ratio': working_set_ratio,
            'avg_reuse_distance': reuse_distance,
            'temporal_locality': temporal_locality,
            'freq_variance': freq_stats['variance'],
            'freq_skewness': freq_stats['skewness'],
            'cache_utilization': cache_utilization,
            'request_rate': len(self.access_history) / max(1, self.access_count / 1000),  # requests per 1000 accesses
        }

        return features

    def _calculate_reuse_distance(self):
        """Calculate average reuse distance."""
        if len(self.access_history) < 2:
            return float('inf')

        reuse_distances = []
        item_positions = {}

        for i, item in enumerate(self.access_history):
            if item in item_positions:
                distance = i - item_positions[item]
                reuse_distances.append(distance)
            item_positions[item] = i

        return np.mean(reuse_distances) if reuse_distances else float('inf')

    def _calculate_temporal_locality(self):
        """Calculate temporal locality score."""
        if len(self.access_history) < 100:
            return 0.0

        window_size = min(50, len(self.access_history) // 2)
        recent_accesses = list(self.access_history)[-window_size:]

        locality_score = 0
        for i in range(1, len(recent_accesses)):
            # Check if current item was accessed recently
            if recent_accesses[i] in recent_accesses[max(0, i-10):i]:
                locality_score += 1

        return locality_score / max(1, len(recent_accesses) - 1)

    def _calculate_frequency_stats(self):
        """Calculate frequency distribution statistics."""
        if not self.item_frequencies:
            return {'variance': 0, 'skewness': 0}

        frequencies = list(self.item_frequencies.values())
        mean_freq = np.mean(frequencies)
        variance = np.var(frequencies)

        # Calculate skewness
        if variance > 0:
            skewness = np.mean([(f - mean_freq) ** 3 for f in frequencies]) / (variance ** 1.5)
        else:
            skewness = 0

        return {'variance': variance, 'skewness': skewness}

    def _predict_hyperparameters(self):
        """Use ML model to predict optimal hyperparameters."""
        features = self._extract_advanced_features()
        if features is None:
            return

        self.features = features

        if self.model is not None:
            # Use trained model for prediction
            feature_vector = np.array([list(features.values())]).reshape(1, -1)
            feature_vector_scaled = self.scaler.transform(feature_vector)

            # Predict hyperparameters
            predictions = self.model.predict(feature_vector_scaled)[0]

            # Map predictions to hyperparameters with bounds
            self.hyperparams['eviction_ratio'] = np.clip(predictions[0], 0.01, 0.5)
            self.hyperparams['frequency_threshold'] = max(1, int(predictions[1]))
            self.hyperparams['recency_weight'] = np.clip(predictions[2], 0.1, 0.9)
            self.hyperparams['frequency_weight'] = 1.0 - self.hyperparams['recency_weight']

        else:
            # Use heuristic-based prediction
            self._heuristic_hyperparameter_adjustment()

        print(f"Updated hyperparameters based on ML prediction:")
        for param, value in self.hyperparams.items():
            print(f"  {param}: {value:.3f}")

    def _heuristic_hyperparameter_adjustment(self):
        """Fallback heuristic when no ML model is available."""
        hit_rate = self.features.get('hit_rate', 0.5)
        entropy = self.features.get('access_pattern_entropy', 1.0)
        temporal_locality = self.features.get('temporal_locality', 0.5)

        # Adjust based on hit rate
        if hit_rate < 0.3:
            self.hyperparams['eviction_ratio'] = 0.2
            self.hyperparams['frequency_threshold'] = 1
        elif hit_rate > 0.7:
            self.hyperparams['eviction_ratio'] = 0.05
            self.hyperparams['frequency_threshold'] = 4

        # Adjust based on temporal locality
        if temporal_locality > 0.6:
            self.hyperparams['recency_weight'] = 0.8
        else:
            self.hyperparams['recency_weight'] = 0.5

        self.hyperparams['frequency_weight'] = 1.0 - self.hyperparams['recency_weight']

    def access(self, item):
        """Access item with ML-enhanced caching logic."""
        self.access_count += 1
        is_hit = item in self.cache

        # Update tracking data
        self.access_history.append(item)
        self.item_frequencies[item] += 1
        self.last_access_times[item] = self.access_count

        # Phase management
        if self.current_phase == "warmup":
            if len(self.cache) / self.cache_size >= self.warmup_threshold:
                self.current_phase = "feature_collection"
                print("Phase: warmup -> feature_collection")
        elif self.current_phase == "feature_collection":
            self.feature_collection_count += 1
            if self.feature_collection_count >= self.feature_collection_requests:
                self.current_phase = "online_prediction"
                self._predict_hyperparameters()
                print("Phase: feature_collection -> online_prediction")

        # Collect statistics
        if is_hit:
            self.hit_count += 1
        else:
            self.miss_count += 1

        # Cache management
        if not is_hit:
            if len(self.cache) >= self.cache_size:
                self._smart_eviction()
            self.cache.append(item)
        else:
            # Move to end (LRU-like behavior)
            self.cache.remove(item)
            self.cache.append(item)

        return is_hit

    def _smart_eviction(self):
        """Advanced eviction using learned hyperparameters."""
        if self.current_phase != "online_prediction":
            # Simple FIFO during warmup and feature collection
            self.cache.pop(0)
            return

        num_to_evict = max(1, int(self.cache_size * self.hyperparams['eviction_ratio']))

        # Score items for eviction
        item_scores = []
        current_time = self.access_count

        for i, item in enumerate(self.cache):
            frequency = self.item_frequencies.get(item, 1)
            last_access = self.last_access_times.get(item, 0)
            recency = max(1, current_time - last_access + 1)

            # Combine frequency and recency with learned weights
            freq_score = min(frequency, 10) / 10.0  # Normalize frequency
            recency_score = 1.0 / recency  # Higher score for recent items

            combined_score = (self.hyperparams['frequency_weight'] * freq_score +
                            self.hyperparams['recency_weight'] * recency_score)

            item_scores.append((combined_score, item))

        # Sort by score (ascending) and evict lowest scoring items
        item_scores.sort()
        for _, item in item_scores[:num_to_evict]:
            if item in self.cache:
                self.cache.remove(item)

    def save_training_data(self, filename):
        """Save collected features and performance data for model training."""
        training_entry = {
            'features': self.features,
            'hyperparameters': self.hyperparams.copy(),
            'performance': {
                'hit_rate': self.hit_count / max(1, self.hit_count + self.miss_count),
                'cache_utilization': len(self.cache) / self.cache_size
            }
        }

        # Save to file
        if os.path.exists(filename):
            with open(filename, 'r') as f:
                data = json.load(f)
        else:
            data = []

        data.append(training_entry)

        with open(filename, 'w') as f:
            json.dump(data, f, indent=2)

    def _load_model(self):
        """Load pre-trained ML model."""
        try:
            with open(self.model_path, 'rb') as f:
                model_data = pickle.load(f)
                self.model = model_data['model']
                self.scaler = model_data['scaler']
            print(f"Loaded ML model from {self.model_path}")
        except Exception as e:
            print(f"Failed to load model: {e}")
            self.model = None

    def get_status(self):
        """Get detailed status information."""
        total_requests = self.hit_count + self.miss_count
        hit_rate = self.hit_count / max(1, total_requests)

        return {
            'phase': self.current_phase,
            'cache_size': len(self.cache),
            'max_cache_size': self.cache_size,
            'total_requests': self.access_count,
            'hit_rate': hit_rate,
            'hyperparameters': self.hyperparams.copy(),
            'features': self.features.copy() if hasattr(self, 'features') else {},
            'model_loaded': self.model is not None
        }
