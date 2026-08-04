// ============================================================================
//  NoteEngine.h — le "cerveau" : relie MIDI, mapping, air, valve et slide.
//
//  Modèle de voix : UNE voix par TROU (un trou physique ne peut sonner qu'un
//  seul sens à la fois). Selon la matrice de capacité :
//     air.simultané  &&  valve.perHole   => souffle + aspiration en même temps
//     sinon                              => une seule direction à la fois,
//                                           arbitrage (reject/steal) sur conflit.
//  Réagit au swap de rôle des réservoirs (assignmentGen) en ré-appliquant les
//  positions de valve de toutes les voix actives.
// ============================================================================
#pragma once
#include "HarmonicaMap.h"
#include "../air/IAirSource.h"
#include "../valve/IValveDriver.h"
#include "../valve/ISlideActuator.h"
#include "../Config.h"

namespace harm {

class NoteEngine {
public:
  void begin(IAirSource* air, IValveDriver* valve, HarmonicaMap* map,
             ISlideActuator* slide, const EngineCfg& cfg) {
    air_ = air; valve_ = valve; map_ = map; slide_ = slide; cfg_ = cfg;
    mixedCapable_ = air_->supportsSimultaneousDirections() && valve_->supportsPerHoleDirection();
    valve_->bindAirSource(air_);
    for (auto& v : voices_) v = Voice{};
    lastAssignmentGen_ = air_->status().assignmentGen;
  }

  void handleMidi(const MidiEvent& e) {
    switch (e.type) {
      case MidiEvent::NoteOn:
        (e.data2 > 0) ? noteOn(e.data1, e.data2) : noteOff(e.data1);
        break;
      case MidiEvent::NoteOff:       noteOff(e.data1); break;
      case MidiEvent::ControlChange: controlChange(e.data1, e.data2); break;
    }
  }

  // Réagit au changement d'assignation Rail<->Direction (inversion du piston).
  void update(uint32_t /*nowMs*/) {
    uint16_t gen = air_->status().assignmentGen;
    if (gen != lastAssignmentGen_) { lastAssignmentGen_ = gen; reapplyValves(); }
  }

  bool     mixedCapable() const { return mixedCapable_; }
  uint8_t  activeVoiceCount() const { uint8_t n = 0; for (auto& v : voices_) if (v.active) ++n; return n; }
  Direction activeDirection() const { for (auto& v : voices_) if (v.active) return v.dir; return Direction::Closed; }

  // Coupe toutes les notes (ferme les valves, relâche l'air, slide au repos).
  // À appeler avant un échange d'harmonica à chaud (les voix référencent des trous).
  void panic() { allNotesOff(); }

private:
  struct Voice { bool active=false; uint8_t note=0; uint8_t hole=0; Direction dir=Direction::Closed; bool slide=false; float base=0.0f; };

  float computeBase(uint8_t vel, float scale) const {
    float b = cfg_.velocityToIntensity ? (vel / 127.0f) : 1.0f;
    return clamp01(b * scale);
  }

  void noteOn(uint8_t note, uint8_t vel) {
    const NoteMapping& m = map_->lookup(note);
    if (!m.valid || m.hole >= MAX_HOLES) return;

    // Contrainte mono-direction (soufflet et/ou valve 1-en-1).
    if (!mixedCapable_) {
      Direction cur = activeDirection();
      if (cur != Direction::Closed && cur != m.direction) {
        if (cfg_.arbitration == Arbitration::Reject) return;
        stopAllVoices();                       // Steal : on libère l'ancienne direction
      }
    }
    // Limite de polyphonie (nouvelle voix seulement).
    if (!voices_[m.hole].active && activeVoiceCount() >= cfg_.maxPolyphony) return;

    if (m.slide && slide_) slide_->setEngaged(true);
    voices_[m.hole] = Voice{true, note, m.hole, m.direction, m.slide, computeBase(vel, m.intensityScale)};
    recomputeAir();
    valve_->setHoleState(m.hole, m.direction);
  }

  void noteOff(uint8_t note) {
    const NoteMapping& m = map_->lookup(note);
    if (!m.valid || m.hole >= MAX_HOLES) return;
    Voice& v = voices_[m.hole];
    if (!v.active || v.note != note) return;   // pas la voix qui tient ce trou
    v.active = false;
    valve_->setHoleState(m.hole, Direction::Closed);
    recomputeAir();
    if (slide_ && !anySlideActive()) slide_->setEngaged(false);
  }

  void controlChange(uint8_t cc, uint8_t val) {
    const float v = val / 127.0f;
    switch (cc) {
      case CC_BREATH:     breath_ = v;     recomputeAir(); break;
      case CC_EXPRESSION: expression_ = v; recomputeAir(); break;
      case CC_MODULATION: modulation_ = v; break;  // vibrato de pression : extension future
      case CC_ALL_NOTES_OFF: allNotesOff(); break;
      default: break;
    }
  }

  // Agrège la demande d'air par direction (max des voix actives) et la pousse
  // à la source. Les facteurs globaux (breath/expression) sont appliqués ici,
  // pour que les CC modifient l'intensité des notes déjà tenues.
  void recomputeAir() {
    float blow = 0.0f, draw = 0.0f;
    for (auto& v : voices_) {
      if (!v.active) continue;
      float eff = clamp01(v.base * breath_ * expression_);
      if (v.dir == Direction::Blow) { if (eff > blow) blow = eff; }
      else if (v.dir == Direction::Draw) { if (eff > draw) draw = eff; }
    }
    (blow > 0.0f) ? air_->request(Direction::Blow, blow) : air_->release(Direction::Blow);
    (draw > 0.0f) ? air_->request(Direction::Draw, draw) : air_->release(Direction::Draw);
  }

  void reapplyValves() {
    for (auto& v : voices_) if (v.active) valve_->setHoleState(v.hole, v.dir);
  }
  void stopAllVoices() {
    for (auto& v : voices_) if (v.active) { valve_->setHoleState(v.hole, Direction::Closed); v.active = false; }
    recomputeAir();
    if (slide_) slide_->setEngaged(false);
  }
  void allNotesOff() { stopAllVoices(); }
  bool anySlideActive() const { for (auto& v : voices_) if (v.active && v.slide) return true; return false; }

  IAirSource*     air_ = nullptr;
  IValveDriver*   valve_ = nullptr;
  HarmonicaMap*   map_ = nullptr;
  ISlideActuator* slide_ = nullptr;
  EngineCfg       cfg_;
  Voice           voices_[MAX_HOLES];
  uint16_t        lastAssignmentGen_ = 0;
  bool            mixedCapable_ = false;
  float           breath_ = 1.0f, expression_ = 1.0f, modulation_ = 0.0f;
};

}  // namespace harm
