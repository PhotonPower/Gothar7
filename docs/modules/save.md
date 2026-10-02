# save

**Zweck:** Spielstände.

## Inhalt eines Spielstands
- Metadaten: Version, Name, Datum, Spielzeit, aktuelle Welt, Screenshot-Thumbnail.
- Pro besuchter Welt: **Delta** zur Ausgangsdatei (entfernte Vobs per `VobId`, geänderte Komponenten, neue Vobs).
- NPCs: Position, Attribute, Inventar, Zustand/Routine, Einstellungen, Tot/Bewusstlos, Wahrnehmungs-Zustand.
- Spieler: wie NPC + Talente, Erfahrung, Tagebuch.
- Skript-Globals (Story-Variablen), gesagte Infos, Timer.
- Mobs: Zustand, Schloss offen, Inhalt.

## Format
Binär, Chunks mit `tag u32, version u16, size u32` → robuste Erweiterung; Komponenten-Serialisierer
registrieren sich mit `tag` + Version + Migrationsfunktion. Schreiben in Temp-Datei + atomisches Umbenennen,
CRC32 je Chunk.

## API
```cpp
namespace g7::save {
Result<void> saveGame(const SaveContext&, std::string_view slot);
Result<void> loadGame(SaveContext&, std::string_view slot);
Result<SaveMeta> readMeta(std::string_view slot);
}
```
