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
g7-cook [--source assets/source] [--out assets/cooked] [--pack data.g7pak] [--clean]
```
Formate und Bibliotheken: ADR 0016. Die Logik steckt in der Bibliothek `g7_cook_lib`
(`tools/asset-cooker/src/Cooker.hpp`, `g7::cook::cook(options)`); `g7-cook` ist die Kommandozeile dazu.

**Stand (Version 1):**
- **glTF/GLB → `.g7mesh`** (`asset/MeshFile.hpp`). Eingebettete Bilder werden als `<mesh>.img<n>.png|jpg` neben
  das Mesh gelegt. Externe Bilder werden selbst gekocht; das Mesh verweist dann per **VFS-Pfad ab der Wurzel**
  darauf (`../textures/wood.png` wird zu `textures/wood.png`).
- **Bilder** (`.png/.jpg/.jpeg/.tga/.bmp`) werden auf Lesbarkeit geprüft und unverändert übernommen.
  KTX2 (UASTC → BC7/BC5) folgt im nächsten Schritt.
- **Übersprungen** werden versteckte Dateien (`.xyz`), Blender-Dateien (`.blend`, `.blend1`) und glTF-Puffer
  (`.bin`, die über die `.gltf` gelesen werden). **Alles andere** (Skripte, Konfiguration, Sounds) wird kopiert.
- **Ausgabe:** lose Dateien unter `--out` (Ordnerstruktur wie die Quelle) oder mit `--pack` ein einziges
  Archiv `<out>/<datei>`. Gleiche Quellen ergeben byte-gleiche Ausgaben. `--clean` leert `--out` vorher.
  Ein Ausgabeordner innerhalb der Quelle wird abgelehnt.
- **Fehler pro Datei** (kaputtes glTF oder Bild, fehlende Textur, Textur außerhalb der Quelle, zwei Quellen mit
  derselben Ausgabe wie `hut.glb` und `hut.gltf`) werden gesammelt und gemeldet. Der Rest wird trotzdem gekocht;
  der Exit-Code ist dann 1.

**Geplant:** KTX2-Texturen, zstd in `.g7pak` v2, Manifest mit Hashes für inkrementelles Kochen
(Quell-Hash + Cooker-Version), `--watch` für Hot-Reload, weitere Prüfungen (Skelett-Namen, Mipmaps).

## Platzhalter-Inhalte
Bis eigene Modelle existieren: Kapseln/Boxen + CC0-Pakete (z. B. Quaternius, Kenney, Poly Haven)
– jeweils mit Lizenz-Eintrag.
