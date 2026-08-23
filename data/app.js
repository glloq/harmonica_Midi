// ---- Page de configuration Harmonica MIDI (vanilla JS, aucune dépendance) ----
'use strict';

const $ = (id) => document.getElementById(id);
function msg(t, ok) { const m = $('msg'); m.textContent = t; m.style.color = ok ? 'var(--ok)' : 'var(--warn)'; }

// ---- Config ----------------------------------------------------------------
async function loadConfig() {
  try {
    const r = await fetch('/api/config');
    $('cfg').value = await r.text();
    msg('config chargée', true);
  } catch (e) { msg('échec de chargement', false); }
}

async function saveConfig() {
  let body = $('cfg').value;
  try { JSON.parse(body); } catch (e) { msg('JSON invalide : ' + e.message, false); return; }
  try {
    const r = await fetch('/api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
    const j = await r.json();
    if (j.ok) msg('sauvegardé' + (j.reboot ? ' — redémarrage conseillé' : ''), true);
    else msg('refusé : ' + (j.error || 'invalide'), false);
  } catch (e) { msg('échec réseau', false); }
}

async function reboot() {
  if (!confirm('Redémarrer le contrôleur ?')) return;
  await fetch('/api/reboot', { method: 'POST' });
  msg('redémarrage…', true);
}

// ---- Calibration -----------------------------------------------------------
async function calibrate(payload) {
  try {
    const r = await fetch('/api/calibrate', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
    const j = await r.json();
    msg(j.ok ? 'calibration OK' : 'calibration refusée', j.ok);
  } catch (e) { msg('échec réseau', false); }
}
function calibrateServo() {
  calibrate({ target: 'servo', channel: +$('cal-ch').value, deg: +$('cal-deg').value });
}

// ---- Presets ---------------------------------------------------------------
async function loadPresets() {
  try {
    const r = await fetch('/api/harmonicas');
    const list = await r.json();
    const ul = $('presets'); ul.innerHTML = '';
    list.forEach((name) => {
      const li = document.createElement('li');
      li.textContent = name.replace(/^\/?presets\//, '').replace(/\.json$/, '');
      li.onclick = () => applyPreset(name);
      ul.appendChild(li);
    });
    if (!list.length) ul.innerHTML = '<li>(aucun)</li>';
  } catch (e) { /* pas de presets */ }
}
// Applique un preset d'harmonica à chaud (échange du mapping, sans reboot).
async function applyPreset(name) {
  const path = name.startsWith('/') ? name : '/presets/' + name;
  if (!confirm('Charger « ' + name + ' » ? Les notes en cours seront coupées.')) return;
  try {
    const preset = await (await fetch(path)).text();
    // Avertissement : un harmonica chromatique a besoin de slide.enabled + reboot.
    try {
      const pj = JSON.parse(preset);
      if (pj.hasSlide && !confirm('Ce preset est chromatique (slide). Les altérations ne sonneront que si "slide.enabled" est activé dans la config puis redémarré. Continuer quand même ?')) return;
    } catch (e) { /* preset non-JSON : laissé au serveur */ }
    const r = await fetch('/api/harmonica', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: preset });
    const j = await r.json();
    if (j.ok) { msg('harmonica « ' + name + ' » appliquée', true); loadConfig(); }
    else msg('preset refusé (JSON harmonica invalide)', false);
  } catch (e) { msg('échec réseau', false); }
}

// ---- Télémétrie (WebSocket + repli polling) --------------------------------
function renderStatus(s) {
  const yn = (b) => (b ? 'oui' : 'non');
  $('s-homed').textContent = yn(s.homed);
  $('s-ready').textContent = yn(s.ready);
  $('s-piston').textContent = (s.pistonMm == null) ? '—' : Number(s.pistonMm).toFixed(1);
  $('s-pblow').textContent = Number(s.pressureBlow ?? 0).toFixed(2);
  $('s-pdraw').textContent = Number(s.pressureDraw ?? 0).toFixed(2);
  $('s-voices').textContent = s.voices ?? 0;
  $('s-mixed').textContent = yn(s.mixedCapable);
  $('s-transports').textContent = s.transports ?? 0;
  $('s-harmo').textContent = s.harmonica ?? '—';
  $('s-heap').textContent = s.freeHeap ? (s.freeHeap / 1024).toFixed(0) + ' Ko' : '—';
  $('s-mock').textContent = yn(s.mock);
  $('s-gen').textContent = s.assignmentGen ?? 0;
  $('s-setpoint').textContent = Number(s.setpointKpa ?? 0).toFixed(2);
  $('s-dropped').textContent = s.droppedMidi ?? 0;
}
function setConn(on) {
  const b = $('conn'); b.textContent = on ? 'en ligne' : 'hors ligne';
  b.className = 'badge ' + (on ? 'on' : 'off');
}

// Télémétrie par polling (~2 Hz). Pas de WebSocket : évite toute course de tâche
// côté serveur (le push WS depuis une tâche étrangère est dangereux).
async function pollStatus() {
  try { const r = await fetch('/api/status'); renderStatus(await r.json()); setConn(true); }
  catch (e) { setConn(false); }
}

// ---- Init ------------------------------------------------------------------
loadConfig();
loadPresets();
pollStatus();
setInterval(pollStatus, 500);
