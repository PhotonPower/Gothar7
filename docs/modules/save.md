# save

**Zweck:** Spielstände – Speichern, Laden, Schnellspeichern, Zustand jeder besuchten Welt (M15, Meilenstein C).

## Stand vor M15
- Modul `save` war ein Platzhalter (`moduleName()`); Abhängigkeiten jetzt core, world_format, world, script,
  gameplay (Punkt 4, freigegeben).
- Vorhanden und genutzt werden: `world::captureWorld` + `Engine::m_leftWorlds` (verlassene Welten bleiben im
  Speicher, M4), `Scene::nextVobId/nextRuntimeVobId` mit Settern „für den Spielstand“, `GameTime::clock/restore`,
  `ScriptVm::story()/setStory()` (Story ohne Funktionen), `asset::encodePng`, zstd (in `asset`, `.g7pak`),
  `fs::writeTextAtomic`/Benutzerpfad (`fs::userPath`), die Aktionen `quick_save`/`quick_load` (F5/F9, ungenutzt) und
  im Menü die ausgegrauten Einträge „Spiel laden“/„Spiel speichern“ (M14 Teil F).

## Teil B (umgesetzt, M15) – Spielzustand erfassen und anwenden
`save/GameState.hpp`, `save/WorldDelta.hpp`; Engine `runtime/EngineSave.cpp`. Ohne Datei-Container (Teil A wartet
auf ADR 0021): der Zustand ist ein JSON-Dokument im Speicher.

```cpp
namespace g7::save {
struct WorldDelta { std::vector<RemovedVob> removed;          // Datei-Vobs, die fehlen (id + Name)
                    std::vector<world::WorldFileVob> changed;  // geänderte Datei-Vobs, neue Welt-Vobs
                    std::vector<world::WorldFileVob> runtime;  // Laufzeit-Vobs (fallen gelassene Items)
                    u64 nextVobId, nextRuntimeVobId; };
WorldDelta diffWorld(const world::WorldFile& base, const world::WorldFile& now);
world::WorldFile applyDelta(world::WorldFile base, const WorldDelta&, usize* dropped = nullptr);
struct MobState { u64 vob; std::string definition; bool locked, open; std::map<std::string, u32> contents; };
enum class Life { Alive, Down, Dead, Vanished };
struct NpcState { std::string name, instance; Vec3 position; f32 yaw; std::optional<gameplay::CharacterState>
                  character; std::string routine; Life life; f32 downSeconds; bool pickpocketed, summoned;
                  f32 summonSeconds; };
struct WorldState { std::string path; WorldDelta delta; std::vector<u64> spentTriggers;
                    std::vector<MobState> mobs; std::vector<NpcState> npcs; };
struct HeroState { gameplay::CharacterState character; Vec3 position; f32 yaw; std::string torch; };
struct GameState { u32 version; f64 clock, secondsPerMinute, scriptClock; u32 nextTimerId;
                   std::vector<script::ScriptVm::NamedTimer> timers; script::Value story;
                   std::optional<HeroState> hero; std::string currentWorld; std::vector<WorldState> worlds; };
std::string toJson(const GameState&);            // stabil: gleicher Zustand, gleiche Bytes
Result<GameState> fromJson(std::string_view);    // Fehler nennen den Eintrag; neuere Version → Fehler
}
// Engine
Result<save::GameState> captureGame();             // Zeit, Story, benannte Timer, Held, aktuelle Welt
Result<void> applyGame(const save::GameState&);    // Welt aus Datei + Delta, NPCs, Items, Mobs, Story, Timer, Held
```
- **Dazu:** `gameplay::CharacterState` mit `Character::state()/restore()` (Ausrüstung ohne erneute Prüfung, unbekannte
  Items fallen weg); `world::writeVobJson/parseVobJson` (ein Vob wie in der Datei); `Scene::spawnVob` nimmt für
  Laufzeit-Vobs eine feste Laufzeit-ID (Spielstand); `Engine::spawnItemAt(instance, count, transform, fixedId)`;
  `ScriptVm::namedTimers()/restoreTimers()` und `after/every(sec, "funktion", args…)` – Timer mit Lua-Funktion
  bleiben beim Laden mit ihrer Restzeit (Systeme wie `every(1.0, fn)` beim Laden der Skripte), benannte kommen aus
  dem Spielstand (Punkt 3).
- **Welt:** Delta gegen die Datei (Vergleich der Vobs als Dateitext; Türen geschlossen mit `open`), Laufzeit-Items mit
  ID und Transform, `nextVobId`/`nextRuntimeVobId`, verbrauchte `once`-Trigger, alle Mobs (Schloss, offen,
  Inhalt). Beim Anwenden passt jeder Delta-Eintrag nur auf einen Vob mit gleichem Namen/Typ, sonst Warnung und
  verworfen (Schutz vor neu erzeugten Welten, Punkt 5). Entfernte Eltern nehmen ihre Kinder mit.
- **NPCs:** in der Reihenfolge des Einsetzens, mit Namen (`mon_wolf#2`), Ort, Blick, Werten, Inventar,
  Tagesablauf (Eintrag neu), Leben (bewusstlos mit Restzeit, tot, verschwunden), beschworen, Taschendiebstahl
  versucht. Die KI beginnt den Zustand ihres Tagesablaufs neu (Punkt 2). Nicht gespeichert: Befehlsliste, Wege,
  Kampfzug, Fluchtzustand, vorübergehende Einstellungen (Closures).
- **Ablauf beim Anwenden:** Held-Werte, Zeit (`restore`, Tagesabläufe neu ausgewertet), Welt entladen, aus Datei +
  Delta laden – die Skripte hören `world_loaded(world, true)` und setzen niemanden ein (`startup.lua`) –, Items,
  Trigger, Mobs, NPCs (die von Skripten eingesetzten werden ersetzt), dann Story und Timer (nach den Handlern),
  Held an seinen Ort, Fackel, Ereignis `game_loaded()`.
- **Noch nicht:** Tiergestalt und Trank-Wirkungen des Helden (enden beim Laden), gezogene Waffe (steckt), Datei
  (Teil A).
- **Tests:** `tests/save/test_game_state.cpp` (alle Felder durch JSON, stabil, Fehler, neuere Version; Delta mit
  entfernten/geänderten/neuen Vobs, nicht mehr passende Einträge, Kinder), `tests/gameplay/test_character.cpp`,
  `tests/script/test_script.cpp` (benannte Timer über einen neuen VM), `tests/world/test_scene.cpp`,
  `test_world_file.cpp`, `tests/runtime/test_engine_save.cpp` (das veränderte Lager: erfassen → neue Engine →
  anwenden → erfassen ergibt dieselben Bytes), `test_engine_leonberg_save.cpp` (ctest `runtime_leonberg_save`,
  Leonberg mit 25 Bewohnern, nur lokal).

## Teil C (umgesetzt, M15) – Besuchte Welten und Weltwechsel
- `Engine::m_leftWorlds` hält je verlassener Welt ihren `save::WorldState` (Delta gegen die Datei, Laufzeit-Items,
  Mobs, NPCs, verbrauchte Trigger) statt nur eines `WorldFile`: beim Weltwechsel `captureCurrentWorld()`, bei der
  Rückkehr `loadWorldState()` (Datei + Delta, `world_loaded(world, true)`, dann Items, Mobs, NPCs, Trigger). Damit
  bleiben fallen gelassene Items (Laufzeit-Vobs), Truhen-Inhalte, Schlösser und NPCs (Werte, tot) über einen
  Weltwechsel erhalten – bis M14 gingen sie verloren (offener Punkt aus M4 erledigt).
- Die Menü-Welt (Hauptmenü) wird beim Verlassen nicht erfasst.
- `captureGame()` enthält alle besuchten Welten (verlassene nach Pfad, dann die aktuelle); `applyGame()` lädt die
  aktuelle und legt die anderen als verlassene Welten ab.
- **Tests:** `tests/runtime/test_engine_save.cpp` (Lager → Höhle → Lager: NPC-Werte, toter Wolf, Item, Truhe;
  Spielstand mit zwei Welten in neuer Engine byte-gleich; danach wieder Höhle und zurück), GPU
  `test_world_vobs.cpp` (Weltwechsel wie bisher).

## Plan M15 (2026-11-01; Teile B und C freigegeben, A wartet auf ADR 0021)

### Grundsätze
- **Gothic-Modell:** Gespeichert wird der *Zustand*, nicht die laufende Simulation. NPCs kommen nach dem Laden an
  ihrem Ort mit Werten, Inventar, Tagesablauf und Lebenszustand zurück; ihre KI beginnt den Zustand ihres
  Tagesablaufs neu (wie Gothic nach dem Laden), halbe Befehle (ein Weg, ein Schlag) werden nicht fortgesetzt.
- **Schichten:** `save` kennt nur **reine Daten** (`SaveGame`, `WorldState`, `NpcState` … ohne Engine-Typen) und
  das Dateiformat. `runtime` erfasst und wendet an (`EngineSave.cpp`), weil nur die Engine Kreaturen, Mobs, Held
  und Welten kennt. `save` braucht zusätzlich `world_format` (für `WorldFile`/Vob-Beschreibungen); die Tabelle in
  `02-architecture.md` bekommt `save: core, world_format, world, script, gameplay`.
- **Erwartbare Fehler** (Datei kaputt, falsche Version, Platte voll) → `Result`, nie ein halb geladener Zustand:
  erst vollständig lesen und prüfen, dann die laufende Welt verwerfen und anwenden.
- **Deterministischer Rundlauf** als Hauptprüfung: speichern → laden → speichern ergibt (ohne Metadaten-Zeit und
  Bild) dieselben Bytes.

### Dateiformat `.g7save` (ADR 0021, vorgeschlagen)
```
Kopf (20 Byte):  magic "G7SV" | format u16 (1) | flags u16 | chunkCount u32 | reserved u32 | headerCrc u32
Chunk:           tag u32 ("META") | version u16 | flags u16 (bit0 zstd) | rawSize u32 | size u32 | crc32 u32 | Daten
```
- **Chunks** (Reihenfolge fest, unbekannte werden mit Warnung übersprungen):
  `META` (Metadaten, immer zuerst, unkomprimiert), `THMB` (Vorschaubild PNG 320×180, unkomprimiert), `GLOB`
  (Zeit, Story, Held, Kapitel, Zähler), `WRLD` je besuchter Welt (Delta, Mobs, Gegenstände, NPCs, Trigger), `CURR`
  (aktuelle Welt, Spielerposition, Kamera-Blick).
- **Nutzlast:** JSON (nlohmann, schon vorhanden; `PRIVATE` in `save`), mit **zstd** gepackt (Stufe 3, außer
  META/THMB). Grund: Vobs werden schon heute als JSON beschrieben (`WorldFile`); dieselben Serialisierer für das
  Welt-Delta zu benutzen, vermeidet einen zweiten Weg für jede Komponente. Größe/Zeit: Leonberg mit 25 Bewohnern
  geschätzt < 300 kB gepackt, < 100 ms. Reicht das nicht, wird nur die Nutzlast binär (Chunk-Version +1).
- **Prüfsumme:** CRC32 (IEEE, eigene Tabelle in `core/Crc32.hpp`, ~30 Zeilen, keine Bibliothek) je Chunk über die
  gespeicherten Bytes und über den Kopf. Falsche CRC → Fehler „Spielstand beschädigt“, der Platz bleibt sichtbar
  mit Hinweis.
- **Versionen:** `format` (Container) und je Chunk `version`. Laden älterer Chunk-Versionen über registrierte
  Migrationsfunktionen `vN → vN+1` (auf dem JSON, `save/Migrations.cpp`); neuere als die Engine kennt → Fehler
  „Spielstand aus neuerer Version“. Dazu in `META` die Spielversion und die **Inhalts-Prägung** (Hash über den Inhalt der besuchten
  `.g7world`-Dateien), nur zur Warnung.
- **Atomisches Schreiben:** über das vorhandene `fs::writeFileAtomic` (Temp-Datei, dann umbenennen; prüfen, dass
  vor dem Umbenennen geflusht wird); der alte Stand bleibt bis zum Umbenennen gültig. Beim
  Schnellspeichern zusätzlich eine Generation zurück (`quick.bak.g7save`).
- **Ablage:** `fs::userPath("saves/")` (Windows `%APPDATA%\Gothar\Gothar\saves`): `slot_01.g7save` … `slot_15`,
  `quick.g7save`, `auto.g7save`.

### Inhalt
| Bereich | gespeichert | Quelle → Wiederherstellung |
|---|---|---|
| Metadaten | Name, Datum (UTC), Spielzeit (Echtzeit gespielt), Spieltag/Uhrzeit, Kapitel, Ort (Welt + nächster Wegpunkt-/Hausname), Stufe des Helden, Spiel-/Formatversion, Sprache, Inhalts-Prägung | Liste der Plätze liest nur Kopf + META + THMB |
| Vorschaubild | 320×180 PNG des letzten Bildes ohne UI | Szene vor `drawGameUi` in kleines Ziel; `runtime` kodiert mit `asset::encodePng`, `save` bekommt nur die Bytes |
| Zeit | `GameTime::clock`, `secondsPerMinute`, Skript-Uhr | `restore`, danach **ein** `Jumped` (Routinen springen) |
| Story | `Story` (alle Lua-Variablen der Geschichte: Aufträge, Tagebuch, Notizen, Kapitel, `told`, Einstellungen `Story.attitudes`) | `setStory`; Funktionen in Story → Fehler beim Speichern (Inhaltsfehler, Test) |
| Timer | nur benannte: `after(sec, "fn_name", args?)`/`every(...)` (neu) | wieder eingereiht; **Closures** nicht – sie gelten als flüchtig (vorübergehende Einstellungen, Effekte); Warnung im Log |
| Held | Charakter (Attribute, Talente, Stufe, Erfahrung, Lernpunkte, Gilde, Inventar, Ausrüstung), Runenplätze, gezogene Waffe (nur „steckt“/„gezogen“), Fackel, Tiergestalt mit Restzeit, Wirkungen (Trank-Boost) mit Restzeit, Position/Blick, Schwimmen/Tauchen + Luft | Spielfigur neu setzen; Kampf-/Animationszustand nicht (steht) |
| Welt-Delta je Welt | gegen die `.g7world`-Datei: entfernte Vob-IDs, geänderte Vobs (Transform/Komponenten), neue Laufzeit-Vobs (fallen gelassene Items) mit fester Laufzeit-ID, `nextVobId`/`nextRuntimeVobId`, verbrauchte `once`-Trigger | Welt aus Datei laden, Delta anwenden, Zähler setzen |
| Mobs je Welt | Schloss zu/auf, offen, Inhalt, Türwinkel (offen/zu), Besitzer unverändert | nach Delta auf `m_mobs` |
| NPCs je Welt | Instanz und Name (`npc#2`), Position/Blick, Charakter (wie Held), Tagesablauf (`routine`), lebt/bewusstlos (Restzeit)/tot/verschwunden, beschworen (Restzeit), Folgen des Helden, Taschendiebstahl versucht, Fluchtzustand nicht | `spawnNpc` an Ort, Werte setzen, Tagesablauf neu anstoßen; Tote liegen als Leiche (plünderbar) |
| Tiere | wie NPCs (Art, Position, Leben, tot) | dito |
| Musik/Klang | nichts (ergibt sich aus Ort, Zeit, Kampf) | – |

**VobId-Vertrag** (coordination.md, bestehend: „je Welt eindeutig, nie wiederverwendet“) wird für M15 **geschärft**:
Der welt-Assembler muss **IDs unveränderter Vobs über Neuerzeugungen stabil halten** (nicht nur eindeutig), sonst
treffen Deltas alter Spielstände falsche Vobs. Nachfrage an welt (über den Koordinator), ob das heute gilt. Schutz in
der Engine: Jeder Delta-Eintrag trägt neben der ID den Vob-Namen bzw. Mob-Typ; passt er nicht mehr, wird der Eintrag
verworfen (Warnung) statt einen falschen Vob zu ändern.

**Welt beim Laden:** Das Ereignis `world_loaded(world)` setzt heute Bewohner ein (`leonberg_people`). Neu:
`world_loaded(world, restored)` – `restored = true` beim Laden eines Spielstands und bei der Rückkehr in eine
gespeicherte Welt; dann setzen die Skripte keine NPCs ein (die kommen aus dem Spielstand). Zusätzlich Ereignis
`game_loaded()` für Inhalte, die nach dem Laden etwas nachziehen.

### Teile
- **Teil A – Format** (`save`, `core`): `core/Crc32.hpp`, `save/SaveFile.hpp`
  (Container lesen/schreiben, Chunks, zstd, CRC, Version), `save/SaveGame.hpp` (Datenmodell), `save/Json.cpp`
  (Datenmodell ↔ JSON), `save/Migrations.hpp`, `save/Slots.hpp` (Platzliste, `readMeta`).
  Tests `tests/save/`: Rundlauf jedes Chunks, CRC-Fehler an jeder Stelle erkannt, abgeschnittene Datei, unbekannter
  Chunk übersprungen, Migration v1→v2 an einem Beispiel-Chunk, atomisches Schreiben (Abbruch vor dem Umbenennen
  lässt den alten Stand), `readMeta` liest ohne die großen Chunks.
- **Teil B – Erfassen/Anwenden einer Welt** (`runtime/EngineSave.cpp`, `world/WorldDelta.hpp`):
  `world::diffWorld(base, now) -> WorldDelta`, `applyDelta`; NPC/Tier/Mob/Gegenstands-Zustände; Held; Zeit; Story;
  benannte Timer (`script`: `after/every` mit Funktionsnamen, `ScriptVm::timers()/restoreTimers`). Laufzeit-Vobs
  bekommen dabei ihren festen Platz (offener Punkt aus M4 erledigt). Tests (runtime, Lager): Item fallen lassen,
  Truhe leeren, Tür aufschließen, NPC töten, Auftrag beginnen → speichern → neues Spiel → laden → alles wie vorher;
  speichern → laden → speichern byte-gleich.
- **Teil C – Besuchte Welten und Weltwechsel:** `LeftWorld` wird zum `save::WorldState` (statt nur `WorldFile`),
  mit NPCs/Tieren/Mobs; Rückkehr in eine Welt stellt NPCs wieder her (heute werden sie per `world_loaded` neu
  eingesetzt). `world_loaded(world, restored)`. Tests: Lager → Höhle → Lager (NPC-Werte, tote Wölfe bleiben tot),
  Spielstand mit zwei Welten.
- **Teil D – Plätze, Schnellspeichern, Menüs** (`ui/SaveScreens.hpp`, `runtime/EngineMenu.cpp`): Bildschirme
  „Speichern“/„Laden“ (Liste der Plätze mit Vorschaubild, Name, Ort, Kapitel, Spieltag/Uhrzeit, Datum; leerer Platz
  „– leer –“; Überschreiben mit Rückfrage; beschädigt/neuere Version ausgegraut mit Grund), Hauptmenü „Spiel laden“,
  Pausenmenü „Speichern“/„Laden“ freigeschaltet, F5/F9 mit Meldung („Schnell gespeichert“), Autosave beim
  Weltwechsel, Ladebildschirm beim Laden. Lua `save_game(slot)`, `load_game(slot)` (Konsole, Tests). Texte in
  `ui.<sprache>.toml`. Tests: Bildschirme ohne GPU (`tests/ui`), Menüfahrt per Tasten (`runtime_ui`).
- **Teil E – Kapitel für Meilenstein C und DoD** (siehe unten), Doku, Roadmap.

### API (Entwurf)
```cpp
namespace g7::save {
struct SaveMeta { std::string name, world, place, language, gameVersion; i64 savedAtUtc; f64 playSeconds;
                  u32 day, minute, chapter, heroLevel; u64 contentStamp; };
struct NpcState { std::string instance, name; Vec3 position; f32 yaw; CharacterState character;
                  std::string routine; LifeState life; f32 lifeSeconds; bool followsHero, pickpocketed, summoned; };
struct WorldState { std::string path; world::WorldDelta delta; std::set<u64> spentTriggers;
                    std::vector<MobState> mobs; std::vector<NpcState> npcs; };
struct SaveGame { SaveMeta meta; std::vector<u8> thumbnailPng; GlobalState global; HeroState hero;
                  std::string currentWorld; std::vector<WorldState> worlds; };
Result<void> writeSave(const fs::Path&, const SaveGame&);      // atomar
Result<SaveGame> readSave(const fs::Path&);                     // vollständig geprüft
Result<SaveMeta> readMeta(const fs::Path&, std::vector<u8>* thumbnail = nullptr);
std::vector<SlotInfo> listSlots(const fs::Path& dir);           // auch beschädigte (mit Grund)
}
namespace g7 {   // Engine
Result<void> saveGame(std::string_view slot, std::string name = {});   // "slot_03", "quick", "auto"
Result<void> loadGame(std::string_view slot);                          // über Ladebildschirm, nächster Frame
bool canSave() const;                                                  // Regeln unten
}
```

### Kapitel für Meilenstein C (Vorschlag)
„Kapitel 1 – Das Lager“ (vorhanden: drei Aufträge, Handel, Lehrer, Kampf, Magie, Kapitel 2 am Ende) **plus ein
kurzes „Kapitel 2 – Ankunft in Leonberg“**: ein Weltwechsel-Trigger am Lagertor nach Leonberg (Startpunkt am Unteren
Tor; Startpunkt-Vob in `leonberg.g7world` → **Auftrag an welt**), ein Auftrag dort (dem Ratsdiener Matthis den Brief
des Vogts bringen; Belohnung, Tagebuch), Rückweg ins Lager möglich. Damit deckt das Kapitel zwei Welten, Weltwechsel
hin und zurück, Handel, Kampf und Tagebuch ab.
**DoD-Test** `tests/runtime/test_engine_chapter_save.cpp` (eigene ctest-Gruppe, Lager + Leonberg, lokal wie die
Leonberg-Gruppen): das Kapitel mit Spielstand nach jedem Abschnitt, jeweils neu gestartete Engine lädt und spielt weiter;
zusätzlich Schnellspeichern/-laden mitten im Kampf und Rückkehr ins Lager (Zustand erhalten). Eine CI-taugliche
Variante nur im Lager (zwei Welten: Lager + Höhle). Begehung durch den Projektinhaber.

### Spielgefühl-Fragen (Vorschlagswerte, rückbaubar)
| # | Frage | Vorschlag |
|---|---|---|
| S1 | Wann darf gespeichert werden? | Überall wie Gothic 1, **auch im Kampf**; nicht im Dialog, beim Benutzen eines Mobs, beim Fallen/Schwimmen unter Wasser, im Ladebildschirm, im Hauptmenü, als Toter |
| S2 | Wie viele Plätze? | 15 Plätze + Schnellspeicher + 1 Autosave (Gothic 1: 15 + Schnell) |
| S3 | Autosave? | ja, ein Platz, beim Weltwechsel (nach dem Laden der neuen Welt) und beim Kapitelwechsel; abschaltbar unter Optionen → Spiel |
| S4 | Name eines Spielstands | automatisch „Ort – Kapitel n – Tag d, hh:mm“; eigene Eingabe später (Texteingabe mit Gamepad offen) |
| S5 | Schnellladen fragt nach? | nein (F9 lädt sofort, wie Gothic); Laden aus dem Menü fragt, wenn seit dem letzten Speichern > 2 min gespielt |
| S6 | Tod des Helden | heute steht er wieder auf (K8). Vorschlag bleibt so; ein Todesbildschirm (Laden/Hauptmenü) erst, wenn der Held sterben kann |
| S7 | Vorschaubild | Spielszene ohne HUD/Menü, Moment des Speicherns (beim Speichern aus dem Pausenmenü: das Bild vor dem Öffnen des Menüs) |
| S8 | Spielstände mit anderem Inhalt | laden mit Warnung (Inhalts-Prägung weicht ab), nicht sperren |

### Technische Punkte (Koordinator 2026-11-01: 2, 3, 4 freigegeben; 5, 6 klärt er mit welt; 1 = ADR 0021 offen)
1. JSON-Nutzlast + zstd im Binär-Container statt rein binär (ADR 0021) – nlohmann und zstd werden zusätzlich in
   `save` `PRIVATE` gelinkt (keine neue Abhängigkeit in `vcpkg.json`).
2. NPC-KI nach dem Laden: Tagesablauf neu beginnen statt Befehlsliste speichern.
3. Lua-Timer: nur benannte Funktionen überdauern; Closures sind flüchtig (Inhaltsregel in `script.md`).
4. Tabelle `02-architecture.md`: `save` zusätzlich `world_format`.
5. Vertrag VobId: „stabil über Neuerzeugungen“ – Rückfrage an welt.
6. Startpunkt am Unteren Tor in Leonberg und ein Weltwechsel-Trigger dorthin (welt) für das Kapitel.

## Format (Kurzfassung, bis M15 umgesetzt)
Binär, Chunks mit `tag u32, version u16, size u32` → robuste Erweiterung; Schreiben in Temp-Datei + atomisches
Umbenennen, CRC32 je Chunk. Einzelheiten oben und in ADR 0021.

## Schnittstelle Spielzeit (Stand M4)
- `world::GameTime` (world.md): zu speichern sind `clock()` (Minuten seit Tag 0, 00:00, mit Bruchteil – daraus Tag
  und Uhrzeit) und `secondsPerMinute()` (Skalierung); laden mit `restore(clock)` und `setSecondsPerMinute`.
- Nach dem Laden bekommen Zuhörer **ein** `Jumped`-Ereignis (NPC-Routinen springen in den Zustand zur geladenen
  Uhrzeit) – `restore` selbst meldet nichts, der Spielstand-Lader löst den Sprung aus (`setTime` bzw. eigenes Ereignis).
