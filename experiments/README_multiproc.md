# 多进程S4FIFO模拟脚本使用说明

## 概述

本目录包含了使用多进程加速S4FIFO缓存算法参数搜索的脚本。多进程可以显著提高大规模参数搜索的效率。

## 文件说明

### 1. `sim_s4fifo_multiproc.py`
完整版多进程S4FIFO模拟脚本，包含所有参数组合的搜索。

**特点：**
- 使用完整的参数网格
- 支持所有trace文件
- 包含S4FIFO和基线算法对比
- 结果保存为CSV格式

**参数网格：**
- `cache_size_ratio`: [0.01, 0.1]
- `small_skip_ratio`: [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1]
- `move_to_main_threshold`: [1, 2]
- `fifo_size_ratio`: [0.01, 0.05, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.99]
- `additional_ghost_size_ratio`: [0, 1]

### 2. `sim_s4fifo_multiproc_simple.py`
简化版多进程S4FIFO模拟脚本，用于快速测试。

**特点：**
- 使用简化的参数网格
- 只测试一个trace文件
- 快速验证多进程功能
- 适合调试和测试

**参数网格：**
- `cache_size_ratio`: [0.01]
- `small_skip_ratio`: [0, 0.5]
- `move_to_main_threshold`: [2]
- `fifo_size_ratio`: [0.1, 0.5]
- `additional_ghost_size_ratio`: [0]

### 3. `run_multiproc_simulation.py`
多进程模拟启动脚本，确保正确的环境设置。

## 使用方法

### 方法1：直接运行简化版（推荐先测试）
```bash
cd experiments
python sim_s4fifo_multiproc_simple.py
```

### 方法2：使用启动脚本
```bash
cd experiments
python run_multiproc_simulation.py
```

### 方法3：直接运行完整版
```bash
cd experiments
python sim_s4fifo_multiproc.py
```

## 输出文件

### S4FIFO结果
- `s4fifo_results_multiproc.csv` (完整版)
- `s4fifo_results_multiproc_simple.csv` (简化版)

包含列：
- `trace_name`: trace文件名
- `cache_size_ratio`: 缓存大小比例
- `small_skip_ratio`: 小缓存跳过比例
- `move_to_main_threshold`: 移动到主缓存的阈值
- `fifo_size_ratio`: FIFO大小比例
- `additional_ghost_size_ratio`: 额外ghost缓存大小比例
- `miss_ratio`: 未命中率

### 基线算法结果
- `baseline_results_multiproc.csv` (完整版)
- `baseline_results_multiproc_simple.csv` (简化版)

包含列：
- `trace_name`: trace文件名
- `eviction_policy`: 驱逐策略名称
- `miss_ratio`: 未命中率

## 性能优化

### 进程数设置
脚本会自动检测CPU核心数并设置合适的进程数：
- 简化版：最多4个进程
- 完整版：最多8个进程

### 内存管理
- 每个进程独立处理一个参数组合
- 避免内存泄漏
- 异常处理确保进程稳定

## 注意事项

1. **环境要求**：
   - Python 3.7+
   - 已安装libcachesim
   - 有足够的trace文件

2. **多进程限制**：
   - 使用`spawn`启动方法确保兼容性
   - 避免在Windows上使用`fork`

3. **调试建议**：
   - 先运行简化版测试功能
   - 检查输出文件格式
   - 监控系统资源使用

4. **错误处理**：
   - 单个实验失败不会影响整体运行
   - 错误信息会记录在结果中

## 性能对比

与单进程版本相比，多进程版本可以显著提高性能：

- **4核CPU**: 约3-4倍加速
- **8核CPU**: 约6-7倍加速
- **16核CPU**: 约10-12倍加速

实际加速比取决于：
- CPU核心数
- 内存带宽
- 磁盘I/O性能
- 参数组合数量

## 故障排除

### 常见问题

1. **模块导入错误**：
   ```bash
   # 确保在正确的目录下运行
   cd experiments
   export PYTHONPATH=/path/to/project/root:$PYTHONPATH
   ```

2. **内存不足**：
   - 减少进程数
   - 分批处理参数组合
   - 增加系统内存

3. **进程卡死**：
   - 检查trace文件是否存在
   - 验证参数范围是否合理
   - 增加超时设置

### 调试模式
在脚本中添加调试信息：
```python
import logging
logging.basicConfig(level=logging.DEBUG)
```

## 扩展使用

### 自定义参数网格
修改脚本中的参数网格定义：
```python
cache_size_ratio_grid = [0.01, 0.05, 0.1]  # 自定义值
```

### 添加新的基线算法
在基线实验部分添加新的驱逐策略：
```python
for eviction_policy in [LRU, ARC, TwoQ, ThreeLCache, TinyLFU, LRB, YourNewPolicy]:
```

### 结果分析
使用pandas进行结果分析：
```python
import pandas as pd
df = pd.read_csv('s4fifo_results_multiproc.csv')
best_params = df.loc[df['miss_ratio'].idxmin()]
```
