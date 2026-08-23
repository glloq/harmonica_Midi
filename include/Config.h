// ============================================================================
//  Config.h — modèle de configuration (structures C++ pures).
//
//  IMPORTANT : ce fichier ne dépend PAS d'ArduinoJson. La (dé)sérialisation
//  JSON vit uniquement dans ConfigStore (cible ESP32). La logique métier
//  (HarmonicaMap, NoteEngine, air, valve) travaille sur ces structs => elle
//  reste testable sur PC sans JSON.
//
//  Tout ce qui décrit un MONTAGE (nombre de trous, type de pompe, type de
//  valve, brochage, mapping de notes) est ici : rien n'est codé en dur dans la
//  logique. Ajouter une variante d'harmonica = éditer du JSON.
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

// ---- Capteurs de pression ---------------------------------------------------
enum class PressureType : uint8_t { Bmp280 = 0, Mpx2010 = 1 };

// Description d'UN capteur (I2C ou analogique) — réutilisée par les sources
// d'air récentes ; les deux premières gardent leurs champs historiques.
struct PressureSensorCfg {
  PressureType type = PressureType::Bmp280;
  uint8_t      addr = 0x76;        // I2C (bmp280)
  int          adcPin = 34;        // analogique (mpx2010)
  float        kpaPerCount = 0.0025f;
};

// ---- Entraînement d'une pompe / turbine ------------------------------------
//  ledc    : MOSFET piloté en PWM matériel ESP32 (moteur DC, pompe diaphragme) ;
//  esc     : variateur brushless commandé en impulsions servo (turbine) ;
//  pca9685 : canal PWM du PCA9685 (pompe lente / driver externe).
enum class PumpDrive : uint8_t { Ledc = 0, Esc = 1, Pca9685 = 2 };

struct PumpCfg {
  PumpDrive drive = PumpDrive::Ledc;
  int       pin = 18;                     // ledc
  uint8_t   channel = 14;                 // esc / pca9685 : canal du bus servo
  float     freqHz = 20000.0f;            // ledc (au-dessus de l'audible)
  uint16_t  escMinUs = 1000, escMaxUs = 2000;
  float     minDuty = 0.15f, maxDuty = 1.0f;   // plage utile (démarrage / limite)
  bool      invert = false;               // driver à logique inversée
};

// ---- Bus de sorties tout-ou-rien (électro-vannes / électroaimants) ---------
enum class DigitalBusImpl : uint8_t { Pca9685 = 0, Gpio = 1 };

struct DigitalBusCfg {
  DigitalBusImpl impl = DigitalBusImpl::Pca9685;
  uint8_t  pcaAddr = 0x41;                // 2e PCA9685 dédié aux vannes
  float    pcaFreqHz = 1000.0f;           // fréquence de hachage du maintien
  uint8_t  gpioCount = 0;                 // impl gpio : nombre de canaux mappés
  int      gpioPins[MAX_OUTPUTS] = {0};   // impl gpio : canal -> broche
  bool     activeLow = false;
  float    holdDuty = 1.0f;               // maintien après le pic (1 = pas de peak&hold)
  uint16_t peakMs = 80;                   // durée du pic plein courant
};

// ---- Source d'air -----------------------------------------------------------
enum class AirImpl : uint8_t {
  DualReservoirPiston = 0,   // 2 réservoirs + piston (design du README)
  SingleBellows       = 1,   // soufflet simple motorisé
  PumpPair            = 2,   // 2 pompes continues opposées (souffle + aspiration)
  SinglePumpReversible= 3    // 1 pompe + aiguillage souffle/aspiration
};

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

// Deux pompes continues opposées : l'une pressurise le plenum "souffle",
// l'autre met le plenum "aspiration" en dépression. Aucun mouvement mécanique
// à gérer, souffle et aspiration disponibles EN MÊME TEMPS et sans limite de
// durée (pas d'inversion de piston), au prix du bruit et de la consommation.
struct PumpPairCfg {
  PumpCfg           blowPump, drawPump;
  PressureSensorCfg blowSensor, drawSensor;
  bool  sharedSensor = false;             // un seul capteur (côté souffle)
  float pressureTargetKpa = 0.30f, pressureToleranceKpa = 0.05f;
  float pressureKp = 2.0f, pressureKi = 1.0f;
  float idleDuty = 0.0f;                  // régime de veille (plenum amorcé)
  uint16_t spinUpMs = 300;                // temps de montée en régime
  uint8_t bleedChannel = 255;             // servo de purge du plenum (255 = aucun)
  int   bleedOpenAngle = 90, bleedClosedAngle = 0;
};

// Une seule pompe + un aiguillage (servo 3 voies ou électro-vanne) qui choisit
// si la pompe pousse (souffle) ou tire (aspiration) : montage le plus économe,
// mais une seule direction à la fois et un temps de bascule à respecter.
enum class DiverterImpl : uint8_t { Servo = 0, Solenoid = 1 };

struct SinglePumpCfg {
  PumpCfg           pump;
  PressureSensorCfg sensor;
  DiverterImpl diverter = DiverterImpl::Servo;
  uint8_t  diverterChannel = 15;          // canal servo OU canal du bus vannes
  int   blowAngle = 30, drawAngle = 150, neutralAngle = 90;
  bool  solenoidBlowState = true;         // impl solénoïde : état = souffle
  uint16_t switchMs = 150;                // bascule avant de laisser sonner
  float pressureTargetKpa = 0.30f, pressureToleranceKpa = 0.05f;
  float pressureKp = 2.0f, pressureKi = 1.0f;
  float idleDuty = 0.0f;
};

struct AirCfg {
  AirImpl impl = AirImpl::DualReservoirPiston;
  DualReservoirCfg dual;
  BellowsCfg       bellows;
  PumpPairCfg      pumps;
  SinglePumpCfg    singlePump;
};

// ---- Distribution (valves) --------------------------------------------------
enum class ValveImpl : uint8_t {
  Valve2in1    = 0,   // servo, 2 entrées -> 1 sortie (choix du rail par trou)
  Valve1in1    = 1,   // servo, porte on/off (direction imposée globalement)
  Solenoid2in1 = 2,   // 2 électro-vannes par trou (rail A / rail B)
  Solenoid1in1 = 3    // 1 électro-vanne par trou (direction globale)
};

struct Pca9685Cfg { uint8_t addr = 0x40; float freqHz = 50.0f; float oscHz = 27000000.0f; };
struct ServoUsCfg { uint16_t min = 500, max = 2500; };

struct Hole2in1  { uint8_t hole = 0, channel = 0; int railAangle = 30, railBangle = 150, closedAngle = 90; };
struct Hole1in1  { uint8_t hole = 0, channel = 0; int openAngle = 90, closedAngle = 0; };
struct HoleSol2  { uint8_t hole = 0, railAchannel = 0, railBchannel = 0; };
struct HoleSol1  { uint8_t hole = 0, channel = 0; };

struct ValveCfg {
  ValveImpl  impl = ValveImpl::Valve2in1;
  Pca9685Cfg pca;
  ServoUsCfg servoUs;
  uint16_t   settleMs = 0;         // débattement mécanique estimé (info + moteur de jeu)
  uint8_t    holeCount = 0;
  Hole2in1   holes2[MAX_HOLES];
  Hole1in1   holes1[MAX_HOLES];
  HoleSol2   holesS2[MAX_HOLES];
  HoleSol1   holesS1[MAX_HOLES];
  DigitalBusCfg solenoids;         // bus utilisé par les impls Solenoid*
};

// ---- Slide (harmonica chromatique) -----------------------------------------
enum class SlideImpl : uint8_t { Servo = 0, Solenoid = 1 };

struct SlideCfg {
  bool      enabled = false;
  SlideImpl impl = SlideImpl::Servo;
  uint8_t   channel = 14;          // servo : canal PCA9685 ; solénoïde : canal du bus vannes
  int       engagedAngle = 120, restAngle = 60;
  uint16_t  settleMs = 40;         // temps de course du slide (info UI / futur délai)
};

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
  bool        ccVolumeEnabled = true;  // CC7 en facteur global
  bool        bendEnabled = true;      // actionnement des bends (surpression)
  float       bendPressureGain = 0.15f;      // supplément d'intensité par demi-ton
  float       pitchBendRangeSemitones = 2.0f;
  float       minIntensity = 0.05f;    // plancher pour qu'une note faible sonne
  uint16_t    holeSettleMs = 0;        // délai valve->air (course du servo)
  uint32_t    noteMaxHoldMs = 0;       // coupe-circuit note bloquée (0 = jamais)
};

// ---- Serveur web ------------------------------------------------------------
// password vide => aucune authentification (dev/mock) ; non vide => Basic Auth
// exigée sur toutes les routes /api/*.
struct WebCfg { char user[24] = "admin"; char password[64] = ""; };

// ---- Système ----------------------------------------------------------------
struct SystemCfg {
  bool  mockMode = false;
  float telemetryHz = 10.0f;
  bool  autoHomeOnBoot = true;
};

// ---- Document complet -------------------------------------------------------
struct Config {
  int          version = 2;
  BoardCfg     board;
  MidiCfg      midi;
  AirCfg       air;
  ValveCfg     valve;
  SlideCfg     slide;
  HarmonicaCfg harmonica;
  EngineCfg    engine;
  WebCfg       web;
  SystemCfg    system;
};

}  // namespace harm
