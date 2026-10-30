import * as THREE from 'three';
import { OrbitControls } from 'three/addons/OrbitControls.js';

const C = { teal: 0x64dec8, orange: 0xf1b275, board: 0x174f48, dark: 0x14242c, metal: 0x9aafba, ink: 0xe5edf1 };
const V = (x, y, z) => new THREE.Vector3(x, y, z);

/** 全部模型由基本几何体生成；内部模块表示软件职责而非真实硅片布局。 */
export class Laboratory {
  constructor(canvas, labels, onPacket) {
    this.canvas = canvas; this.labels = labels; this.onPacket = onPacket;
    this.packets = []; this.anchors = []; this.focused = false;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
    this.renderer.shadowMap.enabled = true; this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.shadowMap.autoUpdate = false;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping; this.renderer.toneMappingExposure = 1.35;
    this.world = new THREE.Scene(); this.world.fog = new THREE.FogExp2(0x0b141b, 0.022);
    this.camera = new THREE.PerspectiveCamera(38, 1, 0.1, 100);
    this.controls = new OrbitControls(this.camera, canvas); this.controls.enableDamping = true;
    this.controls.minDistance = 5; this.controls.maxDistance = 30; this.controls.maxPolarAngle = Math.PI * 0.47;
    this.world.add(new THREE.HemisphereLight(0xc6e7f1, 0x11252b, 2));
    const key = new THREE.DirectionalLight(0xdcf2ff, 3.4); key.position.set(-4, 12, 7);
    key.castShadow = true; key.shadow.mapSize.set(1024, 1024); key.shadow.camera.left = -13; key.shadow.camera.right = 13;
    key.shadow.camera.top = 9; key.shadow.camera.bottom = -9; key.shadow.bias = -0.001; this.world.add(key);
    const rim = new THREE.DirectionalLight(C.teal, 1.8); rim.position.set(3, 4, -6); this.world.add(rim);
    const floor = this.box(this.world, [50, 0.15, 35], [0, -0.25, 0], 0x10212a, 0.8); floor.receiveShadow = true;
    const grid = new THREE.GridHelper(44, 44, 0x31515a, 0x1c343d); grid.position.y = -0.16; grid.material.transparent = true; grid.material.opacity = 0.38; this.world.add(grid);
    this.makeComputer(); this.makeMCU(); this.makeBoard(); this.makeWires();
    this.label('828C 上位机', 'HOST / UART2', V(-6.5, 3.8, 0), 'host');
    this.label('STC12C5A60S2', 'PDIP40 / 22.1184 MHz', V(0, 2.35, 0), 'host');
    this.label('测试主板', 'TEST MAINBOARD / UART1', V(6.5, 2.1, 0), 'board');
    this.label('UART2', '115200 / 8N1', V(-3.5, 0.1, 2.2), 'wire host');
    this.label('UART1', '115200 / 8N1', V(3.4, 0.1, 2.2), 'wire board');
    this.resizeObserver = new ResizeObserver(() => this.resize()); this.resizeObserver.observe(canvas.parentElement);
    this.setView(false); this.resize();
    canvas.addEventListener('webglcontextlost', event => {
      event.preventDefault(); document.getElementById('webgl-error').hidden = false;
    });
  }

  material(color, roughness = 0.55, metalness = 0) { return new THREE.MeshStandardMaterial({ color, roughness, metalness }); }
  box(parent, size, position, color, roughness = 0.5, metalness = 0) {
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(...size), this.material(color, roughness, metalness));
    mesh.position.set(...position); mesh.castShadow = true; mesh.receiveShadow = true; parent.add(mesh); return mesh;
  }
  cylinder(parent, radius, height, position, color) {
    const mesh = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, height, 16), this.material(color, 0.35, 0.5));
    mesh.position.set(...position); mesh.castShadow = true; parent.add(mesh); return mesh;
  }
  texture(text, subtitle = '', background = '#152a32', color = '#d2e9eb') {
    const canvas = document.createElement('canvas'); canvas.width = 768; canvas.height = 256;
    const ctx = canvas.getContext('2d'); ctx.fillStyle = background; ctx.fillRect(0, 0, 768, 256);
    ctx.fillStyle = color; ctx.font = `bold ${Math.min(140, 640 / (text.length * 0.65))}px "Microsoft YaHei", sans-serif`; ctx.textAlign = 'center'; ctx.fillText(text, 384, 130);
    ctx.fillStyle = '#8eafb7'; ctx.font = '29px Consolas, "Microsoft YaHei", monospace'; ctx.fillText(subtitle, 384, 181);
    const texture = new THREE.CanvasTexture(canvas); texture.colorSpace = THREE.SRGBColorSpace; return texture;
  }
  decal(parent, text, subtitle, size, position, flat = true, background, color) {
    const mesh = new THREE.Mesh(new THREE.PlaneGeometry(...size), new THREE.MeshBasicMaterial({ map: this.texture(text, subtitle, background, color), side: THREE.DoubleSide }));
    mesh.position.set(...position); if (flat) mesh.rotation.x = -Math.PI / 2; parent.add(mesh); return mesh;
  }
  makeComputer() {
    const group = new THREE.Group(); group.position.x = -6.5; this.world.add(group);
    this.box(group, [3.75, 2.4, 0.22], [0, 2.05, -0.35], 0x283d49, 0.3, 0.5);
    this.box(group, [3.48, 2.12, 0.04], [0, 2.08, -0.217], 0x030d15);
    const screen = document.createElement('canvas'); screen.width = 960; screen.height = 580;
    this.screenCanvas = screen; this.screenTexture = new THREE.CanvasTexture(screen); this.screenTexture.colorSpace = THREE.SRGBColorSpace;
    const monitor = new THREE.Mesh(new THREE.PlaneGeometry(3.35, 1.97), new THREE.MeshBasicMaterial({ map: this.screenTexture })); monitor.position.set(0, 2.08, -0.19); group.add(monitor);
    this.box(group, [0.25, 0.9, 0.25], [0, 0.64, -0.38], C.metal, 0.3, 0.8);
    this.box(group, [1.5, 0.08, 1.0], [0, 0.15, -0.25], 0x4c6470, 0.3, 0.8);
    this.box(group, [2.9, 0.12, 0.9], [-0.25, 0.16, 1.1], 0x344954, 0.5, 0.3);
    for (let row = 0; row < 4; row++) for (let col = 0; col < 13; col++) this.box(group, [0.16, 0.025, 0.12], [-1.5 + col * 0.2, 0.235, 0.82 + row * 0.19], 0x91a6b0);
    const mouse = new THREE.Mesh(new THREE.SphereGeometry(0.26, 20, 12), this.material(0x77909c)); mouse.scale.set(0.7, 0.45, 1.1); mouse.position.set(1.7, 0.2, 1.15); group.add(mouse);
    this.screenRows = []; this.drawScreen('READY / 等待指令');
  }
  drawScreen(status) {
    const ctx = this.screenCanvas.getContext('2d'); ctx.fillStyle = '#0c1e28'; ctx.fillRect(0, 0, 960, 580);
    ctx.fillStyle = '#61dfca'; ctx.font = 'bold 46px Consolas'; ctx.fillText('828C / TEST CONSOLE', 40, 70);
    ctx.fillStyle = '#264552'; ctx.fillRect(40, 100, 880, 2);
    ctx.font = '26px "Microsoft YaHei", sans-serif'; ctx.fillStyle = '#a5bfcc'; ctx.fillText(status.slice(0, 35), 40, 150);
    ctx.font = '24px Consolas';
    this.screenRows.slice(-5).forEach((row, i) => { ctx.fillStyle = row.includes('NG') || row.includes('ERR') ? '#f1b275' : '#91d6c7'; ctx.fillText(row.slice(0, 56), 40, 220 + i * 62); });
    ctx.fillStyle = '#4c7b87'; ctx.font = '22px Consolas'; ctx.fillText('UART2  115200 / 8N1                 ONLINE', 40, 545);
    this.screenTexture.needsUpdate = true;
  }
  pcb(parent, width, depth) {
    this.box(parent, [width, 0.12, depth], [0, 0.16, 0], C.board, 0.7);
    for (const x of [-width / 2 + 0.2, width / 2 - 0.2]) for (const z of [-depth / 2 + 0.2, depth / 2 - 0.2]) {
      this.cylinder(parent, 0.09, 0.04, [x, 0.24, z], 0xc3a76e); this.cylinder(parent, 0.04, 0.05, [x, 0.27, z], 0x17262e);
    }
    for (let i = 0; i < 7; i++) this.box(parent, [width - 0.7, 0.006, 0.012], [0, 0.225, -depth * 0.4 + i * depth * 0.12], 0x347d69);
  }
  makeMCU() {
    const group = new THREE.Group(); this.world.add(group); this.pcb(group, 4.4, 3.1);
    this.chipBody = this.box(group, [3.2, 0.38, 1.12], [0, 0.57, -0.12], 0x171e25, 0.55);
    this.chipTitle = this.decal(group, 'STC12C5A60S2', 'PDIP40 · 22.1184 MHz', [2.95, 0.95], [0, 0.766, -0.12], true, '#171e25', '#d0d9df');
    this.cylinder(group, 0.075, 0.01, [-1.4, 0.77, -0.42], 0x61707a);
    for (let i = 0; i < 20; i++) for (const side of [-1, 1]) {
      const x = -1.46 + i * 0.154;
      this.box(group, [0.055, 0.045, 0.25], [x, 0.47, -0.12 + side * 0.65], C.metal, 0.25, 0.8);
      this.box(group, [0.055, 0.22, 0.05], [x, 0.35, -0.12 + side * 0.77], C.metal, 0.25, 0.8);
      this.cylinder(group, 0.058, 0.015, [x, 0.23, -0.12 + side * 0.78], 0xb1a26c);
    }
    this.leds = [];
    for (let i = 0; i < 5; i++) {
      this.box(group, [0.34, 0.16, 0.24], [-1.25 + i * 0.62, 0.3, 1.08], 0x182a33);
      const led = this.box(group, [0.16, 0.08, 0.16], [-1.25 + i * 0.62, 0.43, 1.08], C.teal);
      led.material.emissive.setHex(C.teal); this.leds.push(led);
    }
    for (const x of [-2.05, 2.05]) for (let i = 0; i < 3; i++) this.box(group, [0.2, 0.3, 0.22], [x, 0.38, -0.65 + i * 0.3], 0x15242c);
    this.decal(group, 'PROTOCOL BRIDGE', 'UART2  ↔  UART1', [1.85, 0.5], [0, 0.228, -1.15], true, '#174f48');
    this.internals = new THREE.Group(); this.internals.position.y = 1.05; this.internals.visible = false; group.add(this.internals);
    const titles = ['RX', 'BUFFER', 'PARSE', 'JSON', 'TX']; this.internalNodes = [];
    for (let i = 0; i < 5; i++) {
      const block = this.box(this.internals, [0.55, 0.22, 0.68], [-1.28 + i * 0.64, 0, -0.12], 0x24525a, 0.3, 0.3);
      this.decal(this.internals, titles[i], `${i + 1}`, [0.53, 0.5], [-1.28 + i * 0.64, 0.116, -0.12], true, '#24525a', '#a9f0e2');
      this.internalNodes.push(block);
      if (i < 4) this.box(this.internals, [0.1, 0.035, 0.04], [-0.96 + i * 0.64, 0, -0.12], C.teal);
    }
  }
  makeBoard() {
    const group = new THREE.Group(); group.position.x = 6.5; this.world.add(group); this.pcb(group, 4.15, 3.2);
    this.box(group, [1.15, 0.22, 1.15], [-0.5, 0.36, -0.1], 0x1b242b);
    for (let i = 0; i < 9; i++) for (const side of [-1, 1]) this.box(group, [0.075, 0.025, 0.22], [-0.96 + i * 0.115, 0.3, -0.1 + side * 0.65], C.metal, 0.2, 0.8);
    this.box(group, [0.65, 0.3, 1.2], [1.1, 0.4, 0], 0x232a2d);
    for (let i = 0; i < 4; i++) this.cylinder(group, 0.1, 0.1, [1.1, 0.6, -0.45 + i * 0.3], 0xba9b5d);
    for (let i = 0; i < 7; i++) { this.box(group, [0.22, 0.14, 0.26], [-1.3 + i * 0.4, 0.3, 1.1], 0xc2bba2); this.box(group, [0.3, 0.1, 0.1], [-1.2 + i * 0.38, 0.3, -1.1], 0x8b8169); }
    this.decal(group, 'TEST MAINBOARD', 'TEXT RECORDS / OK · NG', [2.4, 0.7], [0, 0.23, -0.85], true, '#174f48');
  }
  makeWires() {
    const curve = (points, color) => {
      const path = new THREE.CatmullRomCurve3(points.map(p => V(...p)));
      const mesh = new THREE.Mesh(new THREE.TubeGeometry(path, 50, 0.025, 8, false), this.material(color, 0.4, 0.4)); this.world.add(mesh); return path;
    };
    this.curves = {
      'host-mcu': curve([[-4.58,0.28,-0.2],[-3.8,0.45,-0.4],[-2.7,0.4,-0.4],[-2.08,0.37,-0.25]], C.teal),
      'mcu-host': curve([[-2.08,0.37,0.4],[-2.7,0.4,0.75],[-3.8,0.45,0.75],[-4.58,0.28,0.4]], C.teal),
      'mcu-board': curve([[2.08,0.37,-0.25],[2.8,0.45,-0.4],[3.7,0.4,-0.4],[4.45,0.3,-0.2]], C.orange),
      'board-mcu': curve([[4.45,0.3,0.4],[3.7,0.45,0.75],[2.8,0.4,0.75],[2.08,0.37,0.4]], C.orange),
    };
    curve([[-4.58,0.14,1.5],[-3.4,0.1,2.2],[0,0.1,2.2],[3.4,0.1,2.2],[4.45,0.14,1.5]], 0x4a6571);
  }
  label(title, subtitle, position, cls = '') {
    const element = document.createElement('div'); element.className = `world-label ${cls}`;
    const strong = document.createElement('strong'); strong.textContent = title; element.append(strong);
    if (subtitle) { const small = document.createElement('small'); small.textContent = subtitle; element.append(small); }
    this.labels.append(element); const label = { element, position }; this.anchors.push(label); return label;
  }
  setView(focused) {
    this.needsRender = true; this.renderer.shadowMap.needsUpdate = true;
    this.focused = focused; this.internals.visible = focused;
    this.chipBody.material.transparent = focused; this.chipBody.material.opacity = focused ? 0.18 : 1; this.chipBody.material.depthWrite = !focused;
    this.chipTitle.visible = !focused;
    this.controls.target.set(0, focused ? 0.5 : 0.65, 0);
    // 横向链路以正面三分之二视角展示，避免远端设备被压缩。
    this.camera.position.set(focused ? 2 : 1.8, focused ? 4.4 : 12, focused ? 4.8 : 18.8);
    this.controls.update();
    this.anchors.forEach(anchor => { anchor.element.hidden = focused && Math.abs(anchor.position.x) > 2.5; });
    this.anchors[1].element.hidden = focused;
  }
  resize() {
    this.needsRender = true;
    const { width, height } = this.canvas.parentElement.getBoundingClientRect();
    if (!width || !height) return;
    this.renderer.setSize(width, height, false); this.camera.aspect = width / height; this.camera.updateProjectionMatrix();
    if (!this.focused) {
      const distance = Math.max(12.6, 10.5 / Math.tan(THREE.MathUtils.degToRad(19)) / this.camera.aspect);
      const direction = V(0.08, 0.52, 0.85).normalize().multiplyScalar(distance);
      this.camera.position.copy(this.controls.target).add(direction);
      this.controls.maxDistance = Math.max(35, distance * 1.3);
    }
    this.controls.update();
  }
  onEvent(event, playing) {
    this.needsRender = true;
    if (event.type === 'reset') { this.clearPackets(); this.screenRows = []; this.drawScreen('READY / 等待指令'); }
    this.leds.forEach((led, i) => {
      const active = i < 4 ? Boolean(event.state.mask & (1 << i)) : event.state.x5Low;
      led.material.color.setHex(active ? (i === 4 ? C.orange : C.teal) : 0x34434b);
      led.material.emissiveIntensity = active ? 0.6 : 0;
    });
    this.internalNodes.forEach((node, i) => { node.material.emissive.setHex(C.teal); node.material.emissiveIntensity = event.stage === i ? 0.6 : 0; });
    if (event.channel === 'mcu-host' && event.type === 'tx-start') { this.screenRows.push(event.raw.trim()); if (this.screenRows.length > 5) this.screenRows.shift(); this.drawScreen(event.state.state); }
    if (!['wire', 'tx-start'].includes(event.type) || !this.curves[event.channel]) return;
    const board = ['mcu-board', 'board-mcu'].includes(event.channel);
    const mesh = new THREE.Mesh(new THREE.SphereGeometry(0.095, 16, 10), new THREE.MeshBasicMaterial({ color: board ? C.orange : C.teal })); this.world.add(mesh);
    const label = document.createElement('button'); label.className = `world-label packet ${board ? 'board' : ''}`; label.type = 'button';
    label.textContent = event.raw.replace(/[\r\n]/g, '').slice(0, 24) || 'CR / LF'; label.setAttribute('aria-label', `查看数据包：${event.raw}`);
    label.onclick = () => this.onPacket(event); this.labels.append(label);
    const packet = { mesh, label, event, curve: this.curves[event.channel], age: playing ? 0 : 0.42 };
    this.packets.push(packet); if (this.packets.length > 10) this.removePacket(this.packets[0]);
  }
  removePacket(packet) {
    this.world.remove(packet.mesh); packet.mesh.geometry.dispose(); packet.mesh.material.dispose(); packet.label.remove(); this.packets.splice(this.packets.indexOf(packet), 1);
  }
  clearPackets() { while (this.packets.length) this.removePacket(this.packets[0]); }
  project(element, position, offset = 0) {
    const p = position.clone().project(this.camera); const width = this.canvas.clientWidth; const height = this.canvas.clientHeight;
    element.style.left = `${(p.x * 0.5 + 0.5) * width}px`; element.style.top = `${(-p.y * 0.5 + 0.5) * height + offset}px`;
    element.style.visibility = p.z > 1 || p.z < -1 || Math.abs(p.x) > 1.2 || Math.abs(p.y) > 1.2 ? 'hidden' : 'visible';
  }
  update(deltaMs, playing, speed) {
    const cameraChanged = this.controls.update();
    if (!cameraChanged && !this.needsRender && !(playing && this.packets.length)) return;
    for (const anchor of this.anchors) this.project(anchor.element, anchor.position);
    for (const packet of [...this.packets]) {
      if (playing) packet.age += deltaMs * speed / 650;
      if (packet.age >= 1.15) { this.removePacket(packet); continue; }
      packet.mesh.position.copy(packet.curve.getPointAt(Math.min(1, packet.age)));
      this.project(packet.label, packet.mesh.position, -22);
      packet.label.hidden = this.focused;
    }
    this.renderer.render(this.world, this.camera);
    this.needsRender = false;
  }
}
