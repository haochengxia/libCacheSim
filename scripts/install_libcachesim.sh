#!/bin/bash
set -euo pipefail

SOURCE=$(readlink -f "${BASH_SOURCE[0]}")
DIR=$(dirname "${SOURCE}")

cd "${DIR}"/../
mkdir -p _build
cd _build
cmake -G Ninja .. -DENABLE_LRB=ON
ninja
cd "${DIR}"
