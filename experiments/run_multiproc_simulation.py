#!/usr/bin/env python3
"""
多进程模拟启动脚本
确保在正确的环境下运行多进程模拟
"""

import os
import sys
import subprocess
import multiprocessing as mp

def main():
    # 添加项目根目录到Python路径
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sys.path.insert(0, project_root)

    # 设置环境变量
    os.environ['PYTHONPATH'] = project_root + ':' + os.environ.get('PYTHONPATH', '')

    print(f"项目根目录: {project_root}")
    print(f"Python路径: {sys.path[0]}")
    print(f"CPU核心数: {mp.cpu_count()}")

    # 运行多进程模拟
    script_path = os.path.join(os.path.dirname(__file__), 'sim_s4fifo_multiproc.py')

    try:
        # 使用subprocess运行，确保环境正确
        result = subprocess.run([
            sys.executable, script_path
        ],
        cwd=os.path.dirname(__file__),
        env=os.environ,
        capture_output=True,
        text=True,
        timeout=3600  # 1小时超时
        )

        if result.returncode == 0:
            print("多进程模拟成功完成！")
            print("输出:")
            print(result.stdout)
        else:
            print("多进程模拟失败！")
            print("错误输出:")
            print(result.stderr)
            print("标准输出:")
            print(result.stdout)

    except subprocess.TimeoutExpired:
        print("模拟超时（1小时）")
    except Exception as e:
        print(f"运行出错: {e}")

if __name__ == "__main__":
    main()
