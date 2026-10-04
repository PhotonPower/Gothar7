# ai

**Zweck:** Das „Eigenleben“ der Welt – das Herz von Gothic. Mechanik in C++, Verhalten in Skripten.

## 1. Wegnetz & Navigation (M9 Teil A, umgesetzt) – `ai/Waynet.hpp`, `runtime/EngineNpcs.cpp`
- **Wegpunkte (WP)** und **Freepoints (FP)** aus dem `waynet`-Block der Welt (world.md „Wegnetz“, Vertrag mit welt),
  `ai::Waynet::build`. Namen ohne Rücksicht auf Groß- und Kleinschreibung (`find`, `findFreepoint`); Freepoint-Typ
  = zweites Namensglied (`FP_SIT_…` → `SIT`). Belegung der Freepoints kommt mit Teil B.
- **A\*** über die ungerichteten Kanten (`path`), Heuristik euklidisch.
- **Route** (`route(von, ziel, gehbar)`): geradeaus, wenn die Linie gehbar ist; sonst zum nächsten von der Position
  aus gehbaren WP, über das Netz zum WP, von dem aus das Ziel gehbar ist, dann zum Ziel. **Glättung:** Von jedem Punkt
  aus geht es zum weitesten Folgepunkt, der noch in gerader Linie gehbar ist.
- **Gehbar** (Engine, `walkableLine`): Kugeln (Radius 0,25 m) in 0,5 / 1,0 / 1,5 m Höhe treffen auf der Linie nichts
  – Zäune mit Lücken zwischen den Latten zählen als Hindernis, niedrige Stufen nicht.
- **NPCs** (eingefügte `Npc`-Instanzen) haben eine eigene Kapsel wie der Held (`physics::CharacterController`):
  Schwerkraft, Stufen, Kollision mit der Welt. `npc_goto(npc, ziel, rennen)` bzw. `Engine::npcGoTo`: drehen sich zum
  nächsten Routenpunkt (6 rad/s), gehen (Geschwindigkeit aus den Blendpunkten des Menschen-Graphen) oder rennen, ein
  Punkt gilt in 0,35 m als erreicht. **Blockiert** (1,5 s ohne 0,3 m Fortschritt): neu planen, höchstens dreimal,
  dann `npc_blocked`; steht etwas auf dem Ziel und der NPC ist schon näher als 1,2 m, gilt er als angekommen. Ankunft:
  Ereignis `npc_arrived(npc, ziel)`.
- **Debug** (F2): Kanten, Wegpunkte mit Namen (bis 40 m), Freepoints mit Blickrichtung, die Routen gehender NPCs.
- Testlager: Wegnetz mit 11 Punkten und 4 Freepoints (um das Südende des Zauns herum; das Tor ist zu).
- Später optional: Navmesh für freie Bewegung im Kampf (ADR, wenn nötig).

## 2. NPC-Zustandsautomat (Gothic: „ZS_“-Zustände)
```
Zustand = { begin(self), loop(self) -> CONTINUE|END, end(self) }   -- definiert in Lua
```
- Ein NPC hat genau einen aktiven Zustand + eine **Befehlswarteschlange** (AI-Queue:
  „gehe zu WP“, „drehe zu X“, „spiele Animation“, „warte“, „sage Text“), die der Zustand füllt.
- Zustandswechsel: durch Routine (Zeitfenster), Wahrnehmung (Unterbrechung), Skript (`startState`).
- Unterbrechung speichert nicht den alten Zustand – nach Ende greift wieder die Routine (wie in Gothic).

## 3. Tagesabläufe (Routinen)
- Liste `(von, bis, Zustand, Wegpunkt)`; zu jeder Spielminute prüft das System Wechsel.
- Routinenwechsel per Skript (`setRoutine(npc, "Rtn_Ruvin_Ch2")`).
- **KI-LOD**: NPCs außerhalb der Simulationsdistanz (z. B. > 80 m) werden nicht simuliert; beim
  Wechsel des Zeitfensters werden sie direkt an den Ziel-WP teleportiert, beim Betreten der Distanz
  wird ihr aktueller Routinen-Zustand gestartet.

## 4. Wahrnehmung
| Sinn | Umsetzung |
|---|---|
| Sehen | Sichtkegel (Winkel, Reichweite je NPC) + Raycast; Spieler im Schleichmodus/Dunkelheit schwerer sichtbar |
| Hören | Lärmereignisse mit Position + Radius (Kampf, Schritte beim Rennen, Zauber, Truhe knacken) |
| Nähe | Radius (z. B. Spieler kommt zu nah) |

Ereignistypen → Skript-Reaktion (`perceptions.lua` je Gilde/NPC-Typ):
`AssessPlayer`, `AssessEnemy`, `AssessFighter` (Waffe gezogen), `AssessThreat`, `AssessTheft`,
`AssessUseMob` (fremde Truhe), `AssessEnterRoom` (Besitz), `AssessDamage`, `AssessMagic`,
`AssessCall` (Kamerad ruft um Hilfe), `AssessTalk`, `ObserveIntruder`.

- Wahrnehmungs-Update gestaffelt (nicht jeder NPC jeden Tick), Takt nach Distanz.
- Jede Wahrnehmung hat Reichweite und Priorität; Zustände können Wahrnehmungen aktivieren/deaktivieren.

## 5. Einstellungen & Gruppen
- Einstellung NPC→Spieler: dauerhaft + temporär (vergisst sich nach Zeit), abgeleitet aus Gilden-Tabelle, wenn nicht gesetzt.
- Kameraden: NPCs derselben Gilde/Freundschaft helfen im Kampf, wenn sie es wahrnehmen.
- Warnungen vor Angriff („Steck die Waffe weg!“) → Eskalationsstufen.

## 6. Monster
- Revier (Mittelpunkt + Radius), Rudel (Anführer + Mitglieder), Routinen Fressen/Schlafen/Umherstreifen,
  Drohen vor Angriff, Fliehen bei < X % Leben oder vor stärkeren Arten, Beutetier/Raubtier-Beziehungen.

## Geplante API
```cpp
namespace g7::ai {
class Waynet { public: std::optional<Path> findPath(Vec3 from, StringId toWp) const; ... };
struct Brain { StateRef current; RoutineRef routine; AiQueue queue; PerceptionSet active; ... }; // component
class AiSystem { public: void fixedUpdate(world::World&, script::ScriptVm&, f64 dt);
                 void emitNoise(Vec3 pos, f32 radius, NoiseType, entt::entity source); };
}
```

## Tests
Szenario-Tests: Testwelt + 3 NPCs, Spielzeit 24 h vorspulen, prüfen dass jeder NPC zur richtigen
Zeit am richtigen WP im richtigen Zustand ist; Spieler zieht Waffe → Wache reagiert binnen N Ticks.
