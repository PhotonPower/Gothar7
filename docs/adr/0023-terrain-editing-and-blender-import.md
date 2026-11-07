# 0023 – Gelände im Editor und Import aus Blender

- **Status:** Angenommen (Projektinhaber, 2026-11-07)
- **Datum:** 2026-11-07
- **Phase:** M16 (Teil F, `docs/modules/editor.md`)

## Kontext
Die Roadmap verlangt „Gelände-Werkzeuge oder Import-Workflow aus Blender“. Leonbergs Gelände entsteht aus
LGL-Daten über welt (`terrain`-Block, `.r16`-Heightmap, Splat-Karten) und soll nicht im Editor geändert werden. Für
eigene Orte (Testlager, kleine Welten, spätere Innen-/Außenorte) fehlt jede Möglichkeit, Gelände zu formen. Innenräume
und Bauwerke werden schon in Blender gebaut (`.glb` mit `COL_`-Knoten).

## Optionen
1. **Höhen-Pinsel im Editor** (anheben, senken, glätten, ebnen; Splat-Schichten malen): direkt im Spiel sichtbar,
   Kollision und Kacheln aktualisiert. Aufwand: Schreiben der `.r16`/Splat-Karten, Teil-Update der Geländekacheln
   und der Kollision.
2. **Nur Import aus Blender:** Gelände in Blender modellieren, Mesh beim Kochen in eine Heightmap rastern
   (`g7-cook --heightmap-from-mesh`). Gut für Großformen, schlecht für Feinarbeit (Hin und Her).
3. **Gelände als normales Mesh** (kein Heightfield): beliebige Formen, aber verliert LOD-Kacheln, Löcher, Splat und
   die schnelle Heightfield-Kollision.
4. **Fremdwerkzeug** (z. B. World Machine/Gaea): Lizenz und kein Weg zurück in die Engine ohne Exportschritt.

## Entscheidung
**Option 1 plus Option 2:** Höhen-Pinsel im Editor für Feinarbeit, Mesh→Heightmap im Cooker für Großformen aus
Blender. Beides schreibt das vorhandene Format (`terrain`-Block v1, `.r16`, Splat), kein neues. Welten mit
`generator`-Kopf (Leonberg) sperren den Pinsel. Zusätzlich setzt ein `.glb`-Import leere Knoten
`VOB_<typ>_<name>` als Vobs (Licht, Startpunkt, Trigger) – Innenräume aus Blender ohne Nacharbeit im Editor.

## Konsequenzen
- `world::Heightfield` bekommt Schreiben, `render::TerrainRenderer` ein Teil-Update von Kacheln, `physics` ein Neubau
  des Heightfield-Körpers nach einem Strich.
- `g7-cook` bekommt `--heightmap-from-mesh` (Tests in `tests/cook`).
- Konvention `VOB_<typ>_<name>` für Blender-Knoten wird in `docs/modules/asset.md` als Vertrag aufgenommen (betrifft
  welt und figuren nur, wenn sie sie nutzen).
- Keine neue Abhängigkeit.
