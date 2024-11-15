import glob

files = glob.glob('/disk/data/privCacheDatasets/*.zst')

for file in files:
    trace_name = file.split("/")[-1]  #.split(".")[0]
    for cache_size_ratio in [0.1]:
        command = f"/users/Haocheng/optimize/libCacheSim/_build/bin/cachesim {file} oracleGeneral S3FIFO {cache_size_ratio} --ignore-obj-size 1 -e \"fifo-size-ratio=0.1,ghost-size-ratio=0.9,ghost-log-name={trace_name}.ghost.log.csv\""
        print(command)