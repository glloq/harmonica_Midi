// ============================================================================
//  schema.js — DESCRIPTION déclarative de toute la configuration.
//
//  C'est le seul fichier à toucher pour exposer une nouvelle option dans l'UI :
//  app.js ne fait que rendre ce descripteur. Un champ = un objet :
//     p  chemin dans config.json (« air.pumpPair.idleDuty »)
//     l  libellé · u unité · h aide (texte sous le champ)
//     t  type : int | num | bool | text | pass | sel | hex
//     o  options (type sel) : [[valeur, libellé], …]
//     w  condition d'affichage : (cfg) => bool   — c'est ce qui rend l'UI
//        MODULAIRE : seuls les réglages du montage choisi sont visibles.
//     min/max/step  bornes de saisie
//  Un bloc peut aussi porter `custom` : un éditeur spécialisé rendu par app.js
//  (tableau des trous, tableau du mapping, presets, banc d'essai).
// ============================================================================
'use strict';

// ---- Implémentations proposées (repli si /api/capabilities est injoignable) --
const AIR_IMPLS = [
  ['dualReservoirPiston', 'Vérin double — 2 réservoirs + piston'],
  ['singleBellows',       'Soufflet simple motorisé'],
  ['pumpPair',            'Deux pompes continues opposées'],
  ['singlePumpReversible','Une pompe + aiguillage souffle/aspiration'],
];
const VALVE_IMPLS = [
  ['valve2in1',    'Servo — 2 entrées → 1 sortie (par trou)'],
  ['valve1in1',    'Servo — porte ouverte/fermée (par trou)'],
  ['solenoid2in1', 'Électro-vannes — 2 par trou (rail A / rail B)'],
  ['solenoid1in1', 'Électro-vanne — 1 par trou'],
];
// Ce que chaque montage d'air implique (utilisé pour les avertissements et
// pour n'afficher que les commandes de banc d'essai qui ont un sens).
const AIR_TRAITS = {
  dualReservoirPiston:  { simultaneous: true,  stepper: true,  pumps: false, homing: true,  sensors: 2 },
  singleBellows:        { simultaneous: false, stepper: true,  pumps: false, homing: false, sensors: 1 },
  pumpPair:             { simultaneous: true,  stepper: false, pumps: true,  homing: false, sensors: 2 },
  singlePumpReversible: { simultaneous: false, stepper: false, pumps: true,  homing: false, sensors: 1 },
};
const VALVE_TRAITS = {
  valve2in1:    { perHole: true,  actuator: 'servo',    channels: 1 },
  valve1in1:    { perHole: false, actuator: 'servo',    channels: 1 },
  solenoid2in1: { perHole: true,  actuator: 'solenoid', channels: 2 },
  solenoid1in1: { perHole: false, actuator: 'solenoid', channels: 1 },
};

const PRESSURE_TYPES = [['bmp280', 'BMP280 (I2C, absolu)'], ['mpx2010', 'MPX2010 (analogique, différentiel)']];
const PUMP_DRIVES    = [['ledc', 'PWM MOSFET (moteur DC)'], ['esc', 'ESC brushless (impulsions)'], ['pca9685', 'Canal PCA9685']];

// Un bloc "pompe" (mêmes champs pour toutes les pompes du projet).
const pumpFields = (base, when) => [
  { p: base + '.drive', l: 'Pilotage', t: 'sel', o: PUMP_DRIVES, w: when },
  { p: base + '.pin', l: 'Broche PWM', t: 'int', min: 0, max: 39, w: (c) => when(c) && g(c, base + '.drive') === 'ledc' },
  { p: base + '.freqHz', l: 'Fréquence PWM', u: 'Hz', t: 'int', min: 100, max: 40000, h: '20 kHz : au-dessus de l’audible, pas de sifflement',
    w: (c) => when(c) && g(c, base + '.drive') === 'ledc' },
  { p: base + '.channel', l: 'Canal du bus servo', t: 'int', min: 0, max: 15, w: (c) => when(c) && g(c, base + '.drive') !== 'ledc' },
  { p: base + '.escMinUs', l: 'Impulsion arrêt', u: 'µs', t: 'int', min: 500, max: 2500, w: (c) => when(c) && g(c, base + '.drive') !== 'ledc' },
  { p: base + '.escMaxUs', l: 'Impulsion plein régime', u: 'µs', t: 'int', min: 500, max: 2500, w: (c) => when(c) && g(c, base + '.drive') !== 'ledc' },
  { p: base + '.minDuty', l: 'Duty mini utile', t: 'num', step: 0.01, min: 0, max: 1, h: 'Seuil de démarrage de la pompe', w: when },
  { p: base + '.maxDuty', l: 'Duty maxi', t: 'num', step: 0.01, min: 0, max: 1, w: when },
  { p: base + '.invert', l: 'Logique inversée', t: 'bool', w: when },
];

// Un bloc "capteur de pression".
const sensorFields = (base, when) => [
  { p: base + '.type', l: 'Type de capteur', t: 'sel', o: PRESSURE_TYPES, w: when },
  { p: base + '.addr', l: 'Adresse I2C', t: 'hex', w: (c) => when(c) && g(c, base + '.type') === 'bmp280' },
  { p: base + '.adcPin', l: 'Broche ADC', t: 'int', min: 32, max: 39, w: (c) => when(c) && g(c, base + '.type') === 'mpx2010' },
  { p: base + '.kpaPerCount', l: 'Échelle', u: 'kPa/pt', t: 'num', step: 0.0001, w: (c) => when(c) && g(c, base + '.type') === 'mpx2010' },
];

// Régulation de pression : mêmes 4 champs pour toutes les sources d'air.
const piFields = (base, when) => [
  { p: base + '.pressureTargetKpa', l: 'Pression cible', u: 'kPa', t: 'num', step: 0.01, min: 0.05, max: 2,
    h: 'Un souffle humain va de 0,1 à 0,5 kPa', w: when },
  { p: base + '.pressureToleranceKpa', l: 'Tolérance', u: 'kPa', t: 'num', step: 0.01, min: 0.01, max: 1, w: when },
  { p: base + '.pressureKp', l: 'Gain P', t: 'num', step: 0.1, min: 0, w: when },
  { p: base + '.pressureKi', l: 'Gain I', t: 'num', step: 0.1, min: 0, w: when },
];

const isAir   = (id) => (c) => g(c, 'air.impl') === id;
const isValve = (id) => (c) => g(c, 'valve.impl') === id;
const usesSolenoids = (c) =>
  (VALVE_TRAITS[g(c, 'valve.impl')] || {}).actuator === 'solenoid' ||
  (g(c, 'slide.enabled') && g(c, 'slide.impl') === 'solenoid') ||
  (g(c, 'air.impl') === 'singlePumpReversible' && g(c, 'air.singlePumpReversible.diverter') === 'solenoid');

// ---- Les onglets ------------------------------------------------------------
const SECTIONS = [
  {
    id: 'status', title: 'État', icon: '📊',
    blocks: [{ custom: 'status' }],
  },

  {
    id: 'air', title: 'Air', icon: '💨',
    blocks: [
      { title: 'Système d’air', fields: [
        { p: 'air.impl', l: 'Montage', t: 'sel', o: AIR_IMPLS, h: 'Détermine si souffle et aspiration peuvent sonner en même temps' },
      ] },
      { title: 'Vérin double — géométrie', w: isAir('dualReservoirPiston'), fields: [
        { p: 'air.dualReservoirPiston.travelMm', l: 'Course totale', u: 'mm', t: 'num', min: 10 },
        { p: 'air.dualReservoirPiston.centerMm', l: 'Position centrale', u: 'mm', t: 'num', min: 0 },
        { p: 'air.dualReservoirPiston.stepsPerMm', l: 'Pas par mm', t: 'num', min: 1, h: '(pas/tour × micropas) ÷ pas de vis' },
        { p: 'air.dualReservoirPiston.reversalMarginMm', l: 'Marge d’inversion', u: 'mm', t: 'num', min: 1,
          h: 'Distance à la butée qui déclenche l’échange des rôles souffle/aspiration' },
        { p: 'air.dualReservoirPiston.homeOnR1', l: 'Homing côté R1', t: 'bool' },
        { p: 'air.dualReservoirPiston.maxSpeedMmS', l: 'Vitesse max', u: 'mm/s', t: 'num', min: 1 },
        { p: 'air.dualReservoirPiston.accelMmS2', l: 'Accélération', u: 'mm/s²', t: 'num', min: 1 },
        { p: 'air.dualReservoirPiston.flowLpm', l: 'Débit visé', u: 'L/min', t: 'num', min: 1 },
      ] },
      { title: 'Vérin double — régulation', w: isAir('dualReservoirPiston'),
        fields: piFields('air.dualReservoirPiston', isAir('dualReservoirPiston')) },
      { title: 'Vérin double — capteurs & vannes de réservoir', w: isAir('dualReservoirPiston'), fields: [
        { p: 'air.dualReservoirPiston.pressureType', l: 'Type de capteur', t: 'sel', o: PRESSURE_TYPES },
        { p: 'air.dualReservoirPiston.r1Addr', l: 'Adresse I2C R1', t: 'hex', w: (c) => g(c, 'air.dualReservoirPiston.pressureType') === 'bmp280' },
        { p: 'air.dualReservoirPiston.r2Addr', l: 'Adresse I2C R2', t: 'hex', w: (c) => g(c, 'air.dualReservoirPiston.pressureType') === 'bmp280' },
        { p: 'air.dualReservoirPiston.r1AdcPin', l: 'ADC R1', t: 'int', w: (c) => g(c, 'air.dualReservoirPiston.pressureType') === 'mpx2010' },
        { p: 'air.dualReservoirPiston.r2AdcPin', l: 'ADC R2', t: 'int', w: (c) => g(c, 'air.dualReservoirPiston.pressureType') === 'mpx2010' },
        { p: 'air.dualReservoirPiston.valveR1Channel', l: 'Servo vanne R1', t: 'int', min: 0, max: 31 },
        { p: 'air.dualReservoirPiston.valveR1Open', l: 'Angle R1 ouverte', u: '°', t: 'int', min: 0, max: 180 },
        { p: 'air.dualReservoirPiston.valveR1Closed', l: 'Angle R1 fermée', u: '°', t: 'int', min: 0, max: 180 },
        { p: 'air.dualReservoirPiston.valveR2Channel', l: 'Servo vanne R2', t: 'int', min: 0, max: 31 },
        { p: 'air.dualReservoirPiston.valveR2Open', l: 'Angle R2 ouverte', u: '°', t: 'int', min: 0, max: 180 },
        { p: 'air.dualReservoirPiston.valveR2Closed', l: 'Angle R2 fermée', u: '°', t: 'int', min: 0, max: 180 },
      ] },

      { title: 'Soufflet — géométrie', w: isAir('singleBellows'), fields: [
        { p: 'air.singleBellows.travelMm', l: 'Course', u: 'mm', t: 'num', min: 10 },
        { p: 'air.singleBellows.centerMm', l: 'Position neutre', u: 'mm', t: 'num', min: 0 },
        { p: 'air.singleBellows.stepsPerMm', l: 'Pas par mm', t: 'num', min: 1 },
        { p: 'air.singleBellows.maxSpeedMmS', l: 'Vitesse max', u: 'mm/s', t: 'num', min: 1 },
        { p: 'air.singleBellows.accelMmS2', l: 'Accélération', u: 'mm/s²', t: 'num', min: 1 },
        { p: 'air.singleBellows.flowLpm', l: 'Débit visé', u: 'L/min', t: 'num', min: 1 },
      ] },
      { title: 'Soufflet — régulation', w: isAir('singleBellows'), fields: piFields('air.singleBellows', isAir('singleBellows')) },
      { title: 'Soufflet — capteur', w: isAir('singleBellows'), fields: [
        { p: 'air.singleBellows.pressureType', l: 'Type de capteur', t: 'sel', o: PRESSURE_TYPES },
        { p: 'air.singleBellows.addr', l: 'Adresse I2C', t: 'hex', w: (c) => g(c, 'air.singleBellows.pressureType') === 'bmp280' },
        { p: 'air.singleBellows.adcPin', l: 'Broche ADC', t: 'int', w: (c) => g(c, 'air.singleBellows.pressureType') === 'mpx2010' },
      ] },

      { title: 'Deux pompes — pompe souffle', w: isAir('pumpPair'), fields: pumpFields('air.pumpPair.blowPump', isAir('pumpPair')) },
      { title: 'Deux pompes — pompe aspiration', w: isAir('pumpPair'), fields: pumpFields('air.pumpPair.drawPump', isAir('pumpPair')) },
      { title: 'Deux pompes — régulation', w: isAir('pumpPair'), fields: [
        ...piFields('air.pumpPair', isAir('pumpPair')),
        { p: 'air.pumpPair.idleDuty', l: 'Régime de veille', t: 'num', step: 0.01, min: 0, max: 1,
          h: 'Garde le plenum amorcé entre deux notes (attaque plus franche)' },
        { p: 'air.pumpPair.spinUpMs', l: 'Montée en régime', u: 'ms', t: 'int', min: 0, max: 5000 },
        { p: 'air.pumpPair.bleedChannel', l: 'Servo de purge', t: 'int', min: 0, max: 255, h: '255 = pas de purge' },
        { p: 'air.pumpPair.bleedOpenAngle', l: 'Purge ouverte', u: '°', t: 'int', min: 0, max: 180, w: (c) => isAir('pumpPair')(c) && g(c, 'air.pumpPair.bleedChannel') < 32 },
        { p: 'air.pumpPair.bleedClosedAngle', l: 'Purge fermée', u: '°', t: 'int', min: 0, max: 180, w: (c) => isAir('pumpPair')(c) && g(c, 'air.pumpPair.bleedChannel') < 32 },
      ] },
      { title: 'Deux pompes — capteurs', w: isAir('pumpPair'), fields: [
        { p: 'air.pumpPair.sharedSensor', l: 'Un seul capteur', t: 'bool', h: 'La dépression est alors supposée symétrique de la surpression' },
        ...sensorFields('air.pumpPair.blowSensor', isAir('pumpPair')),
        ...sensorFields('air.pumpPair.drawSensor', (c) => isAir('pumpPair')(c) && !g(c, 'air.pumpPair.sharedSensor')),
      ] },

      { title: 'Pompe unique — pompe', w: isAir('singlePumpReversible'), fields: pumpFields('air.singlePumpReversible.pump', isAir('singlePumpReversible')) },
      { title: 'Pompe unique — aiguillage', w: isAir('singlePumpReversible'), fields: [
        { p: 'air.singlePumpReversible.diverter', l: 'Actionneur', t: 'sel', o: [['servo', 'Servo 3 voies'], ['solenoid', 'Électro-vanne']] },
        { p: 'air.singlePumpReversible.diverterChannel', l: 'Canal', t: 'int', min: 0, max: 31 },
        { p: 'air.singlePumpReversible.blowAngle', l: 'Angle souffle', u: '°', t: 'int', min: 0, max: 180, w: (c) => isAir('singlePumpReversible')(c) && g(c, 'air.singlePumpReversible.diverter') === 'servo' },
        { p: 'air.singlePumpReversible.drawAngle', l: 'Angle aspiration', u: '°', t: 'int', min: 0, max: 180, w: (c) => isAir('singlePumpReversible')(c) && g(c, 'air.singlePumpReversible.diverter') === 'servo' },
        { p: 'air.singlePumpReversible.neutralAngle', l: 'Angle neutre', u: '°', t: 'int', min: 0, max: 180, w: (c) => isAir('singlePumpReversible')(c) && g(c, 'air.singlePumpReversible.diverter') === 'servo' },
        { p: 'air.singlePumpReversible.solenoidBlowState', l: 'Vanne excitée = souffle', t: 'bool', w: (c) => isAir('singlePumpReversible')(c) && g(c, 'air.singlePumpReversible.diverter') === 'solenoid' },
        { p: 'air.singlePumpReversible.switchMs', l: 'Temps de bascule', u: 'ms', t: 'int', min: 0, max: 2000,
          h: 'Aucune note n’est déclarée « prête » pendant la bascule' },
      ] },
      { title: 'Pompe unique — régulation', w: isAir('singlePumpReversible'), fields: [
        ...piFields('air.singlePumpReversible', isAir('singlePumpReversible')),
        { p: 'air.singlePumpReversible.idleDuty', l: 'Régime de veille', t: 'num', step: 0.01, min: 0, max: 1 },
      ] },
      { title: 'Pompe unique — capteur', w: isAir('singlePumpReversible'), fields: sensorFields('air.singlePumpReversible.sensor', isAir('singlePumpReversible')) },

      { title: 'Moteur pas-à-pas (vérin / soufflet)', w: (c) => (AIR_TRAITS[g(c, 'air.impl')] || {}).stepper, fields: [
        { p: 'board.stepper.step', l: 'Broche STEP', t: 'int', min: 0, max: 39 },
        { p: 'board.stepper.dir', l: 'Broche DIR', t: 'int', min: 0, max: 39 },
        { p: 'board.stepper.enable', l: 'Broche EN', t: 'int', min: 0, max: 39 },
        { p: 'board.stepper.invertEnable', l: 'EN actif bas', t: 'bool', h: 'DRV8825 / TMC2208 : oui' },
      ] },
      { title: 'Fins de course', w: (c) => (AIR_TRAITS[g(c, 'air.impl')] || {}).homing, fields: [
        { p: 'board.endstops.r1', l: 'Broche butée R1', t: 'int', min: 0, max: 39 },
        { p: 'board.endstops.r2', l: 'Broche butée R2', t: 'int', min: 0, max: 39 },
        { p: 'board.endstops.activeLow', l: 'Contact actif bas', t: 'bool' },
      ] },
    ],
  },

  {
    id: 'valve', title: 'Distribution', icon: '🎛️',
    blocks: [
      { title: 'Type de valve', fields: [
        { p: 'valve.impl', l: 'Montage', t: 'sel', o: VALVE_IMPLS,
          h: 'Une valve « 1 entrée » impose la direction globalement : pas de souffle+aspiration simultanés' },
        { p: 'valve.settleMs', l: 'Temps de course', u: 'ms', t: 'int', min: 0, max: 500,
          h: 'Servo ≈ 50-150 ms, électro-vanne ≈ 5-15 ms (info + délai valve→air de l’onglet Jeu)' },
      ] },
      { title: 'Bus servo (PCA9685)', w: (c) => (VALVE_TRAITS[g(c, 'valve.impl')] || {}).actuator === 'servo' ||
                                                 (g(c, 'slide.enabled') && g(c, 'slide.impl') === 'servo') ||
                                                 (AIR_TRAITS[g(c, 'air.impl')] || {}).pumps, fields: [
        { p: 'valve.pca9685.addr', l: 'Adresse I2C', t: 'hex' },
        { p: 'valve.pca9685.freqHz', l: 'Fréquence', u: 'Hz', t: 'num', min: 24, max: 1526, h: '50 Hz pour des servos analogiques' },
        { p: 'valve.pca9685.oscHz', l: 'Oscillateur', u: 'Hz', t: 'num', h: 'À affiner si les angles sont décalés (25-27 MHz)' },
        { p: 'valve.servoUs.min', l: 'Impulsion mini', u: 'µs', t: 'int', min: 100, max: 3000 },
        { p: 'valve.servoUs.max', l: 'Impulsion maxi', u: 'µs', t: 'int', min: 100, max: 3000 },
      ] },
      { title: 'Bus électro-vannes', w: usesSolenoids, fields: [
        { p: 'valve.solenoids.impl', l: 'Pilotage', t: 'sel', o: [['pca9685', '2ᵉ PCA9685 (permet le maintien réduit)'], ['gpio', 'GPIO directes / ULN2803']] },
        { p: 'valve.solenoids.pcaAddr', l: 'Adresse I2C', t: 'hex', w: (c) => usesSolenoids(c) && g(c, 'valve.solenoids.impl') === 'pca9685' },
        { p: 'valve.solenoids.pcaFreqHz', l: 'Fréquence de hachage', u: 'Hz', t: 'num', min: 24, max: 1526, w: (c) => usesSolenoids(c) && g(c, 'valve.solenoids.impl') === 'pca9685' },
        { p: 'valve.solenoids.activeLow', l: 'Sortie active basse', t: 'bool' },
        { p: 'valve.solenoids.holdDuty', l: 'Maintien après le pic', t: 'num', step: 0.05, min: 0.05, max: 1,
          h: '« Peak & hold » : 1 = plein courant en permanence (chauffe)', w: (c) => usesSolenoids(c) && g(c, 'valve.solenoids.impl') === 'pca9685' },
        { p: 'valve.solenoids.peakMs', l: 'Durée du pic', u: 'ms', t: 'int', min: 5, max: 500, w: (c) => usesSolenoids(c) && g(c, 'valve.solenoids.impl') === 'pca9685' },
        { p: 'valve.solenoids.gpioPins', l: 'Broches (canal 0, 1, 2…)', t: 'ints', w: (c) => usesSolenoids(c) && g(c, 'valve.solenoids.impl') === 'gpio' },
      ] },
      { title: 'Trous', custom: 'holes' },
      { title: 'Slide (harmonica chromatique)', fields: [
        { p: 'slide.enabled', l: 'Slide monté', t: 'bool', h: 'Obligatoire pour jouer les altérations d’un chromatique' },
        { p: 'slide.impl', l: 'Actionneur', t: 'sel', o: [['servo', 'Servo'], ['solenoid', 'Électroaimant']], w: (c) => g(c, 'slide.enabled') },
        { p: 'slide.channel', l: 'Canal', t: 'int', min: 0, max: 31, w: (c) => g(c, 'slide.enabled') },
        { p: 'slide.engagedAngle', l: 'Angle enfoncé', u: '°', t: 'int', min: 0, max: 180, w: (c) => g(c, 'slide.enabled') && g(c, 'slide.impl') === 'servo' },
        { p: 'slide.restAngle', l: 'Angle repos', u: '°', t: 'int', min: 0, max: 180, w: (c) => g(c, 'slide.enabled') && g(c, 'slide.impl') === 'servo' },
        { p: 'slide.settleMs', l: 'Temps de course', u: 'ms', t: 'int', min: 0, max: 500, w: (c) => g(c, 'slide.enabled') },
      ] },
    ],
  },

  {
    id: 'harmonica', title: 'Harmonica', icon: '🎼',
    blocks: [
      { title: 'Instrument', fields: [
        { p: 'harmonica.name', l: 'Nom', t: 'text' },
        { p: 'harmonica.holeCount', l: 'Nombre de trous', t: 'int', min: 1, max: 24 },
        { p: 'harmonica.hasSlide', l: 'Utilise le slide', t: 'bool', h: 'Nécessite « Slide monté » dans l’onglet Distribution' },
      ] },
      { title: 'Générateur de mapping', custom: 'generator' },
      { title: 'Mapping note MIDI → trou', custom: 'notes' },
      { title: 'Presets embarqués', custom: 'presets' },
    ],
  },

  {
    id: 'midi', title: 'MIDI', icon: '🎹',
    blocks: [
      { title: 'Général', fields: [
        { p: 'midi.channel', l: 'Canal écouté', t: 'int', min: 0, max: 16, h: '0 = OMNI (tous les canaux)' },
        { p: 'midi.activeWireless', l: 'Sans-fil actif', t: 'sel', o: [['none', 'Aucun'], ['wifi', 'WiFi (RTP-MIDI)'], ['ble', 'Bluetooth LE']],
          h: 'Une seule radio à la fois : l’ESP32 classique n’a qu’un émetteur 2,4 GHz' },
      ] },
      { title: 'MIDI filaire (DIN / UART)', fields: [
        { p: 'midi.serial.enabled', l: 'Activé', t: 'bool' },
        { p: 'midi.serial.rxPin', l: 'Broche RX', t: 'int', min: 0, max: 39, w: (c) => g(c, 'midi.serial.enabled') },
        { p: 'midi.serial.txPin', l: 'Broche TX', t: 'int', min: 0, max: 39, w: (c) => g(c, 'midi.serial.enabled') },
        { p: 'midi.serial.baud', l: 'Débit', u: 'bauds', t: 'int', h: '31250 pour du MIDI DIN standard', w: (c) => g(c, 'midi.serial.enabled') },
      ] },
      { title: 'Bluetooth LE', w: (c) => g(c, 'midi.activeWireless') === 'ble', fields: [
        { p: 'midi.ble.deviceName', l: 'Nom annoncé', t: 'text' },
      ] },
      { title: 'WiFi / RTP-MIDI', w: (c) => g(c, 'midi.activeWireless') === 'wifi', fields: [
        { p: 'midi.wifi.mode', l: 'Mode', t: 'sel', o: [['sta', 'Station (rejoint un réseau)'], ['ap', 'Point d’accès']] },
        { p: 'midi.wifi.ssid', l: 'SSID', t: 'text' },
        { p: 'midi.wifi.password', l: 'Mot de passe réseau', t: 'pass' },
        { p: 'midi.wifi.apPassword', l: 'Mot de passe du point d’accès', t: 'pass' },
        { p: 'midi.wifi.sessionName', l: 'Nom de session RTP', t: 'text' },
        { p: 'midi.wifi.rtpPort', l: 'Port RTP', t: 'int', min: 1, max: 65535 },
      ] },
    ],
  },

  {
    id: 'engine', title: 'Jeu', icon: '🎚️',
    blocks: [
      { title: 'Polyphonie', fields: [
        { p: 'engine.maxPolyphony', l: 'Voix maximum', t: 'int', min: 1, max: 24, h: 'Une voix = un trou' },
        { p: 'engine.arbitration', l: 'Conflit de direction', t: 'sel', o: [['reject', 'Refuser la nouvelle note'], ['steal', 'Couper les notes en cours']],
          h: 'Ne s’applique que si le montage ne sait pas souffler et aspirer en même temps' },
      ] },
      { title: 'Dynamique', fields: [
        { p: 'engine.velocityToIntensity', l: 'Vélocité → pression', t: 'bool' },
        { p: 'engine.minIntensity', l: 'Intensité plancher', t: 'num', step: 0.01, min: 0, max: 1, h: 'Évite qu’une note très douce ne sonne pas du tout' },
        { p: 'engine.ccVolumeEnabled', l: 'CC7 volume', t: 'bool' },
        { p: 'engine.vibratoRateHz', l: 'Vitesse du vibrato (CC1)', u: 'Hz', t: 'num', step: 0.1, min: 0.1, max: 20 },
        { p: 'engine.vibratoDepth', l: 'Profondeur du vibrato', t: 'num', step: 0.01, min: 0, max: 1 },
      ] },
      { title: 'Bends & pitch-bend', fields: [
        { p: 'engine.bendEnabled', l: 'Actionner les bends', t: 'bool', h: 'Traduit les demi-tons de bend en surpression / dépression' },
        { p: 'engine.bendPressureGain', l: 'Gain de bend', t: 'num', step: 0.01, min: 0, max: 2, h: 'Supplément d’intensité par demi-ton', w: (c) => g(c, 'engine.bendEnabled') },
        { p: 'engine.pitchBendRangeSemitones', l: 'Plage du pitch-bend', u: '1/2 tons', t: 'num', step: 0.5, min: 0, max: 24 },
      ] },
      { title: 'Sécurités de jeu', fields: [
        { p: 'engine.holeSettleMs', l: 'Délai valve → air', u: 'ms', t: 'int', min: 0, max: 500,
          h: 'Laisse la valve finir sa course avant d’envoyer l’air (0 = immédiat)' },
        { p: 'engine.noteMaxHoldMs', l: 'Durée maxi d’une note', u: 'ms', t: 'int', min: 0, max: 600000,
          h: 'Coupe une note dont le NoteOff s’est perdu (0 = jamais)' },
      ] },
    ],
  },

  {
    id: 'system', title: 'Système', icon: '⚙️',
    blocks: [
      { title: 'Bus I2C', fields: [
        { p: 'board.i2c.sda', l: 'SDA', t: 'int', min: 0, max: 39 },
        { p: 'board.i2c.scl', l: 'SCL', t: 'int', min: 0, max: 39 },
        { p: 'board.i2c.freqHz', l: 'Fréquence', u: 'Hz', t: 'int', h: '400000 recommandé avec plusieurs PCA9685' },
        { p: 'board.statusLed', l: 'LED d’état', t: 'int', min: -1, max: 39 },
      ] },
      { title: 'Exécution', fields: [
        { p: 'system.mockMode', l: 'Mode simulation', t: 'bool', h: 'Aucun accès matériel : permet de régler l’UI sans banc (SoftAP « Harmonica-Setup »)' },
        { p: 'system.autoHomeOnBoot', l: 'Homing au démarrage', t: 'bool' },
        { p: 'system.telemetryHz', l: 'Fréquence de télémétrie', u: 'Hz', t: 'num', step: 0.5, min: 0.5, max: 50 },
      ] },
      { title: 'Accès web', fields: [
        { p: 'web.user', l: 'Utilisateur', t: 'text' },
        { p: 'web.password', l: 'Mot de passe', t: 'pass', h: 'Vide = aucune authentification (à réserver au réseau de développement)' },
      ] },
    ],
  },

  {
    id: 'bench', title: 'Banc d’essai', icon: '🔧',
    blocks: [{ custom: 'bench' }],
  },

  {
    id: 'raw', title: 'JSON', icon: '📄',
    blocks: [{ custom: 'raw' }],
  },
];
