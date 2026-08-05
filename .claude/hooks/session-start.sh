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

# 1) Dépendances des tests natifs (GitHub, autorisé par le proxy). Téléchargement
#    ATOMIQUE (temp + mv) : un fichier tronqué ne pollue jamais le cache.
mkdir -p .cache/inc .cache/unity
fetch() {  # fetch <url> <dest>
  [ -s "$2" ] && return 0
  curl -fsSL "$1" -o "$2.tmp" && mv "$2.tmp" "$2" || true
}
AJSON_VER="7.2.1"
fetch "https://github.com/bblanchon/ArduinoJson/releases/download/v${AJSON_VER}/ArduinoJson-v${AJSON_VER}.h" .cache/inc/ArduinoJson.h
for f in unity.h unity_internals.h unity.c; do
  fetch "https://raw.githubusercontent.com/ThrowTheSwitch/Unity/v2.6.0/src/$f" ".cache/unity/$f"
done

# 2) PlatformIO (compilation firmware ESP32 + pio test), best-effort.
if ! command -v pio >/dev/null 2>&1; then
  python3 -m pip install -q platformio 2>/dev/null \
    || python3 -m pip install -q --user platformio 2>/dev/null || true
fi
# PATH idempotent : n'ajoute la ligne qu'une seule fois.
if [ -d "$HOME/.local/bin" ] && [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  grep -q '.local/bin' "$CLAUDE_ENV_FILE" 2>/dev/null || echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$CLAUDE_ENV_FILE"
fi

echo "[session-start] tests natifs prêts (tools/run_native_tests.sh) ; pio: $(command -v pio >/dev/null 2>&1 && echo ok || echo absent)"
