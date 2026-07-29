#!/usr/bin/env bash
# ヘルパー(.app)を ZooImage.fmplugin/Contents/Resources/helper/ に同梱し、
# 同梱後に deep ad-hoc 署名する（署名は必ず全同梱物を置いた後に行う）。
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"

PLUGIN="$HERE/plugin/build/ZooImage.fmplugin"
# ホスト arch / universal のどちらのビルドでも拾う。
APP_HOST="$HERE/helper/src-tauri/target/release/bundle/macos/ZooImage.app"
APP_UNI="$HERE/helper/src-tauri/target/universal-apple-darwin/release/bundle/macos/ZooImage.app"
if [ -d "$APP_UNI" ]; then HELPER_APP="$APP_UNI"; else HELPER_APP="$APP_HOST"; fi

[ -d "$PLUGIN" ] || { echo "ERROR: plugin not built ($PLUGIN). Run build-plugin.sh first."; exit 1; }
[ -d "$HELPER_APP" ] || { echo "ERROR: helper .app not built ($HELPER_APP). Run build-helper.sh first."; exit 1; }

DEST="$PLUGIN/Contents/Resources/helper"
rm -rf "$DEST"
mkdir -p "$DEST"
cp -R "$HELPER_APP" "$DEST/"
echo "Bundled helper into: $DEST"

# 署名は inside-out（子 → 親）の順で行う。
#
# Tauri がビルド時に付ける ad-hoc 署名はリソースを封印しておらず
# （`Sealed Resources=none` / Identifier が Cargo 名由来の helper-<hash>）、
# codesign --verify が "code has no resources but signature indicates they must
# be present" で失敗する。親に --deep --force をかけても、この壊れた子の署名は
# 置き換わらない。先に子 .app を単独で署名し直すと Info.plist の
# CFBundleIdentifier (com.veltrea.zooimage.helper) が採用され、リソースも封印される。
codesign --sign - --force --timestamp=none "$DEST/ZooImage.app"
codesign --verify "$DEST/ZooImage.app" || { echo "ERROR: helper signature invalid"; exit 1; }

# 同梱物を全て配置し終えてから親を deep ad-hoc 署名（--timestamp=none は ad-hoc の定石）。
codesign --sign - --deep --force --timestamp=none "$PLUGIN"
codesign --verify "$PLUGIN" && echo "signed + verified: $PLUGIN"
