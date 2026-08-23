// Stub Arduino minimal — UNIQUEMENT pour le contrôle de syntaxe hors ligne
// (tools/check_esp32_syntax.sh). Ne fait rien, n'est jamais lié au firmware.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int  digitalRead(int) { return 0; }
inline int  analogRead(int) { return 0; }
inline void analogReadResolution(int) {}
inline void delay(uint32_t) {}
inline uint32_t millis() { return 0; }
inline void ledcSetup(uint8_t, double, uint8_t) {}
inline void ledcAttachPin(int, uint8_t) {}
inline void ledcWrite(uint8_t, uint32_t) {}

struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(int v) : s(std::to_string(v)) {}
  const char* c_str() const { return s.c_str(); }
  String& operator+=(const char* c) { s += c; return *this; }
  operator const char*() const { return s.c_str(); }
  // ArduinoJson sérialise vers un Print-like : il lui faut write().
  size_t write(uint8_t c) { s.push_back((char)c); return 1; }
  size_t write(const uint8_t* p, size_t n) { s.append((const char*)p, n); return n; }
};

#define SERIAL_8N1 0x800001c
struct HardwareSerial {
  void begin(unsigned long) {}
  void begin(unsigned long, uint32_t, int, int) {}
  void println(const char*) {}
  void print(const char*) {}
  template <class T> void println(T) {}
  template <class T> void print(T) {}
  template <class... A> void printf(const char*, A...) {}
  int  available() { return 0; }
  int  read() { return -1; }
  void write(uint8_t) {}
};
using SerialStub = HardwareSerial;
inline HardwareSerial Serial;
inline HardwareSerial Serial2;

struct EspStub { uint32_t getFreeHeap() { return 0; } void restart() {} };
inline EspStub ESP;

// FreeRTOS (sous-ensemble réellement utilisé)
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
typedef struct { int dummy; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
inline QueueHandle_t xQueueCreate(int, int) { return nullptr; }
inline int  xQueueSend(QueueHandle_t, const void*, int) { return pdTRUE; }
inline int  xQueueReceive(QueueHandle_t, void*, int) { return pdFALSE; }
inline void vTaskDelay(int) {}
inline void xTaskCreatePinnedToCore(void (*)(void*), const char*, int, void*, int, TaskHandle_t*, int) {}
