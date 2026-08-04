// ============================================================================
//  Config.h — modèle de configuration (structures C++ pures).
//
//  IMPORTANT : ce fichier ne dépend PAS d'ArduinoJson. La (dé)sérialisation
//  JSON vit uniquement dans ConfigStore (cible ESP32). La logique métier
//  (HarmonicaMap, NoteEngine, air, valve) travaille sur ces structs => elle
//  reste testable sur PC sans JSON.
// ============================================================================
#pragma once
#include "Types.h"
#include <cstring>

namespace harm {

// ---- Carte / brochage -------------------------------------------------------
struct BoardCfg {
  int      sda = 21, scl = 22;
  uint32_t i2cFreq = 400000;
  int      stepPin = 26, dirPin = 27, enablePin = 25;
  bool     invertEnable = true;          // DRV8825/TMC : EN actif bas
  int      endstopR1 = 32, endstopR2 = 33;
  bool     endstopActiveLow = true;
  int      statusLed = 2;
};

// ---- MIDI -------------------------------------------------------------------
enum class Wireless : uint8_t { None = 0, Wifi = 1, Ble = 2 };

struct SerialMidiCfg { bool enabled = true; int rxPin = 16, txPin = 17; uint32_t baud = 31250; };
struct BleMidiCfg    { char deviceName[32] = "HarmonicaMIDI"; };
struct WifiCfg {
  char     mode[8]     = "sta";          // "sta" | "ap"
  char     ssid[33]    = "";
  char     password[65] = "";
  char     apPassword[65] = "harmonica";
  char     sessionName[32] = "Harmonica";
  uint16_t rtpPort = 5004;
};
struct MidiCfg {
  Wireless activeWireless = Wireless::None;
  uint8_t  channel = 0;                   // 0 = OMNI ; 1..16 = filtre sur ce canal
  SerialMidiCfg serial;
  BleMidiCfg    ble;
  WifiCfg       wifi;
};

// ---- Source d'air -----------------------------------------------------------
enum class AirImpl : uint8_t { DualReservoirPiston = 0, SingleBellows = 1 };
enum class PressureType : uint8_t { Bmp280 = 0, Mpx2010 = 1 };

struct DualReservoirCfg {
  float stepsPerMm = 80.0f, travelMm = 300.0f, centerMm = 150.0f;
  bool  homeOnR1 = true;                  // homing vers l'endstop R1 (sinon R2)
  float reversalMarginMm = 20.0f;
  float maxSpeedMmS = 40.0f, accelMmS2 = 200.0f;
  float pressureTargetKpa = 0.30f, pressureToleranceKpa = 0.05f, flowLpm = 12.0f;
  float pressureKp = 4.0f, pressureKi = 0.5f;   // régulation PI de pression
  PressureType pressureType = PressureType::Bmp280;
  uint8_t r1Addr = 0x76, r2Addr = 0x77;   // BMP280
  int   r1AdcPin = 34, r2AdcPin = 35;     // MPX2010 (analogique)
  uint8_t valveR1Channel = 12, valveR2Channel = 13;
  int   valveR1Open = 90, valveR1Closed = 0, valveR2Open = 90, valveR2Closed = 0;
};

struct BellowsCfg {
  float stepsPerMm = 80.0f, travelMm = 200.0f, centerMm = 100.0f;
  float maxSpeedMmS = 40.0f, accelMmS2 = 200.0f;
  float pressureTargetKpa = 0.30f, pressureToleranceKpa = 0.05f, flowLpm = 12.0f;
  float pressureKp = 4.0f, pressureKi = 0.5f;   // régulation PI de pression
  PressureType pressureType = PressureType::Bmp280;
  uint8_t addr = 0x76;
  int   adcPin = 34;
};

struct AirCfg {
  AirImpl impl = AirImpl::DualReservoirPiston;
  DualReservoirCfg dual;
  BellowsCfg       bellows;
};

// ---- Distribution (valves) --------------------------------------------------
enum class ValveImpl : uint8_t { Valve2in1 = 0, Valve1in1 = 1 };

struct Pca9685Cfg { uint8_t addr = 0x40; float freqHz = 50.0f; float oscHz = 27000000.0f; };
struct ServoUsCfg { uint16_t min = 500, max = 2500; };

struct Hole2in1 { uint8_t hole = 0, channel = 0; int railAangle = 30, railBangle = 150, closedAngle = 90; };
struct Hole1in1 { uint8_t hole = 0, channel = 0; int openAngle = 90, closedAngle = 0; };

struct ValveCfg {
  ValveImpl  impl = ValveImpl::Valve2in1;
  Pca9685Cfg pca;
  ServoUsCfg servoUs;
  uint8_t    holeCount = 0;
  Hole2in1   holes2[MAX_HOLES];
  Hole1in1   holes1[MAX_HOLES];
};

// ---- Slide (harmonica chromatique) -----------------------------------------
struct SlideCfg { bool enabled = false; uint8_t channel = 14; int engagedAngle = 120; int restAngle = 60; };

// ---- Harmonica (mapping générique) -----------------------------------------
struct NoteEntry {
  uint8_t   note = 0;
  uint8_t   hole = 0;
  Direction direction = Direction::Blow;
  bool      slide = false;
  float     intensityScale = 1.0f;
  float     bendSemitones = 0.0f;   // note obtenue par bend (négatif = plus grave)
};
struct HarmonicaCfg {
  char      name[32] = "Diatonic";
  uint8_t   holeCount = 10;
  bool      hasSlide = false;
  uint16_t  noteCount = 0;
  NoteEntry notes[MAX_NOTE_ENTRIES];
};

// ---- Moteur de jeu ----------------------------------------------------------
enum class Arbitration : uint8_t { Reject = 0, Steal = 1 };
struct EngineCfg {
  uint8_t     maxPolyphony = 10;
  Arbitration arbitration = Arbitration::Reject;
  bool        velocityToIntensity = true;
  float       vibratoRateHz = 5.0f;    // vibrato de pression (CC1 modulation)
  float       vibratoDepth = 0.25f;    // profondeur max à CC1 = 127
};

// ---- Système ----------------------------------------------------------------
struct SystemCfg { bool mockMode = false; float telemetryHz = 10.0f; };

// ---- Document complet -------------------------------------------------------
struct Config {
  int          version = 1;
  BoardCfg     board;
  MidiCfg      midi;
  AirCfg       air;
  ValveCfg     valve;
  SlideCfg     slide;
  HarmonicaCfg harmonica;
  EngineCfg    engine;
  SystemCfg    system;
};

}  // namespace harm
