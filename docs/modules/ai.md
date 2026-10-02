# ai

**Zweck:** Das „Eigenleben“ der Welt – das Herz von Gothic. Mechanik in C++, Verhalten in Skripten.

## 1. Wegnetz & Navigation
- **Wegpunkte (WP)**: benannte Knoten mit Position/Richtung, Kanten ungerichtet.
- **Freepoints (FP)**: lose Aufenthaltsorte mit Typ-Präfix (`FP_SIT_`, `FP_STAND_`, `FP_SMALLTALK_`, `FP_ROAM_`),
  werden per Belegung reserviert (`findFreepoint(npc, "SIT", radius)`).
- **A\*** auf dem Wegnetz, Heuristik euklidisch; Pfad = [aktuelle Position → nächster erreichbarer WP → … → Ziel].
- Folgen eines Pfades mit Charakter-Controller, Ausweichen bei Blockade (anderer NPC), Neuplanung bei Hindernis.
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
