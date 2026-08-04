// ============================================================================
//  Valve2in1.h — valve imprimée "2 entrées -> 1 sortie" par trou.
//
//  Un servo par trou choisit la source : rail A (R1) ou rail B (R2), selon la
//  direction demandée et la correspondance courante Direction<->Rail donnée par
//  l'IAirSource. Une position "fermée" bloque le trou (centre bloquant) pour
//  ne pas fuir le rail partagé quand la note est relâchée.
// ============================================================================
#pragma once
#include "IValveDriver.h"
#include "../Config.h"
#include "../hal/Hal.h"

namespace harm {

class Valve2in1 : public IValveDriver {
public:
  Valve2in1(IServoBus& servos, const ValveCfg& cfg) : servos_(servos) {
    holeCount_ = cfg.holeCount;
    for (uint8_t i = 0; i < MAX_HOLES; ++i) idxByHole_[i] = 0xFF;
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i) {
      holes_[i] = cfg.holes2[i];
      if (holes_[i].hole < MAX_HOLES) idxByHole_[holes_[i].hole] = i;
    }
  }

  bool begin() override { allOff(); return true; }
  void update(uint32_t) override {}
  uint8_t holeCount() const override { return holeCount_; }
  bool supportsPerHoleDirection() const override { return true; }
  void bindAirSource(IAirSource* src) override { air_ = src; }

  void setHoleState(uint8_t hole, Direction dir) override {
    if (hole >= MAX_HOLES) return;
    uint8_t idx = idxByHole_[hole];
    if (idx == 0xFF) return;
    const Hole2in1& h = holes_[idx];
    if (dir == Direction::Closed) { servos_.writeAngle(h.channel, h.closedAngle); return; }
    Rail rail = air_ ? air_->railForDirection(dir) : Rail::A;
    servos_.writeAngle(h.channel, (rail == Rail::B) ? h.railBangle : h.railAangle);
  }

  void allOff() override {
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i)
      servos_.writeAngle(holes_[i].channel, holes_[i].closedAngle);
  }

private:
  IServoBus&  servos_;
  IAirSource* air_ = nullptr;
  Hole2in1    holes_[MAX_HOLES];
  uint8_t     idxByHole_[MAX_HOLES];
  uint8_t     holeCount_ = 0;
};

}  // namespace harm
