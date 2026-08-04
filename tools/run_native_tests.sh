#!/bin/bash
# ============================================================================
#  Compile et exécute les tests unitaires natifs (logique pure) avec g++.
#  Récupère ArduinoJson (mono-header) et Unity dans .cache/ si absents.
#  Ne dépend PAS du registre PlatformIO — utile en CI / sessions web.
#  Équivaut à `pio test -e native` pour le cœur logique.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
CACHE="$ROOT/.cache"
mkdir -p "$CACHE/inc" "$CACHE/unity"

AJSON_VER="7.2.1"
if [ ! -f "$CACHE/inc/ArduinoJson.h" ]; then
  echo "[tests] téléchargement ArduinoJson v$AJSON_VER"
  curl -fsSL "https://github.com/bblanchon/ArduinoJson/releases/download/v${AJSON_VER}/ArduinoJson-v${AJSON_VER}.h" \
    -o "$CACHE/inc/ArduinoJson.h"
fi

UNITY_VER="v2.6.0"
for f in unity.h unity_internals.h unity.c; do
  if [ ! -f "$CACHE/unity/$f" ]; then
    echo "[tests] téléchargement Unity $UNITY_VER/$f"
    curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/${UNITY_VER}/src/$f" -o "$CACHE/unity/$f" \
      || curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/$f" -o "$CACHE/unity/$f"
  fi
done

SRCS=$(find src -name '*.cpp' | sort)
OUT="$CACHE/test_core"
echo "[tests] compilation g++ (C++17, -Wall -Wextra)"
g++ -std=gnu++17 -Wall -Wextra -DHARM_NATIVE=1 -DHARM_MOCK=1 -DPIO_UNIT_TESTING \
  -I include -I "$CACHE/inc" -I "$CACHE/unity" \
  test/test_core/test_main.cpp $SRCS "$CACHE/unity/unity.c" -o "$OUT"
echo "[tests] exécution"
"$OUT"
