#!/bin/bash
# ============================================================================
#  check_esp32_syntax.sh — contrôle de SYNTAXE du chemin ESP32 sans matériel
#  ni registre PlatformIO (souvent injoignable en session distante).
#
#  Compile en -fsyntax-only les fichiers gardés par `#if HARM_ARDUINO`
#  (HAL réelle, serveur web, Factory, main) contre les stubs de tools/stubs/.
#  Ce n'est PAS un build : les stubs ne modélisent que les signatures utilisées.
#  Le vrai build reste `pio run -e esp32dev`.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
CACHE=".cache"
[ -s "$CACHE/inc/ArduinoJson.h" ] || { echo "[esp32] ArduinoJson absent du cache — lancer tools/run_native_tests.sh d'abord"; exit 1; }

FILES="src/main.cpp src/Factory.cpp src/ConfigStore.cpp src/web/WebServer.cpp"
echo "[esp32] contrôle de syntaxe (stubs Arduino) : $FILES"
for f in $FILES; do
  g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -DHARM_ARDUINO=1 \
      -I include -I tools/stubs -I "$CACHE/inc" "$f"
done
# Les en-têtes header-only du chemin ESP32 ne sont inclus par personne d'autre.
echo '#include "hal/ArduinoHal.h"
int main() { return 0; }' > "$CACHE/hal_syntax.cpp"
g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -DHARM_ARDUINO=1 \
    -I include -I tools/stubs -I "$CACHE/inc" "$CACHE/hal_syntax.cpp"
echo "[esp32] syntaxe OK"
