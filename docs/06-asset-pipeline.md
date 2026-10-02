# 06 – Asset-Pipeline

## Grundsätze
- Nur **eigene oder frei lizenzierte** Inhalte (CC0, CC-BY mit Nachweis in `assets/LICENSES.md`).
- **Blender** ist das Haupt-Werkzeug; Austauschformat **glTF 2.0 (.glb)**.
- Quellen in `assets/source/`, gekochte Daten in `assets/cooked/` (nicht versioniert).
- Große Binärdateien später über **Git LFS** (Entscheidung, sobald > 100 MB Assets).

## Formate
| Inhalt | Quelle | Laufzeit |
|---|---|---|
| Statische Meshes, Welt-Geometrie | `.glb` | `.g7mesh` (Vertex/Index-Buffer, Submeshes, Material-Refs, AABB, LODs) |
| Skelette, Skin-Meshes | `.glb` | `.g7skel`, `.g7mesh` mit Skin-Gewichten |
| Animationen | `.glb` (eine Datei pro Set) | `.g7anim` (komprimierte Tracks, Events) |
| Texturen | `.png`/`.tga` | `.ktx2` (BC7 Farbe, BC5 Normal, Mipmaps) |
| Materialien | `.g7mat` (TOML/JSON) | dito, vorvalidiert |
| Sounds/Musik/Sprache | `.ogg`/`.wav` | `.ogg` |
| Welten | `.g7world` (JSON, vom Editor) | `.g7worldc` (binär) |
| Skripte | `.lua` | `.lua` (ggf. vorkompiliert) |
| Lokalisierung | `.csv`/`.toml` | Text-Tabelle |

## Konventionen für Blender-Exporte
- Einheiten: **1 Einheit = 1 Meter**, +Y oben in der Engine (glTF-Standard).
- Menschen-Skelett: ein gemeinsames humanoides Rig für alle menschlichen NPCs (Animations-Wiederverwendung wie in Gothic).
  Knochennamen und Attachment-Knochen (`hand_r`, `hand_l`, `spine_weapon_1h`, `spine_weapon_2h`, `bow`, `head`) sind fest definiert in `docs/modules/animation.md`.
- Kollisionsgeometrie: Objekte mit Präfix `COL_` werden nicht gerendert, nur als Kollision gekocht.
- LODs: Suffix `_LOD1`, `_LOD2`.
- Mob-Slots / Interaktionspunkte: Empties mit Präfix `SLOT_` (z. B. `SLOT_USE_0`).

## g7-cook
```
g7-cook --source assets/source --out assets/cooked [--pack data.g7pak] [--watch]
```
- Inkrementell (Hash je Quelle + Cooker-Version).
- `--watch` für Hot-Reload während der Entwicklung.
- Validierung: fehlende Texturen, falsche Skelett-Namen, nicht-quadratische Mipmaps → Fehler mit Pfad.

## Platzhalter-Inhalte
Bis eigene Modelle existieren: Kapseln/Boxen + CC0-Pakete (z. B. Quaternius, Kenney, Poly Haven)
– jeweils mit Lizenz-Eintrag.
