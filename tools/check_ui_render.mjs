// ============================================================================
//  check_ui_render.mjs — rend l'UI hors navigateur pour la tester en CI.
//
//  Un DOM minimal (assez pour app.js : createElement/append/addEventListener…)
//  et un `fetch` bouchonné qui sert data/config.json. On rend ensuite CHAQUE
//  onglet pour CHAQUE combinaison air x valve x slide : toute erreur JS ou tout
//  onglet vide ressort ici au lieu de sortir sur le banc.
//
//  Lancer :  node tools/check_ui_render.mjs
// ============================================================================
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import vm from 'node:vm';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => readFileSync(path.join(ROOT, p), 'utf8');

// ---- DOM minimal ------------------------------------------------------------
let nodeCount = 0;
function makeNode(tag) {
  const n = {
    tag, children: [], attrs: {}, listeners: {}, className: '', _text: '', innerHTML: '',
    style: {}, files: [],
    setAttribute(k, v) { this.attrs[k] = v; },
    addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); },
    append(...kids) { for (const k of kids) { this.children.push(k); nodeCount++; } },
    replaceChildren(...kids) { this.children = []; this.append(...kids); },
    remove() {},
    click() {},
    closest() { return makeNode('div'); },
    querySelector() { return makeNode('input'); },
    get textContent() { return this._text; },
    set textContent(v) { this._text = String(v); },
  };
  return n;
}
const body = makeNode('body');
const byId = {};
for (const id of ['tabs', 'content', 'dirty', 'conn', 'toast']) byId[id] = makeNode('div');

// ---- fetch bouchonné --------------------------------------------------------
const config = JSON.parse(read('data/config.json'));
const STATUS = {
  homed: true, ready: true, pistonMm: 150, pressureBlow: 0.3, pressureDraw: 0.28,
  dutyBlow: 0.5, dutyDraw: 0.4, assignmentGen: 2, setpointKpa: 0.3, voices: 1,
  mixedCapable: true, transports: 1, mock: true, harmonica: 'Diatonic C Richter',
  droppedMidi: 0, holeBlowMask: 1, holeDrawMask: 4, holeCount: 10, slidePresent: true,
  slideEngaged: false, freeHeap: 180000,
  caps: { simultaneous: true, hasPiston: true, needsHoming: true, railsSwap: true,
          hasPumps: false, hasDiverter: false, pressureSensors: 2 },
};
const routes = {
  '/api/config': () => config,
  '/api/status': () => STATUS,
  '/api/capabilities': () => ({ configVersion: 2, maxHoles: 24, air: [], valve: [] }),
  '/api/harmonicas': () => ['diatonic_C.json', 'chromatic_C.json'],
};
const fetchStub = async (url) => {
  const key = String(url).split('?')[0];
  const data = routes[key] ? routes[key]() : (key.startsWith('/presets/') ? JSON.parse(read('data' + key)) : { ok: true });
  return { ok: true, status: 200, text: async () => JSON.stringify(data) };
};

// ---- Bac à sable ------------------------------------------------------------
const sandbox = {
  console, setTimeout, clearTimeout, setInterval: () => 0, Blob: class {}, URL: { createObjectURL: () => '' },
  fetch: fetchStub, confirm: () => true, alert: () => {},
  document: { createElement: makeNode, getElementById: (id) => byId[id] || makeNode('div'), body },
  window: { addEventListener: (t, fn) => { if (t === 'DOMContentLoaded') sandbox.__onload = fn; } },
};
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(read('data/schema.js') + '\nglobalThis.__ui = { SECTIONS, AIR_IMPLS, VALVE_IMPLS };', sandbox, { filename: 'schema.js' });
vm.runInContext(read('data/app.js') + '\nglobalThis.__app = { S, render, validate, sset };', sandbox, { filename: 'app.js' });

const { SECTIONS, AIR_IMPLS, VALVE_IMPLS } = sandbox.__ui;
const app = sandbox.__app;

// ---- Rendu exhaustif --------------------------------------------------------
const failures = [];
await sandbox.__onload();                                   // charge la config, rend l'onglet initial
if (!app.S.cfg) failures.push('la configuration n’a pas été chargée');

for (const air of AIR_IMPLS.map(([id]) => id)) {
  for (const valve of VALVE_IMPLS.map(([id]) => id)) {
    for (const slide of [false, true]) {
      app.sset(app.S.cfg, 'air.impl', air);
      app.sset(app.S.cfg, 'valve.impl', valve);
      app.sset(app.S.cfg, 'slide.enabled', slide);
      for (const sec of SECTIONS) {
        app.S.tab = sec.id;
        const before = nodeCount;
        try { app.render(); } catch (e) { failures.push(`${air}/${valve}/slide=${slide} · onglet « ${sec.title} » : ${e.message}`); continue; }
        if (nodeCount === before) failures.push(`${air}/${valve}/slide=${slide} · onglet « ${sec.title} » : rendu vide`);
      }
    }
  }
}

// ---- Les conditions d'affichage montrent-elles les BONS réglages ? ---------
//  (« rendu non vide » ne suffit pas : on vérifie que le montage choisi fait
//  bien apparaître ses propres réglages et disparaître ceux des autres.)
function visibleFields(cfg) {
  const out = new Set();
  for (const sec of SECTIONS)
    for (const block of sec.blocks) {
      if (block.w && !block.w(cfg)) continue;
      for (const f of block.fields || []) if (!f.w || f.w(cfg)) out.add(f.p);
    }
  return out;
}
function expectVisibility(patch, shown, hidden) {
  for (const [k, v] of Object.entries(patch)) app.sset(app.S.cfg, k, v);
  const vis = visibleFields(app.S.cfg);
  const label = Object.entries(patch).map(([k, v]) => `${k}=${v}`).join(' ');
  for (const p of shown) if (!vis.has(p)) failures.push(`${label} : « ${p} » devrait être visible`);
  for (const p of hidden) if (vis.has(p)) failures.push(`${label} : « ${p} » devrait être masqué`);
}

expectVisibility({ 'air.impl': 'pumpPair' },
  ['air.pumpPair.blowPump.pin', 'air.pumpPair.drawPump.minDuty', 'air.pumpPair.idleDuty', 'air.pumpPair.blowSensor.type'],
  ['air.dualReservoirPiston.travelMm', 'air.singleBellows.addr', 'board.stepper.step', 'board.endstops.r1']);
expectVisibility({ 'air.impl': 'pumpPair', 'air.pumpPair.sharedSensor': true },
  ['air.pumpPair.blowSensor.type'], ['air.pumpPair.drawSensor.type']);
expectVisibility({ 'air.impl': 'pumpPair', 'air.pumpPair.blowPump.drive': 'esc' },
  ['air.pumpPair.blowPump.escMinUs', 'air.pumpPair.blowPump.channel'], ['air.pumpPair.blowPump.pin']);
expectVisibility({ 'air.impl': 'dualReservoirPiston' },
  ['air.dualReservoirPiston.travelMm', 'board.stepper.step', 'board.endstops.r1', 'air.dualReservoirPiston.r1Addr'],
  ['air.pumpPair.idleDuty', 'air.dualReservoirPiston.r1AdcPin']);
expectVisibility({ 'air.impl': 'dualReservoirPiston', 'air.dualReservoirPiston.pressureType': 'mpx2010' },
  ['air.dualReservoirPiston.r1AdcPin'], ['air.dualReservoirPiston.r1Addr']);
expectVisibility({ 'air.impl': 'singlePumpReversible', 'air.singlePumpReversible.diverter': 'solenoid' },
  ['air.singlePumpReversible.solenoidBlowState', 'valve.solenoids.impl'], ['air.singlePumpReversible.blowAngle']);
expectVisibility({ 'air.impl': 'singleBellows', 'valve.impl': 'solenoid2in1', 'slide.enabled': false },
  ['valve.solenoids.holdDuty', 'valve.solenoids.peakMs'], ['slide.channel', 'valve.servoUs.min']);
expectVisibility({ 'valve.impl': 'solenoid2in1', 'valve.solenoids.impl': 'gpio' },
  ['valve.solenoids.gpioPins'], ['valve.solenoids.holdDuty']);
expectVisibility({ 'valve.impl': 'valve2in1', 'slide.enabled': true, 'slide.impl': 'servo' },
  ['valve.servoUs.min', 'slide.engagedAngle'], ['valve.solenoids.impl']);
expectVisibility({ 'midi.activeWireless': 'ble' }, ['midi.ble.deviceName'], ['midi.wifi.ssid']);
expectVisibility({ 'midi.activeWireless': 'wifi' }, ['midi.wifi.ssid', 'midi.wifi.rtpPort'], ['midi.ble.deviceName']);
expectVisibility({ 'engine.bendEnabled': false }, ['engine.pitchBendRangeSemitones'], ['engine.bendPressureGain']);

// La validation doit signaler les incohérences classiques.
app.sset(app.S.cfg, 'harmonica.hasSlide', true);
app.sset(app.S.cfg, 'slide.enabled', false);
if (!app.validate(app.S.cfg).some((p) => p.level === 'err'))
  failures.push('validate() ne détecte pas un mapping chromatique sans slide monté');

if (failures.length) {
  console.error('[ui] ÉCHEC du rendu :');
  for (const f of failures) console.error('  - ' + f);
  process.exit(1);
}
console.log(`[ui] rendu OK — ${SECTIONS.length} onglets x ${AIR_IMPLS.length * VALVE_IMPLS.length * 2} montages ` +
            `+ 12 contrôles de visibilité conditionnelle, ${nodeCount} nœuds produits`);
