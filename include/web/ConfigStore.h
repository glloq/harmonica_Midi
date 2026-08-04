// ============================================================================
//  ConfigStore.h — chargement/sauvegarde de la configuration.
//
//  - deserialize() : JSON -> Config (ArduinoJson, pur, testable sur PC).
//  - begin()/save(): persistance LittleFS (ESP32) + sauvegarde atomique
//                    (écriture dans /config.tmp puis rename) + fallback défaut.
//  Le JSON brut courant est conservé pour être renvoyé tel quel par GET
//  /api/config (pas besoin d'un round-trip de sérialisation).
// ============================================================================
#pragma once
#include "../Config.h"
#include <string>

namespace harm {

class ConfigStore {
public:
  bool          begin();                                  // monte FS, charge fichier ou défaut
  bool          save(const char* json);                   // valide -> écrit -> recharge cfg_
  const Config& config() const { return cfg_; }
  const char*   raw() const { return raw_.c_str(); }

  static bool   deserialize(const char* json, Config& out);   // pur (ArduinoJson)

private:
  Config      cfg_;
  std::string raw_;
};

}  // namespace harm
