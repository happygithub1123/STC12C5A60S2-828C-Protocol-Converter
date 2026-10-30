// 可选浏览器验收。Playwright 仅供开发验证，不属于离线运行依赖。
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createServer } from '../server.mjs';

const moduleArg = process.argv.find(arg => arg.startsWith('--playwright='));
const playwright = moduleArg ? await import(pathToFileURL(moduleArg.slice('--playwright='.length)).href) : await import('playwright');
const server = createServer(); server.listen(0, '127.0.0.1'); await once(server, 'listening');
const url = `http://127.0.0.1:${server.address().port}`;
const browser = await playwright.chromium.launch({
  headless: true, ...(process.env.STC_CHROME_PATH ? { executablePath: process.env.STC_CHROME_PATH } : {}),
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
});
const checks = []; const external = []; const errors = [];
let page;
try {
  const context = await browser.newContext({ viewport: { width: 1440, height: 1080 }, serviceWorkers: 'block' });
  // 断开外网的浏览器测试：保留本机回环，阻止所有非本机请求；路由同时禁用 HTTP 缓存。
  await context.route('**/*', route => {
    if (new URL(route.request().url()).hostname === '127.0.0.1') return route.continue();
    external.push(route.request().url()); return route.abort('internetdisconnected');
  });
  page = await context.newPage(); page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  await page.goto(url); await page.locator('.world-label').first().waitFor();
  assert.equal(await page.locator('#webgl-error').isVisible(), false);
  for (const width of [1440, 960, 600, 390]) {
    await page.setViewportSize({ width, height: 1080 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, `横向溢出 ${width}`);
  }
  await page.setViewportSize({ width: 1440, height: 1080 });
  checks.push('外网请求全部禁止、冷启动 WebGL2 成功，1440/960/600/390 布局无横向溢出');
  await page.locator('#speed').selectOption('4');
  for (const id of ['normal', 'continuous', 'fragment', 'mismatch', 'disconnect', 'button']) {
    const started = Date.now();
    await page.locator('#scenario').selectOption(id); await page.locator('#demo').click();
    await page.waitForFunction(() => document.querySelector('#current-event').textContent.startsWith('收到 end，本轮结束'), null, { timeout: 60000 });
    await page.waitForFunction(() => document.querySelector('#play').textContent.includes('播放'));
    assert.equal(await page.locator('#state-name').innerText(), 'APP_IDLE');
    const result = await page.locator('#record-json').innerText(); assert.ok(result.includes('"MH"'), id);
    checks.push(`场景 ${id} 完整播放并正常 end`); console.log(`PASS scenario ${id} (${Date.now() - started} ms)`);
  }
  await page.locator('#scenario').selectOption('normal'); await page.locator('#demo').click();
  await page.locator('#play').click(); const paused = await page.locator('#event-count').innerText();
  await page.waitForTimeout(800); assert.equal(await page.locator('#event-count').innerText(), paused);
  await page.locator('#step').click(); assert.equal(Number(await page.locator('#event-count').innerText()), Number(paused) + 1);
  await page.waitForTimeout(400); assert.equal(Number(await page.locator('#event-count').innerText()), Number(paused) + 1);
  checks.push('暂停保持显示状态，单步仅新增一个事件');

  await page.locator('#reset').click(); await page.locator('#send-start').click();
  await page.waitForFunction(() => document.querySelector('#state-name').textContent === 'APP_WAIT_FIRST');
  await page.locator('#board-data').fill('D1-1,9,6,V,(2-'); await page.locator('#line-ending').selectOption('none'); await page.locator('#send-board').click();
  await page.waitForFunction(() => document.querySelector('#current-event').textContent.includes('字节进入逻辑接收缓存'));
  assert.equal(await page.locator('#record-json').innerText(), '—');
  await page.locator('#board-data').fill('5=12.6),NG'); await page.locator('#line-ending').selectOption('crlf'); await page.locator('#send-board').click();
  await page.waitForFunction(() => document.querySelector('#record-json').textContent.includes('2-5=12.6 V:NG'));
  await page.locator('.log-row.host button').filter({ hasText: '"MH"' }).last().waitFor();
  await page.locator('.log-row.host button').filter({ hasText: '"MH"' }).last().click();
  assert.equal(await page.locator('#packet-dialog').isVisible(), true); assert.ok((await page.locator('#packet-hex').innerText()).endsWith('0D 0A'));
  await page.locator('#close-dialog').click();
  await page.locator('#x1').uncheck(); await page.waitForFunction(() => document.querySelector('#record-result').textContent.startsWith('ERR'));
  checks.push('手动 START、分段发送、行结束、字节详情及模号断开 ERR 均通过 UI 验证');
  await page.locator('#reset').click(); await page.locator('#x5-tap').click();
  await page.waitForFunction(() => document.querySelector('#state-name').textContent === 'APP_WAIT_FIRST');
  assert.equal(await page.locator('#locked-module').innerText(), '1'); checks.push('手动 X5 点按启动');

  await page.locator('#speed').selectOption('0.25');
  await page.locator('#board-data').fill('Open,10.0,0.0, ohm,(8-78=978.4,9-84=992.2,),NG');
  await page.locator('#scenario').selectOption('normal'); await page.locator('#demo').click();
  if ((await page.locator('#play').innerText()).includes('暂停')) await page.locator('#play').click();
  await page.waitForFunction(() => document.querySelector('#play').textContent.includes('播放'));
  for (let i = 0; i < 50 && !(await page.locator('#current-event').innerText()).startsWith('整条记录校验通过'); i++) await page.locator('#step').click();
  assert.ok((await page.locator('#current-event').innerText()).startsWith('整条记录校验通过'));
  assert.equal(await page.locator('#record-json').innerText(), '—'); assert.equal(await page.locator('#record-first').innerText(), '—');
  await page.locator('#step').click(); assert.ok((await page.locator('#record-first').innerText()).includes('8-78=978.4'));
  assert.equal(await page.locator('#record-json').innerText(), '—');
  await page.locator('#step').click(); assert.ok((await page.locator('#record-json').innerText()).includes('8-78=978.4'));
  checks.push('数据对照严格随校验、首组编码、开始发送逐步显示，不提前显示未来结果');
  await page.locator('#overview').click(); await page.locator('#overview').scrollIntoViewIfNeeded();
  await page.screenshot({ path: fileURLToPath(new URL('../assets/preview.png', import.meta.url)), fullPage: true });
  await page.locator('#chip-view').click(); await page.waitForTimeout(100);
  await page.screenshot({ path: fileURLToPath(new URL('../assets/chip-preview.png', import.meta.url)), fullPage: true });
  checks.push('全景与芯片内部截图已保存');
  const beforeOrbit = await page.locator('#scene').screenshot();
  const box = await page.locator('#scene').boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 70, box.y + box.height / 2 + 25, { steps: 8 }); await page.mouse.up();
  await page.mouse.wheel(0, -200); await page.waitForTimeout(300);
  assert.notDeepEqual(await page.locator('#scene').screenshot(), beforeOrbit);
  checks.push('拖动旋转与滚轮缩放改变三维画面');

  const fallback = await context.newPage();
  await fallback.addInitScript(() => {
    const original = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(type, ...args) { return type.startsWith('webgl') ? null : original.call(this, type, ...args); };
  });
  await fallback.goto(url); await fallback.locator('#webgl-error').waitFor();
  await fallback.locator('#speed').selectOption('4'); await fallback.locator('#rd-num').click();
  await fallback.locator('.log-row.host button').filter({ hasText: '{"Result":"1"}' }).waitFor();
  checks.push('WebGL 不可用时提示可见，RD_NUM 协议与日志仍可操作');
  assert.deepEqual(external, [], '运行时不得尝试请求外网'); assert.deepEqual(errors, [], '主页面不得有浏览器错误');
  checks.push('运行期间没有外网请求和主页面 JavaScript/控制台错误');
  await mkdir(new URL('../artifacts/', import.meta.url), { recursive: true });
  await writeFile(new URL('../artifacts/browser-results.json', import.meta.url), JSON.stringify({ checks, external, errors }, null, 2));
  console.log(JSON.stringify({ checks, external, errors }, null, 2));
} catch (error) {
  await mkdir(new URL('../artifacts/', import.meta.url), { recursive: true });
  if (page) {
    console.error('页面诊断', await page.locator('#current-event').innerText(), await page.locator('#event-log').innerText(), errors);
    await page.screenshot({ path: fileURLToPath(new URL('../artifacts/browser-failure.png', import.meta.url)), fullPage: true });
  }
  throw error;
} finally { await browser.close(); server.close(); }
