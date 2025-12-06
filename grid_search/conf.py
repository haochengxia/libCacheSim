# 参数 (Hyperparameter),选项 (Discrete Classes),类别数,物理意义 (Rationale)
# small_size_ratio,"[0.05, 0.1, 0.2]",3,0.05: Static/Stable0.1: Default0.2: Dynamic/Churn
# move_to_main_thresh,"[1, 2, 4]",3,1: Recency-friendly2: Balanced4: Scan-resistant
# ghost_to_main_thresh,"[0, 1]",2,0: Hit & Promote1: Conservative
# small_skip_ratio,"[0.0, 0.1, 0.25]",3,0.0: Off0.1: Light Filter0.25: Anti-Scan
# ghost_size_ratio,1.0 (Fixed),N/A,Fixed. Maximize history info.

small_size_ratio = [0.05, 0.1, 0.2]
move_to_main_thresh = [1, 2, 4]
ghost_to_main_thresh = [0, 1]
small_skip_ratio = [0.0, 0.1, 0.25]
ghost_size_ratio = 1.0  # Fixed. Maximize history info.
# End of file
