const short = 'Short,99.9,2.0,Mohm,(),OK\r\n';
const open = 'Open,10.0,0.0, ohm,(8-78=978.4,9-84=992.2,),NG\r\n';
const start = (at, module = 1) => ({ at, type: 'host', raw: `(START,${module})` });
const board = (at, raw = short) => ({ at, type: 'board', raw });
const input = (at, mask) => ({ at, type: 'inputs', mask });
const x5 = (at, low) => ({ at, type: 'x5', low });

export function createScenarios(continuous) {
  const lines = continuous.trim().split(/\r?\n/);
  return [
    { id: 'normal', name: '01 · 完整查询与测试', description: '查模号 → START → 主板记录 → JSON → end', actions: [
      { at: 100, type: 'host', raw: '(RD_NUM)' }, { at: 200, type: 'host', raw: 'OK' }, start(300),
      board(500, 'test\r\n'), board(800, open), board(1200), board(1600, 'end\r\n'),
    ] },
    { id: 'continuous', name: '02 · 连续记录转换', description: '使用项目原有六条连续记录，逐条上传', actions: [
      start(100), ...lines.map((line, i) => board(500 + i * 200, `${line}\r\n`)),
    ] },
    { id: 'fragment', name: '03 · 分段接收与等待', description: '间隔超过 3 秒也不报错；补齐行结束才转换', actions: [
      start(100), board(200, 'D1-1,9.0,6.0,V,(2-'),
      { at: 5200, type: 'note', title: '已经等待 5 秒，仍保持会话', detail: '首帧和帧间均没有超时；动画使用演示时间' },
      board(5300, '5=12.6),NG\r\n'), board(10000), board(10200, 'end\r\n'),
    ] },
    { id: 'mismatch', name: '04 · 模号不匹配', description: '错误模号立即 ERR，修正后重新 START 恢复', actions: [
      start(100, 2), input(300, 3), start(400), input(600, 2), start(700, 2), board(900), board(1200, 'end\r\n'),
    ] },
    { id: 'disconnect', name: '05 · 模号断开与恢复', description: '断开停止旧会话，接回不会自动恢复', actions: [
      start(100), board(300), input(500, 0), board(700), input(900, 1),
      { at: 1000, type: 'host', raw: '(RD_NUM)' }, board(1200), start(1500), board(1700), board(2000, 'end\r\n'),
    ] },
    { id: 'button', name: '06 · X5 按键与忙时忽略', description: '按下松开继续；忙时按住跨过 end 不补发', actions: [
      x5(100, true), x5(200, false), board(400), x5(600, true), board(800, 'end\r\n'),
      { at: 5000, type: 'note', title: '忙时按住跨过 end，不启动下一轮' },
      x5(5100, false), x5(5200, true), x5(5300, false), board(5500), board(5800, 'end\r\n'),
    ] },
  ];
}
