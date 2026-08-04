// ============================================================================
//  Types.h — vocabulaire partagé par tous les modules.
//  Aucune dépendance Arduino : compile aussi bien sur ESP32 que sur PC (tests).
// ============================================================================
#pragma once
#include <cstdint>
#include <cmath>

namespace harm {

// Limites de dimensionnement (statiques => pas d'allocation dynamique).
static constexpr uint8_t  MAX_HOLES        = 24;   // couvre chromatique 16 trous + marge
static constexpr uint16_t MAX_NOTE_ENTRIES = 128;  // entrées de mapping note->trou
static constexpr uint8_t  MIDI_NOTES       = 128;

// Intention ACOUSTIQUE d'un trou.
enum class Direction : uint8_t { Closed = 0, Blow = 1, Draw = 2 };

// Rail PHYSIQUE du collecteur d'air (R1 = A, R2 = B). La correspondance
// Direction <-> Rail est détenue par l'IAirSource et peut s'inverser
// (piston du vérin double qui repart dans l'autre sens).
enum class Rail : uint8_t { None = 0, A = 1, B = 2 };

// Évènement MIDI normalisé, produit par n'importe quel transport.
struct MidiEvent {
  enum Type : uint8_t { NoteOn, NoteOff, ControlChange } type;
  uint8_t channel;  // 0..15 (nibble de canal MIDI)
  uint8_t data1;    // numéro de note ou de CC
  uint8_t data2;    // vélocité ou valeur de CC
};

// Instantané de l'état du système d'air, exposé à la télémétrie / au moteur.
struct AirStatus {
  float    pressureBlow  = 0.0f;  // kPa (magnitude) sur le rail souffle courant
  float    pressureDraw  = 0.0f;  // kPa (magnitude) sur le rail aspiration courant
  float    pistonMm      = NAN;   // position piston (vérin double) ; NAN si N/A
  bool     homed         = false;
  bool     ready         = false; // rails vivants dans la tolérance de la cible
  uint16_t assignmentGen = 0;     // incrémenté à chaque échange rôle Rail<->Direction
};

// Numéros de Control Change reconnus (extensible).
enum : uint8_t {
  CC_MODULATION = 1,   // vibrato de pression
  CC_BREATH     = 2,   // intensité globale
  CC_PORTAMENTO = 5,   // (réservé : glissando/bend)
  CC_EXPRESSION = 11,  // intensité globale
  CC_ALL_NOTES_OFF = 123
};

// Utilitaire de bornage (évite <algorithm> côté embarqué).
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline const char* toString(Direction d) {
  switch (d) { case Direction::Blow: return "blow";
               case Direction::Draw: return "draw";
               default: return "closed"; }
}

}  // namespace harm
