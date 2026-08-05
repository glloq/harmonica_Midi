// ============================================================================
//  WebServer.cpp — serveur web asynchrone (ESP32 uniquement).
//  Voir WebServer.h pour le modèle sécurité/concurrence.
// ============================================================================
#include "web/WebServer.h"

#if HARM_ARDUINO
#include <Arduino.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace harm {

static AsyncWebServer g_server(80);
static AsyncWebSocket g_ws("/ws");
static System*        g_sys   = nullptr;
static ConfigStore*   g_store = nullptr;
static QueueHandle_t  g_cmdQueue = nullptr;
static bool           g_reboot = false;
static std::string    g_cfgBuf, g_calBuf, g_harmBuf;
static const size_t   kMaxBody = 8192;   // plafond dur des corps POST

static portMUX_TYPE   g_snapMux = portMUX_INITIALIZER_UNLOCKED;
static StatusSnapshot g_snap;

// ---- Auth (Basic) — désactivée si web.password vide ------------------------
static bool authed(AsyncWebServerRequest* req) {
  if (!g_sys || g_sys->cfg.web.password[0] == '\0') return true;
  return req->authenticate(g_sys->cfg.web.user, g_sys->cfg.web.password);
}
static bool requireAuth(AsyncWebServerRequest* req) {
  if (authed(req)) return true;
  req->requestAuthentication();
  return false;
}

// ---- File de commandes vers le Core 1 --------------------------------------
static bool enqueue(const WebCommand& c) {
  return g_cmdQueue && xQueueSend(g_cmdQueue, &c, 0) == pdTRUE;
}

// ---- Télémétrie (lecture de l'instantané, aucun I2C ici) -------------------
static String buildStatus() {
  StatusSnapshot s;
  portENTER_CRITICAL(&g_snapMux); s = g_snap; portEXIT_CRITICAL(&g_snapMux);
  JsonDocument d;
  d["homed"] = s.air.homed;
  d["ready"] = s.air.ready;
  d["pistonMm"] = s.air.pistonMm;
  d["pressureBlow"] = s.air.pressureBlow;
  d["pressureDraw"] = s.air.pressureDraw;
  d["assignmentGen"] = s.air.assignmentGen;
  d["setpointKpa"] = s.setpointKpa;
  d["voices"] = s.voices;
  d["mixedCapable"] = s.mixedCapable;
  d["pitchBend"] = s.pitchBend;
  d["modulation"] = s.modulation;
  d["transports"] = s.transports;
  d["mock"] = s.mock;
  d["harmonica"] = s.harmonica;
  d["droppedMidi"] = s.droppedMidi;
  d["freeHeap"] = (uint32_t)ESP.getFreeHeap();
  String out; serializeJson(d, out); return out;
}

void WebServer::publishSnapshot(const StatusSnapshot& s) {
  portENTER_CRITICAL(&g_snapMux); g_snap = s; portEXIT_CRITICAL(&g_snapMux);
}

// ---- Config masquée (jamais les secrets en clair) --------------------------
static String redactedConfig() {
  JsonDocument d;
  if (deserializeJson(d, g_store->raw())) return String("{}");
  if (!d["midi"]["wifi"]["password"].isNull())   d["midi"]["wifi"]["password"] = "";
  if (!d["midi"]["wifi"]["apPassword"].isNull()) d["midi"]["wifi"]["apPassword"] = "";
  if (!d["web"]["password"].isNull())            d["web"]["password"] = "";
  String out; serializeJson(d, out); return out;
}

// ---- Calibration -> commande Core 1 ----------------------------------------
static bool queueCalibrate(const char* json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  const char* target = d["target"] | "";
  WebCommand cmd{};
  if (!strcmp(target, "servo")) {
    cmd.channel = d["channel"] | 0;
    if (d["us"].is<int>()) { cmd.type = WebCommand::ServoUs; cmd.value = d["us"].as<int>(); }
    else                   { cmd.type = WebCommand::ServoDeg; cmd.value = d["deg"] | 90; }
  } else if (!strcmp(target, "piston")) {
    const char* action = d["action"] | "center";
    cmd.type = (!strcmp(action, "home")) ? WebCommand::PistonHome : WebCommand::PistonCenter;
  } else if (!strcmp(target, "pressureZero")) {
    cmd.type = WebCommand::PressureTare;
  } else return false;
  return enqueue(cmd);
}

// ---- Accumulation de corps bornée ------------------------------------------
static void appendBody(std::string& buf, uint8_t* data, size_t len, size_t index) {
  if (index == 0) buf.clear();
  if (buf.size() + len <= kMaxBody) buf.append((const char*)data, len);
}

// ---- Routes -----------------------------------------------------------------
void WebServer::begin(System* sys, ConfigStore* store, void* cmdQueue) {
  g_sys = sys; g_store = store; g_cmdQueue = (QueueHandle_t)cmdQueue; g_reboot = false;

  g_server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    req->send(200, "application/json", redactedConfig());   // secrets masqués
  });

  g_server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest* req) {                          // après corps complet
      if (!requireAuth(req)) { g_cfgBuf.clear(); return; }
      bool ok = !g_cfgBuf.empty() && g_store->save(g_cfgBuf.c_str());
      req->send(ok ? 200 : 400, "application/json",
                ok ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":false,\"error\":\"invalid or empty\"}");
      g_cfgBuf.clear();
    },
    nullptr,
    [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t) { appendBody(g_cfgBuf, data, len, index); });

  g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    req->send(200, "application/json", buildStatus());
  });

  g_server.on("/api/calibrate", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!requireAuth(req)) { g_calBuf.clear(); return; }
      bool ok = !g_calBuf.empty() && queueCalibrate(g_calBuf.c_str());
      req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
      g_calBuf.clear();
    },
    nullptr,
    [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t) { appendBody(g_calBuf, data, len, index); });

  g_server.on("/api/harmonica", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!requireAuth(req)) { g_harmBuf.clear(); return; }
      HarmonicaCfg* h = new HarmonicaCfg();
      bool ok = !g_harmBuf.empty() && ConfigStore::deserializeHarmonica(g_harmBuf.c_str(), *h) && h->noteCount > 0;
      if (ok && enqueue({WebCommand::SwapHarmonica, 0, 0, h})) {
        g_store->saveHarmonica(g_harmBuf.c_str());             // persistance (LittleFS)
      } else { delete h; ok = false; }
      req->send(ok ? 200 : 400, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"invalid or empty harmonica\"}");
      g_harmBuf.clear();
    },
    nullptr,
    [](AsyncWebServerRequest*, uint8_t* data, size_t len, size_t index, size_t) { appendBody(g_harmBuf, data, len, index); });

  g_server.on("/api/harmonicas", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    JsonDocument d; JsonArray arr = d.to<JsonArray>();
    File dir = LittleFS.open("/presets");
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String n = f.name();
        const char* c = n.c_str();
        const char* base = strrchr(c, '/');
        arr.add(base ? base + 1 : c);                          // basename stable (quel que soit le core)
      }
    }
    String out; serializeJson(d, out);
    req->send(200, "application/json", out);
  });

  g_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    req->send(200, "application/json", "{\"ok\":true}");
    g_reboot = true;
  });

  // Ne jamais servir les fichiers de config en clair (fuite de secrets).
  auto forbid = [](AsyncWebServerRequest* req) { req->send(403, "text/plain", "forbidden"); };
  g_server.on("/config.json", HTTP_GET, forbid);
  g_server.on("/config.tmp", HTTP_GET, forbid);

  g_ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) c->text(buildStatus());        // snapshot (pas d'I2C)
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
