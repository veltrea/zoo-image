#!/usr/bin/env bash
# verify-all.sh — ZooImage が「完成しているか」を 1 コマンドで確認する。
#
#   ユニットテスト → helper ビルド → plugin ビルド → 同梱 + 署名 → 命名/署名の検証
#   → コンテナ配布物のパッケージ → E2E（FileMaker 抜きで実際にヘルパーを動かす）
#
# 使い方:
#   scripts/verify-all.sh              # ホスト arch でフル検証
#   scripts/verify-all.sh universal    # arm64 + x86_64 で検証
#   scripts/verify-all.sh --quick      # ビルドを省き、既存成果物の検証 + E2E だけ
#
# 終了コード 0 = 全項目パス。1 つでも落ちれば非ゼロで、最後に失敗一覧を出す。
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
export PATH="/opt/homebrew/bin:$PATH"

MODE="${1:-}"
QUICK=0
TARGET_ARG=""
case "$MODE" in
  --quick)   QUICK=1 ;;
  universal) TARGET_ARG="universal" ;;
  "")        ;;
  *) echo "unknown argument: $MODE" >&2; exit 2 ;;
esac

PLUGIN="$HERE/plugin/build/ZooImage.fmplugin"
HELPER_IN_PLUGIN="$PLUGIN/Contents/Resources/helper/ZooImage.app"

PASS=0
FAILURES=()
c_ok()   { PASS=$((PASS+1)); printf '  \033[1;32mok\033[0m   %s\n' "$*"; }
c_bad()  { FAILURES+=("$*");  printf '  \033[1;31mFAIL\033[0m %s\n' "$*"; }
c_say()  { printf '\n\033[1;34m==>\033[0m %s\n' "$*"; }
# check <expected> <actual> <label>
check_eq() {
  if [ "$1" = "$2" ]; then c_ok "$3 = $2"; else c_bad "$3: expected '$1', got '$2'"; fi
}
plist() { /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null; }

# ---------------------------------------------------------------- 1. ユニット
c_say "helper unit tests (pure zoom/pan + playlist logic)"
if (cd helper && pnpm test >/tmp/zooimage-unit.log 2>&1); then
  c_ok "helper unit tests passed ($(grep -c '^PASS' /tmp/zooimage-unit.log) assertions)"
else
  c_bad "helper unit tests failed — see /tmp/zooimage-unit.log"
fi

# ---------------------------------------------------------------- 2. ビルド
if [ "$QUICK" -eq 0 ]; then
  c_say "Building helper (Tauri release)"
  if bash scripts/build-helper.sh $TARGET_ARG >/tmp/zooimage-helper-build.log 2>&1; then
    c_ok "helper built"
  else
    c_bad "helper build failed — see /tmp/zooimage-helper-build.log"
  fi

  c_say "Building plug-in (clean)"
  rm -rf plugin/build
  if bash scripts/build-plugin.sh >/tmp/zooimage-plugin-build.log 2>&1; then
    c_ok "plug-in built"
  else
    c_bad "plug-in build failed — see /tmp/zooimage-plugin-build.log"
  fi

  c_say "Bundling helper into the plug-in + ad-hoc signing"
  if bash scripts/bundle.sh >/tmp/zooimage-bundle.log 2>&1; then
    c_ok "helper bundled and signed"
  else
    c_bad "bundle/sign failed — see /tmp/zooimage-bundle.log"
  fi
else
  c_say "--quick: skipping builds, verifying existing artifacts"
fi

[ -d "$PLUGIN" ] || { c_bad "plug-in bundle missing: $PLUGIN"; }

# ---------------------------------------------------------------- EXIF エンジン
# libzooexif（ZooEXIF から統合）を実 JPEG に対して検証する。FMX に依存しないので
# FileMaker 無しで回せる。ビルド時に一緒に作られる exif_test を使う。
c_say "EXIF engine tests (libzooexif against real JPEGs)"
EXIF_BIN="$HERE/plugin/build/exif_test"
if [ ! -x "$EXIF_BIN" ]; then
  c_bad "exif_test not built (expected at $EXIF_BIN)"
elif "$EXIF_BIN" "$HERE/plugin/tests/fixtures" >/tmp/zooimage-exif.log 2>&1; then
  c_ok "EXIF engine tests passed ($(grep -oE '[0-9]+ checks' /tmp/zooimage-exif.log | tail -1))"
else
  c_bad "EXIF engine tests failed:"
  grep -E 'FAIL' /tmp/zooimage-exif.log | head -8 | sed 's/^/       /'
fi

# ---------------------------------------------------------------- 3. 命名
c_say "Naming — plug-in bundle"
check_eq "ZooImage"              "$(plist "$PLUGIN" CFBundleExecutable)" "CFBundleExecutable"
check_eq "ZooImage"              "$(plist "$PLUGIN" CFBundleName)"       "CFBundleName"
check_eq "com.veltrea.zooimage"  "$(plist "$PLUGIN" CFBundleIdentifier)" "CFBundleIdentifier"
check_eq "Zimg"                  "$(plist "$PLUGIN" CFBundleSignature)"  "CFBundleSignature (Plug-in ID)"
check_eq "FMXT"                  "$(plist "$PLUGIN" CFBundlePackageType)" "CFBundlePackageType"

c_say "Naming — bundled helper"
check_eq "ZooImage"                     "$(plist "$HELPER_IN_PLUGIN" CFBundleName)"       "helper CFBundleName"
check_eq "com.veltrea.zooimage.helper"  "$(plist "$HELPER_IN_PLUGIN" CFBundleIdentifier)" "helper CFBundleIdentifier"

c_say "Naming — compiled binary"
BIN="$PLUGIN/Contents/MacOS/ZooImage"
if [ -f "$BIN" ]; then
  c_ok "executable present at Contents/MacOS/ZooImage"
  # 注意: `strings … | grep -q` は使わない。grep -q はマッチした時点で終了するため、
  # まだ出力中の strings が SIGPIPE で落ち、set -o pipefail のせいでパイプライン全体が
  # 失敗扱いになる（バイナリが大きいほど再現しやすい）。件数を数える形なら入力を
  # 最後まで読むので起きない。
  if [ "$(strings "$BIN" | grep -cx 'Zimg1nnYYnn')" -gt 0 ]; then
    c_ok "options string 'Zimg1nnYYnn' present (ID matches CFBundleSignature)"
  else
    c_bad "options string 'Zimg1nnYYnn' not found in the binary"
  fi
  # universal バイナリでは各 arch に同じ文字列が入るため、必ず重複を除いて数える。
  FN_COUNT=$(strings "$BIN" | grep -oE '^zim_[A-Za-z]+$' | sort -u | wc -l | tr -d ' ')
  check_eq "17" "$FN_COUNT" "registered zim_ function count (unique)"
  EXIF_FN=$(strings "$BIN" | grep -oE '^zim_Exif[A-Za-z]+$' | sort -u | wc -l | tr -d ' ')
  check_eq "2" "$EXIF_FN" "EXIF function count (read-only)"
  ARCHS=$(lipo -archs "$BIN" 2>/dev/null || echo "?")
  check_eq "x86_64 arm64" "$ARCHS" "universal binary architectures"
  if [ "$(strings "$BIN" | grep -c 'ZooImage\.app/Contents/MacOS')" -gt 0 ]; then
    c_ok "helper lookup path points at ZooImage.app"
  else
    c_bad "helper lookup path in the binary does not mention ZooImage.app"
  fi
else
  c_bad "executable missing: $BIN"
fi

# ---------------------------------------------------------------- ライセンス
# 配布物をパーミッシブに保つ。コピーレフト(LGPL/GPL)のライブラリを取り込むと、
# 利用者にライブラリ差し替えの手段を用意する義務(LGPL-2.1 §6)が生じるため入れない。
# EXIF 読み取りは easyexif(BSD-2)。以前 libexif/libiptcdata(LGPL) を使っていた名残が
# 復活していないかを毎回見る。
c_say "Licensing — no copyleft dependencies"
if [ "$(otool -L "$BIN" | tail -n +2 | grep -ciE 'libexif|libiptcdata')" -gt 0 ]; then
  c_bad "an LGPL library (libexif/libiptcdata) is linked into the binary"
else
  c_ok "no LGPL library linked into the binary"
fi
COPYLEFT=$(grep -rliE 'lesser general public|library general public|GNU General Public' \
  plugin/src plugin/third_party 2>/dev/null || true)
if [ -z "$COPYLEFT" ]; then
  c_ok "no copyleft license text under plugin/src or plugin/third_party"
else
  c_bad "copyleft license text found:"; printf '%s\n' "$COPYLEFT" | head -5 | sed 's/^/       /'
fi

c_say "Naming — no legacy names anywhere in the source tree"
# 除外する 2 ファイル:
#   verify-all.sh … このチェック自身がパターン文字列を持つ（自己参照）
#   HANDOVER.md   … 旧名からの改名履歴を意図して記述している
LEGACY=$(grep -rn 'zImG\|ZooImg\|Zoo Image\|com\.veltrea\.zimg\|[^a-z]zi_' . \
  --exclude-dir=build --exclude-dir=node_modules --exclude-dir=target \
  --exclude-dir=dist --exclude-dir=.git --exclude-dir=export \
  --exclude=verify-all.sh --exclude=HANDOVER.md 2>/dev/null || true)
if [ -z "$LEGACY" ]; then
  c_ok "no legacy identifiers (zImG / ZooImg / 'Zoo Image' / zi_ / com.veltrea.zimg)"
else
  c_bad "legacy identifiers still present:"; printf '%s\n' "$LEGACY" | head -10 | sed 's/^/       /'
fi

# ---------------------------------------------------------------- 4. 署名
# 浅い verify で判断する（--deep --strict は framework の symlink で警告が出るが、
# TCC / Gatekeeper が見るのは主実行ファイルの署名）。
c_say "Code signature"
if codesign --verify "$PLUGIN" 2>/dev/null; then
  c_ok "plug-in bundle signature valid"
else
  c_bad "plug-in bundle signature invalid"
fi
if codesign --verify "$HELPER_IN_PLUGIN" 2>/dev/null; then
  c_ok "bundled helper signature valid"
else
  c_bad "bundled helper signature invalid (inside-out signing broken?)"
fi
SIG_ID=$(codesign -dv "$PLUGIN" 2>&1 | sed -n 's/^Identifier=//p')
check_eq "com.veltrea.zooimage" "$SIG_ID" "signing identifier"
HSIG_ID=$(codesign -dv "$HELPER_IN_PLUGIN" 2>&1 | sed -n 's/^Identifier=//p')
check_eq "com.veltrea.zooimage.helper" "$HSIG_ID" "helper signing identifier"
if codesign -dv "$HELPER_IN_PLUGIN" 2>&1 | grep -q 'Sealed Resources version'; then
  c_ok "helper resources are sealed"
else
  c_bad "helper resources not sealed (Tauri's build-time ad-hoc signature was not replaced)"
fi

# ---------------------------------------------------------------- 5. パッケージ
if [ "$QUICK" -eq 0 ]; then
  c_say "Packaging container-installable artifact"
  if bash scripts/package-container.sh >/tmp/zooimage-package.log 2>&1; then
    if [ -f "$HERE/dist/ZooImage.fmplugin.gz" ]; then
      c_ok "dist/ZooImage.fmplugin.gz ($(du -h "$HERE/dist/ZooImage.fmplugin.gz" | cut -f1))"
    else
      c_bad "package-container.sh ran but dist/ZooImage.fmplugin.gz is missing"
    fi
  else
    c_bad "package-container.sh failed — see /tmp/zooimage-package.log"
  fi
fi

# ---------------------------------------------------------------- 6. E2E
c_say "End-to-end (spawn the bundled helper, talk IPC, verify state)"
if node scripts/e2e-test.mjs >/tmp/zooimage-e2e.log 2>&1; then
  # e2e-test.mjs は ANSI 色付きで出すので、末尾の "<n> checks" から件数を取る。
  E2E_N=$(grep -oE '[0-9]+ checks' /tmp/zooimage-e2e.log | tail -1)
  c_ok "E2E passed (${E2E_N:-?}) — see /tmp/zooimage-e2e.log"
else
  c_bad "E2E failed:"
  grep -E 'FAIL|E2E' /tmp/zooimage-e2e.log | head -10 | sed 's/^/       /'
fi

# ---------------------------------------------------------------- 結果
echo
if [ ${#FAILURES[@]} -eq 0 ]; then
  printf '\033[1;32mALL CHECKS PASSED\033[0m — %d checks\n' "$PASS"
  echo "Artifacts:"
  echo "  plug-in : $PLUGIN"
  [ -f "$HERE/dist/ZooImage.fmplugin.gz" ] && echo "  container: $HERE/dist/ZooImage.fmplugin.gz"
  exit 0
fi
printf '\033[1;31mFAILED\033[0m — %d passed, %d failed:\n' "$PASS" "${#FAILURES[@]}"
printf '  - %s\n' "${FAILURES[@]}"
exit 1
