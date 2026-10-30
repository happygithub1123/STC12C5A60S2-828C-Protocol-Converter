import { bytes, getModule, LIMITS, parseRecord, trim } from './record.js';

export const STATES = Object.freeze({ IDLE: 'APP_IDLE', START: 'APP_SENDING_START', WAIT: 'APP_WAIT_FIRST', RECEIVE: 'APP_RECEIVING' });
export const ERR_FRAME = '{"Result":"ERR"}\r\n';
export const CHANNELS = Object.freeze({ HOST: 'host-mcu', PC: 'mcu-host', BOARD: 'board-mcu', TEST: 'mcu-board' });

/** 事件驱动的协议行为模型；schedule/clock 由确定性的虚拟时钟提供。 */
export class BridgeProtocol {
  constructor({ emit, schedule, clock, mask = 1, x5Low = false }) {
    this.emit = emit; this.schedule = schedule; this.clock = clock;
    this.mask = mask; this.state = STATES.IDLE; this.locked = 0; this.error = 0;
    this.host = []; this.board = []; this.command = []; this.discard = false;
    this.tx = null; this.pendingError = false; this.x5Request = 0;
    this.x5Raw = true; this.x5Stable = true; this.x5Blocked = true; this.debounceToken = 0;
    this.setX5(x5Low, true);
  }

  snapshot() {
    return { mask: this.mask, module: getModule(this.mask), locked: this.locked, state: this.state,
      error: this.error, x5Low: this.x5Raw, x5Stable: this.x5Stable, x5Armed: !this.x5Blocked,
      transmitting: this.tx?.channel || null, buffered: this.board.length, commandBytes: this.command.length };
  }

  event(type, title, data = {}) { this.emit({ type, title, ...data, state: this.snapshot() }); }

  setInputs(mask) {
    this.mask = mask & 15;
    this.event('gpio', '读取 X1～X4 输入快照', { detail: `实际模号 ${getModule(this.mask) || '无效'}；低电平有效` });
    if ((this.locked && getModule(this.mask) !== this.locked) || (this.x5Request && getModule(this.mask) !== this.x5Request)) this.fail(6, '活动模号失效；接回后需要重新启动');
    this.pump();
  }

  setX5(low, initial = false) {
    low = Boolean(low);
    if (low && (this.state !== STATES.IDLE || this.pendingError || this.x5Request || this.tx?.kind === 'error')) this.x5Blocked = true;
    if (this.x5Raw === low && !initial) return;
    this.x5Raw = low;
    const token = ++this.debounceToken;
    if (!initial) this.event('gpio', `X5 ${low ? '按下（低）' : '释放（高）'}`, { detail: '高、低电平分别稳定 20 ms 后确认' });
    this.schedule(this.clock() + 20, () => {
      if (token !== this.debounceToken || this.x5Stable === this.x5Raw) return;
      this.x5Stable = this.x5Raw;
      if (!this.x5Stable) {
        this.x5Blocked = false;
        this.event('debounce', 'X5 松开稳定，可以接受下一次按下');
      } else if (!this.x5Blocked) {
        const module = getModule(this.mask);
        if (!module) this.fail(6, 'X5 启动失败：模号必须唯一有效');
        else {
          this.x5Request = module;
          this.event('debounce', 'X5 按下稳定，接受一次启动请求', { detail: `确认模号 ${module}` });
        }
      } else this.event('ignored', 'X5 按下已消耗：上电按住或测试忙，不排队');
      this.pump();
    });
  }

  feedHost(value) {
    const data = bytes(value);
    this.event('wire', '上位机发送', { channel: CHANNELS.HOST, data: Array.from(data), raw: new TextDecoder().decode(data), stage: 0 });
    this.host.push(...data);
    this.pump();
  }

  feedBoard(value) {
    const data = bytes(value);
    this.event('wire', '测试主板发送', { channel: CHANNELS.BOARD, data: Array.from(data), raw: new TextDecoder().decode(data), stage: 0 });
    if (this.state === STATES.IDLE) {
      this.event('ignored', '当前没有活动会话，主板数据丢弃');
      return;
    }
    this.board.push(...data);
    this.event('buffer', '字节进入逻辑接收缓存', { stage: 1, detail: `${this.board.length} 字节等待处理；完整行结束后才转换` });
    this.pump();
  }

  start(module, source) {
    this.locked = module; this.error = 0; this.board = []; this.x5Request = 0;
    this.state = STATES.START;
    if (this.x5Raw) this.x5Blocked = true;
    this.event('state', '核对成功，锁定模号并清除旧主板数据', { stage: 2, detail: `${source} 启动；MH=${module}` });
    this.transmit('test\r\n', CHANNELS.TEST, 'start', () => {
      if (this.state === STATES.START) {
        this.state = STATES.WAIT;
        this.event('state', '等待首条记录', { detail: '没有首帧或帧间超时，主板可以稍后再发送' });
      }
    });
  }

  execute(command) {
    const cmd = trim(command);
    if (!cmd) return;
    this.event('parse', '主循环解析上位机命令', { stage: 2, detail: cmd });
    if (cmd.toUpperCase() === 'OK') { this.event('ignored', '忽略 OK 确认，不等待 ACK'); return; }
    if (cmd === '(RD_NUM)') {
      const module = getModule(this.mask);
      this.transmit(`{"Result":"${module || 'ERR'}"}\r\n`, CHANNELS.PC, 'query');
      return;
    }
    const match = /^\(START,([1-4])\)$/.exec(cmd);
    if (!match) { this.fail(1, '非法命令'); return; }
    const module = getModule(this.mask);
    if (module !== Number(match[1])) { this.fail(6, 'START 模号与唯一有效输入不匹配'); return; }
    this.start(module, 'START');
  }

  commandByte(c) {
    if (this.discard) {
      if (c === 40) { this.discard = false; this.command = []; }
      else { if ([41, 13, 10].includes(c)) this.discard = false; return; }
    }
    if (c === 13 || c === 10) {
      const command = new TextDecoder().decode(new Uint8Array(this.command)); this.command = [];
      this.execute(command); return;
    }
    if ((c < 32 && c !== 9) || this.command.length === LIMITS.command) {
      this.command = []; this.discard = true; this.fail(1, '命令包含控制字节或超过 23 字节'); return;
    }
    this.command.push(c);
    if (this.command.length === 2 && String.fromCharCode(...this.command).toUpperCase() === 'OK') {
      this.command = []; this.event('ignored', '忽略 OK 确认，不等待 ACK'); return;
    }
    if (c === 41) {
      const command = new TextDecoder().decode(new Uint8Array(this.command)); this.command = [];
      this.execute(command);
    }
  }

  pump() {
    if (this.tx) return;
    if (this.pendingError) {
      this.pendingError = false; this.board = [];
      this.transmit(ERR_FRAME, CHANNELS.PC, 'error'); return;
    }
    while (this.host.length && !this.tx) this.commandByte(this.host.shift());
    if (this.tx) return;
    if (this.x5Request && !this.command.length && !this.discard) {
      const module = this.x5Request;
      if (module === getModule(this.mask)) this.start(module, 'X5');
      else this.fail(6, '按键请求执行前模号变化');
      return;
    }
    if (this.state === STATES.IDLE) { this.board = []; return; }
    while (this.board.length && !this.tx) {
      const end = this.board.findIndex(c => c === 13 || c === 10);
      if (end < 0) {
        if (this.board.length > LIMITS.line) this.fail(3, '单行超过 511 字节');
        return;
      }
      if (end > LIMITS.line) { this.fail(3, '单行超过 511 字节'); return; }
      const data = new Uint8Array(this.board.splice(0, end)); this.board.shift();
      let line;
      try { line = new TextDecoder('utf-8', { fatal: true }).decode(data); }
      catch { this.fail(2, '记录包含非法 UTF-8 编码'); return; }
      const marker = trim(line).toLowerCase();
      if (!marker || marker === 'test') { if (marker) this.event('ignored', '忽略主板 test 标记'); continue; }
      if (marker === 'end') {
        if (this.state === STATES.WAIT) { this.fail(2, '没有有效结果就收到 end'); return; }
        this.state = STATES.IDLE; this.locked = 0; this.board = [];
        this.event('state', '收到 end，本轮结束', { detail: '不追加汇总帧，解除旧模号监测' }); return;
      }
      let record;
      try { record = parseRecord(data, this.locked); }
      catch (error) { this.fail(2, error.message); return; }
      this.state = STATES.RECEIVE;
      this.event('parse', '整条记录校验通过', { stage: 2, record, detail: `${record.name}；${record.groupCount} 组均合法` });
      this.event('encode', '提取首组并编码 JSON', { stage: 3, record, detail: record.selected });
      this.transmit(record.raw, CHANNELS.PC, 'result');
    }
  }

  fail(code, reason) {
    if (this.pendingError) return;
    this.error = code; this.state = STATES.IDLE; this.locked = 0; this.x5Request = 0; this.board = [];
    this.pendingError = true;
    if (this.x5Raw) this.x5Blocked = true;
    this.event('error', `ERR · ${reason}`, { detail: `app_last_error=${code}；已开始发送的帧完整发完后返回 ERR` });
    if (!this.tx) this.pump();
  }

  transmit(raw, channel, kind, done = () => {}) {
    const tx = { raw, channel, kind };
    this.tx = tx;
    this.event('tx-start', channel === CHANNELS.TEST ? 'UART1 下发启动指令' : 'UART2 返回上位机', { raw, channel, kind, stage: 4 });
    const duration = bytes(raw).length * 10 / 115200 * 1000;
    this.schedule(this.clock() + duration, () => {
      if (this.tx !== tx) return;
      this.tx = null;
      this.event('tx-end', '当前帧已完整发送', { channel, detail: `${bytes(raw).length} 字节 · 115200 / 8N1 · 理论线时 ${duration.toFixed(3)} ms` });
      done(); this.pump();
    });
  }
}
