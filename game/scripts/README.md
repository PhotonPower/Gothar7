# Spielskripte (Lua)

Der Spielinhalt als Lua-Skripte (seit **M7**) – das Gegenstück zu den Daedalus-Skripten von Gothic 1.
Bezeichner sind eigene, kleingeschriebene Namen (`it_…`, `npc_…`, `dia_…`, `rtn_…`, `zs_…`, `quest_…`).
Lade-Reihenfolge: `lib/` → `data/` → alles Übrige, je alphabetisch. Struktur:

```
scripts/
  items/        Item-Definitionen (Waffen, Ruestungen, Traenke, Runen ...)
  npcs/         NPC-Instanzen (Werte, Gilde, Ausruestung, Tagesablauf)
  lib/          gemeinsame Hilfen (require "lib.util")
  data/         Gilden, Tabellen
  routines/     Tagesablaeufe (rtn_*) – Zeitfenster -> Zustand + Wegpunkt
  states/       KI-Zustaende (zs_*) – Begin/Loop/End-Funktionen (M9)
  dialogs/      Dialog-Infos pro NPC (Bedingung, Text, Auswahl)
  quests/       Quests / Tagebuch-Eintraege
  startup.lua   Hilfen fuer die Konsole, Reaktion auf world_loaded
```

Konsole im Spiel: Taste ^. Referenz aller Engine-Funktionen: `docs/script-api.md`.
Details: `docs/modules/script.md`, `docs/modules/gameplay.md`, `docs/modules/ai.md`.
