# script

**Zweck:** Skript-Laufzeit für Spielinhalt – Ersatz für Gothics **Daedalus**. Sprache: **Lua 5.4**
mit **sol2** (ADR 0006).

## Prinzipien
- Inhalt ist **deklarativ** (Instanz-Tabellen) + **Funktionen** für Verhalten (Bedingungen, Dialog-Ablauf, KI-Zustände).
- Sandbox: keine `io`, `os`, `debug`-Bibliothek, kein `require` außerhalb von `game/scripts`.
- Fehler in Skripten bringen die Engine nicht zum Absturz: Log mit Datei:Zeile, Funktion wird abgebrochen.
- Alle Engine-Funktionen sind in `docs/script-api.md` dokumentiert (ab M7 generiert aus den Bindings).

## Beispiele (Zielbild)
```lua
-- items/weapons.lua
Item "ItMw_1H_Sword_Old" {
    name = "Altes Schwert", category = "melee_1h", value = 40,
    damage = { edge = 18 }, requires = { str = 10 },
    mesh = "items/sword_old.g7mesh",
}

-- npcs/hauptlager/ruvin.lua
Npc "Ruvin" {
    name = "Ruvin", guild = "HUNTER", level = 12, voice = 11,
    attributes = { str = 40, dex = 50, hp = 160 },
    talents = { melee_1h = 2, bow = 1 },
    equipment = { "ItMw_1H_Sword_Old", "ItAr_Hunter_L" },
    routine = "Rtn_Ruvin_Start",
}

-- routines/ruvin.lua
Routine "Rtn_Ruvin_Start" {
    { from = "08:00", to = "22:00", state = "ZS_Stand_Guarding", at = "HC_ENTRANCE" },
    { from = "22:00", to = "08:00", state = "ZS_Sleep",          at = "HC_HUT_RUVIN_BED" },
}

-- dialogs/ruvin.lua
Info "DIA_Ruvin_Hello" {
    npc = "Ruvin", important = true, permanent = false,
    condition = function(self, other) return not Story.metRuvin end,
    run = function(self, other)
        say(other, self, "DIA_Ruvin_Hello_15_00")   -- Spieler spricht (Text-Schlüssel)
        say(self, other, "DIA_Ruvin_Hello_11_01")
        Story.metRuvin = true
        Log.create("TOPIC_Ruvin", "Ruvin hat mir angeboten zu helfen.")
    end,
}
```

## Engine-Seite
```cpp
namespace g7::script {
class ScriptVm {
public:
    Result<void> init(const ScriptConfig&);
    Result<void> loadDirectory(std::string_view root);        // load order: lib → data → content
    InstanceRegistry& instances();                            // Item/Npc/Info/... templates by name
    template <class... A> Result<sol::object> call(FunctionRef, A&&...);
    void tick(f64 dt);                                        // timers, delayed calls
    void hotReload();                                         // dev mode
};
}
```
Bindings werden pro Modul registriert (`gameplay` registriert `say`, `giveItem`…, `ai` registriert `gotoWaypoint`…),
damit `script` keine Abhängigkeit nach oben hat.
