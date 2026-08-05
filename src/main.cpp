// ============================================================================
//  main.cpp — point d'entrée.
//
//  ESP32 : découpage bi-cœur FreeRTOS.
//    - Core 1 (controlTask, prio haute) : draine la file MIDI puis pilote air,
//      valve, slide et le moteur de jeu (déterministe, jamais bloqué par la radio).
//    - Core 0 (netTask) : transports MIDI + serveur web + télémétrie.
//    - Pont : une file FreeRTOS (le sink du routeur enfile, le moteur défile).
//
//  Natif (PC) : un petit main() de démonstration qui joue une séquence en mode
//  mock et journalise les commandes d'actionneurs (voir env native).
// ============================================================================
#include "Factory.h"
#include "web/ConfigStore.h"
#include "hal/Mocks.h"

using namespace harm;

// Le point d'entrée est exclu pendant `pio test` (PIO_UNIT_TESTING) : le runner
// de tests fournit son propre main(), on évite ainsi un double main().
#ifndef PIO_UNIT_TESTING

static ConfigStore g_store;
static System*     g_sys = nullptr;

// ============================================================================
#if HARM_ARDUINO
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "web/WebServer.h"

static QueueHandle_t     g_queue = nullptr;      // évènements MIDI (net -> control)
static QueueHandle_t     g_cmdQueue = nullptr;   // commandes web (async -> control)
static WebServer         g_web;
static bool              g_webActive = false;
static volatile uint32_t g_droppedMidi = 0;

// Exécute une commande web sur le Core 1, seul propriétaire du bus I2C.
static void execCommand(const WebCommand& cmd) {
  const Config& c = g_sys->cfg;
  switch (cmd.type) {
    case WebCommand::ServoUs: {
      int us = cmd.value;
      if (us < c.valve.servoUs.min) us = c.valve.servoUs.min;
      if (us > c.valve.servoUs.max) us = c.valve.servoUs.max;   // clamp (anti-casse)
      if (cmd.channel < 16) g_sys->servos->writeMicros(cmd.channel, us);
      break;
    }
    case WebCommand::ServoDeg: {
      int d = cmd.value; if (d < 0) d = 0; if (d > 180) d = 180;
      if (cmd.channel < 16) g_sys->servos->writeAngle(cmd.channel, d);
      break;
    }
    case WebCommand::PistonHome:   g_sys->air->startHoming(); break;
    case WebCommand::PistonCenter: g_sys->air->startCentering(); break;
    case WebCommand::PressureTare: if (g_sys->pA) g_sys->pA->tare(); if (g_sys->pB) g_sys->pB->tare(); break;
    case WebCommand::SwapHarmonica:
      if (cmd.harmonica) { applyHarmonica(*g_sys, *cmd.harmonica); delete cmd.harmonica; }
      break;
  }
}

// Construit l'instantané de télémétrie sur le Core 1 (lecture capteurs I2C OK ici).
static void publishSnapshot() {
  StatusSnapshot s;
  s.air = g_sys->air->status();
  s.voices = g_sys->engine.activeVoiceCount();
  s.mixedCapable = g_sys->engine.mixedCapable();
  s.pitchBend = g_sys->engine.pitchBend();
  s.modulation = g_sys->engine.modulationDepth();
  s.setpointKpa = g_sys->air->currentSetpointKpa();
  s.transports = g_sys->router.transportCount();
  s.mock = g_sys->mock;
  std::strncpy(s.harmonica, g_sys->map.name(), sizeof(s.harmonica) - 1);
  s.droppedMidi = g_droppedMidi;
  WebServer::publishSnapshot(s);
}

static void controlTask(void*) {
  uint32_t lastSnap = 0;
  for (;;) {
    MidiEvent e;
    while (g_queue && xQueueReceive(g_queue, &e, 0) == pdTRUE) g_sys->engine.handleMidi(e);
    WebCommand cmd;
    while (g_cmdQueue && xQueueReceive(g_cmdQueue, &cmd, 0) == pdTRUE) execCommand(cmd);
    uint32_t now = millis();
    g_sys->air->update(now);
    g_sys->valve->update(now);
    if (g_sys->slide) g_sys->slide->update(now);
    g_sys->engine.update(now);
    float hz = g_sys->cfg.system.telemetryHz;
    uint32_t period = (hz > 0.0f) ? (uint32_t)(1000.0f / hz) : 100;
    if (now - lastSnap >= period) { lastSnap = now; publishSnapshot(); }
    vTaskDelay(1);                       // ~1 kHz (upgrade FastAccelStepper pour + rapide)
  }
}

static void netTask(void*) {
  for (;;) {
    g_sys->router.loop();
    if (g_webActive) {
      g_web.loop(millis());
      if (g_web.rebootRequested()) { delay(200); ESP.restart(); }
    }
    vTaskDelay(2);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[harmonica-midi] boot");

  g_store.begin();
  const Config& c = g_store.config();

  // Bus I2C unique, initialisé ici (Core 1) avec les pins/fréquence de la config.
  Wire.begin(c.board.sda, c.board.scl);
  Wire.setClock(c.board.i2cFreq);

  g_sys = buildSystem(c);

  g_queue    = xQueueCreate(64, sizeof(MidiEvent));
  g_cmdQueue = xQueueCreate(8, sizeof(WebCommand));
  g_sys->router.setSink([](const MidiEvent& e) {
    if (!g_queue || xQueueSend(g_queue, &e, 0) != pdTRUE) g_droppedMidi++;   // drop compté (télémétrie)
  });
  g_sys->router.begin();

  // Réseau/web : sur WiFi (station gérée par le transport RTP) ou en SoftAP de
  // configuration quand on est en mockMode (BLE ou aucun sans-fil actifs).
  if (c.midi.activeWireless == Wireless::Wifi) {
    g_web.begin(g_sys, &g_store, g_cmdQueue); g_webActive = true;
  } else if (c.system.mockMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Harmonica-Setup", c.midi.wifi.apPassword);
    Serial.print("[web] SoftAP http://"); Serial.println(WiFi.softAPIP());
    g_web.begin(g_sys, &g_store, g_cmdQueue); g_webActive = true;
  }

  g_sys->air->startHoming();             // home puis auto-centrage

  xTaskCreatePinnedToCore(controlTask, "control", 8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(netTask,     "net",     8192, nullptr, 1, nullptr, 0);
  Serial.printf("[harmonica-midi] pret : air=%s valve=%s mixte=%d mock=%d\n",
                c.air.impl == AirImpl::DualReservoirPiston ? "verin" : "soufflet",
                c.valve.impl == ValveImpl::Valve2in1 ? "2en1" : "1en1",
                g_sys->engine.mixedCapable(), g_sys->mock);
}

void loop() { vTaskDelay(1000); }

// ============================================================================
#else   // ---- Démonstration native (PC) ----
#include <cstdio>

static void step(System* s, uint32_t& t, int n) { for (int i = 0; i < n; ++i) { s->air->update(t); s->valve->update(t); s->engine.update(t); ++t; } }

int main() {
  g_store.begin();
  g_sys = buildSystem(g_store.config());
  g_sys->router.setSink([](const MidiEvent& e) { g_sys->engine.handleMidi(e); });

  uint32_t t = 0;
  MockLog::enabled = false;
  g_sys->air->startHoming();
  step(g_sys, t, 120);                    // home + centrage (silencieux)

  std::printf("=== Build : air=%s, valve=%s, mixte(souffle+aspire)=%d ===\n",
              g_store.config().air.impl == AirImpl::DualReservoirPiston ? "verin double" : "soufflet",
              g_store.config().valve.impl == ValveImpl::Valve2in1 ? "2-en-1" : "1-en-1",
              g_sys->engine.mixedCapable());

  MockLog::enabled = true;
  std::printf("\n-- NoteOn 60 (Do, souffle, trou 0) --\n");
  g_sys->router.setSink([](const MidiEvent& e) { g_sys->engine.handleMidi(e); });
  g_sys->engine.handleMidi({MidiEvent::NoteOn, 0, 60, 100});
  step(g_sys, t, 20);

  std::printf("\n-- NoteOn 67 (Sol, aspiration, trou 1) : polyphonie souffle+aspiration --\n");
  g_sys->engine.handleMidi({MidiEvent::NoteOn, 0, 67, 100});
  step(g_sys, t, 20);
  std::printf("voix actives = %d\n", g_sys->engine.activeVoiceCount());

  std::printf("\n-- NoteOff 60 puis 67 --\n");
  g_sys->engine.handleMidi({MidiEvent::NoteOff, 0, 60, 0});
  g_sys->engine.handleMidi({MidiEvent::NoteOff, 0, 67, 0});
  step(g_sys, t, 10);
  std::printf("voix actives = %d\n", g_sys->engine.activeVoiceCount());
  return 0;
}
#endif  // HARM_ARDUINO

#endif  // PIO_UNIT_TESTING
