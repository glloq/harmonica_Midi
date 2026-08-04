// ============================================================================
//  Valve1in1.h — valve "1 entrée -> 1 sortie" par trou (simple porte on/off).
//
//  Le servo ouvre ou ferme le passage vers le trou ; la DIRECTION (souffle ou
//  aspiration) est imposée GLOBALEMENT par la source d'air. On ne peut donc
//  sonner qu'un seul sens à la fois (supportsPerHoleDirection() == false).
// ============================================================================
#pragma once
#include "IValveDriver.h"
#include "../Config.h"
#include "../hal/Hal.h"

namespace harm {

class Valve1in1 : public IValveDriver {
public:
  Valve1in1(IServoBus& servos, const ValveCfg& cfg) : servos_(servos) {
    holeCount_ = cfg.holeCount;
    for (uint8_t i = 0; i < MAX_HOLES; ++i) idxByHole_[i] = 0xFF;
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i) {
      holes_[i] = cfg.holes1[i];
      if (holes_[i].hole < MAX_HOLES) idxByHole_[holes_[i].hole] = i;
    }
  }

  bool begin() override { allOff(); return true; }
  void update(uint32_t) override {}
  uint8_t holeCount() const override { return holeCount_; }
  bool supportsPerHoleDirection() const override { return false; }
  void bindAirSource(IAirSource* src) override { air_ = src; }

  void setHoleState(uint8_t hole, Direction dir) override {
    if (hole >= MAX_HOLES) return;
    uint8_t idx = idxByHole_[hole];
    if (idx == 0xFF) return;
    const Hole1in1& h = holes_[idx];
    servos_.writeAngle(h.channel, (dir == Direction::Closed) ? h.closedAngle : h.openAngle);
  }

  void allOff() override {
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i)
      servos_.writeAngle(holes_[i].channel, holes_[i].closedAngle);
  }

private:
  IServoBus&  servos_;
  IAirSource* air_ = nullptr;
  Hole1in1    holes_[MAX_HOLES];
  uint8_t     idxByHole_[MAX_HOLES];
  uint8_t     holeCount_ = 0;
};

}  // namespace harm
