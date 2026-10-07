# Zusammenarbeit der Spuren und Sitzungen

Gothar wird von mehreren parallel laufenden Claude-Code-Sitzungen entwickelt, jede in einem eigenen
Git-Worktree und auf eigenen Branches. Der Projektinhaber (Mensch) trifft Gestaltungs- und
Technologieentscheidungen. Diese Datei regelt Zuständigkeiten, Schnittstellen und Kommunikation.

## Sitzungen

| Name (`/rename`) | Spur | Worktree (Vorschlag) | Zuständig für |
|---|---|---|---|
| `engine` | M0–M17 | `C:\dev\Gothar7` | `engine/`, `game/src`, `tests/`, `tools/asset-cooker`, `tools/editor`, `tools/voice`, Engine-Doku |
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
| Mobs: `assets/source/data/mobs.toml` v1 (Slots `pos` + `facing`-Vektor, `enter`/`loop`/`leave`/`extra`), Mob-Clips `mob/<typ>/…`, Events `pickup`/`use`/`open`/`close`/`hit_anvil`/`lie`/`stand`/`item_from_hand`, Achsen (Y oben, Vorderseite +Z), Mob-Modelle (Tür um +Y an der Angel, `MOB_LID`), Item-Ausrichtung am Socket | engine (Datei), figuren (Slots, Clips) | engine (M8), welt (Mob-Modelle) | `characters-pipeline.md` §3.1 |
| Clip-Namen, `events.toml` (Events; optional `speed` = Eigengeschwindigkeit je Fortbewegungs-Clip in m/s, Engine skaliert die Abspielrate) | figuren | engine (M6) | `characters-pipeline.md` §3 |
| Gesichts-Morph-Targets (15 Namen, Zuordnung nach Namen seit M10/#188, Reihenfolge empfohlen; nur `head_lod0`, gleiche vollständige Liste auf allen Kopf-Meshes und Teilen mit Morphs wie `beard`, Normalen, sparse, ≤ 16 je Mesh) | figuren | engine (M6, M10 Lippensync) | `characters-pipeline.md` §6.1 |
| Figuren beim Bauen: Manifest `figure.toml` v1, Teile mit LODs und Zusammenbau-Daten (`asset.extras.gothar`: Halsring, Masken je Kleidungsstück mit Körper-Hash, Bezug glTF-Primitive; optional `hides` = Rollenliste, Vereinigung), `gothar-chargen assemble` (deterministisch, Python + numpy) vor g7-cook, Ausgabe `figures/<name>.glb` git-ignoriert | figuren | engine (Cooker/CMake `g7_figures`, Laufzeit-Zusammenbau umgesetzt in M6 D2 / #122) | `characters-pipeline.md` §6.2 |
| Figuren-Sets `assets/source/data/figure_sets.toml` (`[sets] <name> = [Manifest-Pfade relativ zu `assets/source`]`; Sets `citizen`, `craftsman`, `guard`, `farmer`, `hunter`, `outcast` und je Geschlecht `<set>_m`/`<set>_f` (`guard`, `hunter` nur `_m`; `<set>` = Vereinigung); benannte Figuren wie `smith` stehen in keinem Set; vereinbart 2026-10-04, Geschlechter-Sets 2026-10-05) | figuren | engine (`Npc.figure_set`: zufällig, je NPC stabil; `Npc.figure` hat Vorrang) | `characters-pipeline.md` §6.4 |
| LOD-Stufen in Figuren-`.glb` (`_lod0`–`_lod2`), Dreiecks-Budget | figuren | engine (M6, Cooker) | `characters-pipeline.md` §2.2 |
| Figuren-Texturen (extern unter `characters/textures/`, Größen je Rolle, Formate, alphaMode MASK, VRAM-Budget) | figuren | engine (M6, Cooker) | `characters-pipeline.md` §2.3 |
| Monster-Rigs (Rig je Art: TOML + Referenz-`.glb`, Pflichtknochen, Clips `<art>/…`, Root Motion `s_walk`/`s_run`/`t_turn_l/r`, Events) | figuren | engine (M6, M9 Monster-KI) | `characters-pipeline.md` §7.1 |
| Monster-Kollisionskapsel `[rig.collision]` (shape/radius/length/offset je Art) | figuren | engine (M5 Physik, M9) | `characters-pipeline.md` §7.1 |
| `.g7mesh`/`.g7pak`/Cooker-Optionen | engine | welt, figuren | `docs/06-asset-pipeline.md`, ADR 0016 |
| Kollision in `.glb`: Knoten `COL_BOX_*`/`COL_HULL_*`/`COL_*` (nur diese kollidieren; ohne `COL_` das Render-Mesh), Budget ≤ 200 Dreiecke je Haus; Figuren ohne `COL_` (Kapsel) | engine | welt (Häuser, Zellen), figuren (Mobs, Requisiten) | `docs/modules/asset.md` „Kollision in Modellen“ |
| Detailstufen statischer Modelle: Render-Knoten `<name>` (oder `<name>_lod0`), `<name>_lod1`, `<name>_lod2` mit gleichem Ursprung und geteilten Materialien; `COL_*` nur einmal, LOD-unabhängig; Auswahl je Vob nach Entfernung Kamera–Bounds-Mitte (engine.toml `[render] lod1_distance` 60, `lod2_distance` 150, Hysterese 10 %); ohne `_lod`-Knoten wie bisher (vereinbart 2026-10-04) | engine | welt (Häuser), figuren | `docs/modules/asset.md` „Detailstufen (LOD) statischer Modelle“ |
| Vob-Typ `water` (Box, Oberfläche = Oberkante, `kind` reserviert) – vereinbart, umgesetzt mit M5 Teil E | engine | welt (Glems) | `docs/modules/world.md` „Vob-Typen“ |
| Autopilot: `route.json` v1, Protokoll `walk.jsonl` und `walk_summary.json` (`gothar --walk`) | welt (Routen), engine (Autopilot) | welt (W3-Begehung, Auswertung) | `docs/modules/tools.md` „Autopilot“ |
| Wegnetz `waynet` in `.g7world` v1 (Punkte `WP_`, Freepoints `FP_<TYP>_`, ungerichtete Kanten per Name, `owner: "worldgen"` wie bei Vobs; vereinbart 2026-10-04) | engine | welt (Wegnetz-Vorschlag aus Straßenachsen, W-G), engine (KI M9, Editor M16) | `docs/modules/world.md` „Wegnetz“ |
| Sprechtexte `assets/source/voice/lines.<sprache>.json` v1 (je Zeile: Text, Sprecher, `gender` m/f, `voice`, Regieanweisung, Kontext (Datei), Status offen/aufgenommen/abgenommen, `orphan`, Takes). **Schlüssel vergibt nur `gothar-voice scan`** aus den Skripten (Texte inline, E2): Dialog `<info>_NN` in Quelltext-Reihenfolge (`description`, `say`, `choice`, Antwort-Tabelle von `teach_menu`), Zurufe `svm_<stimme>_<m|f>_<anlass>_NN` für jede Stimme aus `data/voices.lua` (Projektinhaber: Stimme je Gilde und Geschlecht, `Npc.voice`/`Npc.gender`); geänderter Text → wieder offen, entfernter → nur `orphan`. Die Engine findet den Schlüssel über (Info, Text) bzw. (Stimme, Geschlecht, Text). Ins Repo nur der gewählte Take `voice/<sprache>/<key>.wav`; alle Takes lokal unter `DATA_ROOT/voice/takes/<sprache>/<key>__tNN.wav` (Projektinhaber). CI: `gothar-voice check` (Datenbank passt zu den Skripten). Vereinbart 2026-10-04, Schlüssel und Ablage 2026-10-05 | engine (Schema, Scan, Werkzeug) | Projektinhaber (Regie, TTS-Takes), engine (M10 Dialoge, M13 Sprachausgabe, M14 Untertitel/Lokalisierung, Cooker `.wav` → `.ogg`) | `tools/voice/README.md`, `docs/modules/audio.md` „Sprache“ |
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
   Seit 2026-10-05 ist `main` geschützt (8 Pflicht-Checks: build ×2, coverage, worldgen ×2, chargen ×2, voice) und Auto-Merge
   eingeschaltet: Der Koordinator setzt nach dem Review `gh pr merge <nr> --auto --merge`; GitHub merged, sobald alle Checks
   grün sind. Abgebrochene oder nie gestartete Jobs startet `.github/workflows/rerun-cancelled.yml` bis zu dreimal neu
   (nicht bei echten Fehlern) – vor einem manuellen Neustart etwa 3 Minuten abwarten. Gestapelte PRs dürfen Auto-Merge haben.
   Ein neuer CI-Job muss in die Pflicht-Checks aufgenommen werden (Repo-Einstellung → nur mit Zustimmung des Menschen).
4. Nach dem Merge: betroffene Sitzungen holen `main` beim nächsten Branch-Start (`git fetch` + neuer Branch von `origin/main`).
   Achtung: Alle Worktrees teilen sich die `origin/*`-Refs. Ein `git fetch` einer anderen Sitzung kann `origin/main`
   jederzeit weiterschieben. Vor `git reset --soft origin/main` (Commits zusammenfassen) daher erst `origin/main`
   einmergen und danach `git status` bzw. die vorgemerkten Pfade prüfen – sonst macht der Commit fremde Merges rückgängig.
5. PRs mit „Entscheidung nötig:“ merged der Koordinator **nicht**, bis der Mensch entschieden hat.
