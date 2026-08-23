// ============================================================================
//  WebServer.h — page de configuration + API REST/WebSocket (ESP32).
//
//  Sécurité & concurrence :
//   - toutes les mutations matérielles (calibrate / homing / centrage / tare /
//     échange d'harmonica) sont ENVOYÉES au Core 1 via une file de commandes —
//     aucun accès I2C ni à l'état moteur depuis le Core 0/async.
//   - la télémétrie est lue depuis un INSTANTANÉ publié par le Core 1 (pas d'I2C
//     hors Core 1).
//   - GET /api/config masque les secrets ; /config.json est bloqué ; Basic Auth
//     optionnelle (config web.password).
// ============================================================================
#pragma once
#include "../Factory.h"
#include "ConfigStore.h"

namespace harm {

// Commande émise par le web, exécutée sur le Core 1 (seul propriétaire de l'I2C
// et de l'état moteur). Le champ `channel` porte selon le cas un canal, un
// numéro de trou, une note MIDI ou une direction ; `value` un angle, une µs,
// une vélocité, un état ou un duty en pour-cent.
struct WebCommand {
  enum Type : uint8_t {
    ServoUs, ServoDeg, PistonHome, PistonCenter, PressureTare, SwapHarmonica,
    SolenoidSet,     // channel = canal du bus vannes, value = 0/1
    HoleState,       // channel = trou, value = 0 fermé / 1 souffle / 2 aspiration
    PumpDuty,        // channel = 1 souffle / 2 aspiration, value = duty % (<0 = auto)
    SlideSet,        // value = 0/1
    TestNoteOn,      // channel = note MIDI, value = vélocité
    TestNoteOff,     // channel = note MIDI
    AllOff           // panique : coupe tout
  } type;
  uint8_t       channel = 0;
  int           value = 0;
  HarmonicaCfg* harmonica = nullptr;   // SwapHarmonica : appliqué puis libéré par le Core 1
};

// Instantané de télémétrie publié par le Core 1, consommé par le web.
struct StatusSnapshot {
  AirStatus air;
  AirCaps   caps;                       // ce que le montage courant sait faire
  uint8_t   voices = 0;
  bool      mixedCapable = false;
  float     pitchBend = 0.0f, modulation = 0.0f, setpointKpa = 0.0f;
  uint8_t   transports = 0;
  bool      mock = false;
  char      harmonica[32] = {0};
  uint32_t  droppedMidi = 0;
  uint32_t  holeBlowMask = 0, holeDrawMask = 0;   // trous qui sonnent (bit = trou)
  uint8_t   holeCount = 0;
  uint8_t   airImpl = 0, valveImpl = 0;           // enums AirImpl / ValveImpl
  bool      slidePresent = false, slideEngaged = false;
};

class WebServer {
public:
  // cmdQueue : QueueHandle_t (passé en void* pour ne pas imposer FreeRTOS ici).
  void begin(System* sys, ConfigStore* store, void* cmdQueue);
  void loop(uint32_t nowMs);            // pousse la télémétrie WS (snapshot)
  bool rebootRequested() const;

  static void publishSnapshot(const StatusSnapshot& s);   // appelé par le Core 1
};

}  // namespace harm
