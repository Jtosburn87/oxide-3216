#!/usr/bin/env bash
# Builds Oxide-3216 for Linux and runs the engine tests.
# Usage:  ./build.sh [Release|Debug]
set -euo pipefail
cd "$(dirname "$0")"
config="${1:-Release}"

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE="$config"
cmake --build build-linux --parallel "$(nproc)"

build-linux/EngineTests

echo
echo "Built: build-linux/Oxide3216_artefacts/$config/Oxide-3216"
