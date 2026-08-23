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
  d["dutyBlow"] = s.air.dutyBlow;
  d["dutyDraw"] = s.air.dutyDraw;
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
  d["holeBlowMask"] = s.holeBlowMask;
  d["holeDrawMask"] = s.holeDrawMask;
  d["holeCount"] = s.holeCount;
  d["airImpl"] = s.airImpl;
  d["valveImpl"] = s.valveImpl;
  d["slidePresent"] = s.slidePresent;
  d["slideEngaged"] = s.slideEngaged;
  JsonObject caps = d["caps"].to<JsonObject>();
  caps["simultaneous"] = s.caps.simultaneous;
  caps["hasPiston"] = s.caps.hasPiston;
  caps["needsHoming"] = s.caps.needsHoming;
  caps["railsSwap"] = s.caps.railsSwap;
  caps["hasPumps"] = s.caps.hasPumps;
  caps["hasDiverter"] = s.caps.hasDiverter;
  caps["pressureSensors"] = s.caps.pressureSensors;
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

// ---- Fusion des secrets à la sauvegarde ------------------------------------
// Un secret : "web"/"password" ou "midi"/"wifi"/"apPassword" (k3 optionnel).
static void keepSecret(JsonDocument& in, JsonDocument& cur,
                       const char* k1, const char* k2, const char* k3) {
  JsonObject o = in[k1].is<JsonObject>() ? in[k1].as<JsonObject>() : in[k1].to<JsonObject>();
  const char* leaf = k2;
  if (k3) {
    o = o[k2].is<JsonObject>() ? o[k2].as<JsonObject>() : o[k2].to<JsonObject>();
    leaf = k3;
  }
  const char* v = o[leaf].is<const char*>() ? o[leaf].as<const char*>() : nullptr;
  if (v && !strcmp(v, "__clear__")) { o[leaf] = ""; return; }   // effacement explicite
  if (v && v[0] != '\0') return;                                // nouvelle valeur saisie
  // .as<>() explicite : les deux branches d'un ternaire doivent avoir le MÊME
  // type, or cur[a][b][c] et cur[a][b] sont deux MemberProxy différents.
  JsonVariantConst c = k3 ? cur[k1][k2][k3].as<JsonVariantConst>()
                          : cur[k1][k2].as<JsonVariantConst>();      // sinon : on conserve
  o[leaf] = c.is<const char*>() ? c.as<const char*>() : "";
}

// GET /api/config masque les mots de passe : les renvoyer tels quels EFFACERAIT
// le WiFi à chaque enregistrement depuis l'UI. Contrat avec app.js :
//   clé absente ou chaîne vide -> valeur stockée conservée ;
//   "__clear__"                -> effacement explicite.
static bool mergeSecrets(const std::string& body, std::string& out) {
  JsonDocument incoming, current;
  if (deserializeJson(incoming, body)) return false;
  if (deserializeJson(current, g_store->raw())) current.to<JsonObject>();
  keepSecret(incoming, current, "midi", "wifi", "password");
  keepSecret(incoming, current, "midi", "wifi", "apPassword");
  keepSecret(incoming, current, "web", "password", nullptr);
  out.clear();
  serializeJson(incoming, out);       // `current` reste vivant : les chaînes conservées aussi
  return true;
}

// ---- Capacités du firmware (contrat UI <-> firmware) -----------------------
//  Liste les implémentations réellement compilées et leurs contraintes ; l'UI
//  s'en sert pour ne proposer (et n'exiger) que ce qui existe.
static String buildCapabilities() {
  JsonDocument d;
  d["configVersion"] = 2;
  d["maxHoles"] = MAX_HOLES;
  d["maxNoteEntries"] = MAX_NOTE_ENTRIES;
  d["maxOutputs"] = MAX_OUTPUTS;

  JsonArray air = d["air"].to<JsonArray>();
  auto addAir = [&](const char* id, const char* label, bool simultaneous, bool stepper,
                    bool pumps, bool homing, uint8_t sensors) {
    JsonObject o = air.add<JsonObject>();
    o["id"] = id; o["label"] = label; o["simultaneous"] = simultaneous;
    o["stepper"] = stepper; o["pumps"] = pumps; o["homing"] = homing; o["sensors"] = sensors;
  };
  addAir("dualReservoirPiston", "Verin double (2 reservoirs + piston)", true,  true,  false, true,  2);
  addAir("singleBellows",       "Soufflet simple motorise",             false, true,  false, false, 1);
  addAir("pumpPair",            "Deux pompes continues opposees",        true,  false, true,  false, 2);
  addAir("singlePumpReversible","Une pompe + aiguillage",                false, false, true,  false, 1);

  JsonArray valve = d["valve"].to<JsonArray>();
  auto addValve = [&](const char* id, const char* label, bool perHole, const char* actuator,
                      uint8_t channels) {
    JsonObject o = valve.add<JsonObject>();
    o["id"] = id; o["label"] = label; o["perHoleDirection"] = perHole;
    o["actuator"] = actuator; o["channelsPerHole"] = channels;
  };
  addValve("valve2in1",    "Servo 2 entrees -> 1 sortie", true,  "servo",    1);
  addValve("valve1in1",    "Servo porte on/off",          false, "servo",    1);
  addValve("solenoid2in1", "2 electro-vannes par trou",   true,  "solenoid", 2);
  addValve("solenoid1in1", "1 electro-vanne par trou",    false, "solenoid", 1);

  JsonArray slide = d["slide"].to<JsonArray>();
  slide.add("servo"); slide.add("solenoid");
  JsonArray drives = d["pumpDrives"].to<JsonArray>();
  drives.add("ledc"); drives.add("esc"); drives.add("pca9685");
  JsonArray buses = d["digitalBuses"].to<JsonArray>();
  buses.add("pca9685"); buses.add("gpio");
  JsonArray press = d["pressureTypes"].to<JsonArray>();
  press.add("bmp280"); press.add("mpx2010");
  JsonArray wireless = d["wireless"].to<JsonArray>();
  wireless.add("none"); wireless.add("wifi"); wireless.add("ble");

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
  } else if (!strcmp(target, "solenoid")) {
    cmd.type = WebCommand::SolenoidSet;
    cmd.channel = d["channel"] | 0;
    cmd.value = (d["on"] | false) ? 1 : 0;
  } else if (!strcmp(target, "hole")) {
    cmd.type = WebCommand::HoleState;
    cmd.channel = d["hole"] | 0;
    const char* dir = d["dir"] | "closed";
    cmd.value = !strcmp(dir, "blow") ? 1 : (!strcmp(dir, "draw") ? 2 : 0);
  } else if (!strcmp(target, "pump")) {
    cmd.type = WebCommand::PumpDuty;
    const char* dir = d["dir"] | "blow";
    cmd.channel = !strcmp(dir, "draw") ? 2 : 1;
    cmd.value = d["duty"].is<int>() ? d["duty"].as<int>() : -1;   // % ; absent/-1 = retour auto
  } else if (!strcmp(target, "slide")) {
    cmd.type = WebCommand::SlideSet;
    cmd.value = (d["engaged"] | false) ? 1 : 0;
  } else if (!strcmp(target, "allOff")) {
    cmd.type = WebCommand::AllOff;
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
      std::string merged;
      bool ok = !body.empty() && mergeSecrets(body, merged) && g_store->save(merged.c_str());
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

  // Banc d'essai : joue une note comme si elle venait du MIDI (même chemin).
  g_server.on("/api/test", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      if (!requireAuth(req)) { takeBody(req); return; }
      std::string body = takeBody(req);
      JsonDocument d;
      bool ok = !body.empty() && !deserializeJson(d, body);
      if (ok) {
        const char* action = d["action"] | "";
        WebCommand cmd{};
        if (!strcmp(action, "noteOn")) {
          cmd.type = WebCommand::TestNoteOn;
          cmd.channel = d["note"] | 60;
          int v = d["velocity"] | 100; cmd.value = (v < 1) ? 1 : (v > 127 ? 127 : v);
        } else if (!strcmp(action, "noteOff")) {
          cmd.type = WebCommand::TestNoteOff; cmd.channel = d["note"] | 60;
        } else if (!strcmp(action, "allOff")) {
          cmd.type = WebCommand::AllOff;
        } else ok = false;
        ok = ok && enqueue(cmd);
      }
      req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    },
    nullptr, bodyAccum);

  // Ce que CE firmware sait piloter : l'UI n'affiche que des options réelles.
  g_server.on("/api/capabilities", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!requireAuth(req)) return;
    req->send(200, "application/json", buildCapabilities());
  });

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
