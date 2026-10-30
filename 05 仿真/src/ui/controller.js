import { bytes } from '../protocol/record.js';

const $ = id => document.getElementById(id);
const routes = { 'host-mcu': '上位机 → MCU / UART2', 'mcu-host': 'MCU → 上位机 / UART2', 'board-mcu': '主板 → MCU / UART1', 'mcu-board': 'MCU → 主板 / UART1' };
const visible = value => value.replace(/\r/g, '␍').replace(/\n/g, '␊');
const wireEvent = event => ['wire', 'tx-start'].includes(event.type);

export class Controller {
  constructor(engine, scenarios) {
    this.engine = engine; this.scenarios = scenarios; this.rows = []; this.scene = null; this.stage = -1;
    for (const scenario of scenarios) { const option = document.createElement('option'); option.value = scenario.id; option.textContent = scenario.name; $('scenario').append(option); }
    const describe = () => { $('scenario-description').textContent = scenarios.find(item => item.id === $('scenario').value).description; };
    $('scenario').onchange = describe; describe();
    $('demo').onclick = () => { engine.loadScenario(scenarios.find(item => item.id === $('scenario').value)); this.syncTransport(); };
    $('play').onclick = () => {
      if (engine.playing) engine.pause();
      else if (engine.pristine || (!engine.jobs.length && !engine.pending.length && engine.protocol.state === 'APP_IDLE')) engine.loadScenario(scenarios.find(item => item.id === $('scenario').value));
      else engine.play();
      this.syncTransport();
    };
    $('step').onclick = () => { engine.pause(); engine.step(); this.syncTransport(); };
    $('reset').onclick = () => { engine.reset(); this.syncTransport(); };
    $('speed').onchange = () => engine.setSpeed($('speed').value);
    $('overview').onclick = () => this.setView(false); $('chip-view').onclick = () => this.setView(true);
    for (let i = 1; i <= 4; i++) $(`x${i}`).onchange = () => {
      let mask = 0; for (let x = 1; x <= 4; x++) if ($(`x${x}`).checked) mask |= 1 << (x - 1);
      this.send({ type: 'inputs', mask });
    };
    $('x5-down').onclick = () => this.send({ type: 'x5', low: true });
    $('x5-up').onclick = () => this.send({ type: 'x5', low: false });
    $('x5-tap').onclick = () => {
      const at = this.send({ type: 'x5', low: true }); engine.dispatch({ type: 'x5', low: false }, at + 100);
    };
    $('rd-num').onclick = () => this.send({ type: 'host', raw: '(RD_NUM)' });
    $('send-start').onclick = () => this.send({ type: 'host', raw: `(START,${$('start-module').value})` });
    $('send-ok').onclick = () => this.send({ type: 'host', raw: 'OK' });
    $('send-command').onclick = () => this.send({ type: 'host', raw: $('command').value });
    $('command').onkeydown = event => { if (event.key === 'Enter') $('send-command').click(); };
    const ending = () => ({ crlf: '\r\n', cr: '\r', lf: '\n', none: '' })[$('line-ending').value];
    $('send-board').onclick = () => this.send({ type: 'board', raw: $('board-data').value + ending() });
    $('send-end').onclick = () => this.send({ type: 'board', raw: 'end' + ending() });
    $('wire-only').onchange = () => this.filterLog(); $('clear-log').onclick = () => this.clearLog();
    $('close-dialog').onclick = () => $('packet-dialog').close();
    $('packet-dialog').onclick = event => { if (event.target === $('packet-dialog')) $('packet-dialog').close(); };
    engine.subscribe(event => this.onEvent(event)); this.onEvent(engine.view); this.syncTransport();
  }
  send(action) {
    // 手动操作暂停预设场景，避免后续脚本改写用户刚刚设置的输入。
    if (this.engine.scenario) {
      this.engine.cancelScenario();
    }
    const at = Math.max(this.engine.now, 25);
    this.engine.dispatch(action, at);
    this.engine.play();
    this.syncTransport();
    return at;
  }
  setView(focused) {
    this.scene?.setView(focused);
    if (!focused) this.scene?.resize();
    $('overview').classList.toggle('active', !focused); $('chip-view').classList.toggle('active', focused);
    $('overview').setAttribute('aria-pressed', String(!focused)); $('chip-view').setAttribute('aria-pressed', String(focused));
  }
  syncTransport() { $('play').textContent = this.engine.playing ? 'Ⅱ 暂停' : '▶ 播放'; }
  clearLog() { $('event-log').replaceChildren(); this.rows = []; }
  filterLog() { for (const { row, event } of this.rows) row.classList.toggle('is-hidden', $('wire-only').checked && !wireEvent(event)); }
  onEvent(event) {
    if (!event) return;
    if (event.type === 'reset') {
      this.clearLog(); this.stage = -1; $('record-input').textContent = '等待主板发送一条完整记录…'; $('record-first').textContent = '—'; $('record-json').textContent = '—';
      $('record-result').textContent = '等待完整记录'; $('record-result').className = 'badge';
    }
    const state = event.state;
    $('clock').textContent = event.timeMs.toFixed(3); $('state-name').textContent = state.state;
    $('actual-module').textContent = state.module || '无效'; $('locked-module').textContent = state.locked || '—';
    for (let i = 1; i <= 4; i++) {
      const element = $(`x${i}`); element.checked = Boolean(state.mask & (1 << (i - 1)));
      element.parentElement.querySelector('b').textContent = element.checked ? '低' : '高';
    }
    $('x5-state').textContent = state.x5Low ? '按下 · 低' : state.x5Armed ? '已松开' : '释放确认中';
    $('x5-state').className = `badge ${state.x5Low ? 'ng' : ''}`;
    if (event.stage !== undefined) this.stage = event.stage;
    if (['reset', 'error', 'tx-end'].includes(event.type) || (event.type === 'state' && ['APP_IDLE', 'APP_WAIT_FIRST'].includes(state.state))) this.stage = -1;
    document.querySelectorAll('[data-stage]').forEach(node => node.classList.toggle('active', Number(node.dataset.stage) === this.stage));
    $('current-event').textContent = `${event.title}${event.detail ? ` · ${event.detail}` : ''}`;
    $('event-count').textContent = String(event.seq).padStart(3, '0');
    if (event.record) {
      const record = event.record;
      $('record-input').textContent = record.line;
      if (event.type === 'parse') { $('record-first').textContent = '—'; $('record-json').textContent = '—'; }
      if (event.type === 'encode') $('record-first').textContent = `${record.selected}\n${record.groupCount} 组 → 上传 ${record.first ? '首组' : '结果'}`;
      $('record-result').textContent = record.result; $('record-result').className = `badge ${record.result.toLowerCase()}`;
    }
    if (event.type === 'tx-start' && event.kind === 'result') $('record-json').textContent = visible(event.raw);
    if (event.type === 'error') { $('record-result').textContent = `ERR / ${state.error}`; $('record-result').className = 'badge error'; }
    this.addLog(event); this.scene?.onEvent(event, this.engine.playing); this.syncTransport();
  }
  addLog(event) {
    const row = document.createElement('div');
    const isBoard = ['board-mcu', 'mcu-board'].includes(event.channel);
    row.className = `log-row ${event.channel ? (isBoard ? 'board' : 'host') : event.type === 'error' ? 'error' : ''}`;
    const seq = document.createElement('span'); seq.className = 'seq'; seq.textContent = String(event.seq).padStart(3, '0');
    const time = document.createElement('span'); time.className = 'time'; time.textContent = `${event.timeMs.toFixed(3)} ms`;
    const route = document.createElement('span'); route.className = 'route'; route.textContent = wireEvent(event) ? routes[event.channel] : 'MCU / 逻辑事件';
    const message = document.createElement(wireEvent(event) ? 'button' : 'span'); message.className = 'message';
    message.textContent = wireEvent(event) ? visible(event.raw) : event.title;
    if (wireEvent(event)) { message.type = 'button'; message.onclick = () => this.showPacket(event); }
    row.append(seq, time, route, message); const log = $('event-log');
    const follow = log.scrollHeight - log.scrollTop - log.clientHeight < 50;
    log.append(row); this.rows.push({ row, event });
    if (this.rows.length > 500) this.rows.shift().row.remove();
    row.classList.toggle('is-hidden', $('wire-only').checked && !wireEvent(event));
    if (follow) log.scrollTop = log.scrollHeight;
  }
  showPacket(event) {
    $('packet-meta').textContent = `#${event.seq} · ${event.timeMs.toFixed(3)} ms · ${routes[event.channel]} · ${(event.data || bytes(event.raw)).length} 字节`;
    $('packet-text').textContent = visible(event.raw);
    $('packet-hex').textContent = Array.from(event.data || bytes(event.raw), n => n.toString(16).toUpperCase().padStart(2, '0')).join(' ');
    if (!$('packet-dialog').open) $('packet-dialog').showModal();
  }
}
