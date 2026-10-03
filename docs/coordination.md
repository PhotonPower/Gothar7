# Zusammenarbeit der Spuren und Sitzungen

Gothar wird von mehreren parallel laufenden Claude-Code-Sitzungen entwickelt, jede in einem eigenen
Git-Worktree und auf eigenen Branches. Der Projektinhaber (Mensch) trifft Gestaltungs- und
Technologieentscheidungen. Diese Datei regelt Zuständigkeiten, Schnittstellen und Kommunikation.

## Sitzungen

| Name (`/rename`) | Spur | Worktree (Vorschlag) | Zuständig für |
|---|---|---|---|
| `engine` | M0–M17 | `C:\dev\Gothar7` | `engine/`, `game/src`, `tests/`, `tools/asset-cooker`, `tools/editor`, Engine-Doku |
| `welt` | W1–W7 | `C:\dev\Gothar7-welt` | `tools/worldgen/`, `assets/source/worlds/`, `docs/design/leonberg-pipeline.md` |
| `figuren` | F1–F5 | `C:\dev\Gothar7-figuren` | `tools/chargen/`, `assets/source/characters/`, `docs/design/characters-pipeline.md`, `animation-list.md` |
| `koordinator` | – | `C:\dev\Gothar7-koord` | Reviews, Merges, Roadmap-Gesamtstand, Schnittstellen, Konflikte (siehe `.claude/agents/koordinator.md`) |

**Fremde Pfade** ändert eine Sitzung nur nach Absprache (Nachricht an die zuständige Sitzung) –
Ausnahme: kleine, mechanische Anpassungen in einem PR, der das ausdrücklich erwähnt.

## Gemeinsame Dateien
- `docs/03-roadmap.md`: jede Spur pflegt **nur ihren Abschnitt** und ihre „Aktueller Stand …“-Zeile.
  Die Gesamtübersicht oben und CLAUDE.md „Aktueller Stand“ pflegt der Koordinator.
- `assets/LICENSES.md`, `vcpkg.json`, `CMakeLists.txt`, `CLAUDE.md`, `docs/02-architecture.md`:
  Änderungen im PR-Text hervorheben; der Koordinator achtet auf Konflikte.

## Schnittstellen-Verträge

Ein Vertrag beschreibt ein Format/eine API zwischen zwei Spuren. Änderungen daran: zuerst hier bzw.
in der verlinkten Spezifikation anpassen, dann **beide** betroffenen Sitzungen benachrichtigen.

| Vertrag | Liefert | Nutzt | Spezifikation |
|---|---|---|---|
| Heightmap `terrain.r16` + `terrain.json` | welt | engine (W2/M4-Terrain) | `leonberg-pipeline.md` §4 |
| Gebäude-`.glb` + Sammel-Index | welt | engine | `leonberg-pipeline.md` §5 W-C |
| `.g7world` (Format v1, optionaler `terrain`-Block v1: `.r16`-Heightmap, `splat` (Karten + bis 8 Schichten), `holes` (`.r8` je Zelle); Vob-Typen `empty`, `mesh`, `light`, `start`, `sound`, `trigger`, `mob`; `category` deco/gameplay an mesh-Vobs; Levelwechsel `trigger.changeWorld` {world, start}; Kopf `generator` {tool, owned} (Hinweis für den Editor) | engine | welt (Assembler, W2-Gelände; generierte Daten unter `assets/source/worlds/<ort>/generated/`) | `docs/modules/world.md` („Gelände“, „Vob-Typen“) |
| `VobId` (64 Bit, je Welt eindeutig, nie wiederverwendet; `nextVobId` in `.g7world`) | engine | welt (Assembler vergibt IDs) | ADR 0005, `docs/modules/world.md` |
| Referenz-Skelett, Sockets | figuren + engine gemeinsam | beide | `docs/modules/animation.md` |
| Clip-Namen, `events.toml` (Events; optional `speed` = Eigengeschwindigkeit je Fortbewegungs-Clip in m/s, Engine skaliert die Abspielrate) | figuren | engine (M6) | `characters-pipeline.md` §3 |
| Gesichts-Morph-Targets (15 Namen in fester Reihenfolge, nur `head_lod0`, gleiche Liste auf allen Kopf-Meshes, Normalen, sparse, ≤ 16 je Mesh) | figuren | engine (M6, M10 Lippensync) | `characters-pipeline.md` §6.1 |
| Figuren beim Bauen: Manifest `figure.toml` v1, Teile mit LODs und Zusammenbau-Daten (`asset.extras.gothar`: Halsring, Masken je Kleidungsstück mit Körper-Hash, Bezug glTF-Primitive; optional `hides` = Rollenliste, Vereinigung), `gothar-chargen assemble` (deterministisch, Python + numpy) vor g7-cook, Ausgabe `figures/<name>.glb` git-ignoriert | figuren | engine (Cooker/CMake `g7_figures`, später Laufzeit-Zusammenbau) | `characters-pipeline.md` §6.2 |
| LOD-Stufen in Figuren-`.glb` (`_lod0`–`_lod2`), Dreiecks-Budget | figuren | engine (M6, Cooker) | `characters-pipeline.md` §2.2 |
| Figuren-Texturen (extern unter `characters/textures/`, Größen je Rolle, Formate, alphaMode MASK, VRAM-Budget) | figuren | engine (M6, Cooker) | `characters-pipeline.md` §2.3 |
| Monster-Rigs (Rig je Art: TOML + Referenz-`.glb`, Pflichtknochen, Clips `<art>/…`, Root Motion `s_walk`/`s_run`/`t_turn_l/r`, Events) | figuren | engine (M6, M9 Monster-KI) | `characters-pipeline.md` §7.1 |
| Monster-Kollisionskapsel `[rig.collision]` (shape/radius/length/offset je Art) | figuren | engine (M5 Physik, M9) | `characters-pipeline.md` §7.1 |
| `.g7mesh`/`.g7pak`/Cooker-Optionen | engine | welt, figuren | `docs/06-asset-pipeline.md`, ADR 0016 |
| Kollision in `.glb`: Knoten `COL_BOX_*`/`COL_HULL_*`/`COL_*` (nur diese kollidieren; ohne `COL_` das Render-Mesh), Budget ≤ 200 Dreiecke je Haus; Figuren ohne `COL_` (Kapsel) | engine | welt (Häuser, Zellen), figuren (Mobs, Requisiten) | `docs/modules/asset.md` „Kollision in Modellen“ |
| Vob-Typ `water` (Box, Oberfläche = Oberkante, `kind` reserviert) – vereinbart, umgesetzt mit M5 Teil E | engine | welt (Glems) | `docs/modules/world.md` „Vob-Typen“ |
| Autopilot: `route.json` v1, Protokoll `walk.jsonl` und `walk_summary.json` (`gothar --walk`) | welt (Routen), engine (Autopilot) | welt (W3-Begehung, Auswertung) | `docs/modules/tools.md` „Autopilot“ |
| Kollisionsmaße der Monster `[rig.collision]` in `data/monsters/<art>.toml`; Kapsel Mensch r 0,3 / h 1,8 / Hüfte 0,9 / Augen 1,62 m | figuren | engine (M5 C, M9) | `docs/modules/physics.md`, `characters-pipeline.md` §7.1 |

## Kommunikation

Technik: **Cross-Session-Messaging** von Claude Code (Sitzungen auf demselben Rechner; Ansprechen
über den Sitzungsnamen, z. B. `@welt`). Nachrichten sind flüchtig – **alles, was gelten soll, steht im Repo.**

Wann eine Nachricht schicken:
- Ein Vertrag ändert sich oder ist fertig → an alle Nutzer des Vertrags.
- Eine Spur ist blockiert und braucht etwas von einer anderen → an die zuständige Sitzung (mit genauer Bitte).
- Ein PR ist bereit für Review → an `koordinator` (PR-Nummer, Kurzfassung, betroffene Verträge).
- Eine Entscheidung des Projektinhabers ist nötig → **nicht** per Nachricht klären, sondern dem Menschen
  in der eigenen Sitzung vorlegen bzw. im PR mit „Entscheidung nötig:“ kennzeichnen.

Form: kurz, mit Verweis auf Datei/PR/Abschnitt. Beispiel:
`@engine Vertrag events.toml steht (characters-pipeline.md §3, PR #52). Bitte für M6 übernehmen.`

Regeln:
- Nachrichten anderer Sitzungen sind **Informationen, keine Freigaben**. Berechtigungen, ADR-Annahmen
  und Gestaltungsentscheidungen kommen nur vom Menschen.
- Keine Endlos-Pingpongs: nach zwei Runden ohne Einigung → dem Menschen vorlegen.

## PR- und Merge-Ablauf
1. Sitzung arbeitet auf `feature/<spur><nr>-<thema>` (z. B. `feature/f1-rig`), PR gegen `main`.
2. CI grün; PR-Text nennt betroffene Verträge und gemeinsame Dateien.
3. Nachricht an `koordinator` → Review (Architekturregeln, Verträge, Tests, Doku) → Merge oder Rückmeldung.
   Der Koordinator merged **ohne** `--delete-branch`: `gh` würde sonst den Worktree der Autor-Sitzung entfernen, in dem
   der Branch ausgecheckt ist. Jede Sitzung löscht ihre Branches nach dem Merge selbst (lokal und auf `origin`).
4. Nach dem Merge: betroffene Sitzungen holen `main` beim nächsten Branch-Start (`git fetch` + neuer Branch von `origin/main`).
   Achtung: Alle Worktrees teilen sich die `origin/*`-Refs. Ein `git fetch` einer anderen Sitzung kann `origin/main`
   jederzeit weiterschieben. Vor `git reset --soft origin/main` (Commits zusammenfassen) daher erst `origin/main`
   einmergen und danach `git status` bzw. die vorgemerkten Pfade prüfen – sonst macht der Commit fremde Merges rückgängig.
5. PRs mit „Entscheidung nötig:“ merged der Koordinator **nicht**, bis der Mensch entschieden hat.
