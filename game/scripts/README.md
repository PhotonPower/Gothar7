# Spielskripte (Lua)

Hier entsteht ab **Phase M7** der Spielinhalt als Lua-Skripte – das Gegenstück zu
den Daedalus-Skripten von Gothic 1. Struktur (geplant):

```
scripts/
  items/        Item-Definitionen (Waffen, Ruestungen, Traenke, Runen ...)
  npcs/         NPC-Instanzen (Werte, Gilde, Ausruestung, Tagesablauf)
  routines/     Tagesablaeufe (TA_*) – Zeitfenster -> Zustand + Wegpunkt
  states/       KI-Zustaende (ZS_*) – Begin/Loop/End-Funktionen
  dialogs/      Dialog-Infos pro NPC (Bedingung, Text, Auswahl)
  quests/       Quests / Tagebuch-Eintraege
  guilds.lua    Gilden und Einstellungen untereinander
  startup.lua   Welt-Start: NPCs und Items einfuegen
```

Details: `docs/modules/script.md`, `docs/modules/gameplay.md`, `docs/modules/ai.md`.
