// ============================================================================
//  PumpPair.h — source d'air "deux pompes continues opposées".
//
//  Montage : une pompe (ou turbine) pressurise un plenum SOUFFLE (rail A), une
//  seconde met un plenum ASPIRATION (rail B) en dépression. Chaque pompe est
//  pilotée en PWM ; une régulation PI par rail asservit la pression mesurée à
//  la consigne demandée par le moteur de jeu.
//
//  Par rapport au vérin double :
//   + souffle ET aspiration simultanés, sans limite de durée (pas d'inversion,
//     donc aucune permutation Rail<->Direction : assignmentGen reste à 0) ;
//   + aucun homing, aucune butée, aucun risque de perte de pas ;
//   − bruit continu, consommation, et une pression qui retombe moins vite
//     (d'où la purge optionnelle "bleed" à l'arrêt).
//
//  Un seul capteur peut suffire (sharedSensor) : la dépression est alors
//  supposée symétrique de la surpression — dégradé, mais fonctionnel.
// ============================================================================
#pragma once
#include "IAirSource.h"
#include "../Config.h"
#include "../hal/Hal.h"
#include "../util/PIController.h"

namespace harm {

class PumpPair : public IAirSource {
public:
  // blowSensor / drawSensor : drawSensor peut être nullptr si cfg.sharedSensor.
  // servos : utilisé uniquement pour la vanne de purge optionnelle (bleedChannel).
  PumpPair(IPwmOut& blowPump, IPwmOut& drawPump, IPressureSensor& blowSensor,
           IPressureSensor* drawSensor, IServoBus* servos, const PumpPairCfg& cfg)
    : blow_(blowPump), draw_(drawPump), sBlow_(blowSensor), sDraw_(drawSensor),
      servos_(servos), cfg_(cfg) {}

  bool begin() override {
    blow_.begin(); draw_.begin();
    sBlow_.begin(); if (sDraw_) sDraw_->begin();
    piBlow_.configure(cfg_.pressureKp, cfg_.pressureKi, 0.0f, 1.0f, 1.0f);
    piDraw_.configure(cfg_.pressureKp, cfg_.pressureKi, 0.0f, 1.0f, 1.0f);
    applyDuty(blow_, dutyBlow_ = cfg_.idleDuty, cfg_.blowPump);
    applyDuty(draw_, dutyDraw_ = cfg_.idleDuty, cfg_.drawPump);
    setBleed(true);                 // au repos : plenum à l'air libre
    homed_ = true;                  // rien à référencer mécaniquement
    haveLast_ = false; lastMs_ = 0;
    return true;
  }

  // Rien à référencer mécaniquement : "homé" dès l'allumage.
  void startHoming() override { homed_ = true; }
  bool isHomed() const override { return homed_; }
  void startCentering() override { blowDemand_ = drawDemand_ = 0.0f; }

  void request(Direction dir, float intensity01) override {
    if (dir == Direction::Blow) blowDemand_ = clamp01(intensity01);
    else if (dir == Direction::Draw) drawDemand_ = clamp01(intensity01);
    if (blowDemand_ > 0.0f || drawDemand_ > 0.0f) {
      if (!running_) { running_ = true; startedMs_ = lastMs_; }
      setBleed(false);
    }
  }
  void release(Direction dir) override {
    if (dir == Direction::Blow) blowDemand_ = 0.0f;
    else if (dir == Direction::Draw) drawDemand_ = 0.0f;
    if (blowDemand_ <= 0.0f && drawDemand_ <= 0.0f) { running_ = false; setBleed(true); }
  }

  float currentPressure(Direction dir) const override {
    if (dir == Direction::Blow) return std::fabs(readBlow());
    if (dir == Direction::Draw) return std::fabs(readDraw());
    return 0.0f;
  }
  // Correspondance FIXE : la pompe souffle alimente toujours le rail A.
  Rail railForDirection(Direction dir) const override {
    if (dir == Direction::Blow) return Rail::A;
    if (dir == Direction::Draw) return Rail::B;
    return Rail::None;
  }
  bool supportsSimultaneousDirections() const override { return true; }

  void update(uint32_t nowMs) override {
    float dt = haveLast_ ? (nowMs - lastMs_) / 1000.0f : 0.02f;
    haveLast_ = true; lastMs_ = nowMs;
    if (dt <= 0.0f) dt = 0.001f;
    if (dt > 0.2f) dt = 0.2f;

    setpoint_ = ((blowDemand_ > drawDemand_) ? blowDemand_ : drawDemand_) * cfg_.pressureTargetKpa;

    dutyBlow_ = regulate(piBlow_, blowDemand_, std::fabs(readBlow()), dt, manualBlow_);
    dutyDraw_ = regulate(piDraw_, drawDemand_, std::fabs(readDraw()), dt, manualDraw_);
    applyDuty(blow_, dutyBlow_, cfg_.blowPump);
    applyDuty(draw_, dutyDraw_, cfg_.drawPump);
  }

  AirStatus status() const override {
    AirStatus s;
    s.pressureBlow = std::fabs(readBlow());
    s.pressureDraw = std::fabs(readDraw());
    s.pistonMm = NAN;
    s.dutyBlow = dutyBlow_; s.dutyDraw = dutyDraw_;
    s.homed = homed_;
    // Prêt : régime établi ET pression du (des) rail(s) demandé(s) dans la tolérance.
    const bool spun = !running_ || (lastMs_ - startedMs_) >= cfg_.spinUpMs;
    bool ok = true;
    if (blowDemand_ > 0.0f) ok = ok && std::fabs(std::fabs(readBlow()) - setpoint_) <= cfg_.pressureToleranceKpa;
    if (drawDemand_ > 0.0f) ok = ok && std::fabs(std::fabs(readDraw()) - setpoint_) <= cfg_.pressureToleranceKpa;
    s.ready = homed_ && spun && ok;
    s.assignmentGen = 0;
    return s;
  }

  AirCaps caps() const override {
    AirCaps c;
    c.simultaneous = true; c.hasPiston = false; c.needsHoming = false;
    c.railsSwap = false; c.hasPumps = true;
    c.pressureSensors = cfg_.sharedSensor ? 1 : 2;
    return c;
  }

  float currentSetpointKpa() const override { return setpoint_; }

  bool setManualDuty(Direction dir, float duty01) override {
    if (dir == Direction::Blow)      manualBlow_ = (duty01 < 0.0f) ? -1.0f : clamp01(duty01);
    else if (dir == Direction::Draw) manualDraw_ = (duty01 < 0.0f) ? -1.0f : clamp01(duty01);
    else { manualBlow_ = manualDraw_ = -1.0f; }
    return true;
  }

private:
  float readBlow() const { return const_cast<IPressureSensor&>(sBlow_).readKpa(); }
  float readDraw() const {
    if (sDraw_) return const_cast<IPressureSensor*>(sDraw_)->readKpa();
    return -readBlow();                        // capteur unique : symétrie supposée
  }

  // Duty final d'une pompe : manuel (banc) > régulation PI > veille.
  float regulate(PIController& pi, float demand, float measured, float dt, float manual) {
    if (manual >= 0.0f) { pi.reset(); return manual; }
    if (demand <= 0.0f) { pi.reset(); return cfg_.idleDuty; }
    const float target = demand * cfg_.pressureTargetKpa;
    const float out = pi.update(target - measured, dt);
    return (out < cfg_.idleDuty) ? cfg_.idleDuty : out;
  }

  // Mise à l'échelle dans la plage utile de la pompe (démarrage / limite).
  void applyDuty(IPwmOut& out, float duty01, const PumpCfg& p) {
    float d = clamp01(duty01);
    if (d > 0.0f) d = p.minDuty + d * (p.maxDuty - p.minDuty);
    out.setDuty(p.invert ? 1.0f - d : d);
  }

  void setBleed(bool open) {
    if (!servos_ || cfg_.bleedChannel >= MAX_OUTPUTS) return;
    if (bleedState_ == (int8_t)open) return;
    bleedState_ = (int8_t)open;
    servos_->writeAngle(cfg_.bleedChannel, open ? cfg_.bleedOpenAngle : cfg_.bleedClosedAngle);
  }

  IPwmOut&         blow_;
  IPwmOut&         draw_;
  IPressureSensor& sBlow_;
  IPressureSensor* sDraw_;
  IServoBus*       servos_;
  PumpPairCfg      cfg_;

  PIController piBlow_, piDraw_;
  float    blowDemand_ = 0.0f, drawDemand_ = 0.0f;
  float    dutyBlow_ = 0.0f, dutyDraw_ = 0.0f;
  float    manualBlow_ = -1.0f, manualDraw_ = -1.0f;
  float    setpoint_ = 0.0f;
  bool     homed_ = false, running_ = false, haveLast_ = false;
  uint32_t lastMs_ = 0, startedMs_ = 0;
  int8_t   bleedState_ = -1;
};

}  // namespace harm
