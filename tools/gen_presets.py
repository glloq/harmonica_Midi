#!/usr/bin/env python3
"""Génère la bibliothèque de presets d'harmonicas (data/presets/*.json).

Un preset = uniquement la section "harmonica" de la config (name/holeCount/
hasSlide/notes) : c'est ce qu'attend POST /api/harmonica pour un échange à
chaud. Les mêmes formules sont implémentées côté UI (data/app.js, generators)
afin de pouvoir créer une tonalité sans repasser par ce script.

Conventions :
  - `hole` est indexé à partir de 0 (le trou 1 de l'harmoniciste = hole 0) ;
  - en cas de note en double, la PREMIÈRE entrée gagne : les notes naturelles
    sont donc toujours écrites avant les bends.
"""
import json, pathlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "data/presets"

# ---- Diatonique Richter (10 trous) -----------------------------------------
RICHTER_BLOW = [60, 64, 67, 72, 76, 79, 84, 88, 91, 96]   # C4 E4 G4 C5 E5 G5 C6 E6 G6 C7
RICHTER_DRAW = [62, 67, 71, 74, 77, 81, 83, 86, 89, 93]   # D4 G4 B4 D5 F5 A5 B5 D6 F6 A6
# Bends jouables sur un Richter standard : {trou (0-based): nb de demi-tons}
DRAW_BENDS = {0: 1, 1: 2, 2: 3, 3: 1, 4: 1, 5: 1}
BLOW_BENDS = {7: 1, 8: 1, 9: 2}
KEYS = {"C": 0, "D": 2, "F": 5, "G": 7, "A": 9}


def richter(key: str, bends: bool = False) -> dict:
    off = KEYS[key]
    notes = []
    for h in range(10):
        notes.append({"note": RICHTER_BLOW[h] + off, "hole": h, "dir": "blow"})
        notes.append({"note": RICHTER_DRAW[h] + off, "hole": h, "dir": "draw"})
    if bends:   # après les naturelles : une note déjà mappée garde son trou
        for h, n in DRAW_BENDS.items():
            for s in range(1, n + 1):
                notes.append({"note": RICHTER_DRAW[h] + off - s, "hole": h,
                              "dir": "draw", "bend": -s})
        for h, n in BLOW_BENDS.items():
            for s in range(1, n + 1):
                notes.append({"note": RICHTER_BLOW[h] + off - s, "hole": h,
                              "dir": "blow", "bend": -s})
    return {"name": f"Diatonic {key} Richter" + (" + bends" if bends else ""),
            "holeCount": 10, "hasSlide": False, "notes": notes}


# ---- Accordage solo (chromatique / trémolo / octave) ------------------------
SOLO_BLOW = [0, 4, 7, 12]      # C E G C, par groupe de 4 trous
SOLO_DRAW = [2, 5, 9, 11]      # D F A B


def solo(holes: int, slide: bool, name: str, base: int = 60) -> dict:
    notes = []
    for h in range(holes):
        oct_, idx = divmod(h, 4)
        b = base + 12 * oct_ + SOLO_BLOW[idx]
        d = base + 12 * oct_ + SOLO_DRAW[idx]
        notes.append({"note": b, "hole": h, "dir": "blow"})
        notes.append({"note": d, "hole": h, "dir": "draw"})
        if slide:   # slide enfoncé : tout monte d'un demi-ton
            notes.append({"note": b + 1, "hole": h, "dir": "blow", "slide": True})
            notes.append({"note": d + 1, "hole": h, "dir": "draw", "slide": True})
    return {"name": name, "holeCount": holes, "hasSlide": slide, "notes": notes}


def dump(fname: str, doc: dict) -> None:
    """Écrit compact (une note par ligne) : lisible ET économe sur LittleFS."""
    lines = [f'  "{k}": {json.dumps(v)},' for k, v in doc.items() if k != "notes"]
    body = ",\n".join("    " + json.dumps(n, separators=(", ", ": ")) for n in doc["notes"])
    text = "{\n" + "\n".join(lines) + '\n  "notes": [\n' + body + "\n  ]\n}\n"
    json.loads(text)                       # garde-fou : refuse d'écrire du JSON cassé
    (OUT / fname).write_text(text, encoding="utf-8")
    print(f"[presets] {fname} — {len(doc['notes'])} notes, {doc['holeCount']} trous")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for key in KEYS:
        dump(f"diatonic_{key}.json", richter(key))
    dump("diatonic_C_bends.json", richter("C", bends=True))
    dump("chromatic_C.json", solo(12, True, "Chromatic C 12"))
    dump("chromatic_C_16.json", solo(16, True, "Chromatic C 16", base=48))
    dump("tremolo_C.json", solo(12, False, "Tremolo C 12 (solo)"))
    dump("octave_C.json", solo(10, False, "Octave C 10 (solo)"))
