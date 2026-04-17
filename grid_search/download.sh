#!/bin/bash

BASE="/mnt/cfs/oracleReuse"
cd "$BASE" || exit

declare -A MAPPING
MAPPING=(
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/sample/twitter/"]="sample"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/metaKV/"]="metaKV"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/metaCDN/"]="metaCDN"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/metaStorage/"]="metaStorage"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/msr/"]="msr"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/fiu/"]="fiu"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/cloudphysics/"]="cphy"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/systor/"]="systor"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/.priv/cdn1/"]="akamai"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/tencentPhoto/"]="tencentPhoto"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/wiki/"]="wiki"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/tencentBlock/"]="tencentBlock"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/cacheDatasets/alibabaBlock/"]="alibabaBlock"
    ["https://ftp.pdl.cmu.edu/pub/datasets/cacheDatasets/.priv/cdn2/"]="cf"
)

echo "Starting multiprocess download with progress monitoring..."

# Store PIDs
declare -A PIDS

for URL in "${!MAPPING[@]}"; do
    DIR="${MAPPING[$URL]}"
    TARGET="$BASE/$DIR"
    mkdir -p "$TARGET"
    
    # Run each download in the background
    (
        cd "$TARGET" || exit
        # Use --progress=bar:force so we get continuous progress lines with \r
        wget -c -r -np -nd -R "index.html*" --progress=bar:force "$URL" > "$BASE/${DIR}_wget.log" 2>&1
    ) &
    PIDS[$DIR]=$!
done

# Hide cursor for clean TUI
tput civis
trap 'tput cnorm; exit' INT TERM EXIT

# Progress monitor loop
while true; do
    clear
    echo "================================ Download Progress ================================"
    echo "Files are downloading in parallel. Detail logs are in $BASE/*_wget.log"
    echo "-----------------------------------------------------------------------------------"
    
    active=0
    for DIR in "${MAPPING[@]}"; do
        # Check if process is still running
        if kill -0 "${PIDS[$DIR]}" 2>/dev/null; then
            active=1
            STATE="[RUNNING]"
        else
            STATE="[DONE]   "
        fi
        
        LOGFILE="$BASE/${DIR}_wget.log"
        PROGRESS_INFO="Starting..."
        if [ -f "$LOGFILE" ]; then
            # Extract the latest meaningful progress line from log
            LATEST=$(tail -c 1000 "$LOGFILE" | tr '\r' '\n' | grep -v '^[[:space:]]*$' | tail -n 1 | cut -c 1-60)
            if [ -n "$LATEST" ]; then
                PROGRESS_INFO="$LATEST"
            fi
        fi
        
        printf "%-12s %s %s\n" "$DIR" "$STATE" "$PROGRESS_INFO"
    done
    echo "==================================================================================="
    
    if [ $active -eq 0 ]; then
        break
    fi
    sleep 2
done

tput cnorm
echo "All multi-process downloads finished."
