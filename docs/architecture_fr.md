# Architecture du firmware — Harmonica MIDI modulaire (ESP32)

> Base modulaire et configurable depuis une page web, pour piloter un harmonica
> électromécanique (servos + moteur pas-à-pas). Ce document est la référence de
> conception ; il accompagne le squelette de code présent dans ce dépôt.

## 1. Objectifs et principes

Partant du concept décrit dans le [README](../README.md) (harmonica diatonique
10 trous, vérin double, valves 2-en-1, MIDI USB, Arduino), l'objectif est une
base **générique** capable de gérer :

- plusieurs **types de valves** : servo `2 entrées → 1 sortie`, servo `1 entrée → 1 sortie`,
  **électro-vannes** 2 par trou, ou électro-vanne 1 par trou ;
- plusieurs **systèmes d'air** : **vérin double** (2 réservoirs + piston), **soufflet
  simple**, **deux pompes continues opposées** ou **une pompe + aiguillage** ;
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
                                 HAL (IServoBus, IStepper, IPressureSensor, IEndstops,
                                      IDigitalOutBus, IPwmOut)
                                 réelle (PCA9685 / AccelStepper / BMP280 / LEDC / ESC…) ou mock
```

| Abstraction | Rôle | Implémentations |
|---|---|---|
| `IMidiTransport` (+ `MidiRouter`) | recevoir/parser le MIDI | `SerialMidi`, `BleMidi`, `WifiRtpMidi` |
| `IAirSource` | fournir souffle/aspiration | `DualReservoirPiston`, `SingleBellows`, `PumpPair`, `SinglePumpReversible` |
| `IValveDriver` | router l'air vers chaque trou | `Valve2in1`, `Valve1in1`, `ValveSolenoid2in1`, `ValveSolenoid1in1` |
| `HarmonicaMap` (+ `ISlideActuator`) | note MIDI → trou + sens (+ slide) | générique ; `ServoSlide`, `SolenoidSlide` |
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

### Les autres systèmes d'air

| Montage | Principe | Simultané ? | Compromis |
|---|---|---|---|
| `DualReservoirPiston` | 2 réservoirs + piston sur tige filetée | oui | silencieux et débit maîtrisé, mais butées, homing, inversions |
| `SingleBellows` | 1 soufflet motorisé : comprimer = souffle, détendre = aspiration | non | mécanique minimale, une seule direction, réserve d'air limitée |
| `PumpPair` | 2 pompes/turbines continues : un plenum en surpression, l'autre en dépression | **oui** | aucun homing, jeu illimité dans le temps ; bruit et consommation continus |
| `SinglePumpReversible` | 1 pompe + aiguillage (servo 3 voies ou électro-vanne) | non | le montage le plus économique ; temps de bascule (`switchMs`) à chaque changement de sens |

Les deux montages à pompes régulent la pression **par le régime** (PI → duty PWM)
au lieu de la position d'un piston ; le rail est fixe (souffle = A, aspiration = B),
donc pas de permutation `Rail ↔ Direction` (`assignmentGen` reste à 0). Une purge
optionnelle (`bleedChannel`) remet le plenum à l'air libre au silence, et
`idleDuty` garde les pompes amorcées entre deux notes pour une attaque franche.

### Distribution : servos ou électro-vannes

`Valve2in1`/`Valve1in1` pilotent un servo par trou via le PCA9685.
`ValveSolenoid2in1`/`ValveSolenoid1in1` pilotent des **électro-vannes** via un
`IDigitalOutBus` (2ᵉ PCA9685 en tout-ou-rien, ou GPIO/ULN2803) : commutation
5–15 ms au lieu de 50–150 ms, donc des traits rapides possibles, en échange d'un
courant de maintien. D'où le **« peak & hold »** porté par le bus : plein courant
pendant `peakMs`, puis maintien à `holdDuty` (uniquement si le bus sait moduler).

## 4. Matrice de capacité & polyphonie

Le `NoteEngine` modélise **une voix par trou** (un trou physique ne peut sonner
qu'un seul sens à la fois). Ses capacités dérivent des deux stratégies actives :

| Air \ Valve | `Valve2in1` (servo) | `Valve1in1` (servo) | `Solenoid2in1` | `Solenoid1in1` |
|---|---|---|---|---|
| `DualReservoirPiston` | **simultané** | 1 sens global | **simultané** | 1 sens global |
| `SingleBellows` | 1 sens | 1 sens | 1 sens | 1 sens |
| `PumpPair` | **simultané** | 1 sens global | **simultané** | 1 sens global |
| `SinglePumpReversible` | 1 sens | 1 sens | 1 sens | 1 sens |

`mixedCapable = air.supportsSimultaneousDirections() && valve.supportsPerHoleDirection()`.
Sinon, quand une note de direction opposée à celle en cours arrive, le moteur
applique `engine.arbitration` : `reject` (ignore la nouvelle) ou `steal` (libère
l'ancienne direction). Chaque source publie aussi un descripteur `AirCaps`
(piston ? homing ? pompes ? aiguillage ? combien de capteurs ?) : c'est lui qui
pilote l'affichage de l'UI et les commandes de banc d'essai proposées.

**Contrôleurs MIDI gérés** : `CC1` modulation → **vibrato de pression** (live),
`CC2` breath, `CC7` volume et `CC11` expression → intensité globale (les notes déjà
tenues suivent), `CC123` all-notes-off, **pitch-bend** 14 bits. Vélocité → intensité
si `velocityToIntensity`.

**Bends actionnés** : une note obtenue par bend demande physiquement *plus* de
dépression/surpression. Le moteur traduit les demi-tons (`bend` du mapping +
pitch-bend live) en supplément d'intensité : `intensité × (1 + bendPressureGain ×
|demi-tons|)`, désactivable par `engine.bendEnabled`.

**Sécurités de jeu** : `engine.holeSettleMs` laisse la valve finir sa course avant
d'envoyer l'air (une valve servo met 50–150 ms, une électro-vanne 5–15 ms) ;
`engine.noteMaxHoldMs` coupe une note dont le NoteOff s'est perdu ;
`engine.minIntensity` garantit qu'une note très douce sonne quand même.

## 5. Schéma de configuration (`config.json`)

Un seul document sur LittleFS. Sections : `board` (brochage, I2C), `midi`
(transport actif + identifiants), `air` (`impl` + un bloc par famille :
`dualReservoirPiston`, `singleBellows`, `pumpPair`, `singlePumpReversible`),
`valve` (`impl` + PCA9685 + bus d'électro-vannes + une liste de trous par famille),
`slide`, `harmonica` (nom, `holeCount`, table `notes`), `engine` (polyphonie,
arbitrage, bends, sécurités), `web`, `system`. Voir
[`data/config.json`](../data/config.json) pour l'exemple complet (diatonique C +
vérin double + valve 2-en-1).

> `data/config.json` est la **source de vérité** : `include/web/DefaultConfig.h`
> (le fallback embarqué) en est **généré** par `python3 tools/gen_default_config.py`,
> et le pipeline de test échoue si les deux divergent.

Chaque famille garde son bloc de paramètres même quand elle n'est pas active :
changer `air.impl` ou `valve.impl` suffit à basculer de montage sans ressaisir
quoi que ce soit. Le `holeCount` effectif est celui de la **liste de trous de
l'implémentation choisie** (`valve.<impl>.holes`).

- **Chromatique** : `slide.enabled=true`, `harmonica.hasSlide=true`, et `"slide":true`
  sur les notes concernées (chaque trou donne alors 4 notes : souffle/aspir × slide sorti/rentré).
- **Autre harmonica** : simplement une autre liste `notes` (+ `holeCount`) — zéro code.
- **Changer de famille air/valve** : changer `air.impl` / `valve.impl` et leurs blocs de params.

Les [`presets`](../data/presets/) sont des objets `harmonica` prêts à fusionner
dans la config, générés par `python3 tools/gen_presets.py` : diatoniques Richter
en C/D/F/G/A, diatonique C **avec bends**, chromatiques 12 et 16 trous (slide),
trémolo 12 et octave 10 (accordage solo). Les mêmes formules sont disponibles dans
le **générateur de l'UI** (onglet Harmonica), pour créer une tonalité sans script.

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
| GET  | `/api/status`     | télémétrie (pressions, duty pompes, piston, voix, trous qui sonnent, heap…) |
| GET  | `/api/capabilities` | ce que CE firmware sait piloter (montages, bus, limites) |
| POST | `/api/calibrate`  | `{servo\|piston\|pressureZero\|solenoid\|hole\|pump\|slide\|allOff}` |
| POST | `/api/test`       | `{noteOn\|noteOff\|allOff}` — joue une note par le chemin MIDI normal |
| GET  | `/api/harmonicas` | presets disponibles |
| POST | `/api/harmonica`  | échange l'harmonica **à chaud** (objet `harmonica`, sans reboot) + persiste |
| POST | `/api/reboot`     | redémarrage |

**Banc d'essai** (`/api/calibrate`, `/api/test`) : angle ou µs par servo (pour
trouver `railAangle`/`railBangle`/`closedAngle` de chaque trou), ouverture d'une
électro-vanne, ouverture d'un trou dans un sens, **régime forcé d'une pompe**
(`duty` en %, `-1` rend la main à la régulation), slide, homing/centrage piston,
tare pression (à l'ambiant, indispensable pour un capteur absolu), coupure
générale, et **jeu de notes MIDI** depuis la page.

**Mots de passe** : `GET /api/config` les masque ; à l'enregistrement, une clé
absente ou vide **conserve** la valeur stockée, et la sentinelle `"__clear__"`
l'efface — sans quoi éditer la config depuis l'UI effacerait le mot de passe WiFi.

**Sécurité** : `GET /api/config` **masque les secrets** (mots de passe WiFi/AP/web)
et les fichiers `/config.json` `/config.tmp` ne sont **pas servis** en clair ;
`web.password` (config) active une **authentification Basic** sur toutes les routes
`/api/*` ; les corps POST sont **plafonnés** (8 Ko) et la calibration servo est
**bornée** (µs/angle/canal) côté Core 1. Vide par défaut (dev) → penser à définir
`web.password` sur un réseau partagé.

La page web tourne sur le WiFi quand `activeWireless=="wifi"` ; quand BLE est
choisi, un **SoftAP « mode config »** (activé en `mockMode` ici) expose la page
sans WiFi+BLE simultanés.

### L'interface : un schéma déclaratif, pas des formulaires codés en dur

`data/schema.js` **décrit** la configuration (libellé, unité, type, bornes, aide,
et surtout une condition d'affichage `w`), `data/app.js` la **rend**. Ajouter une
option matérielle = une ligne dans `schema.js` ; aucun code de rendu à écrire.
La condition `w` est ce qui rend l'UI modulaire : choisir `air.impl = pumpPair`
fait disparaître les réglages du piston et apparaître ceux des pompes, choisir
une valve à électro-vannes fait apparaître le bus de vannes et le « peak & hold ».

Onglets : **État** (télémétrie + trous qui sonnent en temps réel + contrôle de
cohérence), **Air**, **Distribution** (dont l'éditeur de trous), **Harmonica**
(générateur de mapping, tableau note→trou, presets, import/export), **MIDI**,
**Jeu**, **Système**, **Banc d'essai**, **JSON** brut.

Deux garde-fous automatiques, exécutés par `tools/run_native_tests.sh` :

- `tools/check_ui_coverage.mjs` — chaque clé de `config.json` est éditable dans
  l'UI, chaque champ de l'UI pointe sur une clé existante, et chaque
  implémentation proposée est reconnue par le parseur C++ ;
- `tools/check_ui_render.mjs` — l'UI est rendue hors navigateur (DOM minimal)
  pour **tous** les onglets × **toutes** les combinaisons air × valve × slide,
  avec des assertions sur la visibilité conditionnelle des réglages.

## 7. Découpage bi-cœur (FreeRTOS)

- **Core 1 — `controlTask`** (~1 kHz, priorité haute) : draine la file MIDI puis
  `air->update` (dont `stepper.run()` + régulation), `valve->update`, `slide->update`,
  `engine->update` (réaction au swap). Déterministe, jamais bloqué par la radio.
- **Core 0 — `netTask`** : `router.loop()` (parsing série/BLE/RTP), serveur web,
  télémétrie.
- **Ponts inter-cœurs** (tout l'accès I2C et à l'état moteur reste sur le Core 1) :
  - **File MIDI** `QueueHandle_t` : le sink du routeur enfile (Core 0), le moteur
    défile (Core 1) ; un échec d'enfilage est **compté** (`droppedMidi` en télémétrie).
  - **File de commandes** web→Core 1 : calibration, homing/centrage, tare, échange
    d'harmonica sont **exécutés sur le Core 1**, jamais depuis la tâche async.
  - **Instantané de télémétrie** publié par le Core 1 sous `portMUX` : `/api/status`
    et le WebSocket le lisent **sans toucher l'I2C** ni l'état moteur.

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
| Pompe souffle / aspiration (option `pumpPair`, `drive=ledc`) | 18 / 19 | PWM LEDC 20 kHz vers MOSFET ; en `drive=esc`, ce sont des canaux du PCA9685 |
| Électro-vannes (option `solenoid*`, `impl=pca9685`) | — | 2ᵉ PCA9685 `0x41`, hachage 1 kHz, driver de puissance obligatoire (ULN2803 / MOSFET + diode de roue libre) |
| Électro-vannes (option `solenoid*`, `impl=gpio`) | au choix | liste `valve.solenoids.gpioPins`, canal 0 = 1ʳᵉ broche |
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
          air/{IAirSource,DualReservoirPiston,SingleBellows,PumpPair,SinglePumpReversible}.h
          valve/{IValveDriver,Valve2in1,Valve1in1,ValveSolenoid,ISlideActuator}.h
          midi/{IMidiTransport,MidiParser,MidiRouter,SerialMidi,BleMidi,WifiRtpMidi}.h
          engine/{HarmonicaMap,NoteEngine}.h
          hal/{Hal,Mocks,ArduinoHal}.h            (+ IDigitalOutBus, IPwmOut)
          web/{ConfigStore,DefaultConfig,WebServer}.h   DefaultConfig.h est GÉNÉRÉ
src/      main.cpp Factory.cpp ConfigStore.cpp
          midi/{BleMidi,WifiRtpMidi}.cpp  web/WebServer.cpp
data/     config.json index.html schema.js app.js style.css   presets/*.json
test/     test_core/test_main.cpp                    (Unity, 36 tests)
tools/    run_native_tests.sh          pipeline complet (config + UI + tests + syntaxe ESP32)
          gen_default_config.py        data/config.json -> DefaultConfig.h
          gen_presets.py               bibliothèque de presets d'harmonicas
          check_ui_coverage.mjs        l'UI couvre-t-elle toute la config ?
          check_ui_render.mjs          rendu de l'UI hors navigateur
          check_esp32_syntax.sh        syntaxe du chemin ESP32 (stubs/)
docs/     architecture_fr.md AUDIT.md
```

Le **cœur** (Types, Config, air, valve, engine, map, parser, router, HAL mock) est
en header-only et sans dépendance Arduino → il compile sur PC. Les en-têtes réels
Arduino/Adafruit et la glue BLE/WiFi/web sont sous `#if HARM_ARDUINO`.

## 10. Compiler, tester, simuler

```bash
# Firmware ESP32 (nécessite l'accès au registre PlatformIO)
pio run -e esp32dev
pio run -e esp32dev -t uploadfs      # envoie data/ (config + web) sur LittleFS
pio run -e esp32dev -t upload

# Pipeline complet SANS le registre PlatformIO (g++ + ArduinoJson + Unity depuis
# .cache/, Node pour l'UI). C'est la commande de CI recommandée :
#   config générée à jour · couverture UI · rendu UI · 36 tests · syntaxe ESP32
./tools/run_native_tests.sh

# Chaque étape séparément :
python3 tools/gen_default_config.py            # régénère DefaultConfig.h
python3 tools/gen_presets.py                   # régénère data/presets/
node    tools/check_ui_coverage.mjs
node    tools/check_ui_render.mjs
./tools/check_esp32_syntax.sh

# Équivalent via PlatformIO si le registre est joignable :
pio test -e native
pio run  -e native && .pio/build/native/program   # démo mock (journalise les actionneurs)
```

> **CI / sessions web** : un hook `SessionStart` (`.claude/hooks/session-start.sh`)
> pré-télécharge les dépendances des tests et installe PlatformIO, pour que
> `./tools/run_native_tests.sh` soit prêt dès l'ouverture d'une session.

**Mode simulation** : flag `-DHARM_MOCK` (env native) ou `system.mockMode=true`
(sur ESP32). Les mocks (`MockStepper`, `MockPressure`, `MockServoBus`,
`MockEndstops`) rejouent la **vraie** logique air/valve/engine en journalisant les
commandes — idéal pour développer le web/MIDI avant que la mécanique existe.

> Vérification réalisée (hôte g++/C++17 + Node) : build natif complet,
> **36 tests unitaires** (mapping, slide, matrice de capacité, arbitrage,
> inversion de rail, parseur MIDI, régulation PI des 4 sources d'air, peak & hold
> des électro-vannes, bends actionnés, délai valve→air, note bloquée, CC7,
> parsing des nouvelles sections, **les 16 combinaisons air × valve montées et
> jouées**), contrôles d'UI (couverture + rendu de 9 onglets × 32 montages) et
> contrôle syntaxique du chemin ESP32 contre des stubs.
> **Non vérifié ici** : le build PlatformIO réel (`pio run -e esp32dev`) — le
> registre PlatformIO est injoignable depuis cette session (`HTTPClientError` au
> téléchargement de `espressif32@6.13.0`). Les stubs couvrent la syntaxe, pas les
> signatures exactes des bibliothèques.

## 11. État & feuille de route

**Implémenté et vérifié sur hôte** (36 tests) : les 4 abstractions + `NoteEngine`,
**4 familles d'air** (`DualReservoirPiston`, `SingleBellows`, `PumpPair`,
`SinglePumpReversible`) et **4 familles de valve** (`Valve2in1`, `Valve1in1`,
`ValveSolenoid2in1`, `ValveSolenoid1in1`), le slide (servo **ou** électroaimant),
`HarmonicaMap` générique, `MidiParser`/`MidiRouter`, `ConfigStore`, la Factory
(les 16 combinaisons montées et jouées en test), la régulation PI de pression,
le **vibrato CC1**, le **pitch-bend** et les **bends actionnés**, les sécurités de
jeu (`holeSettleMs`, `noteMaxHoldMs`, `minIntensity`), la démo mock.

**Implémenté, à valider sur matériel** : HAL réelle (PCA9685 servos et
tout-ou-rien, GPIO, LEDC, ESC, AccelStepper, BMP280, MPX2010, endstops),
transports **BLE** et **WiFi RTP**, serveur web + banc d'essai, bring-up bi-cœur.
Le chemin ESP32 est vérifié **syntaxiquement** (stubs), jamais exécuté.

**Interface** : refonte complète en UI **pilotée par un schéma** (`data/schema.js`)
— 9 onglets, 162 champs, éditeurs de trous et de mapping, générateur de tonalités,
presets, banc d'essai (servo, électro-vanne, trou, pompe, slide, notes MIDI),
contrôle de cohérence, et deux vérificateurs automatiques de l'UI.

**À poursuivre** :
- réglage fin des gains PI **sur banc** pour chacune des 4 sources d'air ;
- optimisation « rester centré » du vérin double ;
- nom BLE / nom de session RTP / port RTP encore fixés par macro de bibliothèque
  (la config est lue mais ignorée par les libs) ;
- montage d'un slide **après coup** : l'actionneur est construit au boot, activer
  `slide.enabled` demande donc un redémarrage ;
- mot de passe de point d'accès aléatoire au premier démarrage ;
- overblows/overdraws (le modèle de bend ne couvre que les bends classiques) ;
- validation matérielle complète (le vrai `pio run` inclus).

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
   éviter les inversions en plein jeu. Les **électro-vannes** (5–15 ms) lèvent
   cette limite : c'est le montage à privilégier pour du jeu rapide.
7. **Électro-vannes** → JAMAIS pilotées directement par un PCA9685 ou une GPIO :
   driver de puissance obligatoire (ULN2803, MOSFET logique) **avec diode de roue
   libre**, alimentation séparée, masse commune. Régler `holdDuty` (≈ 0,3–0,5) pour
   éviter la surchauffe en note tenue ; vérifier que la vanne tient bien au
   maintien réduit avant de baisser davantage.
8. **Pompes continues** → bruit permanent (prévoir un caisson / silencieux
   d'admission), échauffement, et **appel de courant au démarrage** : ne pas
   partager l'alimentation des servos. `minDuty` doit être au-dessus du seuil de
   décollage réel de la pompe, sinon elle cale sans jamais atteindre la consigne.
   Un ESC brushless exige un **armement** (impulsion basse au boot) : c'est ce que
   fait `ServoBusPwmOut::begin()` en envoyant `escMinUs`.
9. **Pompe unique + aiguillage** → `switchMs` doit couvrir la course RÉELLE de
   l'aiguillage ; sous-estimé, les premières dizaines de millisecondes d'une note
   sonneront dans le mauvais sens ou pas du tout.
