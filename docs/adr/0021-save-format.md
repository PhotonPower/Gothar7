# 0021 – Spielstandformat `.g7save`: Chunk-Container mit CRC32, JSON-Nutzlast, zstd

- **Status:** Angenommen (Projektinhaber, 2026-11-01)
- **Datum:** 2026-11-01
- **Phase:** M15

## Kontext
M15 braucht ein Spielstandformat (Roadmap: Binärformat mit Versionsnummer und Migration, Metadaten mit Bild,
atomisches Schreiben, Prüfsumme). Gespeichert werden Metadaten, ein Vorschaubild, globale Werte (Zeit, Story, Held)
und je besuchter Welt ein Delta gegen die `.g7world`-Datei samt NPCs, Mobs und Gegenständen. Vobs und ihre
Komponenten haben schon einen Serialisierer (`world_format`, JSON, ADR 0017). Die Liste der Speicherplätze soll
Metadaten und Bild lesen, ohne den ganzen Spielstand zu entpacken. Ältere Spielstände sollen sich mit späteren
Versionen laden lassen. Der Projektinhaber verlangt keine Lesbarkeit; Spielstände werden nicht versioniert.

## Optionen
1. **Rein binär** (eigene Serialisierer je Struktur) – klein und schnell; aber ein zweiter Serialisierer für jede
   Vob-Komponente neben dem JSON des Weltformats, Migration auf Byte-Ebene mühsam.
2. **Chunk-Container (binär) mit JSON-Nutzlast, zstd-gepackt, CRC32 je Chunk** – Kopf und Chunk-Tabelle robust und
   erweiterbar (unbekannte Chunks überspringen), Metadaten und Bild ohne Entpacken lesbar; die Nutzlast benutzt die
   vorhandenen JSON-Serialisierer, Migrationen arbeiten auf JSON; zstd und nlohmann-json sind schon im Projekt.
   Nachteil: größer und langsamer als binär (geschätzt < 300 kB, < 100 ms für Leonberg; gemessen in Teil B).
3. **Ganzer Spielstand als JSON/zip** – einfach, aber kein schnelles Lesen der Metadaten, zip wäre eine neue
   Bibliothek; Prüfsumme nur über zip.
4. **SQLite** – Transaktionen und Abfragen geschenkt; neue Bibliothek, für einen Schreibvorgang je Spielstand
   überdimensioniert.

## Entscheidung
**Option 2.** Kopf `"G7SV"`, Formatversion u16, Chunk-Anzahl, CRC des Kopfes; je Chunk `tag u32, version u16,
flags u16 (zstd), rawSize u32, size u32, crc32 u32`. Chunks `META` (unkomprimiert, zuerst), `THMB` (PNG 320×180),
`GLOB`, `WRLD` je Welt, `CURR`. CRC32 (IEEE) als eigene kleine Funktion in `core` (keine Bibliothek). zstd und
nlohmann-json `PRIVATE` im Modul `save` – **keine neue Abhängigkeit** in `vcpkg.json`, nur ein weiterer Nutzer.
Schreiben atomar (Temp-Datei, flush, umbenennen). Migration je Chunk-Version auf dem JSON.

## Konsequenzen
- `save` linkt zstd und nlohmann-json privat; Tabelle in `docs/05-build.md` (Nutzer) ergänzen.
- `save` hängt zusätzlich von `world_format` ab (Vob-Serialisierung); Tabelle in `docs/02-architecture.md`.
- Wird die Nutzlast zu groß oder zu langsam, wird sie chunkweise binär (neue Chunk-Version, Migration), ohne den
  Container zu ändern.
- Jede Änderung an gespeicherten Strukturen erhöht die Chunk-Version und bringt eine Migration plus Test mit.

## Umsetzung (M15 Teil A)
`save/SaveFile.hpp`: Chunks META, THMB, GLOB (Spielzustand ohne Welten), WRLD je besuchter Welt; der Chunk `CURR`
des Plans entfällt (die aktuelle Welt steht in GLOB). Die CRC eines Chunks deckt seinen Kopf (Tag, Version, Flags,
Größen) und seine Daten ab, so fällt auch ein umgeschriebenes Tag auf. CRC-32 in `core/Crc32.hpp`.
