#!/bin/bash

# 本地运行GitHub workflow的脚本
# 模拟 .github/workflows/python.yml 的步骤

set -e  # 遇到错误时退出

echo "🚀 开始本地运行Python workflow..."

# 检查Python版本
echo "📋 检查Python版本..."
python_version=$(python3 --version 2>&1 | grep -o '3\.[0-9]\+')
echo "当前Python版本: $python_version"

# 检查是否在正确的目录
if [ ! -f "requirements.txt" ]; then
    echo "❌ 错误: 请在项目根目录运行此脚本"
    exit 1
fi

# 检查系统依赖
echo "🔍 检查系统依赖..."
if ! command -v cmake &> /dev/null; then
    echo "❌ 错误: cmake未安装，请运行: sudo apt-get install build-essential cmake"
    exit 1
fi

if ! pkg-config --exists glib-2.0; then
    echo "❌ 错误: glib-2.0未安装，请运行: sudo apt-get install pkg-config libglib2.0-dev"
    exit 1
fi

# 步骤1: 安装XGBoost和LightGBM
echo "📦 安装XGBoost和LightGBM..."
if command -v conda &> /dev/null; then
    echo "使用conda安装XGBoost和LightGBM..."
    conda install -c conda-forge xgboost lightgbm -y
else
    echo "使用pip安装XGBoost和LightGBM..."
    pip install xgboost lightgbm
fi

# 步骤2: 安装Python依赖
echo "📦 安装Python依赖..."
pip install --upgrade pip
pip install -r requirements.txt
pip install pytest

# 步骤3: 构建libCacheSim-python
echo "🔨 构建libCacheSim-python..."
cd libCacheSim-python
pip install -e .

# 步骤4: 运行测试
echo "🧪 运行测试..."
pytest tests/

echo "✅ workflow执行完成！" 