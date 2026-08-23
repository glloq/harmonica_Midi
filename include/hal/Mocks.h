// ============================================================================
//  Mocks.h — implémentations HAL simulées.
//
//  Utilisées :
//   - sur PC (env native) pour les tests unitaires et la démo de logique ;
//   - sur ESP32 quand system.mockMode = true (développer web/MIDI sans banc).
//
//  Chaque mock est INSPECTABLE (mémorise ses dernières commandes) pour que les
//  tests puissent vérifier "le bon trou a reçu le bon angle", etc.
// ============================================================================
#pragma once
#include "Hal.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace harm {

// Verbosité globale des mocks (les tests peuvent la couper).
struct MockLog {
  static bool enabled;
  __attribute__((__format__(__printf__, 1, 2)))
  static void line(const char* fmt, ...) {
    if (!enabled) return;
    va_list ap; va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap); std::printf("\n");
  }
};
inline bool MockLog::enabled = true;

// ---- Bus de servos simulé ---------------------------------------------------
class MockServoBus : public IServoBus {
public:
  int      lastAngle[32];
  int      lastMicros[32];
  uint32_t writes = 0;

  MockServoBus() { for (int i = 0; i < 32; ++i) { lastAngle[i] = -1; lastMicros[i] = -1; } }
  bool begin() override { MockLog::line("[servo] begin"); return true; }
  void writeAngle(uint8_t ch, int deg) override {
    if (ch < 32) lastAngle[ch] = deg;
    ++writes;
    MockLog::line("[servo] ch=%u angle=%d", ch, deg);
  }
  void writeMicros(uint8_t ch, int us) override {
    if (ch < 32) lastMicros[ch] = us;
    ++writes;
    MockLog::line("[servo] ch=%u us=%d", ch, us);
  }
};

// ---- Moteur simulé ----------------------------------------------------------
//  run() rapproche la position de la cible d'un pas fixe (stepMm) pour que les
//  fins de course puissent se déclencher progressivement pendant le homing.
class MockStepper : public IStepper {
public:
  float posMm = 0, targetMm = 0, stepMm = 5.0f;
  bool  enabled = false;

  void  begin() override { MockLog::line("[stepper] begin"); }
  void  enable(bool on) override { enabled = on; MockLog::line("[stepper] enable=%d", on); }
  void  setKinematics(float, float, float) override {}
  void  moveToMm(float mm) override { targetMm = mm; }
  float positionMm() const override { return posMm; }
  bool  isRunning() const override { return posMm != targetMm; }
  void  run() override {
    if (posMm < targetMm)      posMm = (targetMm - posMm < stepMm) ? targetMm : posMm + stepMm;
    else if (posMm > targetMm) posMm = (posMm - targetMm < stepMm) ? targetMm : posMm - stepMm;
  }
  void  setPositionMm(float mm) override { posMm = mm; targetMm = mm; MockLog::line("[stepper] setPos=%d", (int)mm); }
};

// ---- Capteur de pression simulé --------------------------------------------
//  Valeur réglable (tests) + rampe optionnelle vers une cible (démo mockMode).
class MockPressure : public IPressureSensor {
public:
  float value = 0.0f, target = 0.0f, rampStep = 0.0f, offset = 0.0f;

  bool  begin() override { return true; }
  float readKpa() override {
    if (rampStep > 0.0f) {
      if (value < target) value = (target - value < rampStep) ? target : value + rampStep;
      else if (value > target) value = (value - target < rampStep) ? target : value - rampStep;
    }
    return value - offset;
  }
  void  tare() override { offset = value; }
  void  setKpa(float v) { value = v; }
  void  rampTo(float t, float step) { target = t; rampStep = step; }
};

// ---- Bus de sorties tout-ou-rien simulé -------------------------------------
class MockDigitalBus : public IDigitalOutBus {
public:
  bool     level = true;                 // simule un bus capable de moduler (PCA9685)
  bool     on[MAX_OUTPUTS] = {false};
  float    duty[MAX_OUTPUTS] = {0.0f};
  uint32_t writes = 0;

  bool begin() override { MockLog::line("[sol] begin"); return true; }
  void write(uint8_t ch, bool state) override {
    if (ch >= MAX_OUTPUTS) return;
    on[ch] = state; duty[ch] = state ? 1.0f : 0.0f; ++writes;
    MockLog::line("[sol] ch=%u %s", ch, state ? "ON" : "OFF");
  }
  void writeLevel(uint8_t ch, float d) override {
    if (ch >= MAX_OUTPUTS) return;
    on[ch] = d > 0.0f; duty[ch] = d; ++writes;
    MockLog::line("[sol] ch=%u duty=%d%%", ch, (int)(d * 100));
  }
  void    allOff() override { for (uint8_t i = 0; i < MAX_OUTPUTS; ++i) write(i, false); }
  uint8_t channelCount() const override { return MAX_OUTPUTS; }
  bool    supportsLevel() const override { return level; }
};

// ---- Sortie PWM simulée (pompe) ---------------------------------------------
class MockPwmOut : public IPwmOut {
public:
  float    value = 0.0f;
  uint32_t writes = 0;
  const char* tag = "pump";

  explicit MockPwmOut(const char* name = "pump") : tag(name) {}
  bool  begin() override { return true; }
  void  setDuty(float d) override { value = d; ++writes; MockLog::line("[%s] duty=%d%%", tag, (int)(d * 100)); }
  float duty() const override { return value; }
};

// ---- Fins de course simulées -----------------------------------------------
//  Peuvent être pilotées à la main (setR1/R2) ou calculées depuis la position
//  d'un MockStepper (déclenchées aux extrémités de la course).
class MockEndstops : public IEndstops {
public:
  bool r1 = false, r2 = false;
  IStepper* piston = nullptr;
  float travelMm = 300.0f;

  void bindPiston(IStepper* s, float travel) override { piston = s; travelMm = travel; }
  void begin() override {}
  bool triggeredR1() override { return piston ? piston->positionMm() <= 0.5f : r1; }
  bool triggeredR2() override { return piston ? piston->positionMm() >= travelMm - 0.5f : r2; }
};

}  // namespace harm
