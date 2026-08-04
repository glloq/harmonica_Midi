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
  virtual void update(uint32_t nowMs) = 0;
};

// Implémentation par servo sur le bus PCA9685.
class ServoSlide : public ISlideActuator {
public:
  ServoSlide(IServoBus& servos, const SlideCfg& cfg) : servos_(servos), cfg_(cfg) {}
  bool begin() override { setEngaged(false); return true; }
  void setEngaged(bool engaged) override {
    if (engaged == engaged_) return;
    engaged_ = engaged;
    servos_.writeAngle(cfg_.channel, engaged ? cfg_.engagedAngle : cfg_.restAngle);
  }
  void update(uint32_t) override {}
  bool engaged() const { return engaged_; }

private:
  IServoBus& servos_;
  SlideCfg   cfg_;
  bool       engaged_ = false;
};

}  // namespace harm
