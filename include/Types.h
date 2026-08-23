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
static constexpr uint8_t  MAX_OUTPUTS      = 32;   // 2 x PCA9685 (16 canaux chacun)

// Intention ACOUSTIQUE d'un trou.
enum class Direction : uint8_t { Closed = 0, Blow = 1, Draw = 2 };

// Rail PHYSIQUE du collecteur d'air (R1 = A, R2 = B). La correspondance
// Direction <-> Rail est détenue par l'IAirSource et peut s'inverser
// (piston du vérin double qui repart dans l'autre sens).
enum class Rail : uint8_t { None = 0, A = 1, B = 2 };

// Évènement MIDI normalisé, produit par n'importe quel transport.
struct MidiEvent {
  enum Type : uint8_t { NoteOn, NoteOff, ControlChange, PitchBend } type;
  uint8_t channel;  // 0..15 (nibble de canal MIDI)
  uint8_t data1;    // note / CC / LSB de pitch-bend
  uint8_t data2;    // vélocité / valeur CC / MSB de pitch-bend
};

// Instantané de l'état du système d'air, exposé à la télémétrie / au moteur.
struct AirStatus {
  float    pressureBlow  = 0.0f;  // kPa (magnitude) sur le rail souffle courant
  float    pressureDraw  = 0.0f;  // kPa (magnitude) sur le rail aspiration courant
  float    pistonMm      = NAN;   // position piston/soufflet ; NAN si N/A
  float    dutyBlow      = NAN;   // commande pompe souffle 0..1 ; NAN si N/A
  float    dutyDraw      = NAN;   // commande pompe aspiration 0..1 ; NAN si N/A
  bool     homed         = false;
  bool     ready         = false; // rails vivants dans la tolérance de la cible
  uint16_t assignmentGen = 0;     // incrémenté à chaque échange rôle Rail<->Direction
};

// Ce qu'une source d'air sait faire — publié à l'UI pour n'afficher que les
// réglages et les commandes qui ont un sens pour le montage choisi.
struct AirCaps {
  bool    simultaneous    = false;  // souffle + aspiration en même temps
  bool    hasPiston       = false;  // position linéaire significative (mm)
  bool    needsHoming     = false;  // butées mécaniques à référencer au boot
  bool    railsSwap       = false;  // la correspondance Rail<->Direction peut s'inverser
  bool    hasPumps        = false;  // pompes continues pilotées en PWM
  bool    hasDiverter     = false;  // aiguillage souffle/aspiration partagé
  uint8_t pressureSensors = 1;      // nombre de capteurs de pression attendus
};

// Numéros de Control Change reconnus (extensible).
enum : uint8_t {
  CC_MODULATION = 1,   // vibrato de pression
  CC_BREATH     = 2,   // intensité globale
  CC_PORTAMENTO = 5,   // (réservé : glissando/bend)
  CC_VOLUME     = 7,   // volume canal (facteur global)
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
