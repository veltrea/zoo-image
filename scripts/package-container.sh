#!/usr/bin/env bash
# コンテナ配布用に .fmplugin を xar+gzip で固める（「プラグインファイルのインストール」用）。
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PLUGIN="$HERE/plugin/build/ZooImage.fmplugin"
OUT="$HERE/dist"
[ -d "$PLUGIN" ] || { echo "ERROR: plugin not built ($PLUGIN)."; exit 1; }
mkdir -p "$OUT"
( cd "$(dirname "$PLUGIN")" && xar -cf "$OUT/ZooImage.fmplugin.xar" "$(basename "$PLUGIN")" )
gzip -c "$OUT/ZooImage.fmplugin.xar" > "$OUT/ZooImage.fmplugin.gz"
rm -f "$OUT/ZooImage.fmplugin.xar"
echo "Packaged: $OUT/ZooImage.fmplugin.gz"
