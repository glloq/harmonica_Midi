#pragma once
#include "Arduino.h"
#include "LittleFS.h"
#include <functional>
#define HTTP_GET 1
#define HTTP_POST 2
class AsyncWebServerRequest {
public:
  void* _tempObject = nullptr;
  bool authenticate(const char*, const char*) { return true; }
  void requestAuthentication() {}
  void send(int, const char* = "", const String& = String()) {}
  void send(int, const char*, const char*) {}
};
struct AsyncStaticHandler { AsyncStaticHandler& setDefaultFile(const char*) { return *this; } };
class AsyncWebServer {
public:
  explicit AsyncWebServer(uint16_t) {}
  using Handler = std::function<void(AsyncWebServerRequest*)>;
  using BodyHandler = std::function<void(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t)>;
  void on(const char*, int, Handler) {}
  void on(const char*, int, Handler, std::nullptr_t, BodyHandler) {}
  AsyncStaticHandler& serveStatic(const char*, FsStub&, const char*) { static AsyncStaticHandler h; return h; }
  void onNotFound(Handler) {}
  void begin() {}
};
