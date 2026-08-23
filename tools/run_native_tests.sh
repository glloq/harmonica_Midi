#!/bin/bash
# ============================================================================
#  Contrôles complets du projet, sans matériel ni registre PlatformIO :
#    1. DefaultConfig.h synchrone avec data/config.json ;
#    2. l'UI couvre toute la configuration (tools/check_ui_coverage.mjs) ;
#    3. l'UI se rend pour tous les montages (tools/check_ui_render.mjs) ;
#    4. tests unitaires de la logique pure (g++ + Unity) ;
#    5. syntaxe du chemin ESP32 contre des stubs (tools/check_esp32_syntax.sh).
#  Récupère ArduinoJson (mono-header) et Unity dans .cache/ si absents.
#  Ne dépend PAS du registre PlatformIO — utile en CI / sessions web.
#  Équivaut à `pio test -e native` pour le cœur logique.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
CACHE="$ROOT/.cache"
mkdir -p "$CACHE/inc" "$CACHE/unity"

# Téléchargement ATOMIQUE (temp + mv) : un fichier tronqué ne pollue jamais le cache.
fetch() {  # fetch <url> <dest> [url_fallback]
  [ -s "$2" ] && return 0
  echo "[tests] téléchargement $(basename "$2")"
  curl -fsSL "$1" -o "$2.tmp" && mv "$2.tmp" "$2" && return 0
  [ -n "${3:-}" ] && curl -fsSL "$3" -o "$2.tmp" && mv "$2.tmp" "$2"
}

AJSON_VER="7.2.1"
fetch "https://github.com/bblanchon/ArduinoJson/releases/download/v${AJSON_VER}/ArduinoJson-v${AJSON_VER}.h" "$CACHE/inc/ArduinoJson.h"

UNITY_VER="v2.6.0"
for f in unity.h unity_internals.h unity.c; do
  fetch "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/${UNITY_VER}/src/$f" "$CACHE/unity/$f" \
        "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/$f"
done

# --- Cohérences hors C++ : config par défaut générée, couverture et rendu de l'UI.
python3 tools/gen_default_config.py --check
if command -v node >/dev/null 2>&1; then
  node tools/check_ui_coverage.mjs
  node tools/check_ui_render.mjs
else
  echo "[ui] node absent : contrôles de l'interface ignorés"
fi

SRCS=$(find src -name '*.cpp' | sort)
OUT="$CACHE/test_core"
echo "[tests] compilation g++ (C++17, -Wall -Wextra)"
g++ -std=gnu++17 -Wall -Wextra -DHARM_NATIVE=1 -DHARM_MOCK=1 -DPIO_UNIT_TESTING \
  -I include -I "$CACHE/inc" -I "$CACHE/unity" \
  test/test_core/test_main.cpp $SRCS "$CACHE/unity/unity.c" -o "$OUT"
echo "[tests] exécution"
"$OUT"

# --- Chemin ESP32 : contrôle de syntaxe contre les stubs (pas un vrai build).
./tools/check_esp32_syntax.sh
