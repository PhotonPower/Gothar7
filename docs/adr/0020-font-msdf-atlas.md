# 0020 – Schrift: MSDF-Atlas im Cooker mit msdfgen (Kern) und stb_truetype

- **Status:** Vorgeschlagen (vor M14 Teil A bestätigen)
- **Datum:** 2026-10-31
- **Phase:** M14

## Kontext
Die Spiel-UI (ADR 0009) zeichnet Text in vielen Größen – Untertitel, Listen, Überschriften, Bildschirmmeldungen –
bei Auflösungen von 720p bis 4K und mit UI-Skalierung. Ein Bitmap-Atlas je Größe wird unscharf oder groß; ein
**Signed Distance Field** bleibt in jeder Größe scharf. Einkanal-SDF rundet Ecken und Serifen ab, sobald die
Glyphe größer gezeichnet wird als erzeugt; **MSDF** (drei Kanäle) erhält Ecken. Die Laufzeit soll keine
Schrift-Rasterung und keine neue Bibliothek brauchen: Der Atlas entsteht beim Kochen.

Randbedingungen: Drittbibliotheken `PRIVATE`; der Cooker (`g7-cook`) darf `core`, `asset`, `world_format` nutzen.
`stb` (mit `stb_truetype.h` und `stb_rect_pack.h`) ist bereits Abhängigkeit (ADR 0014). Schriften nur unter CC0 oder
SIL Open Font License (OFL), Quelle in `assets/LICENSES.md`.

## Optionen
1. **msdfgen, nur der Kern** (vcpkg-Port `msdfgen` mit `default-features: false`; MIT, ohne FreeType/Skia/libpng)
   im Cooker; Glyphen-Umrisse liest `stb_truetype` (`stbtt_GetGlyphShape`) und übergibt sie als `msdfgen::Shape`;
   Packen mit `stb_rect_pack`; Kerning aus `stbtt_GetGlyphKernAdvance`. Bewährter MSDF-Algorithmus (Kantenfärbung,
   Fehlerkorrektur), kleine Abhängigkeit, nur im Werkzeug.
2. **msdfgen mit Extensions oder msdf-atlas-gen** – lädt Schriften selbst über FreeType, packt den Atlas; zieht
   FreeType, libpng, tinyxml2 (und für msdf-atlas-gen weitere Ports) nach. `msdf-atlas-gen` ist im aktuellen
   vcpkg-Baseline nicht vorhanden.
3. **Einkanal-SDF mit `stbtt_GetGlyphSDF`** – keine neue Abhängigkeit; abgerundete Ecken bei großen Überschriften,
   für Fließtext ab ~14 px ausreichend.
4. **Eigene MSDF-Implementierung** – keine Abhängigkeit; ~800 Zeilen heikler Geometrie-Code (Kantenfärbung,
   Vorzeichen-Korrektur), die msdfgen bereits gut löst.

## Entscheidung (Vorschlag)
**Option 1.** `g7-cook` erzeugt aus `assets/source/fonts/<name>.ttf|otf` und einer Beschreibung
`<name>.font.toml` (Zeichensatz, Erzeugungsgröße, Distanzbereich) eine Datei `fonts/<name>.g7font`
(Kopf, Metriken, Glyphen, Kerning-Paare, Atlas als unkomprimiertes RGB8/RGBA8 mit zstd – keine verlustbehaftete
Texturkompression, sie zerstört die Distanzwerte). Die Laufzeit (`ui`) liest nur `.g7font`. Ein CMake-Ziel
`g7_fonts` kocht die Schriften beim Bauen (wie `g7_figures`), damit auch Entwicklungs-Builds ohne Pak laufen.

Rückfallebene ohne neue Abhängigkeit: Option 3 hinter derselben `.g7font`-Datei (Feld `kind = sdf|msdf`); die
Laufzeit unterstützt beide Shader-Varianten. Wird Option 1 abgelehnt, startet Teil A mit Option 3.

## Konsequenzen
- Neue Abhängigkeit `msdfgen` (Kern, MIT) **nur in `tools/asset-cooker`**: Eintrag in `vcpkg.json`
  (`{ "name": "msdfgen", "default-features": false }`), Tabelle in `docs/05-build.md`, `PRIVATE` an `g7-cook`.
- Neues gekochtes Format `.g7font` v1 (in `docs/06-asset-pipeline.md` beschreiben).
- Zeichensatz fest je Schrift (Basis-Latein, Latin-1, typografische Anführungszeichen, Gedankenstrich, Ellipse);
  fehlende Glyphe → Ersatzzeichen und einmalige Warnung im Log. Keine dynamische Nachrasterung (kein CJK geplant).
- Komplexe Schriftsysteme (Ligaturen, Rechts-nach-links) sind ausgeschlossen; Kerning nur paarweise.
