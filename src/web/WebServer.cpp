// ============================================================================
//  WebServer.cpp — serveur web asynchrone (ESP32 uniquement).
//  Glue ESPAsyncWebServer ; validation fine sur matériel = phase 5.
// ============================================================================
#include "web/WebServer.h"

#if HARM_ARDUINO
#include <Arduino.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

namespace harm {

// Instances et état partagés (un seul serveur dans le firmware).
static AsyncWebServer g_server(80);
static AsyncWebSocket g_ws("/ws");
static System*        g_sys   = nullptr;
static ConfigStore*   g_store = nullptr;
static bool           g_reboot = false;
static std::string    g_cfgBuf;      // accumulation du corps POST /api/config
static std::string    g_calBuf;      // accumulation du corps POST /api/calibrate

// ---- Télémétrie -------------------------------------------------------------
static String buildStatus() {
  JsonDocument d;
  AirStatus a = g_sys->air->status();
  d["homed"] = a.homed;
  d["ready"] = a.ready;
  d["pistonMm"] = a.pistonMm;
  d["pressureBlow"] = a.pressureBlow;
  d["pressureDraw"] = a.pressureDraw;
  d["assignmentGen"] = a.assignmentGen;
  d["voices"] = g_sys->engine.activeVoiceCount();
  d["mixedCapable"] = g_sys->engine.mixedCapable();
  d["transports"] = g_sys->router.transportCount();
  d["mock"] = g_sys->mock;
  d["harmonica"] = g_sys->map.name();
  d["freeHeap"] = (uint32_t)ESP.getFreeHeap();
  String out; serializeJson(d, out); return out;
}

// ---- Calibration ------------------------------------------------------------
static bool applyCalibrate(const char* json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  const char* target = d["target"] | "";
  if (!strcmp(target, "servo")) {
    uint8_t ch = d["channel"] | 0;
    if (d["us"].is<int>())       g_sys->servos->writeMicros(ch, d["us"].as<int>());
    else                         g_sys->servos->writeAngle(ch, d["deg"] | 90);
    return true;
  }
  if (!strcmp(target, "piston")) {
    const char* action = d["action"] | "center";
    if (!strcmp(action, "home")) g_sys->air->startHoming();
    else                         g_sys->air->startCentering();
    return true;
  }
  if (!strcmp(target, "pressureZero")) {
    if (g_sys->pA) g_sys->pA->tare();
    if (g_sys->pB) g_sys->pB->tare();
    return true;
  }
  return false;
}

// ---- Routes -----------------------------------------------------------------
void WebServer::begin(System* sys, ConfigStore* store) {
  g_sys = sys; g_store = store; g_reboot = false;

  g_server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", g_store->raw());
  });

  g_server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest*) { /* réponse envoyée par le handler de corps */ },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) g_cfgBuf.clear();
      g_cfgBuf.append((const char*)data, len);
      if (index + len >= total) {
        bool ok = g_store->save(g_cfgBuf.c_str());
        req->send(ok ? 200 : 400, "application/json",
                  ok ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":false,\"error\":\"invalid config\"}");
      }
    });

  g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", buildStatus());
  });

  g_server.on("/api/calibrate", HTTP_POST,
    [](AsyncWebServerRequest*) {},
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) g_calBuf.clear();
      g_calBuf.append((const char*)data, len);
      if (index + len >= total) {
        bool ok = applyCalibrate(g_calBuf.c_str());
        req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
      }
    });

  g_server.on("/api/harmonicas", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument d; JsonArray arr = d.to<JsonArray>();
    File dir = LittleFS.open("/presets");
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) arr.add(String(f.name()));
    }
    String out; serializeJson(d, out);
    req->send(200, "application/json", out);
  });

  g_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", "{\"ok\":true}");
    g_reboot = true;
  });

  g_ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) c->text(buildStatus());
  });
  g_server.addHandler(&g_ws);

  g_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  g_server.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "text/plain", "not found"); });
  g_server.begin();
}

void WebServer::loop(uint32_t nowMs) {
  static uint32_t last = 0;
  float hz = g_sys ? g_sys->cfg.system.telemetryHz : 10.0f;
  uint32_t period = (hz > 0.0f) ? (uint32_t)(1000.0f / hz) : 100;
  if (nowMs - last >= period) {
    last = nowMs;
    g_ws.cleanupClients();
    if (g_ws.count() > 0) g_ws.textAll(buildStatus());
  }
}

bool WebServer::rebootRequested() const { return g_reboot; }

}  // namespace harm
#endif  // HARM_ARDUINO
