// ============================================================================
//  SingleBellows.h — source d'air "soufflet simple".
//
//  Une seule chambre mue par un moteur/actionneur linéaire :
//    - comprimer (vers 0 mm)      => surpression   = souffle
//    - détendre  (vers travelMm)  => dépression    = aspiration
//  Conséquence : UNE seule direction possible à un instant donné
//  (supportsSimultaneousDirections() == false).
// ============================================================================
#pragma once
#include "IAirSource.h"
#include "../Config.h"
#include "../hal/Hal.h"
#include "../util/PIController.h"

namespace harm {

class SingleBellows : public IAirSource {
public:
  SingleBellows(IStepper& motor, IPressureSensor& sensor, const BellowsCfg& cfg)
    : motor_(motor), sensor_(sensor), cfg_(cfg) {}

  bool begin() override {
    motor_.begin();
    motor_.setKinematics(cfg_.maxSpeedMmS, cfg_.accelMmS2, cfg_.stepsPerMm);
    motor_.enable(true);
    sensor_.begin();
    pi_.configure(cfg_.pressureKp, cfg_.pressureKi, 0.0f, 1.0f, 1.0f);
    lastMs_ = 0;
    motor_.moveToMm(cfg_.centerMm);
    homed_ = true;                 // pas d'endstop : origine logicielle
    return true;
  }

  void startHoming() override { motor_.moveToMm(cfg_.centerMm); homed_ = true; }
  bool isHomed() const override { return homed_; }
  void startCentering() override { active_ = Direction::Closed; motor_.moveToMm(cfg_.centerMm); }

  void request(Direction dir, float intensity01) override {
    // Une direction à la fois : la plus récente gagne.
    active_ = dir;
    intensity_ = clamp01(intensity01);
  }
  void release(Direction dir) override {
    if (dir == active_) { active_ = Direction::Closed; intensity_ = 0.0f; motor_.moveToMm(cfg_.centerMm); }
  }

  float currentPressure(Direction dir) const override {
    return (dir == active_ && dir != Direction::Closed)
             ? std::fabs(const_cast<IPressureSensor&>(sensor_).readKpa()) : 0.0f;
  }
  Rail railForDirection(Direction dir) const override {
    return (dir == active_ && dir != Direction::Closed) ? Rail::A : Rail::None;
  }
  bool supportsSimultaneousDirections() const override { return false; }

  void update(uint32_t nowMs) override {
    float dt = (lastMs_ == 0) ? 0.02f : (nowMs - lastMs_) / 1000.0f;
    lastMs_ = nowMs;
    if (dt <= 0.0f) dt = 0.001f;
    if (dt > 0.2f) dt = 0.2f;

    if (active_ != Direction::Closed) {
      const float out = pi_.update(cfg_.pressureTargetKpa - std::fabs(sensor_.readKpa()), dt);
      const float end = (active_ == Direction::Blow) ? 0.0f : cfg_.travelMm;  // comprime / détend
      const float pos = motor_.positionMm();
      motor_.moveToMm(pos + (end - pos) * out);   // avance proportionnellement à l'erreur
    } else {
      pi_.reset();
    }
    motor_.run();
  }

  AirStatus status() const override {
    AirStatus s;
    s.pressureBlow = (active_ == Direction::Blow) ? std::fabs(const_cast<IPressureSensor&>(sensor_).readKpa()) : 0.0f;
    s.pressureDraw = (active_ == Direction::Draw) ? std::fabs(const_cast<IPressureSensor&>(sensor_).readKpa()) : 0.0f;
    s.pistonMm     = motor_.positionMm();
    s.homed        = homed_;
    s.ready        = homed_;
    s.assignmentGen = 0;   // pas d'échange de rôle avec un soufflet simple
    return s;
  }

private:
  IStepper&        motor_;
  IPressureSensor& sensor_;
  BellowsCfg       cfg_;
  bool      homed_ = false;
  Direction active_ = Direction::Closed;
  float     intensity_ = 0.0f;
  PIController pi_;
  uint32_t  lastMs_ = 0;
};

}  // namespace harm
