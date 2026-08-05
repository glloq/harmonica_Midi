// ============================================================================
//  DualReservoirPiston.h — source d'air "vérin double" (le design des docs).
//
//  Deux réservoirs R1(=RailA) / R2(=RailB) séparés par un piston sur tige
//  filetée. Convention de géométrie : position 0 mm = R1 comprimé (endstop R1),
//  position travelMm = R2 comprimé (endstop R2).
//    - se déplacer vers 0    => R1 en surpression (souffle) / R2 en dépression
//    - se déplacer vers travel=> R2 en surpression (souffle) / R1 en dépression
//
//  Subtilité clé : consommer du souffle ET de l'aspiration pousse le piston
//  dans le MÊME sens. Il dérive donc vers une extrémité et doit s'INVERSER ;
//  à chaque inversion les rôles souffle/aspiration des réservoirs s'échangent
//  (blowRail bascule A<->B) et assignmentGen est incrémenté pour que le moteur
//  ré-applique les positions de valve.
// ============================================================================
#pragma once
#include "IAirSource.h"
#include "../Config.h"
#include "../hal/Hal.h"
#include "../util/PIController.h"

namespace harm {

class DualReservoirPiston : public IAirSource {
public:
  DualReservoirPiston(IStepper& piston, IPressureSensor& pR1, IPressureSensor& pR2,
                      IEndstops& endstops, IServoBus& servos, const DualReservoirCfg& cfg)
    : piston_(piston), pR1_(pR1), pR2_(pR2), endstops_(endstops), servos_(servos), cfg_(cfg) {}

  bool begin() override {
    // Garde-fou : une marge d'inversion >= travel/2 provoquerait une tempête
    // d'inversions à chaque tick (bandes de fin de course qui se chevauchent).
    if (cfg_.reversalMarginMm > cfg_.travelMm * 0.45f) cfg_.reversalMarginMm = cfg_.travelMm * 0.45f;
    if (cfg_.reversalMarginMm < 1.0f) cfg_.reversalMarginMm = 1.0f;
    piston_.begin();
    piston_.setKinematics(cfg_.maxSpeedMmS, cfg_.accelMmS2, cfg_.stepsPerMm);
    piston_.enable(true);
    pR1_.begin(); pR2_.begin();
    endstops_.begin();
    pi_.configure(cfg_.pressureKp, cfg_.pressureKi, 0.0f, 1.0f, 1.0f);
    lastMs_ = 0;
    blowRail_ = Rail::A;           // au départ R1 = souffle (arbitraire, corrigé au 1er reversal)
    setReservoirValve(Rail::A, false);
    setReservoirValve(Rail::B, false);
    state_ = State::Boot;
    return true;
  }

  void startHoming() override {
    state_ = State::Homing;
    homingDeadlineMs_ = 0;   // fixé au premier tick de Homing
    // Va vers l'endstop configuré (R1 -> vers 0, R2 -> vers travel).
    piston_.moveToMm(cfg_.homeOnR1 ? -2.0f * cfg_.travelMm : 3.0f * cfg_.travelMm);
  }
  bool isHomed() const override { return homed_; }

  void startCentering() override {
    state_ = State::Centering;
    setReservoirValve(Rail::A, true);   // les deux valves ouvertes : chambres à l'air libre
    setReservoirValve(Rail::B, true);
    piston_.moveToMm(cfg_.centerMm);
  }

  void request(Direction dir, float intensity01) override {
    if (dir == Direction::Blow) blowDemand_ = clamp01(intensity01);
    else if (dir == Direction::Draw) drawDemand_ = clamp01(intensity01);
    if (state_ == State::Centering || state_ == State::Boot) if (homed_) state_ = State::Running;
  }
  void release(Direction dir) override {
    if (dir == Direction::Blow) blowDemand_ = 0.0f;
    else if (dir == Direction::Draw) drawDemand_ = 0.0f;
  }

  float currentPressure(Direction dir) const override {
    Rail r = railForDirection(dir);
    if (r == Rail::None) return 0.0f;
    return railKpa(r);
  }
  Rail railForDirection(Direction dir) const override {
    if (dir == Direction::Blow) return blowRail_;
    if (dir == Direction::Draw) return (blowRail_ == Rail::A) ? Rail::B : Rail::A;
    return Rail::None;
  }
  bool supportsSimultaneousDirections() const override { return true; }

  void update(uint32_t nowMs) override {
    switch (state_) {
      case State::Boot: break;
      case State::Homing: {
        piston_.run();
        if (homingDeadlineMs_ == 0)
          homingDeadlineMs_ = nowMs + (uint32_t)((2.0f * cfg_.travelMm / cfg_.maxSpeedMmS) * 1000.0f) + 5000u;
        const bool hit = cfg_.homeOnR1 ? endstops_.triggeredR1() : endstops_.triggeredR2();
        if (hit || nowMs >= homingDeadlineMs_) {       // endstop atteint OU timeout de sécurité
          piston_.setPositionMm(cfg_.homeOnR1 ? 0.0f : cfg_.travelMm);
          homed_ = true;
          startCentering();
        }
        break;
      }
      case State::Centering:
        piston_.run();
        if (!piston_.isRunning()) state_ = State::Running;
        break;
      case State::Running:
        runningControl(nowMs);
        piston_.run();
        break;
    }
  }

  AirStatus status() const override {
    AirStatus s;
    s.pressureBlow  = railKpa(blowRail_);
    s.pressureDraw  = railKpa(blowRail_ == Rail::A ? Rail::B : Rail::A);
    s.pistonMm      = piston_.positionMm();
    s.homed         = homed_;
    s.ready         = homed_ && state_ == State::Running;
    s.assignmentGen = assignmentGen_;
    return s;
  }

private:
  enum class State : uint8_t { Boot, Homing, Centering, Running };

  // Sens de déplacement (mm) qui met le rail souffle courant en surpression.
  int feedDir() const { return (blowRail_ == Rail::A) ? -1 : +1; }
  float feedEndMm() const { return (feedDir() < 0) ? 0.0f : cfg_.travelMm; }

  float railKpa(Rail r) const {   // magnitude (télémétrie / currentPressure)
    if (r == Rail::A) return std::fabs(const_cast<IPressureSensor&>(pR1_).readKpa());
    if (r == Rail::B) return std::fabs(const_cast<IPressureSensor&>(pR2_).readKpa());
    return 0.0f;
  }
  float railKpaSigned(Rail r) const {   // signé (comprimé = +, détendu = -) pour la régulation
    if (r == Rail::A) return const_cast<IPressureSensor&>(pR1_).readKpa();
    if (r == Rail::B) return const_cast<IPressureSensor&>(pR2_).readKpa();
    return 0.0f;
  }

  // Ne commande le servo que sur changement d'état (évite le buzz / trafic I2C).
  void setReservoirValve(Rail r, bool open) {
    int8_t& st = (r == Rail::A) ? valveStateA_ : valveStateB_;
    if (st == (int8_t)open) return;
    st = (int8_t)open;
    if (r == Rail::A) servos_.writeAngle(cfg_.valveR1Channel, open ? cfg_.valveR1Open : cfg_.valveR1Closed);
    else if (r == Rail::B) servos_.writeAngle(cfg_.valveR2Channel, open ? cfg_.valveR2Open : cfg_.valveR2Closed);
  }

  void reverse() {
    blowRail_ = (blowRail_ == Rail::A) ? Rail::B : Rail::A;
    ++assignmentGen_;
  }

  void runningControl(uint32_t nowMs) {
    float dt = (lastMs_ == 0) ? 0.02f : (nowMs - lastMs_) / 1000.0f;
    lastMs_ = nowMs;
    if (dt <= 0.0f) dt = 0.001f;
    if (dt > 0.2f) dt = 0.2f;

    const bool blow = blowDemand_ > 0.0f;
    const bool draw = drawDemand_ > 0.0f;
    const bool idle = !blow && !draw;
    const float pos = piston_.positionMm();

    // Valves réservoir : on ouvre le rail dont la direction est demandée.
    Rail blowR = blowRail_;
    Rail drawR = (blowRail_ == Rail::A) ? Rail::B : Rail::A;
    setReservoirValve(blowR, blow);
    setReservoirValve(drawR, draw);

    // Inversion : obligatoire si on atteint la marge côté "feed" ; anticipée à
    // vide au-delà de 60 % de course pour rester proche du centre (cf README).
    const float margin = cfg_.reversalMarginMm;
    const bool atFeedEnd = (feedDir() < 0) ? (pos <= margin) : (pos >= cfg_.travelMm - margin);
    const bool wayPastCenter = (feedDir() < 0) ? (pos <= cfg_.travelMm * 0.4f)
                                               : (pos >= cfg_.travelMm * 0.6f);
    if (atFeedEnd || (idle && wayPastCenter)) { pi_.reset(); reverse(); return; }

    if (idle) { pi_.reset(); setpoint_ = 0.0f; piston_.moveToMm(pos); return; }

    // Consigne = intensité demandée × cible (=> vélocité / CC breath/expression/vibrato
    // audibles) ; la direction la plus forte fixe la pression.
    const float demand = (blowDemand_ > drawDemand_) ? blowDemand_ : drawDemand_;
    setpoint_ = demand * cfg_.pressureTargetKpa;
    // Régulation PI sur le retour SIGNÉ du rail souffle (comprimé = +). Après une
    // inversion le rail est en dépression -> erreur grande -> le piston recomprime.
    const float out = pi_.update(setpoint_ - railKpaSigned(blowRail_), dt);
    float target = pos + feedDir() * out * cfg_.travelMm;
    if (target < 0.0f) target = 0.0f;
    else if (target > cfg_.travelMm) target = cfg_.travelMm;
    piston_.moveToMm(target);
  }

  IStepper&        piston_;
  IPressureSensor& pR1_;
  IPressureSensor& pR2_;
  IEndstops&       endstops_;
  IServoBus&       servos_;
  DualReservoirCfg cfg_;

  State    state_ = State::Boot;
  bool     homed_ = false;
  Rail     blowRail_ = Rail::A;
  float    blowDemand_ = 0.0f, drawDemand_ = 0.0f;
  uint16_t assignmentGen_ = 0;
  int8_t   valveStateA_ = -1, valveStateB_ = -1;   // -1 inconnu, 0 fermé, 1 ouvert
  PIController pi_;
  uint32_t lastMs_ = 0;
  uint32_t homingDeadlineMs_ = 0;
  float    setpoint_ = 0.0f;
public:
  float currentSetpointKpa() const override { return setpoint_; }   // consigne PI (test/télémétrie)
};

}  // namespace harm
