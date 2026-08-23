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
  if (!s) return AirImpl::DualReservoirPiston;
  if (!std::strcmp(s, "singleBellows"))        return AirImpl::SingleBellows;
  if (!std::strcmp(s, "pumpPair"))             return AirImpl::PumpPair;
  if (!std::strcmp(s, "singlePumpReversible")) return AirImpl::SinglePumpReversible;
  return AirImpl::DualReservoirPiston;
}
static ValveImpl parseValveImpl(const char* s) {
  if (!s) return ValveImpl::Valve2in1;
  if (!std::strcmp(s, "valve1in1"))    return ValveImpl::Valve1in1;
  if (!std::strcmp(s, "solenoid2in1")) return ValveImpl::Solenoid2in1;
  if (!std::strcmp(s, "solenoid1in1")) return ValveImpl::Solenoid1in1;
  return ValveImpl::Valve2in1;
}
static SlideImpl parseSlideImpl(const char* s) {
  return (s && !std::strcmp(s, "solenoid")) ? SlideImpl::Solenoid : SlideImpl::Servo;
}
static DigitalBusImpl parseBusImpl(const char* s) {
  return (s && !std::strcmp(s, "gpio")) ? DigitalBusImpl::Gpio : DigitalBusImpl::Pca9685;
}
static PumpDrive parsePumpDrive(const char* s) {
  if (s && !std::strcmp(s, "esc"))     return PumpDrive::Esc;
  if (s && !std::strcmp(s, "pca9685")) return PumpDrive::Pca9685;
  return PumpDrive::Ledc;
}
static DiverterImpl parseDiverter(const char* s) {
  return (s && !std::strcmp(s, "solenoid")) ? DiverterImpl::Solenoid : DiverterImpl::Servo;
}
static PressureType parsePressure(const char* s) {
  return (s && !std::strcmp(s, "mpx2010")) ? PressureType::Mpx2010 : PressureType::Bmp280;
}
static Arbitration parseArb(const char* s) {
  return (s && !std::strcmp(s, "steal")) ? Arbitration::Steal : Arbitration::Reject;
}

// Sous-objets réutilisés par plusieurs sources d'air.
static void parseSensorInto(JsonVariantConst v, PressureSensorCfg& x) {
  x.type = parsePressure(v["type"] | "bmp280");
  x.addr = toU8Hex(v["addr"], x.addr);
  x.adcPin = v["adcPin"] | x.adcPin;
  x.kpaPerCount = v["kpaPerCount"] | x.kpaPerCount;
}
static void parsePumpInto(JsonVariantConst v, PumpCfg& x) {
  x.drive = parsePumpDrive(v["drive"] | "ledc");
  x.pin = v["pin"] | x.pin;
  x.channel = v["channel"] | x.channel;
  x.freqHz = v["freqHz"] | x.freqHz;
  x.escMinUs = v["escMinUs"] | x.escMinUs;
  x.escMaxUs = v["escMaxUs"] | x.escMaxUs;
  x.minDuty = v["minDuty"] | x.minDuty;
  x.maxDuty = v["maxDuty"] | x.maxDuty;
  x.invert = v["invert"] | x.invert;
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
  c.version = r["version"] | c.version;

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
  {
    JsonVariantConst d = r["air"]["pumpPair"];
    auto& x = c.air.pumps;
    parsePumpInto(d["blowPump"], x.blowPump);
    parsePumpInto(d["drawPump"], x.drawPump);
    parseSensorInto(d["blowSensor"], x.blowSensor);
    parseSensorInto(d["drawSensor"], x.drawSensor);
    if (x.drawSensor.addr == 0x76 && d["drawSensor"]["addr"].isNull()) x.drawSensor.addr = 0x77;
    x.sharedSensor = d["sharedSensor"] | x.sharedSensor;
    x.pressureTargetKpa = d["pressureTargetKpa"] | x.pressureTargetKpa;
    x.pressureToleranceKpa = d["pressureToleranceKpa"] | x.pressureToleranceKpa;
    x.pressureKp = d["pressureKp"] | x.pressureKp;
    x.pressureKi = d["pressureKi"] | x.pressureKi;
    x.idleDuty = d["idleDuty"] | x.idleDuty;
    x.spinUpMs = d["spinUpMs"] | x.spinUpMs;
    x.bleedChannel = d["bleedChannel"] | x.bleedChannel;
    x.bleedOpenAngle = d["bleedOpenAngle"] | x.bleedOpenAngle;
    x.bleedClosedAngle = d["bleedClosedAngle"] | x.bleedClosedAngle;
  }
  {
    JsonVariantConst d = r["air"]["singlePumpReversible"];
    auto& x = c.air.singlePump;
    parsePumpInto(d["pump"], x.pump);
    parseSensorInto(d["sensor"], x.sensor);
    x.diverter = parseDiverter(d["diverter"] | "servo");
    x.diverterChannel = d["diverterChannel"] | x.diverterChannel;
    x.blowAngle = d["blowAngle"] | x.blowAngle;
    x.drawAngle = d["drawAngle"] | x.drawAngle;
    x.neutralAngle = d["neutralAngle"] | x.neutralAngle;
    x.solenoidBlowState = d["solenoidBlowState"] | x.solenoidBlowState;
    x.switchMs = d["switchMs"] | x.switchMs;
    x.pressureTargetKpa = d["pressureTargetKpa"] | x.pressureTargetKpa;
    x.pressureToleranceKpa = d["pressureToleranceKpa"] | x.pressureToleranceKpa;
    x.pressureKp = d["pressureKp"] | x.pressureKp;
    x.pressureKi = d["pressureKi"] | x.pressureKi;
    x.idleDuty = d["idleDuty"] | x.idleDuty;
  }

  // -- valve --
  c.valve.impl = parseValveImpl(r["valve"]["impl"] | "valve2in1");
  c.valve.pca.addr = toU8Hex(r["valve"]["pca9685"]["addr"], c.valve.pca.addr);
  c.valve.pca.freqHz = r["valve"]["pca9685"]["freqHz"] | c.valve.pca.freqHz;
  c.valve.pca.oscHz = r["valve"]["pca9685"]["oscHz"] | c.valve.pca.oscHz;
  c.valve.servoUs.min = r["valve"]["servoUs"]["min"] | c.valve.servoUs.min;
  c.valve.servoUs.max = r["valve"]["servoUs"]["max"] | c.valve.servoUs.max;
  c.valve.settleMs = r["valve"]["settleMs"] | c.valve.settleMs;
  {
    JsonVariantConst d = r["valve"]["solenoids"];
    auto& x = c.valve.solenoids;
    x.impl = parseBusImpl(d["impl"] | "pca9685");
    x.pcaAddr = toU8Hex(d["pcaAddr"], x.pcaAddr);
    x.pcaFreqHz = d["pcaFreqHz"] | x.pcaFreqHz;
    x.activeLow = d["activeLow"] | x.activeLow;
    x.holdDuty = d["holdDuty"] | x.holdDuty;
    x.peakMs = d["peakMs"] | x.peakMs;
    uint8_t n = 0;
    for (JsonVariantConst pin : d["gpioPins"].as<JsonArrayConst>()) {
      if (n >= MAX_OUTPUTS) break;
      x.gpioPins[n++] = pin.as<int>();
    }
    x.gpioCount = n;
  }
  uint8_t h2 = 0, h1 = 0, s2 = 0, s1 = 0;
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
  for (JsonVariantConst e : r["valve"]["solenoid2in1"]["holes"].as<JsonArrayConst>()) {
    if (s2 >= MAX_HOLES) break;
    auto& H = c.valve.holesS2[s2];
    H.hole = e["hole"] | 0;
    H.railAchannel = e["railAchannel"] | 0;
    H.railBchannel = e["railBchannel"] | 0;
    ++s2;
  }
  for (JsonVariantConst e : r["valve"]["solenoid1in1"]["holes"].as<JsonArrayConst>()) {
    if (s1 >= MAX_HOLES) break;
    auto& H = c.valve.holesS1[s1];
    H.hole = e["hole"] | 0; H.channel = e["channel"] | 0;
    ++s1;
  }
  switch (c.valve.impl) {                       // le nombre de trous suit l'impl choisie
    case ValveImpl::Valve2in1:    c.valve.holeCount = h2; break;
    case ValveImpl::Valve1in1:    c.valve.holeCount = h1; break;
    case ValveImpl::Solenoid2in1: c.valve.holeCount = s2; break;
    case ValveImpl::Solenoid1in1: c.valve.holeCount = s1; break;
  }

  // -- slide --
  c.slide.enabled = r["slide"]["enabled"] | c.slide.enabled;
  c.slide.impl = parseSlideImpl(r["slide"]["impl"] | "servo");
  c.slide.channel = r["slide"]["channel"] | c.slide.channel;
  c.slide.engagedAngle = r["slide"]["engagedAngle"] | c.slide.engagedAngle;
  c.slide.restAngle = r["slide"]["restAngle"] | c.slide.restAngle;
  c.slide.settleMs = r["slide"]["settleMs"] | c.slide.settleMs;

  // -- harmonica --
  parseHarmonicaInto(r["harmonica"], c.harmonica);

  // -- engine / system --
  c.engine.maxPolyphony = r["engine"]["maxPolyphony"] | c.engine.maxPolyphony;
  c.engine.arbitration = parseArb(r["engine"]["arbitration"] | "reject");
  c.engine.velocityToIntensity = r["engine"]["velocityToIntensity"] | c.engine.velocityToIntensity;
  c.engine.vibratoRateHz = r["engine"]["vibratoRateHz"] | c.engine.vibratoRateHz;
  c.engine.vibratoDepth = r["engine"]["vibratoDepth"] | c.engine.vibratoDepth;
  c.engine.ccVolumeEnabled = r["engine"]["ccVolumeEnabled"] | c.engine.ccVolumeEnabled;
  c.engine.bendEnabled = r["engine"]["bendEnabled"] | c.engine.bendEnabled;
  c.engine.bendPressureGain = r["engine"]["bendPressureGain"] | c.engine.bendPressureGain;
  c.engine.pitchBendRangeSemitones = r["engine"]["pitchBendRangeSemitones"] | c.engine.pitchBendRangeSemitones;
  c.engine.minIntensity = r["engine"]["minIntensity"] | c.engine.minIntensity;
  c.engine.holeSettleMs = r["engine"]["holeSettleMs"] | c.engine.holeSettleMs;
  c.engine.noteMaxHoldMs = r["engine"]["noteMaxHoldMs"] | c.engine.noteMaxHoldMs;
  copyStr(c.web.user, sizeof(c.web.user), r["web"]["user"] | c.web.user);
  copyStr(c.web.password, sizeof(c.web.password), r["web"]["password"] | c.web.password);
  c.system.mockMode = r["system"]["mockMode"] | c.system.mockMode;
  c.system.telemetryHz = r["system"]["telemetryHz"] | c.system.telemetryHz;
  c.system.autoHomeOnBoot = r["system"]["autoHomeOnBoot"] | c.system.autoHomeOnBoot;
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
