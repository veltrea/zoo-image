#!/usr/bin/env bash
# ZooImage プラグイン(.fmplugin)をビルドする（macOS, universal）。
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$HERE/plugin" -B "$HERE/plugin/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$HERE/plugin/build"
echo "Built plugin: $HERE/plugin/build/ZooImage.fmplugin"
