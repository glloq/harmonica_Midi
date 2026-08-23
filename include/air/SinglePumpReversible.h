// ============================================================================
//  SinglePumpReversible.h — source d'air "une pompe + aiguillage".
//
//  Montage le plus économique : UNE pompe, et un aiguillage (servo 3 voies ou
//  électro-vanne) qui décide si elle POUSSE l'air vers les trous (souffle) ou
//  l'ASPIRE (aspiration). Conséquences directes :
//   - une seule direction à la fois (supportsSimultaneousDirections() == false),
//     l'arbitrage reject/steal du NoteEngine s'applique ;
//   - un temps de bascule (switchMs) pendant lequel la pression n'est pas encore
//     établie : status().ready reste faux, la pompe redémarre après l'aiguillage.
// ============================================================================
#pragma once
#include "IAirSource.h"
#include "../Config.h"
#include "../hal/Hal.h"
#include "../util/PIController.h"

namespace harm {

class SinglePumpReversible : public IAirSource {
public:
  // servos : requis si diverter == Servo ; solenoids : requis si diverter == Solenoid.
  SinglePumpReversible(IPwmOut& pump, IPressureSensor& sensor, IServoBus* servos,
                       IDigitalOutBus* solenoids, const SinglePumpCfg& cfg)
    : pump_(pump), sensor_(sensor), servos_(servos), solenoids_(solenoids), cfg_(cfg) {}

  bool begin() override {
    pump_.begin();
    sensor_.begin();
    pi_.configure(cfg_.pressureKp, cfg_.pressureKi, 0.0f, 1.0f, 1.0f);
    applyDuty(duty_ = cfg_.idleDuty);
    applyDiverter(Direction::Closed);
    homed_ = true;                  // rien à référencer mécaniquement
    haveLast_ = false; lastMs_ = 0;
    return true;
  }

  void startHoming() override { homed_ = true; }
  bool isHomed() const override { return homed_; }
  void startCentering() override { active_ = Direction::Closed; intensity_ = 0.0f; applyDiverter(Direction::Closed); }

  void request(Direction dir, float intensity01) override {
    if (dir == Direction::Closed) return;
    if (dir != active_) { active_ = dir; switchedAtMs_ = lastMs_; applyDiverter(dir); pi_.reset(); }
    intensity_ = clamp01(intensity01);
  }
  void release(Direction dir) override {
    if (dir == active_) { active_ = Direction::Closed; intensity_ = 0.0f; applyDiverter(Direction::Closed); }
  }

  float currentPressure(Direction dir) const override {
    return (dir == active_ && dir != Direction::Closed) ? std::fabs(read()) : 0.0f;
  }
  Rail railForDirection(Direction dir) const override {
    return (dir == active_ && dir != Direction::Closed) ? Rail::A : Rail::None;
  }
  bool supportsSimultaneousDirections() const override { return false; }

  void update(uint32_t nowMs) override {
    float dt = haveLast_ ? (nowMs - lastMs_) / 1000.0f : 0.02f;
    haveLast_ = true; lastMs_ = nowMs;
    if (dt <= 0.0f) dt = 0.001f;
    if (dt > 0.2f) dt = 0.2f;

    if (manual_ >= 0.0f) { pi_.reset(); applyDuty(duty_ = manual_); setpoint_ = 0.0f; return; }
    if (active_ == Direction::Closed) { pi_.reset(); setpoint_ = 0.0f; applyDuty(duty_ = cfg_.idleDuty); return; }

    setpoint_ = intensity_ * cfg_.pressureTargetKpa;
    const float out = pi_.update(setpoint_ - std::fabs(read()), dt);
    duty_ = (out < cfg_.idleDuty) ? cfg_.idleDuty : out;
    applyDuty(duty_);
  }

  AirStatus status() const override {
    AirStatus s;
    const float p = std::fabs(read());
    s.pressureBlow = (active_ == Direction::Blow) ? p : 0.0f;
    s.pressureDraw = (active_ == Direction::Draw) ? p : 0.0f;
    s.pistonMm = NAN;
    s.dutyBlow = (active_ == Direction::Blow) ? duty_ : 0.0f;
    s.dutyDraw = (active_ == Direction::Draw) ? duty_ : 0.0f;
    s.homed = homed_;
    s.ready = homed_ && !switching() &&
              (active_ == Direction::Closed || std::fabs(p - setpoint_) <= cfg_.pressureToleranceKpa);
    s.assignmentGen = 0;
    return s;
  }

  AirCaps caps() const override {
    AirCaps c;
    c.simultaneous = false; c.hasPiston = false; c.needsHoming = false;
    c.railsSwap = false; c.hasPumps = true; c.hasDiverter = true; c.pressureSensors = 1;
    return c;
  }

  float currentSetpointKpa() const override { return setpoint_; }
  bool  setManualDuty(Direction, float duty01) override {
    manual_ = (duty01 < 0.0f) ? -1.0f : clamp01(duty01);
    return true;
  }
  // Vrai tant que l'aiguillage n'a pas fini sa course (pression non établie).
  bool switching() const { return active_ != Direction::Closed && (lastMs_ - switchedAtMs_) < cfg_.switchMs; }

private:
  float read() const { return const_cast<IPressureSensor&>(sensor_).readKpa(); }

  void applyDuty(float duty01) {
    float d = clamp01(duty01);
    if (d > 0.0f) d = cfg_.pump.minDuty + d * (cfg_.pump.maxDuty - cfg_.pump.minDuty);
    pump_.setDuty(cfg_.pump.invert ? 1.0f - d : d);
  }

  void applyDiverter(Direction dir) {
    if (cfg_.diverter == DiverterImpl::Servo) {
      if (!servos_) return;
      const int a = (dir == Direction::Blow) ? cfg_.blowAngle
                  : (dir == Direction::Draw) ? cfg_.drawAngle : cfg_.neutralAngle;
      servos_->writeAngle(cfg_.diverterChannel, a);
    } else {
      if (!solenoids_) return;
      // Électro-vanne 2 positions : repos = l'autre direction (pas d'état neutre).
      const bool blowState = cfg_.solenoidBlowState;
      solenoids_->write(cfg_.diverterChannel, (dir == Direction::Blow) ? blowState : !blowState);
    }
  }

  IPwmOut&         pump_;
  IPressureSensor& sensor_;
  IServoBus*       servos_;
  IDigitalOutBus*  solenoids_;
  SinglePumpCfg    cfg_;

  PIController pi_;
  Direction active_ = Direction::Closed;
  float     intensity_ = 0.0f, duty_ = 0.0f, setpoint_ = 0.0f, manual_ = -1.0f;
  bool      homed_ = false, haveLast_ = false;
  uint32_t  lastMs_ = 0, switchedAtMs_ = 0;
};

}  // namespace harm
