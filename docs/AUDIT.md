# Audit du projet — harmonica_Midi

Audit adversarial réalisé sur 4 axes indépendants (concurrence/architecture,
correction firmware, UI/API/sécurité, build/portabilité), chaque trouvaille
vérifiée dans le code. Ce document consigne les findings **et leur état de
correction**.

**Légende** : ✅ corrigé · 🟡 partiel · ⏳ restant · ℹ️ note/décision.

> Rappel de portée : le cœur logique (natif) est couvert par **36 tests unitaires** ;
> le chemin ESP32 (HAL réelle, transports sans-fil, serveur web) est **vérifié
> syntaxiquement** (`tools/check_esp32_syntax.sh`), pas exécuté sur matériel, et le
> build PlatformIO réel n'a jamais pu être lancé ici (registre injoignable).

## 🔴 Critiques

| # | Finding | Statut |
|---|---------|--------|
| 1 | Retour de pression `fabs()` (non signé) vers le PI → piston coincé après inversion en jeu soutenu | ✅ retour **signé** (`railKpaSigned`), testé (`test_pi_signed_feedback`) |
| 2 | Intensité demandée jamais actionnée → vélocité + CC2/CC11/CC1 inaudibles, pression fixe | ✅ **consigne PI ∝ intensité** (vérin + soufflet), testé (`test_pi_intensity_setpoint`) |
| 3 | `AppleMIDI@^3.3.0` inexistant → build ESP32 impossible | ✅ `@^3.2.0` + plateforme épinglée `espressif32@6.13.0` |
| 4 | I2C + état moteur touchés depuis Core 0 / tâche async (viole « I2C sur Core 1 ») | ✅ **file de commandes** web→Core 1 + **snapshot** de télémétrie sous `portMUX` |
| 5 | Mot de passe WiFi exposé en clair, aucune auth sur aucune route | ✅ secrets **masqués**, `/config.json` bloqué, **Basic Auth** (`web.password`) |

## 🟠 Élevés

| # | Finding | Statut |
|---|---------|--------|
| 6 | `writeMicros` non borné dans `/api/calibrate` → casse servo | ✅ clamp µs/angle/canal côté exécuteur Core 1 |
| 7 | File MIDI : `xQueueSend` timeout 0 non vérifié → NoteOff perdu = note bloquée | ✅ échec **compté** (`droppedMidi` en télémétrie) |
| 8 | Corps POST non borné → OOM | ✅ plafond 8 Ko |
| 9 | `homeOnR1=false` jamais honoré → homing infini | ✅ direction/endstop selon `homeOnR1` + **timeout** |
| 10 | `Wire.begin`/`setClock` jamais appelés → config I2C ignorée (bus 100 kHz) | ✅ appelés au boot (Core 1) |
| 11 | `AsyncWebSocket` muté depuis `netTask` (tâche étrangère) → use-after-free | ✅ **WebSocket retiré**, télémétrie par polling `/api/status` |
| 12 | Jeu de libs/plateforme ESP32 non épinglé (core-2/core-3) | ✅ plateforme épinglée + note de compat ; 🟡 versions web/BLE à valider au 1er build registre |

## 🟡 Moyens

| # | Finding | Statut |
|---|---------|--------|
| 13 | Buffers de corps POST statiques partagés → corruption si requêtes concurrentes | ✅ buffers **par requête** (`req->_tempObject`) |
| 14 | POST corps vide → aucune réponse (connexion pendante) | ✅ réponse d'erreur sur corps vide |
| 15 | `reversalMarginMm > travel/2` → tempête d'inversions ~1 kHz | ✅ garde `< travel/2` à `begin()` |
| 16 | Note vers un trou `>= holeCount` → voix muette bloquée, consomme air/polyphonie | ✅ note ignorée au chargement du mapping |
| 17 | `/api/harmonica` accepte `{}` → coupe tout mapping + persiste vide | ✅ refus si `noteCount == 0` |
| 18 | `deviceName`/`sessionName`/`rtpPort`/`wifi.mode` ignorés (macros de lib) | 🟡 `wifi.mode` honoré ; ⏳ nom BLE/session/port fixés par macro (documenté) |
| 19 | Preset chromatique « à chaud » non fonctionnel sans `slide.enabled` | 🟡 l'UI **avertit et bloque en erreur de cohérence** ; ⏳ actionneur slide construit au boot uniquement |
| 20 | Vibrato demi-écrêté à base ≈ 1.0 | ✅ modulation d'amplitude vers le bas (jamais écrêtée) |
| 21 | Rail aspiration non régulé en jeu souffle+aspiration simultané | ✅ **équilibrage** des deux rails |
| 22 | Mot de passe AP par défaut faible (« harmonica ») ; web ungated sur LAN | 🟡 Basic Auth dispo ; ⏳ AP aléatoire au 1er boot non implémenté |

## 🟢 Bas

| Finding | Statut |
|---------|--------|
| Anti-windup PI = simple clamp ; pas de garde `dt` | ✅ intégration conditionnelle + garde `dt` |
| Sentinelle `lastMs_==0` collisionne avec `millis()==0` | ✅ drapeau `haveLast_` |
| `strncpy` auto-recouvrant si clé absente (UB) | ✅ garde `d==s` |
| `serializeJson(std::string)` fragile à l'ordre d'include | ✅ `ARDUINOJSON_ENABLE_STD_STRING` |
| Téléchargements cache non atomiques (hook/runner) | ✅ temp + `mv`, test `[ -s ]` |
| Hook non idempotent (append PATH) | ✅ `grep` avant append |
| `/api/harmonicas` dépend du format `File::name()` | ✅ basename normalisé serveur |
| `ws://` en dur | ✅ (WebSocket retiré) |
| Compare flottant → recompute ~1 kHz sous vibrato | ✅ seuil `1e-3` |
| `feedEndMm` mort | ✅ supprimé |
| `pressureToleranceKpa` non utilisé | ✅ utilisé (`AirStatus.ready`) |
| `flowLpm`, `CC_PORTAMENTO` non utilisés | ℹ️ conservés (valeur/point d'extension documentés) |
| `new` jamais libéré dans `buildSystem` | ℹ️ intentionnel (une fois au boot) |
| UI thème sombre unique / labels a11y | ⏳ non traité |

## ⏳ Restant (prochaines itérations)

- Actionnement **réel** des bends (le pitch-bend est capté, pas encore traduit en modulation d'air).
- Slide chromatique reconstruit à chaud (aujourd'hui : nécessite `slide.enabled` + reboot).
- Nom d'appairage BLE / nom de session / port RTP configurables (bloqués par les macros des libs).
- AP aléatoire au 1er boot ; thème clair + labels d'accessibilité UI.

## ✅ Vérifié solide (non modifié)

`partitions_huge.csv` (LittleFS via sous-type `spiffs`) · double `main()` évité
(`PIO_UNIT_TESTING`) · `DefaultConfig.h` == `data/config.json` · guards/TU vides
propres, pas de collision de symboles BLE/AppleMIDI ni double init radio · hex /
canal / ArduinoJson v7 corrects · parseur MIDI (running status / temps réel /
vel0 / SysEx) correct · **pas de XSS** (`textContent` partout) · pas de mismatch
d'ID UI · branche soufflet (`pB`/`endstops` null) bien gardée.

## 🔵 Consolidation (session « toutes les options »)

Extension du firmware à l'ensemble des montages envisagés dans le README, et
refonte de l'UI pour qu'ils soient réellement configurables.

### Ajouts

| Domaine | Ajout |
|---|---|
| Air | `PumpPair` (2 pompes continues opposées) et `SinglePumpReversible` (1 pompe + aiguillage servo ou électro-vanne), régulés en PI sur le régime PWM |
| Distribution | `ValveSolenoid2in1` / `ValveSolenoid1in1` (électro-vannes) avec **peak & hold** |
| Slide | `SolenoidSlide` (électroaimant) en plus du servo |
| HAL | `IDigitalOutBus` (PCA9685 tout-ou-rien, GPIO) et `IPwmOut` (LEDC, ESC/impulsions servo) + mocks inspectables |
| Moteur de jeu | **bends actionnés** (surpression ∝ demi-tons), CC7 volume, `holeSettleMs` (valve avant l'air), `noteMaxHoldMs` (note bloquée), `minIntensity`, plage de pitch-bend configurable, masque des trous actifs |
| API | `GET /api/capabilities`, `POST /api/test` (jouer une note), `/api/calibrate` étendu (trou, électro-vanne, pompe, slide, coupure générale) |
| UI | refonte complète pilotée par `data/schema.js` : 9 onglets, 162 champs à affichage conditionnel, éditeurs de trous et de mapping, générateur de tonalités, presets, banc d'essai, contrôle de cohérence |
| Presets | 10 harmonicas générés (`tools/gen_presets.py`) : Richter C/D/F/G/A, Richter C + bends, chromatiques 12 et 16, trémolo, octave |
| Outils | `gen_default_config.py` (config par défaut **générée**), `check_ui_coverage.mjs`, `check_ui_render.mjs`, `check_esp32_syntax.sh` — tous branchés dans `run_native_tests.sh` |

### Défauts trouvés et corrigés en cours de route

| Finding | Statut |
|---|---|
| Enregistrer la config depuis l'UI **effaçait les mots de passe** (WiFi/AP/web), puisque `GET /api/config` les masque | ✅ fusion côté serveur : clé absente ou vide = valeur conservée, `"__clear__"` = effacement explicite |
| `holeCount` déduit d'une seule liste de trous : changer d'implémentation de valve gardait le compte de l'ancienne | ✅ le compte suit la liste de l'implémentation choisie (testé) |
| `keepSecret()` : ternaire entre deux `MemberProxy` ArduinoJson de types différents — **aurait cassé le build ESP32** | ✅ `.as<JsonVariantConst>()` explicite ; détecté par le nouveau contrôle de syntaxe |
| `PumpPair`/`SinglePumpReversible` restaient `homed=false` sans `startHoming()`, donc jamais `ready` | ✅ « homé » dès `begin()` (rien à référencer mécaniquement) |
| `ServoSlide::begin()` ne poussait aucun angle (état initial supposé) | ✅ position de repos écrite au démarrage |
| `DefaultConfig.h` recopié à la main depuis `data/config.json` (dérive silencieuse garantie) | ✅ **généré**, et le pipeline échoue si les deux divergent |

### Restant

- réglage des gains PI **sur banc** pour les 4 sources d'air ;
- nom BLE / session RTP / port RTP toujours fixés par macro de bibliothèque ;
- `slide.enabled` exige un redémarrage (actionneur construit au boot) ;
- mot de passe d'AP aléatoire au premier démarrage ;
- overblows/overdraws non modélisés ;
- **build PlatformIO réel jamais exécuté** dans cet environnement.
