// ============================================================================
//  ValveSolenoid.h — distribution par ÉLECTRO-VANNES (électroaimants).
//
//  Deux variantes, symétriques des valves à servo :
//   - ValveSolenoid2in1 : DEUX vannes par trou (une par rail). La vanne du rail
//     correspondant à la direction demandée s'ouvre, l'autre reste fermée ; les
//     deux se ferment au relâché. Comme pour la valve servo 2-en-1, le rail est
//     demandé à l'IAirSource (il peut s'inverser sur un vérin double).
//   - ValveSolenoid1in1 : UNE vanne par trou, la direction étant imposée
//     globalement par la source d'air.
//
//  Par rapport aux servos : commutation quasi instantanée (~5-15 ms au lieu de
//  50-150 ms), donc des traits rapides possibles ; en contrepartie il faut du
//  courant de maintien, d'où le "peak & hold" (pic plein courant pendant
//  peakMs, puis maintien à holdDuty) porté par le bus de sorties.
// ============================================================================
#pragma once
#include "IValveDriver.h"
#include "../Config.h"
#include "../hal/Hal.h"

namespace harm {

// ---- Base commune : peak & hold sur un canal --------------------------------
class SolenoidValveBase : public IValveDriver {
public:
  SolenoidValveBase(IDigitalOutBus& bus, const DigitalBusCfg& cfg) : bus_(bus), dbg_(cfg) {
    for (uint8_t i = 0; i < MAX_OUTPUTS; ++i) { onSince_[i] = 0; state_[i] = -1; }
  }

  bool begin() override { bus_.begin(); allOff(); return true; }

  // Rétrograde chaque vanne ouverte de son pic vers son courant de maintien.
  void update(uint32_t nowMs) override {
    if (dbg_.holdDuty >= 1.0f || !bus_.supportsLevel()) return;
    for (uint8_t ch = 0; ch < MAX_OUTPUTS; ++ch) {
      if (state_[ch] != 1 || held_[ch]) continue;
      if (nowMs - onSince_[ch] >= dbg_.peakMs) { bus_.writeLevel(ch, dbg_.holdDuty); held_[ch] = true; }
    }
  }

protected:
  void energize(uint8_t ch, bool on, uint32_t nowMs = 0) {
    if (ch >= MAX_OUTPUTS) return;
    if (state_[ch] == (int8_t)on) return;
    state_[ch] = (int8_t)on;
    if (on) {
      onSince_[ch] = nowMs; held_[ch] = false;
      if (bus_.supportsLevel() && dbg_.holdDuty < 1.0f) bus_.writeLevel(ch, 1.0f);
      else bus_.write(ch, true);
    } else {
      held_[ch] = false;
      bus_.write(ch, false);
    }
  }
  void energizeAllOff() { for (uint8_t ch = 0; ch < MAX_OUTPUTS; ++ch) if (state_[ch] == 1) energize(ch, false); }

  IDigitalOutBus& bus_;
  DigitalBusCfg   dbg_;
  uint32_t        onSince_[MAX_OUTPUTS];
  int8_t          state_[MAX_OUTPUTS];   // -1 inconnu, 0 fermé, 1 ouvert
  bool            held_[MAX_OUTPUTS] = {false};
  uint32_t        nowMs_ = 0;
};

// ---- 2 vannes par trou (rail A / rail B) ------------------------------------
class ValveSolenoid2in1 : public SolenoidValveBase {
public:
  ValveSolenoid2in1(IDigitalOutBus& bus, const ValveCfg& cfg)
    : SolenoidValveBase(bus, cfg.solenoids) {
    holeCount_ = cfg.holeCount;
    for (uint8_t i = 0; i < MAX_HOLES; ++i) idxByHole_[i] = 0xFF;
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i) {
      holes_[i] = cfg.holesS2[i];
      if (holes_[i].hole < MAX_HOLES) idxByHole_[holes_[i].hole] = i;
    }
  }

  uint8_t holeCount() const override { return holeCount_; }
  bool    supportsPerHoleDirection() const override { return true; }
  void    bindAirSource(IAirSource* src) override { air_ = src; }

  void update(uint32_t nowMs) override { nowMs_ = nowMs; SolenoidValveBase::update(nowMs); }

  void setHoleState(uint8_t hole, Direction dir) override {
    if (hole >= MAX_HOLES) return;
    const uint8_t idx = idxByHole_[hole];
    if (idx == 0xFF) return;
    const HoleSol2& h = holes_[idx];
    if (dir == Direction::Closed) { energize(h.railAchannel, false); energize(h.railBchannel, false); return; }
    const Rail rail = air_ ? air_->railForDirection(dir) : Rail::A;
    const bool useB = (rail == Rail::B);
    energize(useB ? h.railAchannel : h.railBchannel, false);      // ferme l'autre d'abord
    energize(useB ? h.railBchannel : h.railAchannel, true, nowMs_);
  }

  void allOff() override {
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i) {
      energize(holes_[i].railAchannel, false);
      energize(holes_[i].railBchannel, false);
    }
  }

private:
  IAirSource* air_ = nullptr;
  HoleSol2    holes_[MAX_HOLES];
  uint8_t     idxByHole_[MAX_HOLES];
  uint8_t     holeCount_ = 0;
};

// ---- 1 vanne par trou (direction globale) -----------------------------------
class ValveSolenoid1in1 : public SolenoidValveBase {
public:
  ValveSolenoid1in1(IDigitalOutBus& bus, const ValveCfg& cfg)
    : SolenoidValveBase(bus, cfg.solenoids) {
    holeCount_ = cfg.holeCount;
    for (uint8_t i = 0; i < MAX_HOLES; ++i) idxByHole_[i] = 0xFF;
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i) {
      holes_[i] = cfg.holesS1[i];
      if (holes_[i].hole < MAX_HOLES) idxByHole_[holes_[i].hole] = i;
    }
  }

  uint8_t holeCount() const override { return holeCount_; }
  bool    supportsPerHoleDirection() const override { return false; }
  void    bindAirSource(IAirSource*) override {}

  void update(uint32_t nowMs) override { nowMs_ = nowMs; SolenoidValveBase::update(nowMs); }

  void setHoleState(uint8_t hole, Direction dir) override {
    if (hole >= MAX_HOLES) return;
    const uint8_t idx = idxByHole_[hole];
    if (idx == 0xFF) return;
    energize(holes_[idx].channel, dir != Direction::Closed, nowMs_);
  }

  void allOff() override {
    for (uint8_t i = 0; i < holeCount_ && i < MAX_HOLES; ++i) energize(holes_[i].channel, false);
  }

private:
  HoleSol1 holes_[MAX_HOLES];
  uint8_t  idxByHole_[MAX_HOLES];
  uint8_t  holeCount_ = 0;
};

}  // namespace harm
