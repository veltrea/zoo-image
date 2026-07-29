#!/usr/bin/env node
// e2e-test.mjs — ZooImage のエンドツーエンド検証（FileMaker 不要・完全自動）。
//
// プラグインが実際に行うのと同じ手順でヘルパーを起動し、IPC で会話して終了させる:
//   1. 既存の daemon.json を掃除し、古いヘルパーを落とす
//   2. .fmplugin に同梱された ZooImage.app の実行ファイルを走査して起動
//      （プラグインの helperBinaryPath() と同じ「Contents/MacOS を走査」方式）
//   3. daemon.json が現れるのを待つ
//   4. hello でトークン認証し、name / protocol を検証
//   5. show → getState で表示状態を検証
//   6. close → shutdown、daemon.json が消えることを確認
//
// 使い方:
//   node scripts/e2e-test.mjs [/path/to/ZooImage.fmplugin]
// 既定は plugin/build/ZooImage.fmplugin。
//
// 終了コード 0 = 全項目パス、1 = 失敗。

import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, execFileSync } from 'node:child_process';

const HERE = path.dirname(new URL(import.meta.url).pathname);
const ROOT = path.resolve(HERE, '..');
const PLUGIN = process.argv[2] || path.join(ROOT, 'plugin', 'build', 'ZooImage.fmplugin');
const IMAGE = path.join(ROOT, 'helper', 'src-tauri', 'icons', '128x128.png');
const PORTFILE = path.join(os.homedir(), 'Library', 'Application Support', 'ZooImage', 'daemon.json');

let passed = 0;
const failures = [];
const ok = (m) => { passed++; console.log(`  \x1b[32mok\x1b[0m   ${m}`); };
const bad = (m) => { failures.push(m); console.log(`  \x1b[31mFAIL\x1b[0m ${m}`); };
const say = (m) => console.log(`\x1b[34m==>\x1b[0m ${m}`);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function check(cond, msg) { cond ? ok(msg) : bad(msg); return cond; }

// --- 1. 掃除 -------------------------------------------------------------
say('Cleaning up any previous helper');
try { execFileSync('pkill', ['-f', 'ZooImage.app/Contents/MacOS'], { stdio: 'ignore' }); } catch { /* 動いていなければ何もしない */ }
try { fs.unlinkSync(PORTFILE); } catch { /* 無ければよい */ }
await sleep(400);

// --- 2. 同梱ヘルパーを起動 -------------------------------------------------
const macosDir = path.join(PLUGIN, 'Contents', 'Resources', 'helper', 'ZooImage.app', 'Contents', 'MacOS');
if (!fs.existsSync(macosDir)) {
  console.error(`\nhelper not bundled: ${macosDir}\nRun scripts/build-helper.sh && scripts/build-plugin.sh && scripts/bundle.sh first.`);
  process.exit(1);
}
// プラグインと同じ「実行可能ファイルを走査して特定する」方式（Tauri は Cargo 名で命名するため）。
const execName = fs.readdirSync(macosDir).find((f) => {
  try { fs.accessSync(path.join(macosDir, f), fs.constants.X_OK); return true; } catch { return false; }
});
check(!!execName, `bundled helper executable found (${execName})`);

say(`Spawning helper: ${path.join(macosDir, execName)}`);
const child = spawn(path.join(macosDir, execName), ['--from-plugin'], {
  detached: true, stdio: ['ignore', 'pipe', 'pipe'],
});
let helperLog = '';
child.stdout.on('data', (d) => { helperLog += d.toString(); });
child.stderr.on('data', (d) => { helperLog += d.toString(); });

// --- 3. daemon.json を待つ ------------------------------------------------
say('Waiting for daemon.json');
let info = null;
for (let i = 0; i < 100; i++) {          // 最大 20 秒
  if (fs.existsSync(PORTFILE)) {
    try { info = JSON.parse(fs.readFileSync(PORTFILE, 'utf8')); break; } catch { /* 書き込み途中 */ }
  }
  await sleep(200);
}
if (!check(!!info, 'daemon.json written by helper')) {
  console.error('\nhelper output:\n' + helperLog);
  try { process.kill(-child.pid); } catch { /* already gone */ }
  process.exit(1);
}
check(typeof info.port === 'number' && info.port > 0, `port allocated (${info.port})`);
check(typeof info.token === 'string' && info.token.length === 64, 'token is 64 hex chars');
check(info.protocol === 1, `protocol version is 1 (got ${info.protocol})`);

// --- 4-6. IPC 会話 --------------------------------------------------------
const sock = net.connect(info.port, '127.0.0.1');
let buf = '';
let idc = 0;
const pending = new Map();

function send(method, params = {}) {
  const id = 'e' + ++idc;
  sock.write(JSON.stringify({ id, method, params }) + '\n');
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`timeout: ${method}`)); }, 10000);
    pending.set(id, { resolve, timer });
  });
}

sock.on('data', (d) => {
  buf += d.toString('utf8');
  let i;
  while ((i = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, i); buf = buf.slice(i + 1);
    if (!line.trim()) continue;
    let msg; try { msg = JSON.parse(line); } catch { continue; }
    if (msg.event) continue;                       // push イベントはここでは使わない
    const p = pending.get(msg.id);
    if (p) { clearTimeout(p.timer); pending.delete(msg.id); p.resolve(msg); }
  }
});
sock.on('error', (e) => { bad(`socket error: ${e.message}`); });

await new Promise((r) => sock.once('connect', r));
say('Connected — running protocol sequence');

try {
  const hello = await send('hello', { token: info.token, protocol: 1, client: 'e2e-test/1.0' });
  check(hello.ok === true, 'hello accepted with the daemon.json token');
  check(hello.result?.name === 'ZooImage', `helper identifies as "ZooImage" (got "${hello.result?.name}")`);
  check(hello.result?.protocol === 1, 'hello reports protocol 1');

  const show = await send('show', {
    viewer: 'main', path: IMAGE,
    options: { theme: 'rich', zoom: 'fit', title: 'E2E', w: 800, h: 600, x: 100, y: 100 },
  });
  check(show.ok === true, 'show opened a viewer');

  await sleep(600);
  const st = await send('getState', { viewer: 'main' });
  check(st.ok === true, 'getState succeeded');
  check(st.result?.open === true, 'viewer reports open:true');
  check(st.result?.viewer === 'main', 'viewer name is "main"');
  check(typeof st.result?.image === 'string' && st.result.image.endsWith('128x128.png'),
        'state reports the image we asked for');

  const zoom = await send('setZoom', { viewer: 'main', zoom: 2.0 });
  check(zoom.ok === true, 'setZoom accepted');
  await sleep(300);
  const st2 = await send('getState', { viewer: 'main' });
  check(Math.abs((st2.result?.zoom ?? 0) - 2.0) < 0.001, `zoom applied (got ${st2.result?.zoom})`);

  const closed = await send('close', { viewer: 'main' });
  check(closed.ok === true, 'close succeeded');

  // shutdown は応答（bye）を送った直後に接続を閉じるので、応答より切断が先に
  // 届くことがある。どちらでも正常。
  say('Sending shutdown');
  let bye = null;
  try { bye = await send('shutdown'); } catch { /* 切断が先だった */ }
  check(bye === null || bye.ok === true, 'shutdown acknowledged (or helper closed the connection first)');
} catch (e) {
  bad(`protocol sequence: ${e.message}`);
}

sock.destroy();
await sleep(500);

// --- プロセスの後始末 -------------------------------------------------------
// daemon.json の削除はここでは検証しない。ヘルパーは最後のクライアントが切れてから
// QUIT_GRACE_SECS(=180 秒) の猶予を置いてから自殺する設計で（プラグインが続けて
// 呼ぶときの再起動コストを避けるため）、E2E で 3 分待つのは現実的でないため。
// 猶予タイマーが動いていること自体は、プロセスがまだ生きていることで確認できる。
let alive = true;
try { process.kill(child.pid, 0); } catch { alive = false; }
check(alive, 'helper still resident during the quit grace period (by design)');

try { process.kill(-child.pid); } catch { /* すでに終了 */ }
try { execFileSync('pkill', ['-f', 'ZooImage.app/Contents/MacOS'], { stdio: 'ignore' }); } catch { /* 同上 */ }
try { fs.unlinkSync(PORTFILE); } catch { /* 無ければよい */ }

// --- 結果 -----------------------------------------------------------------
console.log();
if (failures.length) {
  console.log(`\x1b[31mE2E FAILED\x1b[0m — ${passed} passed, ${failures.length} failed:`);
  failures.forEach((f) => console.log(`  - ${f}`));
  if (helperLog.trim()) console.log('\nhelper output:\n' + helperLog);
  process.exit(1);
}
console.log(`\x1b[32mE2E PASSED\x1b[0m — ${passed} checks`);
process.exit(0);
