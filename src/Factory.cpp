// ============================================================================
//  Factory.cpp — assemblage config -> objets (HAL réelle ou mock).
//
//  Seul endroit du projet qui connaît les classes concrètes. Ajouter un
//  système d'air, un type de valve ou un actionneur = ajouter un cas ici,
//  une entrée d'enum dans Config.h et son parsing dans ConfigStore.
// ============================================================================
#include "Factory.h"
#include "hal/Mocks.h"
#include "hal/ArduinoHal.h"
#include "air/DualReservoirPiston.h"
#include "air/SingleBellows.h"
#include "air/PumpPair.h"
#include "air/SinglePumpReversible.h"
#include "valve/Valve2in1.h"
#include "valve/Valve1in1.h"
#include "valve/ValveSolenoid.h"

#if HARM_ARDUINO
#include <Arduino.h>
#include "midi/SerialMidi.h"
#include "midi/BleMidi.h"
#include "midi/WifiRtpMidi.h"
#endif

namespace harm {

// ---- Fabriques HAL (réel si dispo et non-mock, sinon mock) ------------------
static IServoBus* makeServoBus(const Config& c, bool mock) {
#if HARM_ARDUINO
  if (!mock) return new Pca9685Bus(c.valve.pca.addr, c.valve.pca.freqHz, c.valve.pca.oscHz,
                                   c.valve.servoUs.min, c.valve.servoUs.max);
#else
  (void)c;
#endif
  (void)mock;
  return new MockServoBus();
}

static IStepper* makeStepper(const Config& c, bool mock) {
#if HARM_ARDUINO
  if (!mock) return new AccelStepperAdapter(c.board.stepPin, c.board.dirPin,
                                            c.board.enablePin, c.board.invertEnable);
#else
  (void)c;
#endif
  (void)mock;
  return new MockStepper();
}

static IPressureSensor* makePressure(bool mock, PressureType type, uint8_t addr, int adcPin) {
#if HARM_ARDUINO
  if (!mock) {
    if (type == PressureType::Mpx2010) return new Mpx2010Sensor(adcPin);
    return new Bmp280Sensor(addr);
  }
#else
  (void)type; (void)addr; (void)adcPin;
#endif
  (void)mock;
  return new MockPressure();
}
static IPressureSensor* makePressure(bool mock, const PressureSensorCfg& s) {
  return makePressure(mock, s.type, s.addr, s.adcPin);
}

static IEndstops* makeEndstops(const Config& c, bool mock) {
#if HARM_ARDUINO
  if (!mock) return new GpioEndstops(c.board.endstopR1, c.board.endstopR2, c.board.endstopActiveLow);
#else
  (void)c;
#endif
  (void)mock;
  return new MockEndstops();
}

// Bus d'électro-vannes : construit UNE fois, partagé par les valves, le slide
// et l'aiguillage de la pompe unique.
static IDigitalOutBus* makeDigitalBus(const DigitalBusCfg& d, bool mock) {
#if HARM_ARDUINO
  if (!mock) {
    if (d.impl == DigitalBusImpl::Gpio) return new GpioDigitalBus(d.gpioPins, d.gpioCount, d.activeLow);
    return new Pca9685DigitalBus(d.pcaAddr, d.pcaFreqHz, d.activeLow);
  }
#else
  (void)d;
#endif
  (void)mock;
  return new MockDigitalBus();
}

// Sortie de puissance d'une pompe : PWM matériel (MOSFET) ou impulsions servo (ESC).
static IPwmOut* makePump(const PumpCfg& p, IServoBus* servos, bool mock, const char* tag,
                         uint8_t ledcChannel) {
#if HARM_ARDUINO
  if (!mock) {
    if (p.drive == PumpDrive::Ledc) return new LedcPwmOut(p.pin, p.freqHz, ledcChannel);
    return new ServoBusPwmOut(*servos, p.channel, p.escMinUs, p.escMaxUs);   // esc / pca9685
  }
#else
  (void)p; (void)servos; (void)ledcChannel;
#endif
  (void)mock; (void)ledcChannel;
  return new MockPwmOut(tag);
}

// Le bus d'électro-vannes n'existe que s'il sert à quelque chose.
static bool needsSolenoidBus(const Config& c) {
  if (c.valve.impl == ValveImpl::Solenoid1in1 || c.valve.impl == ValveImpl::Solenoid2in1) return true;
  if (c.slide.enabled && c.slide.impl == SlideImpl::Solenoid) return true;
  if (c.air.impl == AirImpl::SinglePumpReversible && c.air.singlePump.diverter == DiverterImpl::Solenoid) return true;
  return false;
}

// ---- Assemblage complet -----------------------------------------------------
System* buildSystem(const Config& cfg) {
  System* s = new System();
  s->cfg = cfg;
  bool mock = cfg.system.mockMode;
#if HARM_MOCK
  mock = true;
#endif
  s->mock = mock;

  s->servos = makeServoBus(cfg, mock);
  s->servos->begin();
  if (needsSolenoidBus(cfg)) s->solenoids = makeDigitalBus(cfg.valve.solenoids, mock);

  // -- Source d'air --
  switch (cfg.air.impl) {
    case AirImpl::DualReservoirPiston: {
      const auto& d = cfg.air.dual;
      s->stepper  = makeStepper(cfg, mock);
      s->pA       = makePressure(mock, d.pressureType, d.r1Addr, d.r1AdcPin);
      s->pB       = makePressure(mock, d.pressureType, d.r2Addr, d.r2AdcPin);
      s->endstops = makeEndstops(cfg, mock);
      if (mock) s->endstops->bindPiston(s->stepper, d.travelMm);
      s->air = new DualReservoirPiston(*s->stepper, *s->pA, *s->pB, *s->endstops, *s->servos, d);
      break;
    }
    case AirImpl::SingleBellows: {
      const auto& b = cfg.air.bellows;
      s->stepper = makeStepper(cfg, mock);
      s->pA      = makePressure(mock, b.pressureType, b.addr, b.adcPin);
      s->air = new SingleBellows(*s->stepper, *s->pA, b);
      break;
    }
    case AirImpl::PumpPair: {
      const auto& p = cfg.air.pumps;
      s->pumpBlow = makePump(p.blowPump, s->servos, mock, "pumpBlow", 4);
      s->pumpDraw = makePump(p.drawPump, s->servos, mock, "pumpDraw", 5);
      s->pA = makePressure(mock, p.blowSensor);
      if (!p.sharedSensor) s->pB = makePressure(mock, p.drawSensor);
      s->air = new PumpPair(*s->pumpBlow, *s->pumpDraw, *s->pA, s->pB, s->servos, p);
      break;
    }
    case AirImpl::SinglePumpReversible: {
      const auto& p = cfg.air.singlePump;
      s->pumpBlow = makePump(p.pump, s->servos, mock, "pump", 4);
      s->pA = makePressure(mock, p.sensor);
      s->air = new SinglePumpReversible(*s->pumpBlow, *s->pA, s->servos, s->solenoids, p);
      break;
    }
  }

  // -- Distribution --
  switch (cfg.valve.impl) {
    case ValveImpl::Valve2in1:    s->valve = new Valve2in1(*s->servos, cfg.valve); break;
    case ValveImpl::Valve1in1:    s->valve = new Valve1in1(*s->servos, cfg.valve); break;
    case ValveImpl::Solenoid2in1: s->valve = new ValveSolenoid2in1(*s->solenoids, cfg.valve); break;
    case ValveImpl::Solenoid1in1: s->valve = new ValveSolenoid1in1(*s->solenoids, cfg.valve); break;
  }

  // -- Slide (chromatique) --
  if (cfg.slide.enabled) {
    if (cfg.slide.impl == SlideImpl::Solenoid && s->solenoids) s->slide = new SolenoidSlide(*s->solenoids, cfg.slide);
    else                                                       s->slide = new ServoSlide(*s->servos, cfg.slide);
  }

  // -- Mise en route + moteur de jeu --
  s->air->begin();
  s->valve->begin();
  if (s->slide) s->slide->begin();
  s->map.load(cfg.harmonica);
  s->engine.begin(s->air, s->valve, &s->map, s->slide, cfg.engine);

  // -- Transports MIDI (série toujours, + au plus un sans-fil) --
  s->router.setChannelFilter(cfg.midi.channel);
#if HARM_ARDUINO
  if (cfg.midi.serial.enabled) s->router.add(new SerialMidi(Serial2, cfg.midi.serial));
  if (cfg.midi.activeWireless == Wireless::Ble)       s->router.add(new BleMidi(cfg.midi.ble));
  else if (cfg.midi.activeWireless == Wireless::Wifi) s->router.add(new WifiRtpMidi(cfg.midi.wifi));
#endif
  return s;
}

}  // namespace harm
