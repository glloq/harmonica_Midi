// ============================================================================
//  Factory.cpp — assemblage config -> objets (HAL réelle ou mock).
// ============================================================================
#include "Factory.h"
#include "hal/Mocks.h"
#include "hal/ArduinoHal.h"
#include "air/DualReservoirPiston.h"
#include "air/SingleBellows.h"
#include "valve/Valve2in1.h"
#include "valve/Valve1in1.h"

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

static IEndstops* makeEndstops(const Config& c, bool mock) {
#if HARM_ARDUINO
  if (!mock) return new GpioEndstops(c.board.endstopR1, c.board.endstopR2, c.board.endstopActiveLow);
#else
  (void)c;
#endif
  (void)mock;
  return new MockEndstops();
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

  // -- Source d'air --
  if (cfg.air.impl == AirImpl::DualReservoirPiston) {
    const auto& d = cfg.air.dual;
    s->stepper  = makeStepper(cfg, mock);
    s->pA       = makePressure(mock, d.pressureType, d.r1Addr, d.r1AdcPin);
    s->pB       = makePressure(mock, d.pressureType, d.r2Addr, d.r2AdcPin);
    s->endstops = makeEndstops(cfg, mock);
    if (mock) s->endstops->bindPiston(s->stepper, d.travelMm);
    s->air = new DualReservoirPiston(*s->stepper, *s->pA, *s->pB, *s->endstops, *s->servos, d);
  } else {
    const auto& b = cfg.air.bellows;
    s->stepper = makeStepper(cfg, mock);
    s->pA      = makePressure(mock, b.pressureType, b.addr, b.adcPin);
    s->air = new SingleBellows(*s->stepper, *s->pA, b);
  }

  // -- Distribution --
  if (cfg.valve.impl == ValveImpl::Valve2in1) s->valve = new Valve2in1(*s->servos, cfg.valve);
  else                                        s->valve = new Valve1in1(*s->servos, cfg.valve);

  // -- Slide (chromatique) --
  if (cfg.slide.enabled) s->slide = new ServoSlide(*s->servos, cfg.slide);

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
