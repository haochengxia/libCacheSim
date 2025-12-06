from dataclasses import dataclass

@dataclass
class Feature:
    cache_capacity_log: float
    # current_s_ratio: float # We ignore this since only collect from default now
    # current_g_ratio: float
