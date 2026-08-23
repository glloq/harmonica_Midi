// ============================================================================
//  WebServer.cpp — serveur web asynchrone (ESP32 uniquement).
//  Voir WebServer.h pour le modèle sécurité/concurrence.
//   - mutations -> file de commandes Core 1 ; télémétrie <- instantané Core 1.
//   - corps POST accumulés PAR REQUÊTE (req->_tempObject), plafonnés.
//   - télémétrie par polling (/api/status) : pas de push WS cross-tâche.
// ============================================================================
#include "web/WebServer.h"

#if HARM_ARDUINO
#include <Arduino.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#define ARDUINOJSON_ENABLE_STD_STRING 1
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string>

namespace harm {

static AsyncWebServer g_server(80);
static System*        g_sys   = nullptr;
static ConfigStore*   g_store = nullptr;
static QueueHandle_t  g_cmdQueue = nullptr;
static bool           g_reboot = false;
static const size_t   kMaxBody = 8192;   // plafond dur des corps POST

static portMUX_TYPE   g_snapMux = portMUX_INITIALIZER_UNLOCKED;
static StatusSnapshot g_snap;

// ---- Auth (Basic) — désactivée si web.password vide ------------------------
static bool requireAuth(AsyncWebServerRequest* req) {
  if (!g_sys || g_sys->cfg.web.password[0] == '\0') return true;
  if (req->authenticate(g_sys->cfg.web.user, g_sys->cfg.web.password)) return true;
  req->requestAuthentication();
  return false;
}

// ---- Corps POST accumulé par requête (pas de buffer statique partagé) ------
static void bodyAccum(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (index == 0) req->_tempObject = (total <= kMaxBody) ? new std::string() : nullptr;
  auto* buf = static_cast<std::string*>(req->_tempObject);
  if (buf && buf->size() + len <= kMaxBody) buf->append((const char*)data, len);
}
static std::string takeBody(AsyncWebServerRequest* req) {
  auto* buf = static_cast<std::string*>(req->_tempObject);
  std::string out = buf ? *buf : std::string();
  delete buf; req->_tempObject = nullptr;
  return out;
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
static bool queueCalibrate(const std::string& json) {
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

// ---- Routes -----------------------------------------------------------------
void WebServer::begin(System* sys, ConfigStore* store, void* cmdQueue) {
  g_sys = sys; g_store = store; g_cmdQueue = (QueueHandle_t)cmdQueue; g_reboot = false;

  g_server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    req->send(200, "application/json", redactedConfig());   // secrets masqués
  });

  g_server.on("/api/config", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!requireAuth(req)) { takeBody(req); return; }
      std::string body = takeBody(req);
      bool ok = !body.empty() && g_store->save(body.c_str());
      req->send(ok ? 200 : 400, "application/json",
                ok ? "{\"ok\":true,\"reboot\":true}" : "{\"ok\":false,\"error\":\"invalid or empty\"}");
    },
    nullptr, bodyAccum);

  g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    req->send(200, "application/json", buildStatus());
  });

  g_server.on("/api/calibrate", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!requireAuth(req)) { takeBody(req); return; }
      std::string body = takeBody(req);
      bool ok = !body.empty() && queueCalibrate(body);
      req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    },
    nullptr, bodyAccum);

  g_server.on("/api/harmonica", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!requireAuth(req)) { takeBody(req); return; }
      std::string body = takeBody(req);
      HarmonicaCfg* h = new HarmonicaCfg();
      bool ok = !body.empty() && ConfigStore::deserializeHarmonica(body.c_str(), *h) && h->noteCount > 0;
      if (ok && enqueue({WebCommand::SwapHarmonica, 0, 0, h})) {
        g_store->saveHarmonica(body.c_str());               // persistance (LittleFS)
      } else { delete h; ok = false; }
      req->send(ok ? 200 : 400, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"invalid or empty harmonica\"}");
    },
    nullptr, bodyAccum);

  g_server.on("/api/harmonicas", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    JsonDocument d; JsonArray arr = d.to<JsonArray>();
    File dir = LittleFS.open("/presets");
    if (dir && dir.isDirectory()) {
      for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String n = f.name();
        const char* c = n.c_str();
        const char* base = strrchr(c, '/');
        arr.add(base ? base + 1 : c);                          // basename stable
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

  g_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  g_server.onNotFound([](AsyncWebServerRequest* req) { req->send(404, "text/plain", "not found"); });
  g_server.begin();
}

// Télémétrie par polling côté UI (/api/status) : rien à pousser ici, on évite
// toute mutation d'AsyncWebSocket depuis une tâche étrangère.
void WebServer::loop(uint32_t) {}

bool WebServer::rebootRequested() const { return g_reboot; }

}  // namespace harm
#endif  // HARM_ARDUINO
