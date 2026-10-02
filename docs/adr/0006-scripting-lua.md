# 0006 – Skriptsprache: Lua 5.4 mit sol2

- **Status:** Vorgeschlagen (vor M7 bestätigen)
- **Datum:** 2026-10-02
- **Phase:** M7

## Kontext
Gothic definiert Items, NPCs, Dialoge, KI-Zustände und Routinen in **Daedalus**, einer eigenen
C-artigen Skriptsprache. Das ist der Schlüssel zur Moddbarkeit.

## Optionen
1. **Lua 5.4 + sol2** – klein, schnell, bewährt in Spielen, Tabellen ideal für deklarative Instanzen, Hot-Reload einfach; dynamisch typisiert.
2. **Eigene Daedalus-ähnliche Sprache** – maximal nah am Original, statisch typisiert; Compiler + VM + Tooling = enormer Aufwand.
3. **AngelScript** – C++-ähnlich, statisch typisiert; kleinere Community.
4. **C# (Mono/.NET hosting)** – mächtig, gutes Tooling; schwergewichtig, komplexe Einbettung.
5. **Wren / Squirrel** – klein; wenig verbreitet.

## Entscheidung
Lua 5.4 + sol2. Statische Prüfung über LuaLS-Annotationen (`---@class`) und eine Engine-seitige
Validierung der Instanz-Tabellen beim Laden.

## Konsequenzen
Typfehler werden zur Ladezeit statt Kompilierzeit erkannt → Validierung muss gründlich sein.
