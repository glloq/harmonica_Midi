// ============================================================================
//  ConfigStore.cpp — (dé)sérialisation JSON <-> Config + persistance LittleFS.
//  La partie deserialize() est pure (ArduinoJson) et compile aussi sur PC.
// ============================================================================
#include "web/ConfigStore.h"
#include "web/DefaultConfig.h"
#define ARDUINOJSON_ENABLE_STD_STRING 1   // garantit serializeJson(doc, std::string) quel que soit l'ordre d'include
#include <ArduinoJson.h>
#include <cstring>
#include <cstdlib>

namespace harm {

// ---- Helpers locaux ---------------------------------------------------------
static void copyStr(char* d, size_t n, const char* s) {
  if (!s) { d[0] = '\0'; return; }
  if (d == s) return;              // même buffer (clé JSON absente) : évite un strncpy recouvrant (UB)
  std::strncpy(d, s, n - 1); d[n - 1] = '\0';
}
static uint8_t toU8Hex(JsonVariantConst v, uint8_t def) {
  if (v.is<const char*>()) { const char* s = v.as<const char*>(); return s ? (uint8_t)strtol(s, nullptr, 0) : def; }
  if (v.is<int>()) return (uint8_t)v.as<int>();
  return def;
}
static Direction parseDir(const char* s) {
  if (!s) return Direction::Blow;
  if (!std::strcmp(s, "draw")) return Direction::Draw;
  if (!std::strcmp(s, "closed")) return Direction::Closed;
  return Direction::Blow;
}
static Wireless parseWireless(const char* s) {
  if (s && !std::strcmp(s, "wifi")) return Wireless::Wifi;
  if (s && !std::strcmp(s, "ble")) return Wireless::Ble;
  return Wireless::None;
}
static AirImpl parseAirImpl(const char* s) {
  return (s && !std::strcmp(s, "singleBellows")) ? AirImpl::SingleBellows : AirImpl::DualReservoirPiston;
}
static ValveImpl parseValveImpl(const char* s) {
  return (s && !std::strcmp(s, "valve1in1")) ? ValveImpl::Valve1in1 : ValveImpl::Valve2in1;
}
static PressureType parsePressure(const char* s) {
  return (s && !std::strcmp(s, "mpx2010")) ? PressureType::Mpx2010 : PressureType::Bmp280;
}
static Arbitration parseArb(const char* s) {
  return (s && !std::strcmp(s, "steal")) ? Arbitration::Steal : Arbitration::Reject;
}

// Remplit une HarmonicaCfg depuis un objet JSON "harmonica" (réutilisé par la
// config complète ET par le chargement d'un preset autonome).
static void parseHarmonicaInto(JsonVariantConst h, HarmonicaCfg& hc) {
  hc = HarmonicaCfg{};
  copyStr(hc.name, sizeof(hc.name), h["name"] | hc.name);
  hc.holeCount = h["holeCount"] | hc.holeCount;
  hc.hasSlide  = h["hasSlide"] | hc.hasSlide;
  uint16_t n = 0;
  for (JsonVariantConst e : h["notes"].as<JsonArrayConst>()) {
    if (n >= MAX_NOTE_ENTRIES) break;
    auto& N = hc.notes[n];
    N.note = e["note"] | 0;
    N.hole = e["hole"] | 0;
    N.direction = parseDir(e["dir"] | "blow");
    N.slide = e["slide"] | false;
    N.intensityScale = e["intensityScale"] | 1.0f;
    N.bendSemitones = e["bend"] | 0.0f;
    ++n;
  }
  hc.noteCount = n;
}

// ---- JSON -> Config ---------------------------------------------------------
bool ConfigStore::deserialize(const char* json, Config& c) {
  JsonDocument doc;
  if (::deserializeJson(doc, json)) return false;
  JsonObjectConst r = doc.as<JsonObjectConst>();
  c = Config{};                                  // repart des valeurs par défaut
  c.version = r["version"] | 1;

  // -- board --
  c.board.sda = r["board"]["i2c"]["sda"] | c.board.sda;
  c.board.scl = r["board"]["i2c"]["scl"] | c.board.scl;
  c.board.i2cFreq = r["board"]["i2c"]["freqHz"] | c.board.i2cFreq;
  c.board.stepPin = r["board"]["stepper"]["step"] | c.board.stepPin;
  c.board.dirPin = r["board"]["stepper"]["dir"] | c.board.dirPin;
  c.board.enablePin = r["board"]["stepper"]["enable"] | c.board.enablePin;
  c.board.invertEnable = r["board"]["stepper"]["invertEnable"] | c.board.invertEnable;
  c.board.endstopR1 = r["board"]["endstops"]["r1"] | c.board.endstopR1;
  c.board.endstopR2 = r["board"]["endstops"]["r2"] | c.board.endstopR2;
  c.board.endstopActiveLow = r["board"]["endstops"]["activeLow"] | c.board.endstopActiveLow;
  c.board.statusLed = r["board"]["statusLed"] | c.board.statusLed;

  // -- midi --
  c.midi.activeWireless = parseWireless(r["midi"]["activeWireless"] | "none");
  c.midi.channel = r["midi"]["channel"] | c.midi.channel;
  c.midi.serial.enabled = r["midi"]["serial"]["enabled"] | c.midi.serial.enabled;
  c.midi.serial.rxPin = r["midi"]["serial"]["rxPin"] | c.midi.serial.rxPin;
  c.midi.serial.txPin = r["midi"]["serial"]["txPin"] | c.midi.serial.txPin;
  c.midi.serial.baud = r["midi"]["serial"]["baud"] | c.midi.serial.baud;
  copyStr(c.midi.ble.deviceName, sizeof(c.midi.ble.deviceName), r["midi"]["ble"]["deviceName"] | c.midi.ble.deviceName);
  copyStr(c.midi.wifi.mode, sizeof(c.midi.wifi.mode), r["midi"]["wifi"]["mode"] | c.midi.wifi.mode);
  copyStr(c.midi.wifi.ssid, sizeof(c.midi.wifi.ssid), r["midi"]["wifi"]["ssid"] | c.midi.wifi.ssid);
  copyStr(c.midi.wifi.password, sizeof(c.midi.wifi.password), r["midi"]["wifi"]["password"] | c.midi.wifi.password);
  copyStr(c.midi.wifi.apPassword, sizeof(c.midi.wifi.apPassword), r["midi"]["wifi"]["apPassword"] | c.midi.wifi.apPassword);
  copyStr(c.midi.wifi.sessionName, sizeof(c.midi.wifi.sessionName), r["midi"]["wifi"]["sessionName"] | c.midi.wifi.sessionName);
  c.midi.wifi.rtpPort = r["midi"]["wifi"]["rtpPort"] | c.midi.wifi.rtpPort;

  // -- air --
  c.air.impl = parseAirImpl(r["air"]["impl"] | "dualReservoirPiston");
  {
    JsonVariantConst d = r["air"]["dualReservoirPiston"];
    auto& x = c.air.dual;
    x.stepsPerMm = d["stepsPerMm"] | x.stepsPerMm;
    x.travelMm = d["travelMm"] | x.travelMm;
    x.centerMm = d["centerMm"] | x.centerMm;
    x.homeOnR1 = d["homeOnR1"] | x.homeOnR1;
    x.reversalMarginMm = d["reversalMarginMm"] | x.reversalMarginMm;
    x.maxSpeedMmS = d["maxSpeedMmS"] | x.maxSpeedMmS;
    x.accelMmS2 = d["accelMmS2"] | x.accelMmS2;
    x.pressureTargetKpa = d["pressureTargetKpa"] | x.pressureTargetKpa;
    x.pressureToleranceKpa = d["pressureToleranceKpa"] | x.pressureToleranceKpa;
    x.flowLpm = d["flowLpm"] | x.flowLpm;
    x.pressureKp = d["pressureKp"] | x.pressureKp;
    x.pressureKi = d["pressureKi"] | x.pressureKi;
    x.pressureType = parsePressure(d["pressureType"] | "bmp280");
    x.r1Addr = toU8Hex(d["r1Addr"], x.r1Addr);
    x.r2Addr = toU8Hex(d["r2Addr"], x.r2Addr);
    x.r1AdcPin = d["r1AdcPin"] | x.r1AdcPin;
    x.r2AdcPin = d["r2AdcPin"] | x.r2AdcPin;
    x.valveR1Channel = d["valveR1Channel"] | x.valveR1Channel;
    x.valveR2Channel = d["valveR2Channel"] | x.valveR2Channel;
    x.valveR1Open = d["valveR1Open"] | x.valveR1Open;
    x.valveR1Closed = d["valveR1Closed"] | x.valveR1Closed;
    x.valveR2Open = d["valveR2Open"] | x.valveR2Open;
    x.valveR2Closed = d["valveR2Closed"] | x.valveR2Closed;
  }
  {
    JsonVariantConst d = r["air"]["singleBellows"];
    auto& x = c.air.bellows;
    x.stepsPerMm = d["stepsPerMm"] | x.stepsPerMm;
    x.travelMm = d["travelMm"] | x.travelMm;
    x.centerMm = d["centerMm"] | x.centerMm;
    x.maxSpeedMmS = d["maxSpeedMmS"] | x.maxSpeedMmS;
    x.accelMmS2 = d["accelMmS2"] | x.accelMmS2;
    x.pressureTargetKpa = d["pressureTargetKpa"] | x.pressureTargetKpa;
    x.pressureToleranceKpa = d["pressureToleranceKpa"] | x.pressureToleranceKpa;
    x.flowLpm = d["flowLpm"] | x.flowLpm;
    x.pressureKp = d["pressureKp"] | x.pressureKp;
    x.pressureKi = d["pressureKi"] | x.pressureKi;
    x.pressureType = parsePressure(d["pressureType"] | "bmp280");
    x.addr = toU8Hex(d["addr"], x.addr);
    x.adcPin = d["adcPin"] | x.adcPin;
  }

  // -- valve --
  c.valve.impl = parseValveImpl(r["valve"]["impl"] | "valve2in1");
  c.valve.pca.addr = toU8Hex(r["valve"]["pca9685"]["addr"], c.valve.pca.addr);
  c.valve.pca.freqHz = r["valve"]["pca9685"]["freqHz"] | c.valve.pca.freqHz;
  c.valve.pca.oscHz = r["valve"]["pca9685"]["oscHz"] | c.valve.pca.oscHz;
  c.valve.servoUs.min = r["valve"]["servoUs"]["min"] | c.valve.servoUs.min;
  c.valve.servoUs.max = r["valve"]["servoUs"]["max"] | c.valve.servoUs.max;
  uint8_t h2 = 0, h1 = 0;
  for (JsonVariantConst e : r["valve"]["valve2in1"]["holes"].as<JsonArrayConst>()) {
    if (h2 >= MAX_HOLES) break;
    auto& H = c.valve.holes2[h2];
    H.hole = e["hole"] | 0; H.channel = e["channel"] | 0;
    H.railAangle = e["railAangle"] | 30; H.railBangle = e["railBangle"] | 150; H.closedAngle = e["closedAngle"] | 90;
    ++h2;
  }
  for (JsonVariantConst e : r["valve"]["valve1in1"]["holes"].as<JsonArrayConst>()) {
    if (h1 >= MAX_HOLES) break;
    auto& H = c.valve.holes1[h1];
    H.hole = e["hole"] | 0; H.channel = e["channel"] | 0;
    H.openAngle = e["openAngle"] | 90; H.closedAngle = e["closedAngle"] | 0;
    ++h1;
  }
  c.valve.holeCount = (c.valve.impl == ValveImpl::Valve2in1) ? h2 : h1;

  // -- slide --
  c.slide.enabled = r["slide"]["enabled"] | c.slide.enabled;
  c.slide.channel = r["slide"]["channel"] | c.slide.channel;
  c.slide.engagedAngle = r["slide"]["engagedAngle"] | c.slide.engagedAngle;
  c.slide.restAngle = r["slide"]["restAngle"] | c.slide.restAngle;

  // -- harmonica --
  parseHarmonicaInto(r["harmonica"], c.harmonica);

  // -- engine / system --
  c.engine.maxPolyphony = r["engine"]["maxPolyphony"] | c.engine.maxPolyphony;
  c.engine.arbitration = parseArb(r["engine"]["arbitration"] | "reject");
  c.engine.velocityToIntensity = r["engine"]["velocityToIntensity"] | c.engine.velocityToIntensity;
  c.engine.vibratoRateHz = r["engine"]["vibratoRateHz"] | c.engine.vibratoRateHz;
  c.engine.vibratoDepth = r["engine"]["vibratoDepth"] | c.engine.vibratoDepth;
  copyStr(c.web.user, sizeof(c.web.user), r["web"]["user"] | c.web.user);
  copyStr(c.web.password, sizeof(c.web.password), r["web"]["password"] | c.web.password);
  c.system.mockMode = r["system"]["mockMode"] | c.system.mockMode;
  c.system.telemetryHz = r["system"]["telemetryHz"] | c.system.telemetryHz;
  return true;
}

// JSON "harmonica" autonome (fichier preset) -> HarmonicaCfg.
bool ConfigStore::deserializeHarmonica(const char* json, HarmonicaCfg& out) {
  JsonDocument doc;
  if (::deserializeJson(doc, json)) return false;
  parseHarmonicaInto(doc.as<JsonVariantConst>(), out);
  return true;
}

// Remplace la section "harmonica" de la config courante par l'objet fourni,
// puis persiste (save() valide + écrit + recharge cfg_).
bool ConfigStore::saveHarmonica(const char* harmonicaJson) {
  JsonDocument doc, hdoc;
  if (::deserializeJson(doc, raw_.c_str())) return false;
  if (::deserializeJson(hdoc, harmonicaJson)) return false;
  doc["harmonica"] = hdoc.as<JsonObjectConst>();   // copie profonde du sous-arbre
  std::string out;
  serializeJson(doc, out);
  return save(out.c_str());
}

// ---- Persistance ------------------------------------------------------------
#if HARM_ARDUINO
#include <LittleFS.h>
static const char* kPath = "/config.json";
static const char* kTmp  = "/config.tmp";

bool ConfigStore::begin() {
  bool mounted = LittleFS.begin(true);
  raw_.clear();
  if (mounted && LittleFS.exists(kPath)) {
    File f = LittleFS.open(kPath, "r");
    if (f) { raw_ = std::string(f.readString().c_str()); f.close(); }
  }
  if (raw_.empty()) raw_ = kDefaultConfigJson();
  if (!deserialize(raw_.c_str(), cfg_)) { raw_ = kDefaultConfigJson(); deserialize(raw_.c_str(), cfg_); }
  return mounted;
}

bool ConfigStore::save(const char* json) {
  Config test;
  if (!deserialize(json, test)) return false;          // refuse un JSON invalide
  File f = LittleFS.open(kTmp, "w");
  if (!f) return false;
  f.print(json);
  f.close();
  LittleFS.remove(kPath);
  LittleFS.rename(kTmp, kPath);                          // rename atomique
  raw_ = json;
  cfg_ = test;
  return true;
}
#else   // cible native : pas de FS, on part du défaut embarqué
bool ConfigStore::begin() { raw_ = kDefaultConfigJson(); return deserialize(raw_.c_str(), cfg_); }
bool ConfigStore::save(const char* json) {
  Config t; if (!deserialize(json, t)) return false;
  raw_ = json; cfg_ = t; return true;
}
#endif

}  // namespace harm
