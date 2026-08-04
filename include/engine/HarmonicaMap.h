// ============================================================================
//  HarmonicaMap.h — table de correspondance note MIDI -> {trou, sens, slide}.
//
//  Générique et 100 % pilotée par la config : nombre de trous variable, sens
//  souffle/aspiration, dimension "slide" pour le chromatique, trim d'intensité
//  par note. Indexée directement par numéro de note (O(1), ~1 Ko).
//  En cas de note en double (ex. sol jouable sur 2 trous), la PREMIÈRE entrée
//  de la config gagne (comportement documenté).
// ============================================================================
#pragma once
#include "../Types.h"
#include "../Config.h"
#include <cstring>

namespace harm {

struct NoteMapping {
  bool      valid = false;
  uint8_t   hole = 0;
  Direction direction = Direction::Closed;
  bool      slide = false;
  float     intensityScale = 1.0f;
  float     bendSemitones = 0.0f;   // note obtenue par bend (modélisation)
};

class HarmonicaMap {
public:
  void load(const HarmonicaCfg& cfg) {
    for (uint16_t i = 0; i < MIDI_NOTES; ++i) table_[i] = NoteMapping{};
    holeCount_  = cfg.holeCount;
    needsSlide_ = cfg.hasSlide;
    std::strncpy(name_, cfg.name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
    for (uint16_t i = 0; i < cfg.noteCount && i < MAX_NOTE_ENTRIES; ++i) {
      const NoteEntry& e = cfg.notes[i];
      if (e.note >= MIDI_NOTES) continue;
      NoteMapping& m = table_[e.note];
      if (m.valid) continue;                 // première entrée prioritaire
      m.valid = true; m.hole = e.hole; m.direction = e.direction;
      m.slide = e.slide; m.intensityScale = e.intensityScale; m.bendSemitones = e.bendSemitones;
    }
  }

  const NoteMapping& lookup(uint8_t note) const {
    static const NoteMapping kInvalid{};
    return (note < MIDI_NOTES) ? table_[note] : kInvalid;
  }

  uint8_t     holeCount() const { return holeCount_; }
  bool        needsSlide() const { return needsSlide_; }
  const char* name() const { return name_; }

private:
  NoteMapping table_[MIDI_NOTES];
  uint8_t     holeCount_ = 10;
  bool        needsSlide_ = false;
  char        name_[32] = {0};
};

}  // namespace harm
