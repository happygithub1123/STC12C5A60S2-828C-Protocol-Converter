import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { SimulationEngine } from '../src/simulation/engine.js';
import { createScenarios } from '../src/simulation/scenarios.js';
import { parseRecord, bytes, getModule } from '../src/protocol/record.js';
import { ERR_FRAME, STATES } from '../src/protocol/bridge.js';

const short = 'Short,99.9,2.0,Mohm,(),OK\r\n';
const input = (mask, at) => ({ type: 'inputs', mask, at });
const host = (raw, at) => ({ type: 'host', raw, at });
const board = (raw, at) => ({ type: 'board', raw, at });
const key = (low, at) => ({ type: 'x5', low, at });
function drain(engine) {
  let count = 0; while (engine.step()) assert.ok(++count < 5000, '调度不能死循环'); return engine;
}
function run(actions, options = {}) {
  const engine = new SimulationEngine(); engine.reset(options);
  for (const action of actions) engine.dispatch(action, action.at ?? 100);
  return drain(engine);
}
const outputs = engine => engine.history.filter(event => event.type === 'tx-start');
const pc = engine => outputs(engine).filter(event => event.channel === 'mcu-host').map(event => event.raw);
const starts = engine => outputs(engine).filter(event => event.channel === 'mcu-board').map(event => event.raw);

const mappings = JSON.parse(await readFile(new URL('../assets/sample-mapping.json', import.meta.url), 'utf8'));
for (const [i, sample] of mappings.entries()) test(`现有映射样本 ${i + 1}：${Object.keys(sample.expected)[2]}，字节及 CRLF`, () => {
  const engine = run([host('(START,1)', 100), board(sample.input + '\r\n', 200)]);
  const expected = JSON.stringify(sample.expected) + '\r\n';
  assert.deepEqual(bytes(pc(engine)[0]), bytes(expected));
  assert.deepEqual(starts(engine), ['test\r\n']);
});

test('样本快照与项目原始 28 条资料相同', async () => {
  const original = JSON.parse(await readFile(new URL('../../04 测试验证/results/sample_mapping.json', import.meta.url), 'utf8'));
  assert.deepEqual(mappings, original); assert.equal(mappings.length, 28);
});

for (let mask = 0; mask < 16; mask++) test(`X1～X4 输入组合 ${mask.toString(2).padStart(4, '0')}：RD_NUM 与 START`, () => {
  const actual = getModule(mask);
  const engine = run([host('(RD_NUM)', 100), host(`(START,${actual || 1})`, 200)], { mask });
  assert.equal(pc(engine)[0], `{"Result":"${actual || 'ERR'}"}\r\n`);
  assert.deepEqual(starts(engine), actual ? ['test\r\n'] : []);
  if (!actual) assert.equal(pc(engine)[1], ERR_FRAME);
});

for (let module = 1; module <= 4; module++) test(`模号 ${module} 锁定到 MH`, () => {
  const engine = run([host(`(START,${module})`, 100), board(short, 200)], { mask: 1 << (module - 1) });
  assert.equal(JSON.parse(pc(engine)[0]).MH, String(module));
});

test('六条连续输入与项目期望输出逐字节一致，end 不汇总', async () => {
  const source = await readFile(new URL('../assets/continuous-input.txt', import.meta.url), 'utf8');
  const expected = (await readFile(new URL('../assets/continuous-expected.jsonl', import.meta.url), 'utf8')).trim().split(/\r?\n/).map(line => line + '\r\n');
  const engine = run([host('(START,1)', 100), ...source.trim().split(/\r?\n/).map((line, i) => board(line + '\r\n', 200 + i * 100))]);
  assert.deepEqual(pc(engine), expected); assert.equal(engine.protocol.state, STATES.IDLE);
});

test('没有帧间超时；未收完整的片段不生成 JSON', () => {
  const engine = run([host('(START,1)', 100), board('D1-1,9,6,V,(2-', 200), board('5=12.6),NG\r\n', 74200)]);
  assert.equal(pc(engine).length, 1); assert.equal(JSON.parse(pc(engine)[0])['D1-1'], '2-5=12.6 V:NG');
  assert.equal(engine.history.find(event => event.type === 'encode').timeMs, 74200);
  assert.equal(engine.protocol.error, 0);
});

test('支持 CR / LF / CRLF，不因 CRLF 重复上传；ACK 忽略', () => {
  const engine = run([host('(START,1)\r\nOK\r\nok', 100), board(short + short.replace('\r\n', '\r') + short.replace('\r\n', '\n'), 200)]);
  assert.equal(pc(engine).length, 3); assert.equal(starts(engine).length, 1);
});

test('空闲数据丢弃，重新 START 清理旧片段，首条前 end 报错', () => {
  const engine = run([board(short, 50), host('(START,1)', 100), board('stale', 200), host('(START,1)', 300), board(short, 400), board('end\r\n', 500), board(short, 600)]);
  assert.equal(pc(engine).length, 1); assert.equal(starts(engine).length, 2);
  assert.deepEqual(pc(run([host('(START,1)', 100), board('end\r\n', 200)])), [ERR_FRAME]);
});

test('模号不匹配错误后修正即可 START；查询不启动', () => {
  const engine = run([host('(START,2)', 100), input(2, 200), host('(RD_NUM)', 300), board(short, 400), host('(START,2)', 500), board(short, 600)]);
  assert.deepEqual(pc(engine).slice(0, 2), [ERR_FRAME, '{"Result":"2"}\r\n']);
  assert.equal(starts(engine).length, 1); assert.equal(JSON.parse(pc(engine)[2]).MH, '2');
});

test('断开只主动报错一次，接回不恢复，查询后 START 独立恢复', () => {
  const engine = run([host('(START,1)', 100), board(short, 200), input(0, 300), board(short, 400), input(1, 500), board(short, 600), host('(RD_NUM)', 700), host('(START,1)', 800), board(short, 900)]);
  assert.equal(pc(engine).filter(raw => raw === ERR_FRAME).length, 1); assert.equal(starts(engine).length, 2);
  assert.equal(pc(engine).filter(raw => raw.includes('"MH"')).length, 2);
});

test('合法 JSON、查询、test 发送中断开：完整帧先于 ERR', () => {
  for (const [trigger, at] of [[host('(RD_NUM)', 200), 200.1], [board(short, 200), 200.1], [host('(START,1)', 200), 200.1]]) {
    const initial = [host('(START,1)', 100)];
    const engine = run([...initial, trigger, input(0, at)]);
    const index = engine.history.findIndex(event => event.type === 'error');
    const tail = engine.history.slice(index);
    assert.ok(tail.findIndex(event => event.type === 'tx-end') < tail.findIndex(event => event.type === 'tx-start' && event.raw === ERR_FRAME));
    assert.equal(pc(engine).at(-1), ERR_FRAME);
  }
});

test('X5 19 ms 抖动不启动，20 ms 稳定确认一次，释放继续，长按不重复', () => {
  assert.equal(starts(run([key(true, 100), key(false, 119)])).length, 0);
  const engine = run([key(true, 100), key(false, 121), board(short, 200), board(short, 7000)]);
  assert.equal(starts(engine).length, 1); assert.equal(pc(engine).length, 2);
  assert.equal(engine.history.find(event => event.type === 'debounce' && event.title.includes('接受一次')).timeMs, 120);
});

test('X5 忙时按下跨过 end 不排队，必须释放再按', () => {
  const engine = run([key(true, 100), key(false, 200), board(short, 300), key(true, 400), board('end\r\n', 410), key(false, 800), key(true, 900), key(false, 1000), board(short, 1100)]);
  assert.equal(starts(engine).length, 2); assert.equal(pc(engine).length, 2);
  assert.equal(outputs(engine).filter(event => event.channel === 'mcu-board')[1].timeMs, 920);
});

test('X5 上电按住不启动，释放稳定后再按有效；无效模号按键 ERR', () => {
  assert.equal(starts(run([board(short, 1000)], { x5Low: true })).length, 0);
  assert.equal(starts(run([key(false, 100), key(true, 200)], { x5Low: true })).length, 1);
  assert.deepEqual(pc(run([key(true, 100)], { mask: 0 })), [ERR_FRAME]);
});

test('有效 START 优先于被未完成命令阻塞的按键请求，只发一次 test', () => {
  const engine = run([host('(START,', 50), key(true, 100), key(false, 130), host('1)', 200)]);
  assert.equal(starts(engine).length, 1);
});

test('按键请求等待命令边界，松开不取消；执行前模号改变则 ERR', () => {
  const accepted = run([host('(RD_', 50), key(true, 100), key(false, 130), host('NUM)', 200)]);
  assert.equal(starts(accepted).length, 1);
  const changed = run([host('(RD_', 50), key(true, 100), key(false, 130), input(2, 150), host('NUM)', 200)]);
  assert.equal(starts(changed).length, 0); assert.ok(pc(changed).includes(ERR_FRAME));
});

test('保留精度、符号、科学计数法和单位，转义中文引号反斜杠', () => {
  const record = parseRecord('测"试\\,10,0,mA,(点"\\=+1.250E-3,q=2),ok', 3);
  assert.deepEqual(JSON.parse(record.raw), { MH: '3', Result: 'OK', '测"试\\': '点"\\=+1.250E-3 mA:OK' });
  assert.equal(parseRecord('N,1,0,,(p=1.),OK', 1).selected, 'p=1.:OK');
});

test('后续非法组、空组、保留名、非法 UTF-8 与超容量均拒绝，可 START 恢复', () => {
  for (const record of ['N,1,0,V,(a=1,broken),OK', 'N,1,0,V,(a=1,,b=2),OK', 'MH,1,0,V,(),OK', `${'测'.repeat(11)},1,0,V,(),OK`, 'N,1,0,123456789,(),OK', 'N,1,0,V,(a=abc),OK']) {
    const engine = run([host('(START,1)', 100), board(record + '\r\n', 200), host('(START,1)', 300), board(short, 400)]);
    assert.equal(pc(engine)[0], ERR_FRAME); assert.equal(pc(engine).length, 2);
  }
  assert.equal(pc(run([host('(START,1)', 100), board(new Uint8Array([0xff, 13]), 200)]))[0], ERR_FRAME);
  assert.equal(run([host('(START,1)', 100), board('A'.repeat(512), 200)]).protocol.error, 3);
  assert.throws(() => parseRecord('N,1,0,V,(,a=1),OK', 1));
});

test('非法命令、23 字节边界及重同步', () => {
  for (const command of ['(START,0)', '(START,5)', '(BAD)', 'A'.repeat(24), '\x01']) {
    assert.equal(pc(run([host(command, 100)]))[0], ERR_FRAME);
  }
  assert.equal(starts(run([host('A'.repeat(24), 100), host('(START,1)', 200)])).length, 1);
});

test('所有六类场景可运行到终点且序号、时间有序', async () => {
  const continuous = await readFile(new URL('../assets/continuous-input.txt', import.meta.url), 'utf8');
  for (const scenario of createScenarios(continuous)) {
    const engine = new SimulationEngine(); engine.loadScenario(scenario); drain(engine);
    assert.ok(starts(engine).length > 0, scenario.id); assert.equal(engine.protocol.state, STATES.IDLE, scenario.id);
    engine.history.forEach((event, i) => { assert.equal(event.seq, i + 1); if (i) assert.ok(event.timeMs >= engine.history[i - 1].timeMs); });
  }
});

test('暂停不推进；单步只发布一个事件；重置清理旧任务；速度不改变输出', () => {
  const engine = new SimulationEngine(); engine.dispatch(host('(START,1)', 100), 100);
  const before = JSON.stringify(engine.protocol.snapshot()); const seq = engine.sequence;
  for (let i = 0; i < 100; i++) engine.tick(100);
  assert.equal(JSON.stringify(engine.protocol.snapshot()), before); assert.equal(engine.sequence, seq);
  engine.step(); assert.equal(engine.sequence, seq + 1);
  engine.reset(); drain(engine); assert.equal(starts(engine).length, 0); assert.equal(engine.protocol.state, STATES.IDLE);
  const all = [];
  for (const speed of [0.25, 1, 4]) {
    const e = new SimulationEngine(); e.dispatch(host('(START,1)'), 100); e.dispatch(board(short), 200); e.setSpeed(speed); e.play();
    for (let i = 0; (e.jobs.length || e.pending.length) && i < 10000; i++) e.tick(100);
    all.push(pc(e));
  }
  assert.deepEqual(all[0], all[1]); assert.deepEqual(all[1], all[2]); assert.equal(all[0].length, 1);
});

test('手动接管取消未来场景动作，保留正在发送帧的结束任务', () => {
  const engine = new SimulationEngine(); engine.loadScenario({ id: 'test', actions: [host('(START,1)', 100), board(short, 200)] });
  while (!engine.protocol.tx) engine.step();
  engine.cancelScenario(); drain(engine);
  assert.equal(engine.protocol.state, STATES.WAIT); assert.equal(pc(engine).length, 0);
});
