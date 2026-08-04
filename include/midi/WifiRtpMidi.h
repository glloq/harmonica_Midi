// ============================================================================
//  WifiRtpMidi.h — transport MIDI par le réseau (RTP-MIDI / AppleMIDI, UDP).
//
//  Implémentation dans src/midi/WifiRtpMidi.cpp (glue AppleMIDI + WiFi, sous
//  garde Arduino). Se connecte en STA d'après la config. Découvrable depuis
//  macOS "Configuration audio et MIDI" ou rtpMIDI sous Windows. Phase 4.
// ============================================================================
#pragma once
#include "IMidiTransport.h"
#include "../Config.h"

namespace harm {

class WifiRtpMidi : public IMidiTransport {
public:
  explicit WifiRtpMidi(const WifiCfg& cfg);
  bool        begin() override;
  void        loop() override;
  const char* name() const override { return "wifi-rtp"; }
  void        ingest(const MidiEvent& e) { emit(e); }

private:
  WifiCfg cfg_;
};

}  // namespace harm
