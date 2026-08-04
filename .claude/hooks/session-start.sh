#!/bin/bash
# ============================================================================
#  SessionStart hook — prépare l'environnement pour compiler/tester le firmware
#  dans les sessions Claude Code on the web.
#   - pré-télécharge ArduinoJson + Unity (tests natifs g++) dans .cache/
#   - installe PlatformIO (best-effort) pour `pio run` / `pio test`
#  Idempotent, non interactif, exécuté uniquement en session distante.
# ============================================================================
set -euo pipefail

# Sessions distantes (web) uniquement — no-op en local.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then exit 0; fi

cd "${CLAUDE_PROJECT_DIR:-.}"

# 1) Dépendances des tests natifs (téléchargées via GitHub, autorisé par le proxy).
mkdir -p .cache/inc .cache/unity
AJSON_VER="7.2.1"
[ -f .cache/inc/ArduinoJson.h ] || \
  curl -fsSL "https://github.com/bblanchon/ArduinoJson/releases/download/v${AJSON_VER}/ArduinoJson-v${AJSON_VER}.h" \
    -o .cache/inc/ArduinoJson.h || true
for f in unity.h unity_internals.h unity.c; do
  [ -f ".cache/unity/$f" ] || \
    curl -fsSL "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.0/src/$f" -o ".cache/unity/$f" || true
done

# 2) PlatformIO (compilation firmware ESP32 + pio test), best-effort.
if ! command -v pio >/dev/null 2>&1; then
  python3 -m pip install -q platformio 2>/dev/null \
    || python3 -m pip install -q --user platformio 2>/dev/null || true
fi
if [ -d "$HOME/.local/bin" ] && [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$CLAUDE_ENV_FILE"
fi

echo "[session-start] tests natifs prêts (tools/run_native_tests.sh) ; pio: $(command -v pio >/dev/null 2>&1 && echo ok || echo absent)"
