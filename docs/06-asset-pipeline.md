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
  Knochennamen und Sockets sind im Abschnitt „Referenz-Skelett“ in `docs/modules/animation.md` festgelegt; Clip-Namen und Events: `docs/design/characters-pipeline.md`.
- Kollisionsgeometrie: Objekte mit Präfix `COL_` werden nicht gerendert, nur als Kollision gekocht.
- LODs: Suffix `_LOD1`, `_LOD2`.
- Mob-Slots / Interaktionspunkte: Empties mit Präfix `SLOT_` (z. B. `SLOT_USE_0`).

## g7-cook
```
g7-cook [--source assets/source] [--out assets/cooked] [--pack data.g7pak] [--level 19]
        [--textures ktx2|copy] [--uastc-level 2] [--full] [--clean]
```
Formate und Bibliotheken: ADR 0016. Die Logik steckt in der Bibliothek `g7_cook_lib`
(`tools/asset-cooker/src/Cooker.hpp`, `g7::cook::cook(options)`); `g7-cook` ist die Kommandozeile dazu.

**Stand (Version 1):**
- **glTF/GLB → `.g7mesh`** (`asset/MeshFile.hpp`). Eingebettete Bilder werden als `<mesh>.img<n>.png|jpg` neben
  das Mesh gelegt. Externe Bilder werden selbst gekocht; das Mesh verweist dann per **VFS-Pfad ab der Wurzel**
  darauf (`../textures/wood.png` wird zu `textures/wood.png`).
- **Bilder** (`.png/.jpg/.jpeg/.tga/.bmp`) werden auf Lesbarkeit geprüft. Mit **`--textures ktx2`** (Vorgabe von
  `g7-cook`, sofern mit libktx gebaut) werden sie zu `<name>.ktx2`; mit `--textures copy` bleiben sie unverändert
  (Vorgabe der Bibliothek `CookOptions` und von Builds ohne libktx). Bei KTX2 gilt:
  - vollständige Mip-Kette, im Cooker gerechnet (Box-Filter; Farbe in linearem Licht gemittelt, Normalen gemittelt
    und neu normiert);
  - UASTC (`--uastc-level 0..4`, Vorgabe 2) mit zstd-Superkompression. Mehrere Texturen werden parallel kodiert,
    jede einzelne einthreadig (Hänger im basisu-Job-Pool, `docs/modules/asset.md`).
  - **Normal-Maps** erkennt der Cooker an `normalImage` in den Materialien aller Meshes; sie werden zweikanalig
    (X, Y, linear) für BC5 gespeichert. Alles andere ist sRGB-Farbe. Wird ein Bild als Farbe **und** als Normal-Map
    benutzt, ist das ein Fehler.
  - **Gelände** (M4): Der Cooker liest den `terrain`-Block jeder `.g7world` unter der Quelle. `splat.maps` sind
    **Daten**: lineares RGBA (KTX2 `R8G8B8A8_UNORM` → BC7 linear), Mips je Kanal unabhängig gemittelt, Alpha ist
    Gewicht der 4. Schicht (kein Premultiply, RGB bleibt auch bei Alpha 0). UASTC ist leicht verlustbehaftet
    (wenige Stufen von 255), für Gewichte unkritisch. `layers[].albedo` ist Farbe, `layers[].normal` Normal-Map.
    Bild als Splat **und** als Farbe/Normal-Map: Fehler. Die Löchermaske (`.r8`) und die Heightmap (`.r16`) werden
    unverändert kopiert (verlustfrei, im `.g7pak` zstd). Zur Laufzeit nimmt die Engine `x.ktx2` statt `x.png`, wenn
    vorhanden (`preferCooked`), die `.g7world` darf also die Quell-PNGs nennen.
  - Bildverweise in `.g7mesh` zeigen dann auf `.ktx2` (`mimeType` `image/ktx2`), auch für herausgelöste
    eingebettete Bilder.
  - Benötigt libktx (vcpkg); ein Build ohne libktx lehnt `--textures ktx2` ab.
- **Übersprungen** werden versteckte Dateien (`.xyz`), Blender-Dateien (`.blend`, `.blend1`) und glTF-Puffer
  (`.bin`, die über die `.gltf` gelesen werden). **Alles andere** (Skripte, Konfiguration, Sounds) wird kopiert.
- **Ausgabe:** lose Dateien unter `--out` (Ordnerstruktur wie die Quelle) oder mit `--pack` ein einziges
  Archiv `<out>/<datei>` (`.g7pak` v2, zstd pro Eintrag; `--level 1..22`, Vorgabe 19, 1–3 für schnelle Entwicklungsläufe). Gleiche Quellen ergeben byte-gleiche Ausgaben. `--clean` leert `--out` vorher.
  Ein Ausgabeordner innerhalb der Quelle wird abgelehnt.
- **Fehler pro Datei** (kaputtes glTF oder Bild, fehlende Textur, Textur außerhalb der Quelle, zwei Quellen mit
  derselben Ausgabe wie `hut.glb` und `hut.gltf`) werden gesammelt und gemeldet. Der Rest wird trotzdem gekocht;
  der Exit-Code ist dann 1.

**Inkrementelles Kochen (Manifest):** `<out>/.g7cook/manifest.txt` (Textformat, tab-getrennt, sortiert;
`tools/asset-cooker/src/Manifest.hpp`) hält je Quelle einen **Schlüssel** und ihre Ausgaben mit Hash und Größe fest.
- Der Schlüssel ist ein 64-Bit-FNV-1a-Hash über die Bytes der Quelle, ihre Abhängigkeiten, die Cooker-Version
  (`kCookerVersion`, wird bei Formatänderungen erhöht) und die Optionen, die die Ausgabe beeinflussen. Bei Bildern im
  KTX2-Modus zählt zusätzlich die Verwendung (Farbe, Normal-Map oder Gelände-Daten) dazu.
- Abhängigkeiten eines glTF sind die `uri`-Einträge im JSON (bei `.glb` im JSON-Chunk), also externe `.bin` und Bilder.
  `data:`-URIs zählen nicht.
- **Unveränderter Schlüssel:** Die alten Ausgaben werden wiederverwendet; lose Dateien werden per Hash geprüft,
  im Pack-Modus kommen die Einträge aus dem alten Archiv. Unveränderte lose Dateien werden nicht neu geschrieben,
  und hat sich gar nichts geändert, bleibt auch das Archiv unangetastet. So bleiben Zeitstempel stabil, und Hot-Reload löst nicht unnötig aus.
- **Gelöschte Quellen:** Ihre Ausgaben werden entfernt, aber nur Dateien, die im Manifest stehen; fremde Dateien bleiben.
  Fehlgeschlagene Quellen kommen nicht ins Manifest.
- Passen Cooker-Version oder Optionen nicht oder ist das Manifest kaputt, wird alles gekocht. `--full` erzwingt das.
- **Bericht:** „N cooked, M reused“. Ein inkrementeller Lauf erzeugt byte-gleiche Ergebnisse wie ein vollständiger.
  Die Testszene (47 Dateien) braucht ohne Änderungen 0,16 s statt 3,5 s.

**Geplant:** `--watch` für Hot-Reload während der Entwicklung; vorkomprimierte Einträge im `PakWriter`, damit
bei Teiländerungen nicht alle Einträge neu komprimiert werden; weitere Prüfungen (Skelett-Namen, Mipmaps);
Skelette und Animationen (M6).

## Platzhalter-Inhalte
Bis eigene Modelle existieren: Kapseln/Boxen + CC0-Pakete (z. B. Quaternius, Kenney, Poly Haven)
– jeweils mit Lizenz-Eintrag.
