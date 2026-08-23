#!/usr/bin/env python3
"""Génère include/web/DefaultConfig.h depuis data/config.json.

data/config.json est la SEULE source de vérité de la configuration par défaut :
ce script en dérive le fallback embarqué (utilisé quand LittleFS est vide ou
illisible). Lancer après toute modification de data/config.json ; le mode
--check échoue si les deux ont divergé (appelé par tools/run_native_tests.sh).
"""
import json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "data/config.json"
DST = ROOT / "include/web/DefaultConfig.h"

HEADER = '''// ============================================================================
//  DefaultConfig.h — configuration par défaut embarquée (fallback).
//
//  ⚠ FICHIER GÉNÉRÉ — ne pas éditer à la main.
//  Source : data/config.json · Régénération : python3 tools/gen_default_config.py
//
//  Sert de secours si /config.json est absent ou illisible sur LittleFS, et de
//  réponse GET /api/config par défaut. Build de référence : diatonique C +
//  vérin double + valve 2-en-1.
// ============================================================================
#pragma once

namespace harm {

inline const char* kDefaultConfigJson() {
  return R"JSON(
'''
FOOTER = ''')JSON";
}

}  // namespace harm
'''

def render() -> str:
    text = SRC.read_text(encoding="utf-8").strip()
    json.loads(text)                      # refuse de générer depuis un JSON cassé
    if ")JSON" in text:
        sys.exit("data/config.json contient le délimiteur )JSON")
    return HEADER + text + "\n" + FOOTER

if __name__ == "__main__":
    out = render()
    if "--check" in sys.argv:
        if DST.read_text(encoding="utf-8") != out:
            sys.exit("DefaultConfig.h désynchronisé de data/config.json "
                     "— lancer : python3 tools/gen_default_config.py")
        print("[config] DefaultConfig.h synchrone avec data/config.json")
    else:
        DST.write_text(out, encoding="utf-8")
        print(f"[config] {DST.relative_to(ROOT)} régénéré depuis data/config.json")
