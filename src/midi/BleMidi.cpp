// ============================================================================
//  BleMidi.cpp — glue BLE-MIDI (lathoub/Arduino-BLE-MIDI + NimBLE).
//  Compilé uniquement pour la cible ESP32 (#if HARM_ARDUINO).
// ============================================================================
#include "midi/BleMidi.h"

#if HARM_ARDUINO
#include <BLEMIDI_Transport.h>
#include <hardware/BLEMIDI_ESP32_NimBLE.h>

// Instance globale BLE-MIDI. NOTE : le nom d'appairage est fixé ici à la
// compilation par la macro ; adapter si besoin selon la version de la lib.
BLEMIDI_CREATE_INSTANCE("HarmonicaMIDI", MIDI_BLE)

namespace harm {

static BleMidi* g_bleSelf = nullptr;

// Les callbacks de la lib sont de simples pointeurs de fonction (sans contexte)
// => on repasse par un pointeur statique vers l'instance active.
static void bleNoteOn (uint8_t ch, uint8_t note, uint8_t vel) {
  if (g_bleSelf) g_bleSelf->ingest({MidiEvent::NoteOn,  (uint8_t)(ch - 1), note, vel});
}
static void bleNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
  if (g_bleSelf) g_bleSelf->ingest({MidiEvent::NoteOff, (uint8_t)(ch - 1), note, vel});
}
static void bleCC     (uint8_t ch, uint8_t cc,   uint8_t val) {
  if (g_bleSelf) g_bleSelf->ingest({MidiEvent::ControlChange, (uint8_t)(ch - 1), cc, val});
}

BleMidi::BleMidi(const BleMidiCfg& cfg) : cfg_(cfg) {}

bool BleMidi::begin() {
  g_bleSelf = this;
  MIDI_BLE.begin(MIDI_CHANNEL_OMNI);
  MIDI_BLE.setHandleNoteOn(bleNoteOn);
  MIDI_BLE.setHandleNoteOff(bleNoteOff);
  MIDI_BLE.setHandleControlChange(bleCC);
  return true;
}

void BleMidi::loop() { MIDI_BLE.read(); }

}  // namespace harm
#endif  // HARM_ARDUINO
