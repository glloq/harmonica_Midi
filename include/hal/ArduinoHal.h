// ============================================================================
//  ArduinoHal.h — implémentations HAL réelles (ESP32 uniquement).
//
//  Tout est sous #if HARM_ARDUINO : sur la cible native ce fichier est vide,
//  donc aucune dépendance Arduino/Adafruit ne fuit vers les tests PC.
// ============================================================================
#pragma once
#include "Hal.h"

#if HARM_ARDUINO
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <AccelStepper.h>
#include <Adafruit_BMP280.h>

namespace harm {

// ---- PCA9685 : bus de servos ------------------------------------------------
class Pca9685Bus : public IServoBus {
public:
  Pca9685Bus(uint8_t addr, float freqHz, float oscHz, uint16_t usMin, uint16_t usMax)
    : drv_(addr), freq_(freqHz), osc_(oscHz), usMin_(usMin), usMax_(usMax) {}

  bool begin() override {
    if (!drv_.begin()) return false;
    drv_.setOscillatorFrequency((uint32_t)osc_);
    drv_.setPWMFreq(freq_);
    return true;
  }
  void writeAngle(uint8_t ch, int deg) override {
    if (deg < 0) deg = 0;
    if (deg > 180) deg = 180;
    writeMicros(ch, usMin_ + (int)((long)(usMax_ - usMin_) * deg / 180));
  }
  void writeMicros(uint8_t ch, int us) override { drv_.writeMicroseconds(ch, us); }

private:
  Adafruit_PWMServoDriver drv_;
  float    freq_, osc_;
  uint16_t usMin_, usMax_;
};

// ---- Piston / soufflet : AccelStepper --------------------------------------
class AccelStepperAdapter : public IStepper {
public:
  AccelStepperAdapter(int stepPin, int dirPin, int enablePin, bool invertEnable)
    : stepper_(AccelStepper::DRIVER, stepPin, dirPin), enPin_(enablePin), invEn_(invertEnable) {}

  void begin() override { pinMode(enPin_, OUTPUT); enable(false); }
  void enable(bool on) override { digitalWrite(enPin_, (on ^ invEn_) ? HIGH : LOW); }
  void setKinematics(float maxSpeedMmS, float accelMmS2, float stepsPerMm) override {
    stepsPerMm_ = stepsPerMm;
    stepper_.setMaxSpeed(maxSpeedMmS * stepsPerMm);
    stepper_.setAcceleration(accelMmS2 * stepsPerMm);
  }
  void  moveToMm(float mm) override { stepper_.moveTo((long)lroundf(mm * stepsPerMm_)); }
  float positionMm() const override { return stepper_.currentPosition() / stepsPerMm_; }
  bool  isRunning() const override { return stepper_.distanceToGo() != 0; }
  void  run() override { stepper_.run(); }
  void  setPositionMm(float mm) override { stepper_.setCurrentPosition((long)lroundf(mm * stepsPerMm_)); }

private:
  // mutable : currentPosition()/distanceToGo() d'AccelStepper ne sont pas const.
  mutable AccelStepper stepper_;
  int   enPin_;
  bool  invEn_;
  float stepsPerMm_ = 80.0f;
};

// ---- Capteur BMP280 (I2C, baromètre absolu utilisé en gauge) ---------------
class Bmp280Sensor : public IPressureSensor {
public:
  explicit Bmp280Sensor(uint8_t addr) : addr_(addr) {}
  bool begin() override {
    if (!bmp_.begin(addr_)) return false;
    bmp_.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2,
                     Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X16,
                     Adafruit_BMP280::STANDBY_MS_1);
    tare();
    return true;
  }
  float readKpa() override { return bmp_.readPressure() / 1000.0f - offsetKpa_; }
  void  tare() override { offsetKpa_ = bmp_.readPressure() / 1000.0f; }

private:
  Adafruit_BMP280 bmp_;
  uint8_t addr_;
  float   offsetKpa_ = 0.0f;
};

// ---- Capteur analogique type MPX2010DP -------------------------------------
//  NOTE : le MPX2010 non amplifié sort ~mV ; en pratique un ampli
//  instrumentation précède l'ADC. kpaPerCount est à ajuster à la calibration.
class Mpx2010Sensor : public IPressureSensor {
public:
  explicit Mpx2010Sensor(int adcPin, float kpaPerCount = 0.0025f)
    : pin_(adcPin), scale_(kpaPerCount) {}
  bool  begin() override { analogReadResolution(12); tare(); return true; }
  float readKpa() override { return analogRead(pin_) * scale_ - offset_; }
  void  tare() override { offset_ = analogRead(pin_) * scale_; }

private:
  int   pin_;
  float scale_, offset_ = 0.0f;
};

// ---- Bus de sorties tout-ou-rien : PCA9685 dédié aux électro-vannes ---------
//  Le même composant que les servos, mais à haute fréquence et en duty pur :
//  0 % = fermé, 100 % = pic d'ouverture, duty intermédiaire = maintien.
class Pca9685DigitalBus : public IDigitalOutBus {
public:
  Pca9685DigitalBus(uint8_t addr, float freqHz, bool activeLow)
    : drv_(addr), freq_(freqHz), activeLow_(activeLow) {}

  bool begin() override {
    if (!drv_.begin()) return false;
    drv_.setPWMFreq(freq_);
    allOff();
    return true;
  }
  void write(uint8_t ch, bool on) override { writeLevel(ch, on ? 1.0f : 0.0f); }
  void writeLevel(uint8_t ch, float duty01) override {
    if (ch >= 16) return;
    if (duty01 < 0.0f) duty01 = 0.0f;
    if (duty01 > 1.0f) duty01 = 1.0f;
    const float d = activeLow_ ? 1.0f - duty01 : duty01;
    if (d >= 1.0f)      drv_.setPWM(ch, 4096, 0);        // full ON
    else if (d <= 0.0f) drv_.setPWM(ch, 0, 4096);        // full OFF
    else                drv_.setPWM(ch, 0, (uint16_t)(d * 4095.0f));
  }
  void    allOff() override { for (uint8_t c = 0; c < 16; ++c) write(c, false); }
  uint8_t channelCount() const override { return 16; }
  bool    supportsLevel() const override { return true; }

private:
  Adafruit_PWMServoDriver drv_;
  float freq_;
  bool  activeLow_;
};

// ---- Bus de sorties tout-ou-rien : GPIO directes (MOSFET / ULN2803) --------
class GpioDigitalBus : public IDigitalOutBus {
public:
  GpioDigitalBus(const int* pins, uint8_t count, bool activeLow)
    : count_(count > MAX_OUTPUTS ? MAX_OUTPUTS : count), activeLow_(activeLow) {
    for (uint8_t i = 0; i < count_; ++i) pins_[i] = pins[i];
  }
  bool begin() override {
    for (uint8_t i = 0; i < count_; ++i) { pinMode(pins_[i], OUTPUT); write(i, false); }
    return true;
  }
  void write(uint8_t ch, bool on) override {
    if (ch >= count_) return;
    digitalWrite(pins_[ch], (on ^ activeLow_) ? HIGH : LOW);
  }
  void    allOff() override { for (uint8_t i = 0; i < count_; ++i) write(i, false); }
  uint8_t channelCount() const override { return count_; }

private:
  int     pins_[MAX_OUTPUTS] = {0};
  uint8_t count_;
  bool    activeLow_;
};

// ---- Sortie PWM matérielle (LEDC) : pompe DC sur MOSFET ---------------------
class LedcPwmOut : public IPwmOut {
public:
  LedcPwmOut(int pin, float freqHz, uint8_t channel, uint8_t resolutionBits = 10)
    : pin_(pin), freq_(freqHz), ch_(channel), bits_(resolutionBits) {}
  bool begin() override {
    ledcSetup(ch_, freq_, bits_);
    ledcAttachPin(pin_, ch_);
    setDuty(0.0f);
    return true;
  }
  void  setDuty(float d) override {
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    duty_ = d;
    ledcWrite(ch_, (uint32_t)(d * ((1u << bits_) - 1)));
  }
  float duty() const override { return duty_; }

private:
  int     pin_;
  float   freq_;
  uint8_t ch_, bits_;
  float   duty_ = 0.0f;
};

// ---- Sortie PWM via impulsions servo : ESC brushless / driver PCA9685 ------
class ServoBusPwmOut : public IPwmOut {
public:
  ServoBusPwmOut(IServoBus& bus, uint8_t channel, uint16_t minUs, uint16_t maxUs)
    : bus_(bus), ch_(channel), minUs_(minUs), maxUs_(maxUs) {}
  bool begin() override { setDuty(0.0f); return true; }
  void setDuty(float d) override {
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    duty_ = d;
    bus_.writeMicros(ch_, minUs_ + (int)((maxUs_ - minUs_) * d));
  }
  float duty() const override { return duty_; }

private:
  IServoBus& bus_;
  uint8_t    ch_;
  uint16_t   minUs_, maxUs_;
  float      duty_ = 0.0f;
};

// ---- Fins de course GPIO ----------------------------------------------------
class GpioEndstops : public IEndstops {
public:
  GpioEndstops(int r1Pin, int r2Pin, bool activeLow)
    : r1_(r1Pin), r2_(r2Pin), activeLow_(activeLow) {}
  void begin() override { pinMode(r1_, INPUT_PULLUP); pinMode(r2_, INPUT_PULLUP); }
  bool triggeredR1() override { return digitalRead(r1_) == (activeLow_ ? LOW : HIGH); }
  bool triggeredR2() override { return digitalRead(r2_) == (activeLow_ ? LOW : HIGH); }

private:
  int  r1_, r2_;
  bool activeLow_;
};

}  // namespace harm
#endif  // HARM_ARDUINO
