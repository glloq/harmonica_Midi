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
      case MidiEvent::PitchBend: {
        int v = ((int)e.data2 << 7) | e.data1;   // 0..16383, centre 8192
        pitchBend_ = (v - 8192) / 8192.0f;       // -1..~+1
        if (cfg_.bendEnabled) recomputeAir();    // le bend est ACTIONNÉ (surpression)
        break;
      }
    }
  }

  // Réagit au swap Rail<->Direction (inversion piston), applique le vibrato,
  // libère l'air des voix dont la valve a fini sa course, et coupe les notes
  // restées bloquées trop longtemps (NoteOff perdu).
  void update(uint32_t nowMs) {
    lastNowMs_ = nowMs;
    uint16_t gen = air_->status().assignmentGen;
    if (gen != lastAssignmentGen_) { lastAssignmentGen_ = gen; reapplyValves(); }

    if (pendingAir_) {                       // délai valve -> air (holeSettleMs)
      bool stillPending = false, released = false;
      for (auto& v : voices_) {
        if (!v.active || v.airAtMs == 0) continue;
        if ((int32_t)(nowMs - v.airAtMs) >= 0) { v.airAtMs = 0; released = true; }
        else stillPending = true;
      }
      pendingAir_ = stillPending;
      if (released) recomputeAir();
    }

    if (cfg_.noteMaxHoldMs > 0) {            // coupe-circuit note bloquée
      bool cut = false;
      for (auto& v : voices_)
        if (v.active && (uint32_t)(nowMs - v.startedMs) > cfg_.noteMaxHoldMs) {
          valve_->setHoleState(v.hole, Direction::Closed); v.active = false; cut = true;
        }
      if (cut) { recomputeAir(); if (slide_ && !anySlideActive()) slide_->setEngaged(false); }
    }

    float vs = (activeVoiceCount() > 0) ? vibratoScale(nowMs) : 1.0f;
    if (std::fabs(vs - vibScale_) > 1e-3f) { vibScale_ = vs; recomputeAir(); }   // vibrato live (CC1)
  }

  bool     mixedCapable() const { return mixedCapable_; }
  uint8_t  activeVoiceCount() const { uint8_t n = 0; for (auto& v : voices_) if (v.active) ++n; return n; }
  Direction activeDirection() const { for (auto& v : voices_) if (v.active) return v.dir; return Direction::Closed; }
  float    pitchBend() const { return pitchBend_; }                       // -1..+1
  float    pitchBendSemitones() const { return pitchBend_ * cfg_.pitchBendRangeSemitones; }
  float    modulationDepth() const { return modulation_; }
  // Masque des trous qui sonnent dans une direction (bit N = trou N) : sert à
  // l'affichage temps réel de l'harmonica dans l'UI.
  uint32_t holeMask(Direction d) const {
    uint32_t m = 0;
    for (uint8_t i = 0; i < MAX_HOLES; ++i) if (voices_[i].active && voices_[i].dir == d) m |= (1u << i);
    return m;
  }

  // Coupe toutes les notes (ferme les valves, relâche l'air, slide au repos).
  // À appeler avant un échange d'harmonica à chaud (les voix référencent des trous).
  void panic() { allNotesOff(); }

private:
  struct Voice {
    bool active = false;
    uint8_t note = 0, hole = 0;
    Direction dir = Direction::Closed;
    bool  slide = false;
    float base = 0.0f;
    float bend = 0.0f;        // demi-tons de bend portés par le mapping
    uint32_t airAtMs = 0;     // 0 = air déjà accordé ; sinon échéance (holeSettleMs)
    uint32_t startedMs = 0;
  };

  float computeBase(uint8_t vel, float scale) const {
    float b = cfg_.velocityToIntensity ? (vel / 127.0f) : 1.0f;
    return clamp01(b * scale);
  }

  // Facteur de vibrato de pression piloté par CC1 (modulation). 1.0 si inactif.
  // Modulation d'amplitude vers le BAS uniquement (plage [1-depth, 1]) : jamais
  // écrêtée à 1.0, donc symétrique même à pleine intensité (base ≈ 1.0).
  float vibratoScale(uint32_t ms) const {
    if (modulation_ <= 0.0f) return 1.0f;
    const float depth = modulation_ * cfg_.vibratoDepth;
    return 1.0f - depth * (0.5f - 0.5f * std::sin(6.2831853f * cfg_.vibratoRateHz * (ms / 1000.0f)));
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
    Voice v;
    v.active = true; v.note = note; v.hole = m.hole; v.dir = m.direction; v.slide = m.slide;
    v.base = computeBase(vel, m.intensityScale);
    v.bend = m.bendSemitones;
    v.startedMs = lastNowMs_;
    // La valve part la première ; l'air ne suit qu'après sa course (holeSettleMs),
    // sinon on souffle dans un trou encore fermé (fuite + attaque molle).
    if (cfg_.holeSettleMs > 0) { v.airAtMs = lastNowMs_ + cfg_.holeSettleMs; pendingAir_ = true; }
    valve_->setHoleState(m.hole, m.direction);
    voices_[m.hole] = v;
    recomputeAir();
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
      case CC_MODULATION: modulation_ = v; break;  // vibrato de pression (appliqué dans update)
      case CC_VOLUME:     if (cfg_.ccVolumeEnabled) { volume_ = v; recomputeAir(); } break;
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
      if (!v.active || v.airAtMs != 0) continue;          // voix en attente de sa valve
      float eff = v.base * breath_ * expression_ * volume_ * vibScale_;
      // Actionnement du bend : plier une anche demande PLUS de dépression /
      // surpression. On traduit les demi-tons (mapping + pitch-bend live) en
      // supplément d'intensité, borné par clamp01.
      if (cfg_.bendEnabled) {
        const float semis = std::fabs(v.bend + pitchBendSemitones());
        if (semis > 0.0f) eff *= (1.0f + cfg_.bendPressureGain * semis);
      }
      if (eff < cfg_.minIntensity) eff = cfg_.minIntensity;   // plancher audible
      eff = clamp01(eff);
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
    for (auto& v : voices_) if (v.active) { valve_->setHoleState(v.hole, Direction::Closed); v.active = false; v.airAtMs = 0; }
    recomputeAir();
    if (slide_) slide_->setEngaged(false);
  }
  void allNotesOff() { pendingAir_ = false; stopAllVoices(); }
  bool anySlideActive() const { for (auto& v : voices_) if (v.active && v.slide) return true; return false; }

  IAirSource*     air_ = nullptr;
  IValveDriver*   valve_ = nullptr;
  HarmonicaMap*   map_ = nullptr;
  ISlideActuator* slide_ = nullptr;
  EngineCfg       cfg_;
  Voice           voices_[MAX_HOLES];
  uint16_t        lastAssignmentGen_ = 0;
  bool            mixedCapable_ = false;
  float           breath_ = 1.0f, expression_ = 1.0f, volume_ = 1.0f, modulation_ = 0.0f;
  float           vibScale_ = 1.0f, pitchBend_ = 0.0f;
  uint32_t        lastNowMs_ = 0;
  bool            pendingAir_ = false;
};

}  // namespace harm
