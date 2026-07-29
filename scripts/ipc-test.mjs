#!/usr/bin/env node
// ZooImage プラグイン ↔ ZooImage ヘルパー の IPC を FileMaker 抜きで検証するテストクライアント。
// ヘルパー(ZooImage.app)が起動済み(daemon.json あり)であることが前提。
//
//   node scripts/ipc-test.mjs [画像パス]
//
// 既定の画像は Tauri アイコン(128x128.png)。hello→subscribe→show→zoom→theme→window
// の順に送り、応答とイベントを表示する。接続は開いたままにして push イベントを待つ。

import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const HERE = path.dirname(new URL(import.meta.url).pathname);
const ROOT = path.resolve(HERE, '..');

function portFilePath() {
  if (process.platform === 'win32') {
    return path.join(process.env.LOCALAPPDATA || '', 'ZooImage', 'daemon.json');
  }
  return path.join(os.homedir(), 'Library', 'Application Support', 'ZooImage', 'daemon.json');
}

const img = process.argv[2]
  ? path.resolve(process.argv[2])
  : path.join(ROOT, 'helper', 'src-tauri', 'icons', '128x128.png');

const pf = portFilePath();
if (!fs.existsSync(pf)) {
  console.error(`daemon.json not found at ${pf}\nStart the helper first (pnpm tauri dev).`);
  process.exit(2);
}
const info = JSON.parse(fs.readFileSync(pf, 'utf8'));
console.log(`connecting to 127.0.0.1:${info.port} (helper v${info.version}, proto ${info.protocol})`);
console.log(`image: ${img}`);

const sock = net.connect(info.port, '127.0.0.1');
let buf = '';
let idc = 0;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function send(method, params = {}) {
  const id = 'c' + ++idc;
  const line = JSON.stringify({ id, method, params });
  console.log('>>', line);
  sock.write(line + '\n');
  return id;
}

sock.on('connect', () => {
  send('hello', { token: info.token, protocol: info.protocol, client: 'ipc-test' });
});

let started = false;
sock.on('data', (d) => {
  buf += d.toString('utf8');
  let i;
  while ((i = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, i);
    buf = buf.slice(i + 1);
    if (!line.trim()) continue;
    const msg = JSON.parse(line);
    if (msg.event) console.log('EV', line);
    else console.log('<<', line);
    if (!started && msg.ok && msg.result && msg.result.name === 'ZooImage') {
      started = true;
      runSequence();
    }
  }
});

sock.on('error', (e) => console.error('socket error', e.message));
sock.on('close', () => {
  console.log('connection closed');
  process.exit(0);
});

async function runSequence() {
  send('subscribe');
  send('setScript', { viewer: 'main', file: 'Demo', script: 'OnZiEvent' });
  send('show', {
    viewer: 'main',
    path: img,
    options: { theme: 'rich', zoom: 'fit', title: 'IPC test', w: 900, h: 700, x: 120, y: 120 },
  });
  await sleep(800);
  send('setZoom', { viewer: 'main', zoom: 2.0 });
  await sleep(800);
  send('setZoom', { viewer: 'main', zoom: 'fit' });
  await sleep(800);
  send('setTheme', { viewer: 'main', theme: 'minimal' });
  await sleep(1000);
  send('setTheme', { viewer: 'main', theme: 'rich' });
  await sleep(800);
  send('setWindow', { viewer: 'main', x: 200, y: 160, w: 1000, h: 760 });
  await sleep(400);
  send('getState', { viewer: 'main' });
  await sleep(400);
  console.log('--- sequence sent; keeping connection open for events (Ctrl+C to quit) ---');
}
