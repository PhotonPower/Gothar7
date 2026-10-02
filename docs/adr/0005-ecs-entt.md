# 0005 – Szenenmodell: EnTT-ECS mit Vob-Konzept

- **Status:** Akzeptiert (2026-10-03, mit den Ergänzungen VobId, API-Grenze und Version unten)
- **Datum:** 2026-10-02, ergänzt 2026-10-03
- **Phase:** M4

## Kontext
Gothic (ZenGin) nutzt eine klassische Klassenhierarchie (`zCVob` → `oCNpc`, `oCMob` …). Wir brauchen
Tausende Objekte, Speicherbarkeit und Flexibilität (ein Mob mit Licht und Sound).

## Optionen
1. **EnTT (ECS)** – sehr schnell, Komposition statt Vererbung, header-only, weit verbreitet; Template-lastig, Debugging etwas schwerer.
2. **Klassenhierarchie wie ZenGin** – intuitiv, nah am Vorbild; starre Hierarchie, virtuelle Aufrufe, schwer erweiterbar.
3. **Eigenes Entity-Component-System** – Kontrolle; Aufwand.

## Entscheidung
EnTT. Das Gothic-Vokabular bleibt erhalten: jede Entity mit `Vob`-Komponente ist ein Vob; „Mob“,
„Npc“, „Item“ sind Komponenten-Kombinationen. Persistente `VobId`s für Saves und Skripte.

### VobId – Format und Lebensdauer (Vertrag mit der Welt-Spur)
- `struct VobId { u64 value; }`, `0` = keine/ungültig. Eindeutig **je Welt**, stabil über Speichern/Laden,
  **nie wiederverwendet** – auch nicht nach dem Löschen eines Vobs.
- **Gespeichert** in `.g7world` (jeder Vob trägt seine ID) zusammen mit dem Zähler `nextVobId` der Welt.
- **Vergabe:** beim Erzeugen im Editor bzw. im Welt-Assembler (W3) aus `nextVobId`; der Zähler steigt nur.
  IDs aus importierten Teilwelten werden beim Zusammenführen neu vergeben, nie übernommen.
- **Zur Laufzeit** erzeugte Vobs (gespawnte Items, NPCs) erhalten IDs aus dem oberen Bereich
  `0x8000'0000'0000'0000 …` (`kRuntimeVobIdBase`), gezählt im Spielstand – sie kollidieren nie mit Welt-IDs, auch
  wenn die Welt später neue Vobs bekommt.
- Saves und Skripte verweisen ausschließlich über `VobId` (bzw. Namen) auf Vobs; `entt::entity` ist flüchtig und
  wird nie gespeichert.
- Der Vertrag steht in `docs/coordination.md` (Tabelle „Schnittstellen-Verträge“) und `docs/modules/world.md`.

### Grenze der öffentlichen API
- EnTT ist eine **öffentliche** Abhängigkeit von `world` (Ausnahme laut Architekturregeln): Komponenten sind
  einfache Structs, `entt::entity` darf in Signaturen von `world` vorkommen.
- Die `entt::registry` selbst gehört **nicht** zur öffentlichen API. `world::World` bietet die Operationen an,
  die obere Module brauchen: Vobs erzeugen/zerstören, nach `VobId`/Namen suchen, Komponenten setzen/lesen/entfernen,
  über Komponenten-Kombinationen iterieren (`World::each<Components...>(fn)`), Transform-Hierarchie.
- Obere Module (`ai`, `gameplay`, `ui`, `save`, `runtime`) greifen **nur über `World`** zu, nicht direkt auf die
  Registry. Braucht ein heißer Pfad später direkten Zugriff (z. B. Gruppen/Views über viele Komponenten), wird das
  in einer Zusatz-ADR begründet und als markierte Ausnahme (`World::registryForSystem()`) eingeführt.
- Skripte sehen nie Entity-Handles, nur `VobId`/Namen (CLAUDE.md).

### Version und Lizenz
- EnTT **3.16.0** (vcpkg-Port `entt`, `"version>=": "3.16.0"` in `vcpkg.json`; Baseline liefert 3.16.0),
  header-only, **MIT-Lizenz**. Der `nodeps`-Build lädt dieselbe Version per FetchContent (mit M4-Umsetzung).

## Konsequenzen
- `world` linkt EnTT öffentlich; Includes von `world` ziehen EnTT-Header mit (Kompilierzeit beobachten).
- Die VobId-Regeln sind ein Vertrag mit der Welt-Spur: Änderungen nur nach Absprache mit `welt`.
- Debugging: Komponenten-Inspektor im Editor (M4/M16) über `World::each`.
