#!/usr/bin/env bash
# ZooImage ヘルパー(.app)をリリースビルドする。
#   scripts/build-helper.sh            # ホスト arch (既定)
#   scripts/build-helper.sh universal  # arm64+x86_64（両 rust target が必要）
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE/helper"

pnpm install >/dev/null 2>&1 || pnpm install

if [ "${1:-}" = "universal" ]; then
  rustup target add aarch64-apple-darwin x86_64-apple-darwin >/dev/null 2>&1 || true
  pnpm tauri build --bundles app --target universal-apple-darwin
  APP="$HERE/helper/src-tauri/target/universal-apple-darwin/release/bundle/macos/ZooImage.app"
else
  pnpm tauri build --bundles app
  APP="$HERE/helper/src-tauri/target/release/bundle/macos/ZooImage.app"
fi

echo "Built helper: $APP"
