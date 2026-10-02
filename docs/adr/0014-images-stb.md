# 0014 – Bilddekodierung: stb_image (KTX2 ab M3)

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** M2

## Kontext
Texturen kommen in M2 als PNG/JPEG – eingebettet in `.glb` oder als Datei neben `.gltf`. KTX2 (BC7/BC5,
vorkomprimiert) entsteht erst durch den Asset-Cooker in M3; bis dahin gibt es keine KTX2-Dateien im Projekt.

## Optionen
1. **stb_image** – Header-only, PNG/JPEG/TGA/BMP, weit erprobt, gemeinfrei/MIT; in vcpkg (`stb`).
2. **libpng + libjpeg-turbo** – Referenzimplementierungen, aber zwei schwerere Bibliotheken.
3. **Windows Imaging Component** – nur Windows.

## Entscheidung
stb_image, **PRIVATE in `asset`** (`asset::decodeImage`/`loadImage` liefern immer RGBA8). Die
Implementierung wird einmal in `asset/src/StbImage.cpp` kompiliert. `stb_image_write` nutzen nur Tests.
Das Preset `nodeps` holt stb per `FetchContent` (gleiche Revision wie der vcpkg-Port).
**KTX2** (libktx) kommt mit dem Cooker in M3; die M2-Roadmap-Aufgabe „Texturen (PNG/KTX2)“ wird ohne KTX2
abgehakt und KTX2 in M3 geführt.

## Konsequenzen
- PNG/JPEG werden zur Laufzeit dekodiert und Mipmaps auf der GPU erzeugt – genug für M2, für große Welten
  zu langsam und zu speicherhungrig; deshalb ab M3 vorkomprimiertes KTX2.
- Keine Bilddekodierung in anderen Modulen; `render` lädt nur `asset::ImageData` hoch.
