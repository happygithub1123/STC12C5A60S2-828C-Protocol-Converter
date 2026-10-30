import { SimulationEngine } from './simulation/engine.js';
import { createScenarios } from './simulation/scenarios.js';
import { Controller } from './ui/controller.js';

async function main() {
  const response = await fetch('./assets/continuous-input.txt');
  if (!response.ok) throw new Error('读取本地演示样本失败');
  const scenarios = createScenarios(await response.text());
  const engine = new SimulationEngine(); const ui = new Controller(engine, scenarios);
  try {
    const { Laboratory } = await import('./scene/laboratory.js');
    ui.scene = new Laboratory(document.getElementById('scene'), document.getElementById('world-labels'), event => ui.showPacket(event));
    ui.scene.onEvent(engine.view, false);
  } catch (error) {
    document.getElementById('webgl-error').hidden = false;
    console.error('3D 初始化失败：', error);
  }
  let last = performance.now();
  const frame = now => {
    const delta = Math.min(now - last, 250); last = now;
    const playing = engine.playing;
    engine.tick(delta); if (playing !== engine.playing) ui.syncTransport();
    ui.scene?.update(delta, engine.playing, engine.speed);
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}
main().catch(error => {
  document.getElementById('current-event').textContent = `启动失败：${error.message}。请通过“启动仿真.cmd”打开。`;
  console.error(error);
});
