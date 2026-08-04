// ============================================================================
//  SerialMidi.h — transport MIDI filaire (prise DIN 5 broches / UART série).
//
//  ESP32 uniquement. Lit UART2 à 31250 bauds et alimente le MidiParser.
//  L'entrée DIN passe par un optocoupleur (ex. 6N138) ; TX optionnel = thru.
// ============================================================================
#pragma once
#include "IMidiTransport.h"
#include "MidiParser.h"
#include "../Config.h"

#if HARM_ARDUINO
#include <Arduino.h>

namespace harm {

class SerialMidi : public IMidiTransport {
public:
  SerialMidi(HardwareSerial& ser, const SerialMidiCfg& cfg) : ser_(ser), cfg_(cfg) {}
  bool begin() override {
    ser_.begin(cfg_.baud, SERIAL_8N1, cfg_.rxPin, cfg_.txPin);
    parser_.reset();
    return true;
  }
  void loop() override {
    while (ser_.available()) parser_.feed((uint8_t)ser_.read(), [this](const MidiEvent& e) { emit(e); });
  }
  const char* name() const override { return "serial"; }

private:
  HardwareSerial& ser_;
  SerialMidiCfg   cfg_;
  MidiParser      parser_;
};

}  // namespace harm
#endif  // HARM_ARDUINO
