# script

**Zweck:** Skript-Laufzeit für Spielinhalt – Ersatz für Gothics **Daedalus**. Sprache: **Lua 5.4** (5.4.7)
mit **sol2** 3.5.0 (ADR 0006, akzeptiert). Lua und sol2 sind privat im Modul; die öffentliche API kennt nur
`g7::script::Value`.

## Prinzipien
- Inhalt ist **deklarativ** (Instanz-Tabellen) + **Funktionen** für Verhalten (Bedingungen, Dialog-Ablauf, KI-Zustände).
- Sandbox: keine `io`, `os`, `debug`-Bibliothek, kein `load`/`dofile`/`loadfile`/`collectgarbage`, `require` nur
  unterhalb von `game/scripts`.
- Fehler in Skripten bringen die Engine nicht zum Absturz: Log mit Datei:Zeile, die Datei bzw. Funktion wird
  abgebrochen, der Rest läuft weiter.
- **Bezeichner** sind eigene, kleingeschriebene Namen mit Unterstrich (`it_sword_old`, `npc_gate_guard`,
  `dia_gate_guard_hello`, `rtn_…`, `zs_…`); die Struktur ist Gothic-artig, aber ohne Original-Bezeichner oder
  -Figurennamen (ADR 0008, Entscheidung Projektinhaber 2026-10-03).
- Alle Engine-Funktionen sind in `docs/script-api.md` dokumentiert (ab M7 Teil B generiert aus den Bindings).

## Beispiele (Zielbild)
```lua
-- items/weapons.lua
Item "it_sword_old" {
    name = "Altes Schwert", category = "melee_1h", value = 40,
    damage = { edge = 18 }, requires = { str = 10 },
    mesh = "items/sword_old.g7mesh",
}

-- npcs/camp/gate_guard.lua
Npc "npc_gate_guard" {
    name = "Torwache", guild = "guard", level = 12, voice = 1,
    attributes = { str = 40, dex = 30, hp = 160 },
    talents = { melee_1h = 2 },
    equipment = { "it_sword_old" },
    routine = "rtn_gate_guard_start",
}

-- routines/gate_guard.lua
Routine "rtn_gate_guard_start" {
    { from = "08:00", to = "22:00", state = "zs_stand_guarding", at = "wp_camp_gate" },
    { from = "22:00", to = "08:00", state = "zs_sleep",          at = "wp_camp_guard_bed" },
}

-- dialogs/gate_guard.lua
Info "dia_gate_guard_hello" {
    npc = "npc_gate_guard", important = true, permanent = false,
    condition = function(self, other) return not Story.met_gate_guard end,
    run = function(self, other)
        say(other, self, "dia_gate_guard_hello_00")   -- Spieler spricht (Text-Schlüssel)
        say(self, other, "dia_gate_guard_hello_01")
        Story.met_gate_guard = true
        Log.create("topic_gate_guard", "Die Torwache hat mich durchgelassen.")
    end,
}
```

## Engine-Seite (umgesetzt mit M7 Teil A)
```cpp
namespace g7::script {
class Value;            // nil, bool, i64, f64, string, Table (Folge + benannte Felder), FunctionRef
struct ScriptConfig {   // runtime liefert die Dateien (script hängt nur von core ab)
    std::function<Result<std::string>(std::string_view path)> readFile;   // "items/weapons.lua"
    std::function<std::vector<std::string>()> listFiles;                  // alle .lua unterhalb der Wurzel
    std::function<void(std::string_view)> print;                          // Vorgabe: Log
    u64 instructionLimit = 50'000'000; usize memoryLimit = 256 MiB;
};
struct FieldSpec { name; Type type /*Any, Boolean, Integer, Number, String, StringList, Table, Function*/;
                   bool required; optional<f64> min, max; std::string refKind; };
struct InstanceKind { name; std::vector<FieldSpec> fields; bool allowUnknownFields; };
struct Instance { kind; name; Value fields; file; line; };
class ScriptVm {
public:
    static Result<ScriptVm> create(ScriptConfig);
    void defineKind(InstanceKind);                     // Lua: `Item "name" { … }`
    usize loadAll();                                   // lib/ → data/ → Rest, je alphabetisch; dann Schema-Prüfung
    Result<Value> runString(code, chunkName);          // Konsole: erst als Ausdruck, sonst als Anweisung
    Result<Value> call(FunctionRef, span<const Value>); Result<Value> callGlobal(name, args);
    Value global(name) const; void setGlobal(name, const Value&);
    const std::vector<ScriptError>& errors() const;    // Datei, Zeile, Text
    span<const Instance> instances() const; const Instance* findInstance(kind, name) const;
    std::vector<const Instance*> instancesOf(kind) const;
};
std::vector<std::string> loadOrder(std::vector<std::string> paths);
}
```
- **Lade-Reihenfolge:** `lib/` (gemeinsame Hilfen), `data/` (Gilden, Tabellen), dann aller übrige Inhalt; innerhalb
  alphabetisch. Ein Fehler stoppt nur seine Datei.
- **Instanzen:** `defineKind` legt eine globale Lua-Funktion an; `Item "it_x" { … }` merkt sich Tabelle, Datei und
  Zeile. Doppelte Namen sind ein Fehler (der erste gilt). Nach dem Laden prüft das Schema Typen, Pflichtfelder,
  Wertebereiche, Verweise auf andere Instanzen (`refKind`, auch in Listen) und unbekannte Felder – Meldungen wie
  `npcs/camp/gate_guard.lua:2: Npc "npc_gate_guard": field 'level' = 300 is outside 1 .. 100`. Welche Arten es gibt
  und welche Felder sie haben, legt `gameplay` fest (M7 Teil C).
- **Funktionen** in Instanzen (Bedingungen, Dialog-Abläufe) werden als `FunctionRef` aufbewahrt und mit `call`
  aufgerufen; gültig bis zum nächsten Neuladen.
- **Grenzen:** Jeder Lauf (Datei, Konsolenzeile, Aufruf) hat ein Befehlsbudget (Endlosschleifen enden mit Fehler),
  der Speicher der VM ist begrenzt. `print` geht ins Log bzw. in die Konsole.
- **Bindings** registriert jedes Modul selbst (`gameplay` registriert `say`, `giveItem`…, `ai` registriert
  `gotoWaypoint`…), damit `script` keine Abhängigkeit nach oben hat – Schnittstelle mit Teil B.
