// ============================================================================
//  MidiParser.h — parseur d'octets MIDI (running status inclus).
//
//  Indépendant du transport et d'Arduino => testable sur PC. Ne remonte que les
//  messages utiles (NoteOn / NoteOff / ControlChange) ; les autres messages de
//  canal sont consommés (bonne longueur) puis ignorés, le temps réel système
//  (0xF8..0xFF) est ignoré sans casser le running status.
// ============================================================================
#pragma once
#include "IMidiTransport.h"

namespace harm {

class MidiParser {
public:
  void reset() { status_ = 0; idx_ = 0; }

  void feed(uint8_t b, const MidiSink& sink) {
    if (b & 0x80) {                       // octet de statut
      if (b >= 0xF8) return;              // temps réel système : ignoré, running status préservé
      if (b >= 0xF0) { status_ = 0; idx_ = 0; return; }  // système commun : reset
      status_ = b; idx_ = 0; return;      // message de canal
    }
    if (status_ == 0) return;             // octet de données sans statut : ignoré
    data_[idx_++] = b;
    const uint8_t hi = status_ & 0xF0;
    const uint8_t need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;  // ProgramChange/ChannelPressure = 1
    if (idx_ < need) return;
    idx_ = 0;                             // running status conservé

    MidiEvent e; e.channel = status_ & 0x0F; e.data1 = data_[0]; e.data2 = (need > 1) ? data_[1] : 0;
    if (hi == 0x90)      { e.type = MidiEvent::NoteOn;        sink(e); }
    else if (hi == 0x80) { e.type = MidiEvent::NoteOff;       sink(e); }
    else if (hi == 0xB0) { e.type = MidiEvent::ControlChange; sink(e); }
    else if (hi == 0xE0) { e.type = MidiEvent::PitchBend;     sink(e); }  // data1=LSB, data2=MSB
    // 0xA0/0xC0/0xD0 : consommés mais non remontés
  }

private:
  uint8_t status_ = 0, idx_ = 0, data_[2] = {0, 0};
};

}  // namespace harm
