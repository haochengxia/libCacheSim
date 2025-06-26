#!/usr/bin/env python3
"""
Train a machine learning model for S4FIFO hyperparameter prediction.
This script generates training data and trains a model to predict optimal hyperparameters.
"""

import numpy as np
import json
import pickle
from sklearn.ensemble import RandomForestRegressor
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import mean_squared_error, r2_score
import matplotlib.pyplot as plt
from ml_s4fifo import MLEnhancedS4FIFO
import random

def generate_diverse_workloads():
    """Generate diverse workloads for training data collection."""
    workload_configs = [
        # High temporal locality, small working set
        {'requests': 20000, 'working_set': 500, 'temporal_locality': 0.8, 'zipf_alpha': 1.2},
        # Medium temporal locality, medium working set
        {'requests': 20000, 'working_set': 1000, 'temporal_locality': 0.6, 'zipf_alpha': 1.0},
        # Low temporal locality, large working set
        {'requests': 20000, 'working_set': 2000, 'temporal_locality': 0.3, 'zipf_alpha': 0.8},
        # Uniform access pattern
        {'requests': 20000, 'working_set': 1500, 'temporal_locality': 0.1, 'zipf_alpha': 0.0},
        # Highly skewed access pattern
        {'requests': 20000, 'working_set': 800, 'temporal_locality': 0.9, 'zipf_alpha': 1.5},
    ]

    workloads = []

    for config in workload_configs:
        workload = generate_synthetic_workload(**config)
        workloads.append((workload, config))

    return workloads

def generate_synthetic_workload(requests, working_set, temporal_locality, zipf_alpha):
    """Generate synthetic workload with specified characteristics."""
    workload = []
    recent_items = []

    # Generate Zipf distribution if alpha > 0
    if zipf_alpha > 0:
        zipf_weights = [1.0 / (i ** zipf_alpha) for i in range(1, working_set + 1)]
        zipf_weights = np.array(zipf_weights)
        zipf_weights = zipf_weights / zipf_weights.sum()
    else:
        zipf_weights = None

    for _ in range(requests):
        if random.random() < temporal_locality and recent_items:
            # Access recently used item
            item = random.choice(recent_items[-50:])
        else:
            # Access item based on popularity distribution
            if zipf_weights is not None:
                item = np.random.choice(range(1, working_set + 1), p=zipf_weights)
            else:
                item = random.randint(1, working_set)

        workload.append(item)
        recent_items.append(item)

        # Keep recent items list manageable
        if len(recent_items) > 100:
            recent_items = recent_items[-50:]

    return workload

def collect_training_data():
    """Collect training data by running different cache configurations on diverse workloads."""
    training_data = []
    workloads = generate_diverse_workloads()

    # Different cache sizes to test
    cache_sizes = [200, 500, 1000]

    # Hyperparameter combinations to test
    hyperparameter_configs = [
        {'eviction_ratio': 0.05, 'frequency_threshold': 3, 'recency_weight': 0.8},
        {'eviction_ratio': 0.1, 'frequency_threshold': 2, 'recency_weight': 0.7},
        {'eviction_ratio': 0.2, 'frequency_threshold': 1, 'recency_weight': 0.6},
        {'eviction_ratio': 0.15, 'frequency_threshold': 4, 'recency_weight': 0.9},
        {'eviction_ratio': 0.3, 'frequency_threshold': 1, 'recency_weight': 0.5},
    ]

    total_experiments = len(workloads) * len(cache_sizes) * len(hyperparameter_configs)
    experiment_count = 0

    print(f"Collecting training data from {total_experiments} experiments...")

    for workload, workload_config in workloads:
        for cache_size in cache_sizes:
            for hyperparam_config in hyperparameter_configs:
                experiment_count += 1
                print(f"Experiment {experiment_count}/{total_experiments}: "
                      f"cache_size={cache_size}, workload={workload_config['working_set']}")

                # Create cache with specific hyperparameters
                cache = MLEnhancedS4FIFO(cache_size=cache_size,
                                       feature_collection_requests=min(5000, len(workload)//4))

                # Manually set hyperparameters for this experiment
                cache.hyperparams = hyperparam_config.copy()
                cache.hyperparams['frequency_weight'] = 1.0 - hyperparam_config['recency_weight']

                # Run workload
                for item in workload:
                    cache.access(item)

                # Extract features and performance metrics
                status = cache.get_status()

                if status['features']:  # Only collect if features were extracted
                    training_entry = {
                        'features': status['features'],
                        'hyperparameters': [
                            hyperparam_config['eviction_ratio'],
                            hyperparam_config['frequency_threshold'],
                            hyperparam_config['recency_weight']
                        ],
                        'performance': status['hit_rate'],
                        'workload_config': workload_config,
                        'cache_size': cache_size
                    }
                    training_data.append(training_entry)

    print(f"Collected {len(training_data)} training samples")
    return training_data

def train_model(training_data):
    """Train a machine learning model to predict optimal hyperparameters."""

    # Prepare features and targets
    X = []  # Features
    y = []  # Target hyperparameters (we'll optimize for hit rate)

    # Group by feature similarity and find best hyperparameters for each group
    feature_groups = {}

    for entry in training_data:
        features = entry['features']
        feature_key = tuple(round(v, 2) for v in features.values())  # Round for grouping

        if feature_key not in feature_groups:
            feature_groups[feature_key] = []

        feature_groups[feature_key].append(entry)

    # For each feature group, find the hyperparameters that gave the best performance
    for feature_key, group_entries in feature_groups.items():
        if len(group_entries) > 1:  # Only use groups with multiple samples
            # Find best performing hyperparameters
            best_entry = max(group_entries, key=lambda x: x['performance'])

            X.append(list(best_entry['features'].values()))
            y.append(best_entry['hyperparameters'])

    X = np.array(X)
    y = np.array(y)

    print(f"Training with {len(X)} feature vectors")
    print(f"Feature dimensions: {X.shape[1]}")
    print(f"Target dimensions: {y.shape[1]}")

    # Scale features
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)

    # Split data
    X_train, X_test, y_train, y_test = train_test_split(
        X_scaled, y, test_size=0.2, random_state=42
    )

    # Train model
    model = RandomForestRegressor(
        n_estimators=100,
        max_depth=10,
        min_samples_split=5,
        min_samples_leaf=2,
        random_state=42
    )

    model.fit(X_train, y_train)

    # Evaluate model
    y_pred = model.predict(X_test)
    mse = mean_squared_error(y_test, y_pred)
    r2 = r2_score(y_test, y_pred)

    print(f"\nModel Performance:")
    print(f"Mean Squared Error: {mse:.4f}")
    print(f"R² Score: {r2:.4f}")

    # Feature importance
    feature_names = list(training_data[0]['features'].keys())
    importance = model.feature_importances_

    print(f"\nFeature Importance:")
    for i, (name, imp) in enumerate(zip(feature_names, importance)):
        print(f"  {name}: {imp:.3f}")

    return model, scaler, feature_names

def save_model(model, scaler, feature_names, filename):
    """Save the trained model."""
    model_data = {
        'model': model,
        'scaler': scaler,
        'feature_names': feature_names,
        'metadata': {
            'model_type': 'RandomForestRegressor',
            'target_hyperparameters': ['eviction_ratio', 'frequency_threshold', 'recency_weight'],
            'training_date': str(np.datetime64('now'))
        }
    }

    with open(filename, 'wb') as f:
        pickle.dump(model_data, f)

    print(f"Model saved to {filename}")

def evaluate_model_performance():
    """Evaluate the trained model on new workloads."""
    print("\n=== Model Evaluation ===")

    # Load model
    model_file = '/users/Haocheng/mylcs/libCacheSim-python/s4fifo_model.pkl'

    if not os.path.exists(model_file):
        print("Model file not found. Please train the model first.")
        return

    # Generate test workloads
    test_workloads = [
        generate_synthetic_workload(15000, 800, 0.7, 1.1),
        generate_synthetic_workload(15000, 1200, 0.4, 0.9),
    ]

    cache_size = 500

    for i, workload in enumerate(test_workloads):
        print(f"\nTest Workload {i+1}:")

        # Test with ML model
        ml_cache = MLEnhancedS4FIFO(cache_size=cache_size,
                                   feature_collection_requests=3000,
                                   model_path=model_file)

        for item in workload:
            ml_cache.access(item)

        ml_status = ml_cache.get_status()
        print(f"  ML-Enhanced S4FIFO Hit Rate: {ml_status['hit_rate']:.3f}")

        # Test with default hyperparameters
        default_cache = MLEnhancedS4FIFO(cache_size=cache_size,
                                       feature_collection_requests=3000)

        for item in workload:
            default_cache.access(item)

        default_status = default_cache.get_status()
        print(f"  Default S4FIFO Hit Rate: {default_status['hit_rate']:.3f}")

        improvement = (ml_status['hit_rate'] - default_status['hit_rate']) / default_status['hit_rate'] * 100
        print(f"  Improvement: {improvement:+.1f}%")

if __name__ == "__main__":
    import os

    # Collect training data
    training_data = collect_training_data()

    # Save training data
    with open('/users/Haocheng/mylcs/libCacheSim-python/training_data.json', 'w') as f:
        json.dump(training_data, f, indent=2)

    # Train model
    model, scaler, feature_names = train_model(training_data)

    # Save model
    save_model(model, scaler, feature_names,
               '/users/Haocheng/mylcs/libCacheSim-python/s4fifo_model.pkl')

    # Evaluate model
    evaluate_model_performance()
