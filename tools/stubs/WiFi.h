#pragma once
#include "Arduino.h"
#define WIFI_AP 2
#define WIFI_STA 1
struct IPAddressStub { const char* toString() const { return "0.0.0.0"; } };
struct WiFiStub {
  void mode(int) {}
  bool softAP(const char*, const char*) { return true; }
  IPAddressStub softAPIP() { return {}; }
  int  begin(const char*, const char*) { return 0; }
  int  status() { return 3; }
};
inline WiFiStub WiFi;
