#pragma once
#include "Arduino.h"
class Adafruit_PWMServoDriver {
public:
  explicit Adafruit_PWMServoDriver(uint8_t = 0x40) {}
  bool begin() { return true; }
  void setOscillatorFrequency(uint32_t) {}
  void setPWMFreq(float) {}
  void setPWM(uint8_t, uint16_t, uint16_t) {}
  void writeMicroseconds(uint8_t, uint16_t) {}
};
