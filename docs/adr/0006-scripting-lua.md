# 0006 – Skriptsprache: Lua 5.4 mit sol2

- **Status:** Akzeptiert (2026-10-03, Entscheidung Projektinhaber)
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

Der Projektinhaber hat die Alternativen (eigene Daedalus-Sprache, AngelScript, C#) bewusst erwogen und sich für Lua
entschieden (2026-10-03).

**Versionen:** Lua **5.4.7** (MIT) und sol2 **3.5.0** (MIT, header-only), beide `PRIVATE` im Modul `script`; die
öffentliche API trägt nur `g7::script::Value`. Die vcpkg-Baseline liefert inzwischen Lua 5.5 (geänderte Sprache und
C-API) – `vcpkg.json` legt Lua daher per `overrides` auf 5.4.7 fest. `nodeps` lädt dieselben Versionen per
FetchContent (Lua aus den Quellen gebaut, sol2 nur der include-Ordner, beide mit SHA256).

**Bezeichner:** eigene, kleingeschriebene Namen (`it_sword_old`, `npc_…`, `dia_…`); die Struktur bleibt Gothic-artig
(Item, Npc, Info, Quest, Routine) – keine Original-Bezeichner oder -Figurennamen (ADR 0008).

## Konsequenzen
Typfehler werden zur Ladezeit statt Kompilierzeit erkannt → Validierung muss gründlich sein (Schema je
Instanz-Art beim Laden, `docs/modules/script.md`).
