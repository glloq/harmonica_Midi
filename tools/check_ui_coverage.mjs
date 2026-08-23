// ============================================================================
//  check_ui_coverage.mjs — l'UI couvre-t-elle TOUTE la configuration ?
//
//  Trois vérifications, exécutées par tools/run_native_tests.sh :
//   1. chaque feuille de data/config.json est éditable depuis l'UI (champ du
//      schéma, ou éditeur spécialisé déclaré ci-dessous) ;
//   2. chaque champ du schéma pointe vers un chemin qui existe réellement dans
//      data/config.json (détecte les fautes de frappe silencieuses) ;
//   3. chaque identifiant d'implémentation proposé par l'UI est reconnu par le
//      parseur C++ (src/ConfigStore.cpp) — sinon l'option serait ignorée.
//
//  Lancer :  node tools/check_ui_coverage.mjs
// ============================================================================
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import vm from 'node:vm';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => readFileSync(path.join(ROOT, p), 'utf8');

// ---- Charge schema.js dans un bac à sable (il n'utilise que `g`) ------------
const sandbox = { g: (obj, p) => p.split('.').reduce((o, k) => (o == null ? undefined : o[k]), obj) };
vm.createContext(sandbox);
// `const` au niveau script n'atterrit pas sur l'objet global : on l'exporte.
vm.runInContext(read('data/schema.js') + '\nglobalThis.__ui = { SECTIONS, AIR_IMPLS, VALVE_IMPLS };',
                sandbox, { filename: 'schema.js' });
const { SECTIONS, AIR_IMPLS, VALVE_IMPLS } = sandbox.__ui;

// Tableaux rendus par un éditeur spécialisé d'app.js plutôt que par un champ.
const CUSTOM_COVERED = [
  'valve.valve2in1.holes', 'valve.valve1in1.holes',
  'valve.solenoid2in1.holes', 'valve.solenoid1in1.holes',
  'harmonica.notes',
];
const NOT_EDITABLE = ['version'];   // métadonnée du document

const cfg = JSON.parse(read('data/config.json'));
const fieldPaths = new Set();
for (const sec of SECTIONS)
  for (const block of sec.blocks)
    for (const f of block.fields || []) fieldPaths.add(f.p);

// Feuilles du document (un tableau compte comme une feuille).
function leaves(obj, prefix = '') {
  const out = [];
  for (const [k, v] of Object.entries(obj)) {
    const p = prefix ? `${prefix}.${k}` : k;
    if (v && typeof v === 'object' && !Array.isArray(v)) out.push(...leaves(v, p));
    else out.push(p);
  }
  return out;
}
const exists = (p) => p.split('.').reduce((o, k) => (o == null ? undefined : o[k]), cfg) !== undefined;

const errors = [];

for (const leaf of leaves(cfg)) {
  if (NOT_EDITABLE.includes(leaf) || fieldPaths.has(leaf) || CUSTOM_COVERED.includes(leaf)) continue;
  errors.push(`config.json → UI : « ${leaf} » n'est éditable dans aucun onglet`);
}
for (const p of fieldPaths) {
  if (!exists(p)) errors.push(`UI → config.json : le champ « ${p} » ne correspond à aucune clé`);
}

// Les identifiants proposés doivent être reconnus par le parseur C++.
const parser = read('src/ConfigStore.cpp');
for (const [id] of [...AIR_IMPLS, ...VALVE_IMPLS])
  if (!parser.includes(`"${id}"`)) errors.push(`« ${id} » est proposé par l'UI mais absent de ConfigStore.cpp`);

if (errors.length) {
  console.error('[ui] ÉCHEC de la couverture :');
  for (const e of errors) console.error('  - ' + e);
  process.exit(1);
}
console.log(`[ui] couverture OK — ${fieldPaths.size} champs, ${leaves(cfg).length} clés de configuration, ` +
            `${AIR_IMPLS.length} systèmes d'air, ${VALVE_IMPLS.length} types de valve`);
