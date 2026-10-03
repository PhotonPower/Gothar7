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

## Schnittstelle zum Weltwechsel (Stand M4)
- Die Engine hält je **verlassener Welt** deren Zustand im Speicher (`Engine::m_leftWorlds`): ein `world::WorldFile`
  (Vobs wie beim Verlassen, `nextVobId`, Gelände/Wegnetz/Zonen aus der Datei) plus die Menge der verbrauchten
  `once`-Trigger. Beim Rückkehren wird daraus gespawnt statt aus der Datei.
- Für den Spielstand heißt das: je besuchter Welt dieses `WorldFile` bzw. sein **Delta zur Ausgangsdatei** (oben)
  plus die verbrauchten Trigger; die aktuelle Welt wird beim Speichern genauso erfasst (`captureWorld`).
- **Offener Punkt – Laufzeit-Vobs:** `captureWorld` lässt Vobs ab `kRuntimeVobIdBase` (fallen gelassene Items,
  gespawnte NPCs) weg. Bis M4 gibt es keine; sobald es sie gibt, müssen sie in den Weltzustand (Erfassen und Spawnen
  mit fester Laufzeit-ID) – sonst gehen sie beim Weltwechsel verloren.
- Global, nicht je Welt: Spielzeit/Tag-Nacht, Story-Variablen (world.md).
