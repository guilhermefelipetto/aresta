#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

config="${1:-Debug}"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE="$config"
cmake --build build

ln -sf build/bin/aresta aresta
