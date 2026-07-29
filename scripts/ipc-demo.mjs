#!/usr/bin/env node
// リッチテーマ + フィルムストリップの見た目確認用ドライブ。
// 稼働中ヘルパーに show + loadList + navigate を送って、少し待ってから終了する。
import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const pf = path.join(os.homedir(), 'Library', 'Application Support', 'ZooImage', 'daemon.json');
const info = JSON.parse(fs.readFileSync(pf, 'utf8'));

const iconsDir = path.join(ROOT, 'helper', 'src-tauri', 'icons');
const imgs = fs
  .readdirSync(iconsDir)
  .filter((f) => f.endsWith('.png'))
  .map((f) => path.join(iconsDir, f));

const sock = net.connect(info.port, '127.0.0.1');
let idc = 0;
const send = (method, params = {}) =>
  sock.write(JSON.stringify({ id: 'd' + ++idc, method, params }) + '\n');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

sock.on('data', () => {});
sock.on('connect', async () => {
  send('hello', { token: info.token, protocol: info.protocol, client: 'ipc-demo' });
  await sleep(150);
  send('show', {
    viewer: 'main',
    path: imgs[0],
    options: { theme: 'rich', zoom: 'fit', title: 'ZooImage — demo', w: 1100, h: 820, x: 140, y: 90 },
  });
  await sleep(300);
  send('loadList', { viewer: 'main', items: imgs, index: 0 });
  await sleep(300);
  send('navigate', { viewer: 'main', to: 2 });
  await sleep(500);
  console.log(`drove viewer with ${imgs.length} images`);
  sock.end();
});
