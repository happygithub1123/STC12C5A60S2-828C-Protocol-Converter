import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { createServer } from '../server.mjs';

test('本机服务在中文空格路径下提供页面、模块、本地依赖及样本', async t => {
  const server = createServer(); server.listen(0, '127.0.0.1'); await once(server, 'listening');
  t.after(() => server.close()); const base = `http://127.0.0.1:${server.address().port}`;
  for (const file of ['/', '/src/main.js', '/src/scene/laboratory.js', '/vendor/three/three.module.js', '/vendor/three/three.core.js', '/vendor/three/OrbitControls.js', '/assets/sample-mapping.json']) {
    const response = await fetch(base + file); assert.equal(response.status, 200, file);
    assert.ok((await response.text()).length > 0); assert.ok(response.headers.get('content-security-policy').includes("connect-src 'self'"));
  }
  const module = await fetch(base + '/src/main.js'); assert.ok(module.headers.get('content-type').startsWith('text/javascript'));
  assert.equal((await fetch(base + '/%2e%2e%5cREADME.md')).status, 404);
  assert.equal((await fetch(base, { method: 'POST' })).status, 405);
});

test('运行源码没有远程 URL、字体导入及网络依赖', async () => {
  const files = ['index.html', 'src/main.js', 'src/ui/style.css', 'src/ui/controller.js', 'src/scene/laboratory.js'];
  for (const file of files) {
    const source = await readFile(new URL(`../${file}`, import.meta.url), 'utf8');
    assert.doesNotMatch(source, /https?:\/\//, file); assert.doesNotMatch(source, /@import\s|@font-face/, file);
  }
});

test('Windows 启动脚本为 ASCII 内容及 CRLF，避免批处理多字节解析错误', async () => {
  const data = await readFile(new URL('../启动仿真.cmd', import.meta.url));
  assert.ok(data.every(byte => byte < 128));
  assert.doesNotMatch(data.toString(), /(?<!\r)\n/);
  assert.match(data.toString(), /node "%~dp0server\.mjs" %\*/);
});

test('本地 Three.js 依赖和样本与记录的 SHA-256 相同', async () => {
  const provenance = JSON.parse(await readFile(new URL('../assets/provenance.json', import.meta.url), 'utf8'));
  for (const entry of [...provenance.three.files.map(item => ({ ...item, path: `vendor/three/${item.file}` })), ...provenance.samples.map(item => ({ ...item, path: item.snapshot }))]) {
    const digest = createHash('sha256').update(await readFile(new URL(`../${entry.path}`, import.meta.url))).digest('hex');
    assert.equal(digest, entry.sha256, entry.path);
  }
});
