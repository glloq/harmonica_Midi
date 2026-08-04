// ============================================================================
//  IMidiTransport.h — interface commune des transports MIDI.
//
//  Chaque transport (série DIN, BLE, WiFi RTP) parse son flux et produit des
//  MidiEvent normalisés via le "sink". Aucune dépendance Arduino ici.
// ============================================================================
#pragma once
#include "../Types.h"
#include <functional>
#include <utility>

namespace harm {

using MidiSink = std::function<void(const MidiEvent&)>;

class IMidiTransport {
public:
  virtual ~IMidiTransport() = default;
  virtual bool        begin() = 0;
  virtual void        loop() = 0;          // scrutation non bloquante
  virtual const char* name() const = 0;
  void setSink(MidiSink s) { sink_ = std::move(s); }

protected:
  void emit(const MidiEvent& e) { if (sink_) sink_(e); }
  MidiSink sink_;
};

}  // namespace harm
