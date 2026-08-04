// ============================================================================
//  WebServer.h — page de configuration + API REST/WebSocket (ESP32).
//
//  Implémentation dans src/web/WebServer.cpp (ESPAsyncWebServer, sous garde
//  Arduino). Sert les assets LittleFS et expose :
//    GET  /api/config      -> config.json brut
//    POST /api/config      -> valide + sauvegarde atomique
//    GET  /api/status      -> télémétrie instantanée (JSON)
//    POST /api/calibrate   -> {servo | piston | pressureZero}
//    GET  /api/harmonicas  -> presets disponibles
//    POST /api/reboot      -> redémarrage (applique une config nécessitant reboot)
//    WS   /ws              -> télémétrie poussée à system.telemetryHz
// ============================================================================
#pragma once
#include "../Factory.h"
#include "ConfigStore.h"

namespace harm {

class WebServer {
public:
  void begin(System* sys, ConfigStore* store);
  void loop(uint32_t nowMs);          // pousse la télémétrie WS
  bool rebootRequested() const;
};

}  // namespace harm
