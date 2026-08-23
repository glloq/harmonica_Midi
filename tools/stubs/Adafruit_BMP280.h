#pragma once
#include "Arduino.h"
class Adafruit_BMP280 {
public:
  enum sensor_mode { MODE_NORMAL };
  enum sensor_sampling { SAMPLING_X2, SAMPLING_X16 };
  enum sensor_filter { FILTER_X16 };
  enum standby_duration { STANDBY_MS_1 };
  bool begin(uint8_t = 0x76) { return true; }
  void setSampling(sensor_mode, sensor_sampling, sensor_sampling, sensor_filter, standby_duration) {}
  float readPressure() { return 101325.0f; }
};
