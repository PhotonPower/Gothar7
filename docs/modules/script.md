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

-- npcs/oldcamp/diego.lua
Npc "Diego" {
    name = "Diego", guild = "SHADOW", level = 12, voice = 11,
    attributes = { str = 40, dex = 50, hp = 160 },
    talents = { melee_1h = 2, bow = 1 },
    equipment = { "ItMw_1H_Sword_Old", "ItAr_Shadow_L" },
    routine = "Rtn_Diego_Start",
}

-- routines/diego.lua
Routine "Rtn_Diego_Start" {
    { from = "08:00", to = "22:00", state = "ZS_Stand_Guarding", at = "OC_ENTRANCE" },
    { from = "22:00", to = "08:00", state = "ZS_Sleep",          at = "OC_HUT_DIEGO_BED" },
}

-- dialogs/diego.lua
Info "DIA_Diego_Hello" {
    npc = "Diego", important = true, permanent = false,
    condition = function(self, other) return not Story.metDiego end,
    run = function(self, other)
        say(other, self, "DIA_Diego_Hello_15_00")   -- Spieler spricht (Text-Schlüssel)
        say(self, other, "DIA_Diego_Hello_11_01")
        Story.metDiego = true
        Log.create("TOPIC_Diego", "Diego hat mir angeboten zu helfen.")
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
