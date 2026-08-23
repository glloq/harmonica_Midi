// ============================================================================
//  WifiRtpMidi.cpp — glue AppleMIDI / RTP-MIDI sur WiFi.
//  Compilé uniquement pour la cible ESP32 (#if HARM_ARDUINO).
// ============================================================================
#include "midi/WifiRtpMidi.h"

#if HARM_ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <AppleMIDI.h>

// Crée les globales `AppleMIDI` (session) et `MIDI` (interface) sur le port 5004.
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE()

namespace harm {

static WifiRtpMidi* g_rtpSelf = nullptr;

static void rtpNoteOn (uint8_t ch, uint8_t note, uint8_t vel) {
  if (g_rtpSelf) g_rtpSelf->ingest({MidiEvent::NoteOn,  (uint8_t)(ch - 1), note, vel});
}
static void rtpNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
  if (g_rtpSelf) g_rtpSelf->ingest({MidiEvent::NoteOff, (uint8_t)(ch - 1), note, vel});
}
static void rtpCC     (uint8_t ch, uint8_t cc,   uint8_t val) {
  if (g_rtpSelf) g_rtpSelf->ingest({MidiEvent::ControlChange, (uint8_t)(ch - 1), cc, val});
}

WifiRtpMidi::WifiRtpMidi(const WifiCfg& cfg) : cfg_(cfg) {}

bool WifiRtpMidi::begin() {
  g_rtpSelf = this;
  // Respecte wifi.mode ("ap" -> point d'accès, sinon station non bloquante).
  if (!strcmp(cfg_.mode, "ap")) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(cfg_.ssid[0] ? cfg_.ssid : "Harmonica-RTP", cfg_.password);
  } else if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg_.ssid, cfg_.password);
  }
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(rtpNoteOn);
  MIDI.setHandleNoteOff(rtpNoteOff);
  MIDI.setHandleControlChange(rtpCC);
  return true;
}

void WifiRtpMidi::loop() { MIDI.read(); }

}  // namespace harm
#endif  // HARM_ARDUINO
