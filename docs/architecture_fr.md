# Architecture du firmware — Harmonica MIDI modulaire (ESP32)

> Base modulaire et configurable depuis une page web, pour piloter un harmonica
> électromécanique (servos + moteur pas-à-pas). Ce document est la référence de
> conception ; il accompagne le squelette de code présent dans ce dépôt.

## 1. Objectifs et principes

Partant du concept décrit dans le [README](../README.md) (harmonica diatonique
10 trous, vérin double, valves 2-en-1, MIDI USB, Arduino), l'objectif est une
base **générique** capable de gérer :

- plusieurs **types de valves** : `2 entrées → 1 sortie` **ou** `1 entrée → 1 sortie` ;
- plusieurs **systèmes d'air** : **vérin double** (2 réservoirs + piston) **ou** **soufflet simple** ;
- plusieurs **types d'harmonicas** : diatonique multi-tonalités, chromatique (slide), trémolo/octave, nombre de trous variable ;
- du MIDI reçu en **WiFi** (RTP/AppleMIDI), **Bluetooth** (BLE-MIDI) ou **filaire** (DIN/UART série).

Principes directeurs :

1. **Pattern *stratégie* partout.** Air, valve, transport MIDI, moteur et capteur
   sont des **interfaces** ; les classes concrètes sont choisies **au boot depuis
   `config.json`** par une *factory*. Rien au-dessus de la HAL ne connaît le
   montage physique réel.
2. **Config-driven, pas code-driven.** Le type d'harmonica est **pure donnée** :
   ajouter une tonalité = éditer le JSON, jamais recompiler.
3. **Compile et tourne sans matériel.** Une HAL *mock* permet d'exécuter la
   logique sur PC (tests) ou sur un ESP32 nu (`mockMode`).
4. **Un seul sans-fil à la fois.** L'ESP32 classique a une radio 2,4 GHz unique
   et une RAM limitée : WiFi **ou** BLE, sélectionné en config. Le série DIN est
   toujours disponible.

## 2. Vue d'ensemble des modules

```
                 ┌─────────────┐   MidiEvent   ┌──────────────┐
  DIN / BLE /    │ IMidiTransport ├────────────▶│  MidiRouter  │
  WiFi RTP  ───▶ │  (x1..3)     │  (+filtre canal)└──────┬───────┘
                 └─────────────┘                        │ file inter-cœurs
                                                        ▼
   config.json ──▶ Factory ──▶  ┌───────────────────────────────────┐
   (LittleFS)                   │            NoteEngine              │
                                │  voix/trou · matrice de capacité · │
                                │  arbitrage · réaction au swap      │
                                └───┬─────────────┬─────────────┬────┘
                                    ▼             ▼             ▼
                              HarmonicaMap    IAirSource    IValveDriver (+ ISlideActuator)
                              note→trou/sens  vérin/soufflet  2en1 / 1en1
                                                    │             │
                                                    ▼             ▼
                                                 HAL (IStepper, IPressureSensor, IEndstops, IServoBus)
                                                 réelle (PCA9685/AccelStepper/BMP280…) ou mock
```

| Abstraction | Rôle | Implémentations |
|---|---|---|
| `IMidiTransport` (+ `MidiRouter`) | recevoir/parser le MIDI | `SerialMidi`, `BleMidi`, `WifiRtpMidi` |
| `IAirSource` | fournir souffle/aspiration | `DualReservoirPiston`, `SingleBellows` |
| `IValveDriver` | router l'air vers chaque trou | `Valve2in1`, `Valve1in1` |
| `HarmonicaMap` (+ `ISlideActuator`) | note MIDI → trou + sens (+ slide) | générique ; `ServoSlide` |
| `NoteEngine` | colle le tout, polyphonie, arbitrage | — |
| HAL | matériel | réel (`Pca9685Bus`, `AccelStepperAdapter`, `Bmp280Sensor`, `Mpx2010Sensor`, `GpioEndstops`) + mocks |

## 3. Le vocabulaire clé : `Direction` vs `Rail`

- `Direction { Closed, Blow, Draw }` = intention **acoustique** d'un trou.
- `Rail { None, A, B }` = plomberie **physique** (R1 = A, R2 = B).

Toute la subtilité du vérin double vit dans la correspondance `Direction ↔ Rail`,
détenue par l'`IAirSource` via `railForDirection(dir)`.

### La subtilité du vérin double

Consommer du **souffle** (l'air quitte R1) *et* de l'**aspiration** (l'air entre
dans R2) poussent le piston **dans le même sens**. Il dérive donc vers une
extrémité et doit **s'inverser** périodiquement. À chaque inversion, les rôles
souffle/aspiration des réservoirs **s'échangent** (`blowRail` bascule A↔B) et un
compteur `assignmentGen` est incrémenté. Le `NoteEngine` surveille ce compteur et
**ré-applique les positions de valve** de toutes les voix actives (chaque valve
2-en-1 bascule de rail pour conserver sa direction acoustique). D'où l'objectif du
README : *rester centré / minimiser le switch de valves*.

`DualReservoirPiston` implémente : homing sur endstop → auto-centrage (2 valves
réservoir ouvertes, sans son) → régulation de pression (avance le piston tant que
la pression du rail souffle est sous la cible) → inversion à la marge de course
(ou anticipée à vide au-delà de 60 % pour rester centré).

## 4. Matrice de capacité & polyphonie

Le `NoteEngine` modélise **une voix par trou** (un trou physique ne peut sonner
qu'un seul sens à la fois). Ses capacités dérivent des deux stratégies actives :

| Air \ Valve | `Valve2in1` | `Valve1in1` |
|---|---|---|
| `DualReservoirPiston` | **souffle + aspiration simultanés** (montage documenté) | 1 sens global |
| `SingleBellows` | 1 sens à la fois | 1 sens à la fois |

`mixedCapable = air.supportsSimultaneousDirections() && valve.supportsPerHoleDirection()`.
Seule la case en haut à gauche est `true`. Sinon, quand une note de direction
opposée à celle en cours arrive, le moteur applique `engine.arbitration` :
`reject` (ignore la nouvelle) ou `steal` (libère l'ancienne direction).

CC gérés : `CC2` breath et `CC11` expression modulent l'intensité globale (donc
la pression demandée, y compris pour les notes déjà tenues) ; `CC1` modulation est
mémorisé (vibrato de pression = extension) ; `CC123` all-notes-off. Vélocité →
intensité si `velocityToIntensity`.

## 5. Schéma de configuration (`config.json`)

Un seul document sur LittleFS. Sections : `board` (brochage, I2C), `midi`
(transport actif + identifiants), `air` (`impl` + params des deux familles),
`valve` (`impl` + PCA9685 + angles par trou pour les deux familles), `slide`,
`harmonica` (nom, `holeCount`, table `notes`), `engine` (polyphonie, arbitrage),
`system` (`mockMode`, télémétrie). Voir [`data/config.json`](../data/config.json)
pour l'exemple complet (diatonique C + vérin double + valve 2-en-1).

- **Chromatique** : `slide.enabled=true`, `harmonica.hasSlide=true`, et `"slide":true`
  sur les notes concernées (chaque trou donne alors 4 notes : souffle/aspir × slide sorti/rentré).
- **Autre harmonica** : simplement une autre liste `notes` (+ `holeCount`) — zéro code.
- **Changer de famille air/valve** : changer `air.impl` / `valve.impl` et leurs blocs de params.

Les [`presets`](../data/presets/) (`diatonic_C.json`, `chromatic_C.json`) sont des
objets `harmonica` prêts à fusionner dans la config.

## 6. Persistance & API web

`ConfigStore` charge/valide/sauvegarde `config.json` (ArduinoJson v7). Sauvegarde
**atomique** (`/config.tmp` puis rename) et **fallback** sur un défaut embarqué si
le fichier est absent ou illisible. Le JSON brut est conservé pour être renvoyé tel
quel par l'API.

Serveur : `ESPAsyncWebServer` (asynchrone → ne bloque jamais la boucle de contrôle).

| Méthode | Route | Rôle |
|---|---|---|
| GET  | `/api/config`     | config.json brut |
| POST | `/api/config`     | valide → sauvegarde atomique |
| GET  | `/api/status`     | télémétrie (pressions, piston, voix, heap, transport…) |
| POST | `/api/calibrate`  | `{servo\|piston\|pressureZero}` |
| GET  | `/api/harmonicas` | presets disponibles |
| POST | `/api/reboot`     | redémarrage |
| WS   | `/ws`             | télémétrie poussée à `system.telemetryHz` |

**Calibration** : angle par servo (trouver `railAangle`/`railBangle`/`closedAngle`
par trou et les angles des valves réservoir), homing/centrage piston, tare
pression (à faire à l'ambiant, indispensable pour un capteur absolu).

La page web tourne sur le WiFi quand `activeWireless=="wifi"` ; quand BLE est
choisi, un **SoftAP « mode config »** (activé en `mockMode` ici) expose la page
sans WiFi+BLE simultanés.

## 7. Découpage bi-cœur (FreeRTOS)

- **Core 1 — `controlTask`** (~1 kHz, priorité haute) : draine la file MIDI puis
  `air->update` (dont `stepper.run()` + régulation), `valve->update`, `slide->update`,
  `engine->update` (réaction au swap). Déterministe, jamais bloqué par la radio.
- **Core 0 — `netTask`** : `router.loop()` (parsing série/BLE/RTP), serveur web,
  télémétrie.
- **Pont** : une `QueueHandle_t` — le sink du routeur enfile (Core 0), le moteur
  défile (Core 1). La gigue réseau ne touche jamais l'actionnement.

> Voie d'upgrade : **FastAccelStepper** (stepping matériel RMT/MCPWM) pour des
> cadences de pas élevées sans jitter, au lieu d'`AccelStepper::run()` cadencé par
> le tick.

## 8. Ressources ESP32 classique & brochage

Tous les servos sont déportés sur le PCA9685 → l'ESP32 ne dépense que 2 broches
(I2C) pour 12–15 servos.

| Fonction | GPIO | Note |
|---|---|---|
| I2C SDA / SCL | 21 / 22 | PCA9685 `0x40` + 2× BMP280 `0x76`/`0x77`, 400 kHz, **I2C confiné au Core 1** |
| Stepper STEP / DIR / EN | 26 / 27 / 25 | vers DRV8825 / TMC2208 (EN actif bas) |
| Endstop R1 / R2 | 32 / 33 | `INPUT_PULLUP` vers GND |
| MIDI DIN RX / TX | 16 / 17 | UART2 @ 31250, opto 6N138 en entrée |
| Pression analog R1/R2 (option MPX2010) | 34 / 35 | ADC1 (compatible WiFi) |
| LED statut | 2 | onboard |

Bus I2C unique : PCA9685 `0x40`, BMP280 `0x76` (SDO→GND) et `0x77` (SDO→VCC) — pas
de collision ; un 2ᵉ PCA9685 `0x41` est possible au-delà de 16 servos. RAM/flash :
WiFi **+** BLE **+** serveur async ensemble = risque OOM et gigue radio → **un seul
sans-fil**, table `huge_app` (3 Mo app + ~1 Mo LittleFS, sans OTA), NimBLE plutôt que
Bluedroid.

## 9. Structure du dépôt

```
platformio.ini            env esp32dev (LittleFS, huge_app) + env native (tests/mock)
partitions_huge.csv
include/  Types.h Config.h Factory.h
          air/{IAirSource,DualReservoirPiston,SingleBellows}.h
          valve/{IValveDriver,Valve2in1,Valve1in1,ISlideActuator}.h
          midi/{IMidiTransport,MidiParser,MidiRouter,SerialMidi,BleMidi,WifiRtpMidi}.h
          engine/{HarmonicaMap,NoteEngine}.h
          hal/{Hal,Mocks,ArduinoHal}.h
          web/{ConfigStore,DefaultConfig,WebServer}.h
src/      main.cpp Factory.cpp ConfigStore.cpp
          midi/{BleMidi,WifiRtpMidi}.cpp  web/WebServer.cpp
data/     config.json index.html app.js style.css   presets/{diatonic_C,chromatic_C}.json
test/     test_core/test_main.cpp                    (Unity)
docs/     architecture_fr.md
```

Le **cœur** (Types, Config, air, valve, engine, map, parser, router, HAL mock) est
en header-only et sans dépendance Arduino → il compile sur PC. Les en-têtes réels
Arduino/Adafruit et la glue BLE/WiFi/web sont sous `#if HARM_ARDUINO`.

## 10. Compiler, tester, simuler

```bash
# Firmware ESP32
pio run -e esp32dev
pio run -e esp32dev -t uploadfs      # envoie data/ (config + web) sur LittleFS
pio run -e esp32dev -t upload

# Logique pure sur PC (aucun matériel)
pio test -e native                   # tests unitaires
pio run  -e native && .pio/build/native/program   # démo mock (journalise les actionneurs)
```

**Mode simulation** : flag `-DHARM_MOCK` (env native) ou `system.mockMode=true`
(sur ESP32). Les mocks (`MockStepper`, `MockPressure`, `MockServoBus`,
`MockEndstops`) rejouent la **vraie** logique air/valve/engine en journalisant les
commandes — idéal pour développer le web/MIDI avant que la mécanique existe.

> Vérification réalisée pour ce squelette (hôte g++/C++17) : build natif complet,
> **8 tests unitaires** (mapping, slide, matrice de capacité, arbitrage reject/steal,
> inversion de rail du vérin, parseur MIDI running-status, sélection de rail 2-en-1)
> et contrôle syntaxique des chemins ESP32 (HAL réelle, Factory, tâches, serveur web).

## 11. État & feuille de route

**Implémenté et vérifié sur hôte** : les 4 abstractions + `NoteEngine`, les **deux**
familles air (`DualReservoirPiston`, `SingleBellows`) et valve (`Valve2in1`,
`Valve1in1`), le slide, `HarmonicaMap` générique, `MidiParser`/`MidiRouter`,
`SerialMidi`, `ConfigStore`, la Factory, la démo mock et les tests.

**Implémenté, à valider sur matériel** : HAL réelle (PCA9685/AccelStepper/BMP280/
MPX2010/endstops), transports **BLE** (phase 3) et **WiFi RTP** (phase 4), serveur
web + calibration (phase 5), bring-up bi-cœur.

**À poursuivre** : régulation de pression PI plus fine, optimisation « rester
centré », expression complète (CC1 vibrato, `CC5`/pitch-bend → extension de
`NoteMapping` pour les bends), presets complets par famille, fusion de preset
côté UI.

## 12. Risques & recommandations matérielles

1. **Coexistence WiFi+BLE** → un seul sans-fil à la fois ; NimBLE ; SoftAP config
   pour les builds BLE ; surveiller `freeHeap` via `/api/status`.
2. **Alimentation servos** → 12–15 servos peuvent pointer ~1 A chacun : **5 V dédié
   10–20 A**, masse commune, condensateurs, rail V+ isolé du 5 V logique (sinon
   brownout au boot). Échelonner les mouvements simultanés.
3. **Capteur de pression** → le BMP280 est **absolu et bruité** à ±0,1 kPa ;
   préférer un **MPX2010DP** (différentiel) ou un capteur série SDP. `IPressureSensor`
   rend le remplacement trivial ; **tare à l'ambiant** obligatoire.
4. **Perte de pas** → boucle fermée par la pression + endstops comme référence
   absolue (le 2ᵉ endstop = sécurité du README) ; re-homing pendant les repos ;
   option StallGuard (TMC2208).
5. **Valve 2-en-1 imprimée** → prévoir un **centre bloquant** réel (position fermée)
   pour qu'un trou relâché ne fuie pas le rail partagé.
6. **Latence d'air vs tempo** → le débattement servo (~50–150 ms) limite les traits
   rapides ; servos rapides métal, faible course angulaire, piston centré pour
   éviter les inversions en plein jeu.
```
