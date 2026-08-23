// ============================================================================
//  ISlideActuator.h / ServoSlide — actionneur du bouton "slide" (chromatique).
//
//  Un harmonica chromatique décale d'un demi-ton toutes les notes quand le
//  slide est enfoncé. On modélise ça par un actionneur binaire (enfoncé / repos)
//  piloté par le NoteEngine selon le champ "slide" du mapping.
// ============================================================================
#pragma once
#include "../Types.h"
#include "../Config.h"
#include "../hal/Hal.h"

namespace harm {

class ISlideActuator {
public:
  virtual ~ISlideActuator() = default;
  virtual bool begin() = 0;
  virtual void setEngaged(bool engaged) = 0;
  virtual bool engaged() const = 0;
  virtual void update(uint32_t nowMs) = 0;
};

// Implémentation par servo sur le bus PCA9685.
class ServoSlide : public ISlideActuator {
public:
  ServoSlide(IServoBus& servos, const SlideCfg& cfg) : servos_(servos), cfg_(cfg) {}
  bool begin() override { engaged_ = true; setEngaged(false); return true; }
  void setEngaged(bool engaged) override {
    if (engaged == engaged_) return;
    engaged_ = engaged;
    servos_.writeAngle(cfg_.channel, engaged ? cfg_.engagedAngle : cfg_.restAngle);
  }
  void update(uint32_t) override {}
  bool engaged() const override { return engaged_; }

private:
  IServoBus& servos_;
  SlideCfg   cfg_;
  bool       engaged_ = false;
};

// Implémentation par électroaimant / vanne "pousse-slide" : plus rapide que le
// servo (course franche), mais sans position intermédiaire.
class SolenoidSlide : public ISlideActuator {
public:
  SolenoidSlide(IDigitalOutBus& bus, const SlideCfg& cfg) : bus_(bus), cfg_(cfg) {}
  bool begin() override { engaged_ = true; setEngaged(false); return true; }
  void setEngaged(bool engaged) override {
    if (engaged == engaged_) return;
    engaged_ = engaged;
    bus_.write(cfg_.channel, engaged);
  }
  void update(uint32_t) override {}
  bool engaged() const override { return engaged_; }

private:
  IDigitalOutBus& bus_;
  SlideCfg        cfg_;
  bool            engaged_ = false;
};

}  // namespace harm
