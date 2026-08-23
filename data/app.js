// ============================================================================
//  app.js — moteur de rendu de la page de configuration (vanilla JS, 0 dépendance).
//
//  Il ne connaît AUCUN réglage en particulier : il rend ce que décrit
//  schema.js (SECTIONS), lit/écrit dans l'objet `cfg` par chemin, et masque
//  les blocs dont la condition `w` est fausse. Ajouter une option matérielle
//  = une ligne dans schema.js, rien ici.
//
//  Routes utilisées : GET/POST /api/config · GET /api/status ·
//  GET /api/capabilities · POST /api/calibrate · POST /api/test ·
//  GET /api/harmonicas · POST /api/harmonica · POST /api/reboot
// ============================================================================
'use strict';

// ---- Accès par chemin ("air.pumpPair.idleDuty") -----------------------------
const g = (obj, path) => path.split('.').reduce((o, k) => (o == null ? undefined : o[k]), obj);
const sset = (obj, path, val) => {
  const keys = path.split('.');
  const last = keys.pop();
  let o = obj;
  for (const k of keys) { if (typeof o[k] !== 'object' || o[k] === null) o[k] = {}; o = o[k]; }
  o[last] = val;
};

// ---- État global ------------------------------------------------------------
const S = {
  cfg: null,          // configuration en cours d'édition
  caps: null,         // /api/capabilities (ce que le firmware sait faire)
  status: {},         // dernière télémétrie
  tab: 'status',
  dirty: false,
  online: false,
  clearedSecrets: {}, // chemins de secrets marqués « à effacer »
};
const SECRET_SENTINEL = '__clear__';

// ---- Petits helpers DOM -----------------------------------------------------
const $ = (id) => document.getElementById(id);
function el(tag, attrs = {}, ...kids) {
  const n = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') n.className = v;
    else if (k === 'html') n.innerHTML = v;
    else if (k.startsWith('on')) n.addEventListener(k.slice(2), v);
    else if (v !== null && v !== undefined && v !== false) n.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false) n.append(kid);
  return n;
}
function toast(text, kind = 'ok') {
  const t = $('toast');
  t.textContent = text;
  t.className = 'toast show ' + kind;
  clearTimeout(toast._t);
  toast._t = setTimeout(() => { t.className = 'toast'; }, 3500);
}
const NOTE_NAMES = ['Do', 'Do#', 'Ré', 'Ré#', 'Mi', 'Fa', 'Fa#', 'Sol', 'Sol#', 'La', 'La#', 'Si'];
const noteName = (n) => (n >= 0 && n <= 127) ? NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1) : '?';

// ---- Réseau -----------------------------------------------------------------
async function api(path, opts) {
  const r = await fetch(path, opts);
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const txt = await r.text();
  return txt ? JSON.parse(txt) : {};
}
const post = (path, body) =>
  api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });

async function loadConfig() {
  try {
    S.cfg = await api('/api/config');
    S.dirty = false; S.clearedSecrets = {};
    render();
    toast('Configuration chargée');
  } catch (e) { toast('Chargement impossible : ' + e.message, 'err'); }
}

async function loadCaps() {
  try { S.caps = await api('/api/capabilities'); }
  catch (e) { S.caps = null; }        // repli : les listes de schema.js suffisent
}

// Les secrets ne sont jamais renvoyés en clair par le firmware : on n'envoie
// que ceux que l'utilisateur a réellement saisis, et le sentinelle d'effacement
// quand il demande explicitement de les vider.
function payloadForSave() {
  const out = JSON.parse(JSON.stringify(S.cfg));
  for (const p of SECRET_PATHS) {
    const v = g(out, p);
    if (S.clearedSecrets[p]) { sset(out, p, SECRET_SENTINEL); continue; }
    if (v === '' || v === undefined) {
      const keys = p.split('.'); const last = keys.pop();
      const parent = keys.reduce((o, k) => (o == null ? undefined : o[k]), out);
      if (parent) delete parent[last];        // absent => le firmware garde l'existant
    }
  }
  return out;
}

async function saveConfig() {
  const problems = validate(S.cfg).filter((p) => p.level === 'err');
  if (problems.length && !confirm('Des erreurs de cohérence subsistent :\n\n- ' +
      problems.map((p) => p.text).join('\n- ') + '\n\nSauvegarder quand même ?')) return;
  try {
    const j = await post('/api/config', payloadForSave());
    if (j.ok) {
      S.dirty = false; S.clearedSecrets = {};
      renderBar();
      toast('Sauvegardé — redémarrer pour appliquer le câblage');
    } else toast('Refusé : ' + (j.error || 'configuration invalide'), 'err');
  } catch (e) { toast('Échec réseau : ' + e.message, 'err'); }
}

async function reboot() {
  if (!confirm('Redémarrer le contrôleur ?')) return;
  try { await post('/api/reboot', {}); toast('Redémarrage en cours…'); }
  catch (e) { toast('Échec réseau', 'err'); }
}

const calibrate = (payload) =>
  post('/api/calibrate', payload)
    .then((j) => { if (!j.ok) toast('Commande refusée', 'err'); })
    .catch(() => toast('Échec réseau', 'err'));
const testNote = (action, note, velocity) => post('/api/test', { action, note, velocity }).catch(() => {});
const panic = () => post('/api/test', { action: 'allOff' }).then(() => toast('Tout coupé')).catch(() => {});

// ---- Contrôle de cohérence --------------------------------------------------
//  Le firmware ignore silencieusement ce qui n'est pas routable ; l'UI, elle,
//  doit le DIRE avant que l'utilisateur ne cherche pourquoi un trou est muet.
function validate(cfg) {
  const out = [];
  if (!cfg) return out;
  const add = (level, text) => out.push({ level, text });
  const airId = g(cfg, 'air.impl'), valveId = g(cfg, 'valve.impl');
  const air = AIR_TRAITS[airId] || {}, valve = VALVE_TRAITS[valveId] || {};
  const holes = g(cfg, `valve.${valveId}.holes`) || [];
  const holeCount = g(cfg, 'harmonica.holeCount') || 0;
  const notes = g(cfg, 'harmonica.notes') || [];

  if (holes.length < holeCount)
    add('err', `${holeCount} trous déclarés sur l’harmonica mais ${holes.length} câblés dans « Distribution »`);
  const badNotes = notes.filter((n) => n.hole >= holeCount);
  if (badNotes.length)
    add('err', `${badNotes.length} note(s) pointent vers un trou ≥ ${holeCount} : elles seront ignorées`);
  if (g(cfg, 'harmonica.hasSlide') && !g(cfg, 'slide.enabled'))
    add('err', 'Le mapping utilise le slide mais aucun slide n’est monté (onglet Distribution)');
  if (!notes.length) add('err', 'Aucune note mappée : l’instrument restera muet');

  const dup = new Map();
  notes.forEach((n) => dup.set(n.note, (dup.get(n.note) || 0) + 1));
  const dups = [...dup.entries()].filter(([, c]) => c > 1);
  if (dups.length)
    add('info', `${dups.length} note(s) en double (${dups.slice(0, 6).map(([n]) => noteName(n)).join(', ')}…) : la première entrée gagne`);

  if (!(air.simultaneous && valve.perHole))
    add('info', 'Ce montage ne peut pas souffler et aspirer en même temps — arbitrage « ' +
                (g(cfg, 'engine.arbitration') === 'steal' ? 'couper les notes en cours' : 'refuser la nouvelle note') + ' »');
  if ((g(cfg, 'engine.maxPolyphony') || 0) > holeCount && holeCount)
    add('info', `Polyphonie (${g(cfg, 'engine.maxPolyphony')}) supérieure au nombre de trous (${holeCount})`);

  // Collisions de canaux sur le bus servo (chaque servo doit être seul).
  const used = new Map();
  const claim = (ch, who) => {
    if (ch === undefined || ch === null || ch > 31) return;
    if (used.has(ch)) add('err', `Canal servo ${ch} utilisé deux fois : ${used.get(ch)} et ${who}`);
    else used.set(ch, who);
  };
  if (valve.actuator === 'servo') holes.forEach((h, i) => claim(h.channel, `trou ${i + 1}`));
  if (airId === 'dualReservoirPiston') {
    claim(g(cfg, 'air.dualReservoirPiston.valveR1Channel'), 'vanne R1');
    claim(g(cfg, 'air.dualReservoirPiston.valveR2Channel'), 'vanne R2');
  }
  if (airId === 'pumpPair') claim(g(cfg, 'air.pumpPair.bleedChannel'), 'purge plenum');
  if (airId === 'singlePumpReversible' && g(cfg, 'air.singlePumpReversible.diverter') === 'servo')
    claim(g(cfg, 'air.singlePumpReversible.diverterChannel'), 'aiguillage');
  if (g(cfg, 'slide.enabled') && g(cfg, 'slide.impl') === 'servo') claim(g(cfg, 'slide.channel'), 'slide');
  for (const pumpPath of ['air.pumpPair.blowPump', 'air.pumpPair.drawPump', 'air.singlePumpReversible.pump']) {
    if (!pumpPath.startsWith('air.' + airId)) continue;
    if (g(cfg, pumpPath + '.drive') !== 'ledc') claim(g(cfg, pumpPath + '.channel'), 'pompe');
  }
  if (valve.actuator === 'solenoid' && g(cfg, 'valve.solenoids.impl') === 'gpio') {
    const need = holes.length * valve.channels;
    const have = (g(cfg, 'valve.solenoids.gpioPins') || []).length;
    if (have < need) add('err', `Bus GPIO : ${need} broches nécessaires pour ${holes.length} trous, ${have} déclarées`);
  }
  return out;
}

// ---- Rendu d'un champ -------------------------------------------------------
const SECRET_PATHS = ['midi.wifi.password', 'midi.wifi.apPassword', 'web.password'];

function markDirty() { S.dirty = true; renderBar(); }

function fieldRow(f) {
  const v = g(S.cfg, f.p);
  let input;
  const onChange = (conv) => (ev) => {
    sset(S.cfg, f.p, conv(ev.target));
    markDirty();
    if (f.rerender !== false) renderSection();   // les conditions `w` peuvent changer
  };

  switch (f.t) {
    case 'bool':
      input = el('input', { type: 'checkbox', ...(v ? { checked: true } : {}), onchange: onChange((t) => t.checked) });
      break;
    case 'sel':
      input = el('select', { onchange: onChange((t) => t.value) },
        f.o.map(([val, lab]) => el('option', { value: val, ...(val === v ? { selected: true } : {}) }, lab)));
      break;
    case 'pass': {
      const cleared = !!S.clearedSecrets[f.p];
      input = el('div', { class: 'pass' },
        el('input', {
          type: 'password', value: '', placeholder: cleared ? '(sera effacé)' : '•••••••• (inchangé)',
          oninput: (ev) => { sset(S.cfg, f.p, ev.target.value); delete S.clearedSecrets[f.p]; markDirty(); },
        }),
        el('button', { class: 'mini', type: 'button', onclick: () => {
          S.clearedSecrets[f.p] = true; sset(S.cfg, f.p, ''); markDirty(); renderSection();
        } }, 'effacer'));
      break;
    }
    case 'hex':
      input = el('input', { type: 'text', value: typeof v === 'number' ? '0x' + v.toString(16) : (v ?? ''),
        onchange: onChange((t) => t.value.trim()) });
      break;
    case 'ints':   // liste d'entiers séparés par des virgules (broches GPIO)
      input = el('input', { type: 'text', value: (v || []).join(', '), placeholder: '13, 14, 27, 33',
        onchange: onChange((t) => t.value.split(',').map((x) => parseInt(x, 10)).filter((x) => !isNaN(x))) });
      break;
    case 'text':
      input = el('input', { type: 'text', value: v ?? '', onchange: onChange((t) => t.value) });
      break;
    default: {   // int / num
      const step = f.t === 'int' ? 1 : (f.step || 0.01);
      input = el('input', {
        type: 'number', value: v ?? 0, step,
        ...(f.min !== undefined ? { min: f.min } : {}), ...(f.max !== undefined ? { max: f.max } : {}),
        onchange: onChange((t) => {
          let x = f.t === 'int' ? parseInt(t.value, 10) : parseFloat(t.value);
          if (isNaN(x)) x = 0;
          if (f.min !== undefined && x < f.min) x = f.min;
          if (f.max !== undefined && x > f.max) x = f.max;
          t.value = x;
          return x;
        }),
      });
    }
  }
  return el('div', { class: 'field' },
    el('label', {}, f.l, f.u ? el('span', { class: 'unit' }, ' (' + f.u + ')') : null),
    input,
    f.h ? el('small', {}, f.h) : null);
}

// ---- Éditeurs spécialisés ---------------------------------------------------
const HOLE_COLUMNS = {
  valve2in1:    [['hole', 'Trou', 0], ['channel', 'Canal servo', 0], ['railAangle', 'Angle rail A', 30], ['railBangle', 'Angle rail B', 150], ['closedAngle', 'Angle fermé', 90]],
  valve1in1:    [['hole', 'Trou', 0], ['channel', 'Canal servo', 0], ['openAngle', 'Angle ouvert', 90], ['closedAngle', 'Angle fermé', 0]],
  solenoid2in1: [['hole', 'Trou', 0], ['railAchannel', 'Vanne rail A', 0], ['railBchannel', 'Vanne rail B', 1]],
  solenoid1in1: [['hole', 'Trou', 0], ['channel', 'Vanne', 0]],
};

function holesEditor() {
  const impl = g(S.cfg, 'valve.impl');
  const cols = HOLE_COLUMNS[impl] || HOLE_COLUMNS.valve2in1;
  const path = `valve.${impl}.holes`;
  let rows = g(S.cfg, path);
  if (!Array.isArray(rows)) { rows = []; sset(S.cfg, path, rows); }

  // Nouvelle ligne : valeurs par défaut de la colonne, sauf le numéro de trou et
  // les canaux qui suivent l'index (un canal par trou, deux pour un 2-en-1).
  const newRow = (i) => {
    const r = {};
    for (const [key, , def] of cols) r[key] = key === 'hole' ? i : def;
    if (impl === 'solenoid2in1') { r.railAchannel = 2 * i; r.railBchannel = 2 * i + 1; }
    else if ('channel' in r) r.channel = i;
    return r;
  };

  const setCount = (n) => {
    n = Math.max(0, Math.min(24, n));
    while (rows.length > n) rows.pop();
    while (rows.length < n) rows.push(newRow(rows.length));
    markDirty(); renderSection();
  };

  const table = el('table', { class: 'grid-table' },
    el('thead', {}, el('tr', {}, cols.map(([, label]) => el('th', {}, label)), el('th', {}, ''))),
    el('tbody', {}, rows.map((row, i) => el('tr', {},
      cols.map(([key]) => el('td', {}, el('input', {
        type: 'number', value: row[key] ?? 0, min: 0, max: key.includes('ngle') ? 180 : 31,
        onchange: (ev) => { row[key] = parseInt(ev.target.value, 10) || 0; markDirty(); },
      }))),
      el('td', {},
        el('button', { class: 'mini', title: 'Tester ce trou (souffle)',
          onpointerdown: () => calibrate({ target: 'hole', hole: row.hole, dir: 'blow' }),
          onpointerup: () => calibrate({ target: 'hole', hole: row.hole, dir: 'closed' }),
          onpointerleave: () => calibrate({ target: 'hole', hole: row.hole, dir: 'closed' }) }, '▶'),
        el('button', { class: 'mini danger', onclick: () => { rows.splice(i, 1); markDirty(); renderSection(); } }, '✕')),
    ))));

  return el('div', {},
    el('div', { class: 'row' },
      el('label', {}, 'Nombre de trous câblés ',
        el('input', { type: 'number', min: 0, max: 24, value: rows.length,
          onchange: (ev) => setCount(parseInt(ev.target.value, 10) || 0) })),
      el('button', { class: 'mini', onclick: () => {
        rows.forEach((r, i) => {
          r.hole = i;
          if (impl === 'solenoid2in1') { r.railAchannel = 2 * i; r.railBchannel = 2 * i + 1; }
          else r.channel = i;
        });
        markDirty(); renderSection();
      } }, 'Numéroter trous + canaux'),
      el('button', { class: 'mini', onclick: () => {
        if (!rows.length) return;
        const ref = rows[0];
        rows.forEach((r) => cols.forEach(([k]) => { if (k.includes('ngle')) r[k] = ref[k]; }));
        markDirty(); renderSection();
      } }, 'Copier les angles de la ligne 1' ,),
      el('button', { class: 'mini', onclick: () => setCount(rows.length + 1) }, '+ trou')),
    rows.length ? table : el('p', { class: 'muted' }, 'Aucun trou câblé : commencez par régler le nombre de trous.'),
    el('small', {}, 'Le numéro de trou est celui du mapping (trou 1 de l’harmoniciste = 0).'));
}

function notesEditor() {
  const notes = g(S.cfg, 'harmonica.notes') || [];
  const hasSlide = !!g(S.cfg, 'harmonica.hasSlide');
  const holeMax = (g(S.cfg, 'harmonica.holeCount') || 1) - 1;

  const cell = (row, key, attrs, conv) => el('input', {
    ...attrs, value: row[key] ?? attrs.value ?? 0,
    onchange: (ev) => { row[key] = conv(ev.target); markDirty(); renderSection(); },
  });

  const body = notes.map((n, i) => el('tr', {},
    el('td', {}, cell(n, 'note', { type: 'number', min: 0, max: 127 }, (t) => Math.max(0, Math.min(127, parseInt(t.value, 10) || 0)))),
    el('td', { class: 'muted' }, noteName(n.note)),
    el('td', {}, cell(n, 'hole', { type: 'number', min: 0, max: holeMax }, (t) => parseInt(t.value, 10) || 0)),
    el('td', {}, el('select', { onchange: (ev) => { n.dir = ev.target.value; markDirty(); } },
      [['blow', 'souffle'], ['draw', 'aspiration']].map(([v, l]) =>
        el('option', { value: v, ...((n.dir || 'blow') === v ? { selected: true } : {}) }, l)))),
    hasSlide ? el('td', {}, el('input', { type: 'checkbox', ...(n.slide ? { checked: true } : {}),
      onchange: (ev) => { n.slide = ev.target.checked; markDirty(); } })) : null,
    el('td', {}, cell(n, 'bend', { type: 'number', step: 0.5, min: -3, max: 3, value: n.bend || 0 }, (t) => parseFloat(t.value) || 0)),
    el('td', {}, cell(n, 'intensityScale', { type: 'number', step: 0.05, min: 0.1, max: 2, value: n.intensityScale ?? 1 }, (t) => parseFloat(t.value) || 1)),
    el('td', {},
      el('button', { class: 'mini', title: 'Jouer cette note',
        onpointerdown: () => testNote('noteOn', n.note, 100),
        onpointerup: () => testNote('noteOff', n.note),
        onpointerleave: () => testNote('noteOff', n.note) }, '▶'),
      el('button', { class: 'mini danger', onclick: () => { notes.splice(i, 1); markDirty(); renderSection(); } }, '✕')),
  ));

  return el('div', {},
    el('div', { class: 'row' },
      el('button', { class: 'mini', onclick: () => {
        notes.push({ note: 60, hole: 0, dir: 'blow' }); markDirty(); renderSection();
      } }, '+ note'),
      el('button', { class: 'mini', onclick: () => {
        notes.sort((a, b) => a.note - b.note || a.hole - b.hole); markDirty(); renderSection();
      } }, 'Trier par note'),
      el('button', { class: 'mini', onclick: exportHarmonica }, 'Exporter (.json)'),
      el('label', { class: 'mini file' }, 'Importer…',
        el('input', { type: 'file', accept: '.json', onchange: importHarmonica })),
      el('button', { class: 'mini primary', onclick: applyHarmonicaHot },
        'Appliquer à chaud'),
      el('span', { class: 'muted' }, notes.length + ' entrée(s)')),
    notes.length ? el('table', { class: 'grid-table' },
      el('thead', {}, el('tr', {},
        el('th', {}, 'Note'), el('th', {}, ''), el('th', {}, 'Trou'), el('th', {}, 'Sens'),
        hasSlide ? el('th', {}, 'Slide') : null, el('th', {}, 'Bend'), el('th', {}, 'Intensité'), el('th', {}, ''))),
      el('tbody', {}, body)) : el('p', { class: 'muted' }, 'Mapping vide — utilisez le générateur ci-dessus.'),
    el('small', {}, 'Note en double : la première ligne gagne. Un bend négatif abaisse la note (surpression demandée en plus).'));
}

// ---- Générateurs de mapping (mêmes formules que tools/gen_presets.py) -------
const RICHTER_BLOW = [60, 64, 67, 72, 76, 79, 84, 88, 91, 96];
const RICHTER_DRAW = [62, 67, 71, 74, 77, 81, 83, 86, 89, 93];
const DRAW_BENDS = { 0: 1, 1: 2, 2: 3, 3: 1, 4: 1, 5: 1 };
const BLOW_BENDS = { 7: 1, 8: 1, 9: 2 };
const SOLO_BLOW = [0, 4, 7, 12], SOLO_DRAW = [2, 5, 9, 11];
const KEYS = { C: 0, 'C#': 1, D: 2, Eb: 3, E: 4, F: 5, 'F#': 6, G: 7, Ab: 8, A: 9, Bb: 10, B: 11 };

function genRichter(off, bends) {
  const notes = [];
  for (let h = 0; h < 10; h++) {
    notes.push({ note: RICHTER_BLOW[h] + off, hole: h, dir: 'blow' });
    notes.push({ note: RICHTER_DRAW[h] + off, hole: h, dir: 'draw' });
  }
  if (bends) {
    for (const [h, n] of Object.entries(DRAW_BENDS))
      for (let s = 1; s <= n; s++) notes.push({ note: RICHTER_DRAW[h] + off - s, hole: +h, dir: 'draw', bend: -s });
    for (const [h, n] of Object.entries(BLOW_BENDS))
      for (let s = 1; s <= n; s++) notes.push({ note: RICHTER_BLOW[h] + off - s, hole: +h, dir: 'blow', bend: -s });
  }
  return { notes, holeCount: 10, hasSlide: false };
}
function genSolo(holes, slide, off, base = 60) {
  const notes = [];
  for (let h = 0; h < holes; h++) {
    const oct = Math.floor(h / 4), idx = h % 4;
    const b = base + off + 12 * oct + SOLO_BLOW[idx], d = base + off + 12 * oct + SOLO_DRAW[idx];
    notes.push({ note: b, hole: h, dir: 'blow' });
    notes.push({ note: d, hole: h, dir: 'draw' });
    if (slide) {
      notes.push({ note: b + 1, hole: h, dir: 'blow', slide: true });
      notes.push({ note: d + 1, hole: h, dir: 'draw', slide: true });
    }
  }
  return { notes, holeCount: holes, hasSlide: slide };
}

function generatorPanel() {
  const st = generatorPanel.state ||= { type: 'richter', key: 'C', bends: false, holes: 12 };
  const sel = (label, value, options, onchange) => el('label', {}, label,
    el('select', { onchange: (e) => { onchange(e.target.value); renderSection(); } },
      options.map(([v, l]) => el('option', { value: v, ...(String(v) === String(value) ? { selected: true } : {}) }, l))));

  const build = () => {
    const off = KEYS[st.key];
    let out;
    if (st.type === 'richter') out = genRichter(off, st.bends);
    else if (st.type === 'chromatic') out = genSolo(+st.holes, true, off, +st.holes > 12 ? 48 : 60);
    else out = genSolo(+st.holes, false, off, 60);
    const label = st.type === 'richter' ? `Diatonic ${st.key} Richter${st.bends ? ' + bends' : ''}`
                : st.type === 'chromatic' ? `Chromatic ${st.key} ${st.holes}`
                : `Solo/Trémolo ${st.key} ${st.holes}`;
    if (!confirm(`Remplacer le mapping actuel par « ${label} » (${out.notes.length} notes) ?`)) return;
    sset(S.cfg, 'harmonica.notes', out.notes);
    sset(S.cfg, 'harmonica.holeCount', out.holeCount);
    sset(S.cfg, 'harmonica.hasSlide', out.hasSlide);
    sset(S.cfg, 'harmonica.name', label);
    markDirty(); renderSection();
    toast(`Mapping « ${label} » généré`);
  };

  return el('div', { class: 'row' },
    sel('Type ', st.type, [['richter', 'Diatonique Richter (10 trous)'], ['chromatic', 'Chromatique (slide)'], ['solo', 'Solo / trémolo / octave']],
      (v) => { st.type = v; }),
    sel('Tonalité ', st.key, Object.keys(KEYS).map((k) => [k, k]), (v) => { st.key = v; }),
    st.type === 'richter'
      ? el('label', {}, el('input', { type: 'checkbox', ...(st.bends ? { checked: true } : {}),
          onchange: (e) => { st.bends = e.target.checked; } }), ' avec bends')
      : sel('Trous ', st.holes, [[8, '8'], [10, '10'], [12, '12'], [14, '14'], [16, '16']], (v) => { st.holes = v; }),
    el('button', { class: 'primary', onclick: build }, 'Générer le mapping'),
    el('small', {}, 'Le générateur écrit dans la configuration ; « Appliquer à chaud » l’envoie sans redémarrer.'));
}

// ---- Presets ----------------------------------------------------------------
function presetsPanel() {
  const list = el('div', { class: 'chips' }, el('span', { class: 'muted' }, 'chargement…'));
  api('/api/harmonicas').then((names) => {
    list.innerHTML = '';
    if (!names.length) { list.append(el('span', { class: 'muted' }, '(aucun preset sur le système de fichiers)')); return; }
    names.forEach((n) => list.append(el('button', { class: 'chip', onclick: () => applyPreset(n) },
      n.replace(/^\/?presets\//, '').replace(/\.json$/, ''))));
  }).catch(() => { list.innerHTML = ''; list.append(el('span', { class: 'muted' }, '(liste indisponible)')); });
  return el('div', {}, list,
    el('small', {}, 'Un clic charge le preset dans l’éditeur ET l’applique à chaud (les notes en cours sont coupées).'));
}

async function applyPreset(name) {
  const path = name.startsWith('/') ? name : '/presets/' + name;
  try {
    const preset = await api(path);
    if (preset.hasSlide && !g(S.cfg, 'slide.enabled') &&
        !confirm('Ce preset est chromatique : sans « Slide monté » (onglet Distribution) les altérations ne sonneront pas. Continuer ?')) return;
    sset(S.cfg, 'harmonica', preset);
    markDirty();
    const j = await post('/api/harmonica', preset);
    toast(j.ok ? `Harmonica « ${preset.name} » appliqué` : 'Preset refusé', j.ok ? 'ok' : 'err');
    render();
  } catch (e) { toast('Preset illisible : ' + e.message, 'err'); }
}

async function applyHarmonicaHot() {
  try {
    const j = await post('/api/harmonica', g(S.cfg, 'harmonica'));
    toast(j.ok ? 'Mapping appliqué à chaud' : 'Mapping refusé', j.ok ? 'ok' : 'err');
  } catch (e) { toast('Échec réseau', 'err'); }
}

function exportHarmonica() {
  const blob = new Blob([JSON.stringify(g(S.cfg, 'harmonica'), null, 2)], { type: 'application/json' });
  const a = el('a', { href: URL.createObjectURL(blob), download: (g(S.cfg, 'harmonica.name') || 'harmonica') + '.json' });
  document.body.append(a); a.click(); a.remove();
}
function importHarmonica(ev) {
  const file = ev.target.files[0];
  if (!file) return;
  file.text().then((txt) => {
    const h = JSON.parse(txt);
    if (!Array.isArray(h.notes)) throw new Error('pas de tableau « notes »');
    sset(S.cfg, 'harmonica', h); markDirty(); renderSection();
    toast(`« ${h.name || file.name} » importé (${h.notes.length} notes)`);
  }).catch((e) => toast('Import impossible : ' + e.message, 'err'));
}

// ---- Onglet État ------------------------------------------------------------
function statusPanel() {
  const s = S.status, caps = s.caps || {};
  const yn = (b) => (b ? 'oui' : 'non');
  const num = (v, d = 2) => (v === null || v === undefined || Number.isNaN(v)) ? '—' : Number(v).toFixed(d);
  const tiles = [
    ['Liaison', S.online ? 'en ligne' : 'hors ligne'],
    ['Harmonica', s.harmonica || '—'],
    ['Prêt', yn(s.ready)],
    ['Voix actives', s.voices ?? 0],
    ['Souffle + aspiration', yn(s.mixedCapable)],
    ['Pression souffle (kPa)', num(s.pressureBlow)],
    ['Pression aspiration (kPa)', num(s.pressureDraw)],
    ['Consigne (kPa)', num(s.setpointKpa)],
  ];
  if (caps.needsHoming || caps.hasPiston) {
    tiles.push(['Homing', yn(s.homed)]);
    tiles.push(['Position (mm)', num(s.pistonMm, 1)]);
  }
  if (caps.railsSwap) tiles.push(['Échanges de réservoir', s.assignmentGen ?? 0]);
  if (caps.hasPumps) {
    tiles.push(['Pompe souffle', num(s.dutyBlow * 100, 0) + ' %']);
    tiles.push(['Pompe aspiration', num(s.dutyDraw * 100, 0) + ' %']);
  }
  if (s.slidePresent) tiles.push(['Slide', s.slideEngaged ? 'enfoncé' : 'repos']);
  tiles.push(['Transports MIDI', s.transports ?? 0], ['MIDI perdus', s.droppedMidi ?? 0],
             ['RAM libre', s.freeHeap ? (s.freeHeap / 1024).toFixed(0) + ' Ko' : '—'],
             ['Mode simulation', yn(s.mock)]);

  const problems = validate(S.cfg);
  return el('div', {},
    el('div', { class: 'grid' }, tiles.map(([k, v]) => el('div', {}, k, el('span', {}, String(v))))),
    holeStrip(),
    problems.length
      ? el('div', { class: 'checks' }, el('h3', {}, 'Cohérence de la configuration'),
          problems.map((p) => el('div', { class: 'check ' + p.level }, p.text)))
      : el('div', { class: 'checks' }, el('div', { class: 'check ok' }, 'Configuration cohérente.')));
}

// Bandeau visuel des trous : bleu = souffle, orange = aspiration (temps réel).
function holeStrip() {
  const count = g(S.cfg, 'harmonica.holeCount') || S.status.holeCount || 0;
  if (!count) return null;
  const blow = S.status.holeBlowMask || 0, draw = S.status.holeDrawMask || 0;
  return el('div', { class: 'holes', id: 'holestrip' },
    Array.from({ length: count }, (_, i) => el('div', {
      class: 'hole' + ((blow >> i) & 1 ? ' blow' : '') + ((draw >> i) & 1 ? ' draw' : ''),
    }, String(i + 1))));
}

// ---- Onglet banc d'essai ----------------------------------------------------
function benchPanel() {
  // Les capacités viennent de la télémétrie : le banc pilote le montage qui
  // TOURNE, pas celui en cours d'édition (qui ne prendra effet qu'au redémarrage).
  const caps = S.status.caps || AIR_TRAITS[g(S.cfg, 'air.impl')] || {};
  const valve = VALVE_TRAITS[g(S.cfg, 'valve.impl')] || {};
  const holes = g(S.cfg, `valve.${g(S.cfg, 'valve.impl')}.holes`) || [];
  const st = benchPanel.state ||= { servoCh: 0, servoDeg: 90, solCh: 0, velocity: 100, note: 60 };
  const blocks = [];

  blocks.push(el('div', { class: 'block' },
    el('h3', {}, 'Sécurité'),
    el('div', { class: 'row' },
      el('button', { class: 'warn', onclick: panic }, '⏹ Tout couper'),
      el('small', {}, 'Ferme toutes les valves, relâche l’air et coupe les notes.'))));

  if (caps.needsHoming || caps.hasPiston || caps.homing || caps.stepper) {
    blocks.push(el('div', { class: 'block' },
      el('h3', {}, 'Vérin / soufflet'),
      el('div', { class: 'row' },
        el('button', { onclick: () => calibrate({ target: 'piston', action: 'home' }) }, 'Homing'),
        el('button', { onclick: () => calibrate({ target: 'piston', action: 'center' }) }, 'Centrer'),
        el('button', { onclick: () => calibrate({ target: 'pressureZero' }) }, 'Zéro pression (tare)'))));
  }

  if (caps.hasPumps || caps.pumps) {
    const pumpRow = (dir, label) => el('div', { class: 'row' },
      el('label', {}, label,
        el('input', { type: 'range', min: 0, max: 100, value: 0,
          oninput: (e) => { e.target.nextElementSibling.textContent = e.target.value + ' %';
                            calibrate({ target: 'pump', dir, duty: +e.target.value }); } }),
        el('span', { class: 'muted' }, '0 %')),
      el('button', { class: 'mini', onclick: (e) => {
        const range = e.target.closest('.row').querySelector('input[type=range]');
        range.value = 0; range.nextElementSibling.textContent = 'auto';
        calibrate({ target: 'pump', dir, duty: -1 });
      } }, 'Rendre à la régulation'));
    blocks.push(el('div', { class: 'block' }, el('h3', {}, 'Pompes'),
      pumpRow('blow', 'Souffle '),
      caps.simultaneous ? pumpRow('draw', 'Aspiration ') : null,
      el('small', {}, 'Le curseur force le régime ; « rendre à la régulation » redonne la main au PI.')));
  }

  blocks.push(el('div', { class: 'block' },
    el('h3', {}, 'Servo unitaire'),
    el('div', { class: 'row' },
      el('label', {}, 'Canal ', el('input', { type: 'number', min: 0, max: 31, value: st.servoCh,
        onchange: (e) => { st.servoCh = +e.target.value; } })),
      el('label', {}, 'Angle ', el('input', { type: 'range', min: 0, max: 180, value: st.servoDeg,
        oninput: (e) => { st.servoDeg = +e.target.value; e.target.nextElementSibling.textContent = e.target.value + '°';
                          calibrate({ target: 'servo', channel: st.servoCh, deg: st.servoDeg }); } }),
        el('span', { class: 'muted' }, st.servoDeg + '°')),
      el('label', {}, 'µs ', el('input', { type: 'number', min: 500, max: 2500, step: 10, value: 1500,
        onchange: (e) => calibrate({ target: 'servo', channel: st.servoCh, us: +e.target.value }) })))));

  if (valve.actuator === 'solenoid' || (g(S.cfg, 'slide.enabled') && g(S.cfg, 'slide.impl') === 'solenoid')) {
    blocks.push(el('div', { class: 'block' },
      el('h3', {}, 'Électro-vanne unitaire'),
      el('div', { class: 'row' },
        el('label', {}, 'Canal ', el('input', { type: 'number', min: 0, max: 31, value: st.solCh,
          onchange: (e) => { st.solCh = +e.target.value; } })),
        el('button', { onmousedown: () => calibrate({ target: 'solenoid', channel: st.solCh, on: true }),
                       onmouseup: () => calibrate({ target: 'solenoid', channel: st.solCh, on: false }) }, 'Maintenir ouverte'),
        el('button', { class: 'mini', onclick: () => calibrate({ target: 'solenoid', channel: st.solCh, on: false }) }, 'Fermer'))));
  }

  if (g(S.cfg, 'slide.enabled')) {
    blocks.push(el('div', { class: 'block' }, el('h3', {}, 'Slide'),
      el('div', { class: 'row' },
        el('button', { onclick: () => calibrate({ target: 'slide', engaged: true }) }, 'Enfoncer'),
        el('button', { onclick: () => calibrate({ target: 'slide', engaged: false }) }, 'Relâcher'))));
  }

  if (holes.length) {
    const hold = (hole, dir) => ({
      onpointerdown: () => calibrate({ target: 'hole', hole, dir }),
      onpointerup: () => calibrate({ target: 'hole', hole, dir: 'closed' }),
      onpointerleave: () => calibrate({ target: 'hole', hole, dir: 'closed' }),
    });
    blocks.push(el('div', { class: 'block' },
      el('h3', {}, 'Valves trou par trou'),
      el('div', { class: 'holegrid' }, holes.map((h) => el('div', { class: 'holecell' },
        el('span', {}, 'trou ' + (h.hole + 1)),
        el('button', { class: 'mini blow', ...hold(h.hole, 'blow') }, 'souffle'),
        valve.perHole ? el('button', { class: 'mini draw', ...hold(h.hole, 'draw') }, 'aspir.') : null))),
      el('small', {}, 'Maintenir le bouton ouvre la valve ; la relâcher la referme.')));
  }

  const mapped = [...new Set((g(S.cfg, 'harmonica.notes') || []).map((n) => n.note))].sort((a, b) => a - b);
  blocks.push(el('div', { class: 'block' },
    el('h3', {}, 'Jouer des notes'),
    el('div', { class: 'row' },
      el('label', {}, 'Vélocité ', el('input', { type: 'range', min: 1, max: 127, value: st.velocity,
        oninput: (e) => { st.velocity = +e.target.value; e.target.nextElementSibling.textContent = e.target.value; } }),
        el('span', { class: 'muted' }, st.velocity)),
      el('button', { class: 'mini', onclick: () => playScale(mapped, st.velocity) }, '▶ Jouer toutes les notes mappées')),
    el('div', { class: 'keys' }, mapped.map((n) => el('button', {
      class: 'key',
      onpointerdown: () => testNote('noteOn', n, st.velocity),
      onpointerup: () => testNote('noteOff', n),
      onpointerleave: () => testNote('noteOff', n),
    }, noteName(n), el('small', {}, String(n))))),
    mapped.length ? null : el('p', { class: 'muted' }, 'Aucune note mappée.')));

  return el('div', {}, blocks);
}

async function playScale(notes, velocity) {
  for (const n of notes) {
    await testNote('noteOn', n, velocity);
    await new Promise((r) => setTimeout(r, 260));
    await testNote('noteOff', n);
    await new Promise((r) => setTimeout(r, 60));
  }
}

// ---- Onglet JSON brut -------------------------------------------------------
function rawPanel() {
  const ta = el('textarea', { spellcheck: 'false' }, JSON.stringify(S.cfg, null, 2));
  return el('div', {},
    el('p', { class: 'muted' }, 'Édition directe du document. « Relire » réinjecte ce texte dans les formulaires ; les mots de passe restent masqués côté firmware.'),
    ta,
    el('div', { class: 'row' },
      el('button', { onclick: () => {
        try { S.cfg = JSON.parse(ta.value); markDirty(); render(); toast('Document relu'); }
        catch (e) { toast('JSON invalide : ' + e.message, 'err'); }
      } }, 'Relire dans les formulaires'),
      el('button', { class: 'mini', onclick: () => { ta.value = JSON.stringify(S.cfg, null, 2); } }, 'Reformater')));
}

const CUSTOM = { status: statusPanel, holes: holesEditor, notes: notesEditor,
                 generator: generatorPanel, presets: presetsPanel, bench: benchPanel, raw: rawPanel };

// ---- Rendu général ----------------------------------------------------------
function renderTabs() {
  $('tabs').replaceChildren(...SECTIONS.map((sec) => el('button', {
    class: 'tab' + (sec.id === S.tab ? ' active' : ''),
    onclick: () => { S.tab = sec.id; render(); },
  }, sec.icon + ' ' + sec.title)));
}

function renderSection() {
  const sec = SECTIONS.find((x) => x.id === S.tab) || SECTIONS[0];
  const out = el('div', {});
  for (const block of sec.blocks) {
    if (block.w && !block.w(S.cfg)) continue;
    const body = block.custom
      ? CUSTOM[block.custom]()
      : el('div', { class: 'fields' }, block.fields.filter((f) => !f.w || f.w(S.cfg)).map(fieldRow));
    out.append(el('section', { class: 'card' }, block.title ? el('h2', {}, block.title) : null, body));
  }
  $('content').replaceChildren(out);
}

function renderBar() {
  $('dirty').textContent = S.dirty ? '● modifications non enregistrées' : '';
  $('conn').textContent = S.online ? 'en ligne' : 'hors ligne';
  $('conn').className = 'badge ' + (S.online ? 'on' : 'off');
}

function render() {
  if (!S.cfg) return;
  renderTabs(); renderSection(); renderBar();
}

// ---- Télémétrie (polling ; pas de WebSocket, cf. WebServer.h) ---------------
async function pollStatus() {
  try {
    S.status = await api('/api/status');
    S.online = true;
    if (S.tab === 'status') renderSection();   // seul onglet qui affiche du temps réel
  } catch (e) { S.online = false; }
  renderBar();
}

// ---- Démarrage --------------------------------------------------------------
window.addEventListener('DOMContentLoaded', async () => {
  $('btn-save').onclick = saveConfig;
  $('btn-reload').onclick = () => { if (!S.dirty || confirm('Abandonner les modifications ?')) loadConfig(); };
  $('btn-reboot').onclick = reboot;
  $('btn-panic').onclick = panic;
  window.addEventListener('beforeunload', (e) => { if (S.dirty) { e.preventDefault(); e.returnValue = ''; } });
  await loadCaps();
  await loadConfig();
  pollStatus();
  setInterval(pollStatus, 700);
});
