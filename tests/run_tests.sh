#!/usr/bin/env bash
# Build and run GDMenu's host-side core tests (no Geode / no GD needed).
#   bash tests/run_tests.sh
# Uses the same GDReplayFormat commit the mod builds against; if it isn't checked
# out yet it is fetched into .deps/ (gitignored).
set -euo pipefail
cd "$(dirname "$0")/.."

GDR_INC="${GDR_INCLUDE:-}"
if [ -z "$GDR_INC" ]; then
    if [ -d .deps/gdr/include ]; then
        GDR_INC=.deps/gdr/include
    elif [ -d ../.deps/gdr/include ]; then
        GDR_INC=../.deps/gdr/include
    else
        echo "fetching GDReplayFormat into .deps/ ..."
        git clone --depth 1 https://github.com/maxnut/GDReplayFormat.git .deps/gdr
        GDR_INC=.deps/gdr/include
    fi
fi

mkdir -p build
CXX="${CXX:-g++}"
"$CXX" -std=c++23 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I src/core -I "$GDR_INC" \
    tests/test_replay_io.cpp -o build/test_replay_io
./build/test_replay_io
