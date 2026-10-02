# 0005 – Szenenmodell: EnTT-ECS mit Vob-Konzept

- **Status:** Vorgeschlagen (vor M4 bestätigen)
- **Datum:** 2026-10-02
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

## Konsequenzen
EnTT ist öffentliche Abhängigkeit von `world` (Registry in der API). Skripte sehen nie Entity-Handles, nur `VobId`/Namen.
