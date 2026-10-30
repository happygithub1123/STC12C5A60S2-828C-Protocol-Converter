import { BridgeProtocol } from '../protocol/bridge.js';

/** 调度动作与内部截止时间；每次 step 只发布一个可观察事件。 */
export class SimulationEngine {
  constructor() { this.listeners = new Set(); this.speed = 1; this.reset(); }
  subscribe(listener) { this.listeners.add(listener); return () => this.listeners.delete(listener); }
  reset(options = {}) {
    this.now = 0; this.sequence = 0; this.order = 0; this.jobs = []; this.pending = [];
    this.history = []; this.playing = false; this.credit = 0; this.scenario = ''; this.pristine = true;
    this.protocol = new BridgeProtocol({
      ...options, clock: () => this.now,
      emit: event => this.pending.push({ ...event, timeMs: this.now }),
      schedule: (at, run) => this.schedule(at, run),
    });
    this.pending.unshift({ type: 'reset', title: '就绪 · 选择场景或手动操作', timeMs: 0, state: this.protocol.snapshot() });
    this.step();
  }
  schedule(at, run) {
    const job = { at: Math.max(this.now, at), order: this.order++, run };
    this.jobs.push(job);
    this.jobs.sort((a, b) => a.at - b.at || a.order - b.order);
    return job;
  }
  dispatch(action, at = this.now, fromScenario = false) {
    this.pristine = false;
    const job = this.schedule(at, () => {
      switch (action.type) {
        case 'host': this.protocol.feedHost(action.raw); break;
        case 'board': this.protocol.feedBoard(action.raw); break;
        case 'inputs': this.protocol.setInputs(action.mask); break;
        case 'x5': this.protocol.setX5(action.low); break;
        case 'note': this.protocol.event('note', action.title, { detail: action.detail || '' }); break;
        default: throw new Error(`未知动作：${action.type}`);
      }
    });
    job.fromScenario = fromScenario;
  }
  step() {
    while (!this.pending.length && this.jobs.length) {
      const job = this.jobs.shift(); this.now = job.at; job.run();
    }
    if (!this.pending.length) return null;
    const event = Object.freeze({ ...this.pending.shift(), seq: ++this.sequence });
    this.history.push(event);
    if (this.history.length > 1500) this.history.shift();
    this.view = event;
    for (const listener of this.listeners) listener(event);
    return event;
  }
  tick(deltaMs) {
    if (!this.playing) return;
    this.credit += Math.min(deltaMs, 250) * this.speed;
    if (this.credit >= 650) { this.credit %= 650; if (!this.step()) this.pause(); }
  }
  play() { this.playing = true; }
  pause() { this.playing = false; }
  setSpeed(value) { this.speed = Math.max(0.25, Math.min(4, Number(value) || 1)); }
  loadScenario(scenario) {
    this.reset(scenario.initial || {}); this.scenario = scenario.id;
    for (const item of scenario.actions) this.dispatch(item, item.at, true);
    this.play();
  }
  cancelScenario() { this.jobs = this.jobs.filter(job => !job.fromScenario); this.scenario = ''; }
}
