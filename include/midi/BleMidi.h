// ============================================================================
//  BleMidi.h — transport BLE-MIDI (ESP32, pile NimBLE).
//
//  L'implémentation vit dans src/midi/BleMidi.cpp (glue bibliothèque, sous
//  garde Arduino). Le nom d'appairage vient de la config (best-effort selon la
//  version de la lib). Validation fine sur matériel = phase 3.
// ============================================================================
#pragma once
#include "IMidiTransport.h"
#include "../Config.h"

namespace harm {

class BleMidi : public IMidiTransport {
public:
  explicit BleMidi(const BleMidiCfg& cfg);
  bool        begin() override;
  void        loop() override;
  const char* name() const override { return "ble"; }
  void        ingest(const MidiEvent& e) { emit(e); }   // appelé par les callbacks C

private:
  BleMidiCfg cfg_;
};

}  // namespace harm
