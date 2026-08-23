#pragma once
#include "Arduino.h"
class AccelStepper {
public:
  enum Mode { DRIVER = 1 };
  AccelStepper(Mode, int, int) {}
  void setMaxSpeed(float) {}
  void setAcceleration(float) {}
  void moveTo(long) {}
  long currentPosition() { return 0; }
  long distanceToGo() { return 0; }
  void run() {}
  void setCurrentPosition(long) {}
};
