// ============================================================================
//  MidiRouter.h — agrège les transports actifs et applique le filtre de canal.
//
//  Le série DIN est toujours ajouté ; au plus UN transport sans-fil (WiFi OU
//  BLE) est ajouté (contrainte ESP32 classique). Le sink du routeur pousse
//  ensuite l'évènement dans la file inter-cœurs consommée par le NoteEngine.
// ============================================================================
#pragma once
#include "IMidiTransport.h"

namespace harm {

class MidiRouter {
public:
  void add(IMidiTransport* t) {
    if (!t || count_ >= kMax) return;
    transports_[count_++] = t;
    t->setSink([this](const MidiEvent& e) { dispatch(e); });
  }
  void begin() { for (uint8_t i = 0; i < count_; ++i) transports_[i]->begin(); }
  void loop()  { for (uint8_t i = 0; i < count_; ++i) transports_[i]->loop(); }

  void setSink(MidiSink s) { sink_ = std::move(s); }
  void setChannelFilter(uint8_t ch) { channel_ = ch; }   // 0 = OMNI ; 1..16 = ce canal
  uint8_t transportCount() const { return count_; }

private:
  void dispatch(const MidiEvent& e) {
    if (channel_ != 0 && (uint8_t)(e.channel + 1) != channel_) return;
    if (sink_) sink_(e);
  }

  static constexpr uint8_t kMax = 4;
  IMidiTransport* transports_[kMax] = {nullptr, nullptr, nullptr, nullptr};
  uint8_t  count_ = 0;
  uint8_t  channel_ = 0;
  MidiSink sink_;
};

}  // namespace harm
