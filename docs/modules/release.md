# release – Plan M17 (Paket, Versions-Info, Absturzberichte)

Stand: **Plan freigegeben** (Projektinhaber, 2026-11-07; alle Vorschläge übernommen). Kein Code in diesem PR.
Ziel ist ein Paket, das ohne Repository läuft: ein ZIP mit dem Spiel, den gekochten Assets in `.g7pak`, den Lizenzen
und Pflichtangaben, einer sichtbaren Version und lokalen Absturzberichten. Die Entscheidungen des Projektinhabers stehen am Ende,
nummeriert.

## Umsetzungsstand
| Teil | PR | Stand |
|---|---|---|
| A – Versions- und Build-Info | #380 | umgesetzt |
| B – Absturzberichte | #382 | umgesetzt |
| C – Paket | dieser Teil-C-PR | umgesetzt |

Abweichungen bei A:
- Die Build-Info entsteht nicht beim Konfigurieren, sondern **bei jedem Build** (`cmake/BuildInfo.cmake` als Ziel
  `g7_build_info`). Die Datei wird nur bei geändertem Inhalt neu geschrieben: Ein Build ohne Änderung übersetzt
  nichts, Hash und „verändert“-Kennzeichen stimmen trotzdem immer.
- Version 0.3.0 nach Meilenstein C (Entscheidung 5). Der Git-Tag setzt die Version noch nicht selbst. Teil C prüft
  beim Paketbau, dass Tag und `project(VERSION)` übereinstimmen.
- Der Fenstertitel zeigt die Build-Zeile nur in Debug-Builds; im Release steht dort „Gothar“.

Abweichungen bei B:
- Statt eines versteckten `--crash-test` in `gothar` stürzt ein eigener kleiner Prozess ab (`g7_crash_probe`). So
  hängt der Test nicht am Spiel, und der Test-Runner stürzt nie selbst ab.
- Unter Windows landet eine ungefangene C++-Ausnahme beim Ausnahme-Filter, nicht bei `std::terminate`. Der Bericht
  nennt dann „uncaught exception (C++ exception 0xE06D7363 …)“ ohne `what()`. Unter Linux steht `what()` dabei.
- Der Hinweis (Entscheidung 3) ist ein Menüeintrag „Absturzbericht ansehen“ über „Beenden“. Er öffnet den Ordner und
  erscheint in der Sitzung, in der die Berichte neu sind; danach gelten sie als gesehen.
- Ein Stapelüberlauf wird unter Windows erkannt, der Bericht kann dann aber ausfallen: Der Filter läuft auf dem
  übergelaufenen Stapel. Nachbessern nur bei Bedarf (eigener Thread für den Bericht).

Abweichungen bei C:
- Die Lizenzprüfung steckt in `g7-cook --check-licences <assets> [<CREDITS.txt> <Versionszeile>]` (C++,
  `tools/asset-cooker/src/Licences.cpp`, Tests in `tests/cook`), nicht in einem Python-Skript: CI braucht so keinen
  neuen Job. Sie prüft:
  - Jede Datei unter `assets/source` hat in `assets/licenses.toml` eine Quelle; der erste Treffer gilt, `*` innerhalb
    eines Ordners, `**` über Ordner hinweg.
  - Deren Lizenzarten dürfen ausgeliefert werden.
  - Lizenzarten mit Namensnennung haben eine `credit`-Zeile.
  - Jede Quelle steht mit ihrer `md`-Wendung in `LICENSES.md`.
  - Jede Lizenz, die eine „Lizenz“-Spalte in `LICENSES.md` nennt, hat eine Art in `licenses.toml`. Erkannt werden CC0,
    CC BY 4.0, Datenlizenz Deutschland, ODbL, OFL, Public Domain, eigene Arbeit (des Projekts bzw. des
    Projektinhabers) und die **CMU-Nutzungsbedingungen** (figuren #369; der `LICENSES.md`-Eintrag darf bis zum
    Merge fehlen, `optional_md`). Ein ctest-Fall prüft das Repository bei jedem Lauf.
- `CREDITS.txt` ordnet die Pflichtangaben nach `credit_order` (LGL, OSM, Schriften, CMU) und nennt danach die
  freiwilligen Dank-Zeilen.
- Die Skripte liegen im Paket als Ordner `scripts/` neben dem Spiel, nicht in `data.g7pak`: Der Cooker kennt nur
  eine Quelle. Die Release-Engine liest sie dort ohnehin (`mountScripts`). Sie sind eigene Arbeit (MIT).
- `config/engine.toml` mountet `data/*.g7pak` (Priorität 20). In der Entwicklung fehlt der Ordner, dann geschieht
  nichts.
- `gothar` kennt `--user-dir`. Der Paket-Test und die ctest-Aufrufe von `gothar` legen damit nichts im echten
  Benutzerordner an; `SDL_GetPrefPath` würde ihn sonst beim Start erzeugen.
- Der Paket-Test ist ein eigenes Testprogramm (`g7_tests_package`, Label `package`). Ohne gebautes Paket
  (`G7_PACKAGE` leer) prüft er nichts. Er entpackt nur in `runTempDir()`.
- Der Tag-Abgleich (Entscheidung 5) steckt in `tools/release/release_data.cmake`: Ein Tag `vX.Y.Z` auf HEAD muss
  `project(VERSION)` entsprechen, sonst bricht der Paketbau ab.
- Der Menüpunkt „Mitwirkende“ (Entscheidung 10) erscheint, wenn `CREDITS.txt` neben dem Spiel liegt, also im Paket,
  und zeigt sie als Schriftstück.
- CI: `.github/workflows/release.yml` läuft auf Tags `v*` (oder von Hand). Es baut das ZIP (Windows) bzw. `.tar.gz`
  (Linux, ohne Zusage) und bewahrt Paket und `.pdb` 90 Tage als Artefakte auf. Veröffentlicht wird nichts.

## Ausgangslage
- **Bauen:** `release`-Preset und CI-Job `release (ubuntu-24.04)` (optimierter Build, Tests ohne GPU, `--smoke-test`).
  Ein Paket entsteht nirgends; `gothar_data` kopiert `config/`, `shaders/` und `scripts/` neben die ausführbare Datei.
- **Assets:** `g7-cook` kocht `assets/source` nach `assets/cooked` bzw. packt ein `.g7pak` (zstd, Manifest, ADR 0016).
  Skripte kommen in Entwicklungs-Builds aus `game/scripts`, sonst aus der Kopie neben dem Spiel (`EngineScripts.cpp`).
  Figuren aus Teilen baut `g7_figures` (git-ignoriert); Figuren und Clips laden als glTF (ADR 0019: der Cooker kopiert sie).
- **Version:** `g7::kEngineVersion` 0.1.0 (`core/Version.hpp`) steht in der ersten Log-Zeile und im Spielstand
  (`SaveMeta::gameVersion`). Ein Git-Hash fehlt überall.
- **Abstürze:** kein Handler. `G7_ASSERT` bricht ab, ohne etwas zu hinterlassen außer dem Log.
- **Lizenzen:** `assets/LICENSES.md` führt jede Fremdquelle (Geodaten LGL und OSM mit Pflichtangabe, sonst CC0).
  Die Code-Bibliotheken (vcpkg) stehen in `docs/05-build.md`, nicht im Paket.

## Teil A – Versions- und Build-Info
- **Erzeugt beim Konfigurieren** (CMake, keine neue Bibliothek): `g7/core/BuildInfo.hpp` im Build-Ordner mit Version
  (aus `project(VERSION …)`), kurzem Git-Hash, „verändert“-Kennzeichen (ungespeicherte Änderungen), Build-Typ und
  Datum des Commits. Das Bau-Datum bleibt außen vor, damit gleiche Quellen gleiche Dateien ergeben.
  Ohne Git (Quell-ZIP) steht `unknown` da.
- **Neu gebaut**, wenn sich `HEAD` ändert (CMake beobachtet `.git/HEAD` und die Ref-Datei), sonst nicht – sonst baut
  jeder Commit alles neu.
- **Sichtbar:**
  - Log, erste Zeile: `Gothar 0.4.0 (abc1234, release)`.
  - Hauptmenü unten rechts, klein und blass (Entscheidung 4).
  - `gothar --version` druckt dieselbe Zeile.
  - Spielstand: `SaveMeta::gameVersion` mit Hash; das Laden prüft weiterhin nur das Format, nicht den Hash.
  - Absturzbericht (Teil B) und Fenstertitel in Entwicklungs-Builds.
- **Tests:** `tests/core`: Format der Zeile; `tests/runtime`: erste Log-Zeile enthält den Hash bzw. `unknown`.

## Teil B – Absturzberichte (lokal, nichts wird hochgeladen)
- **Ort:** Modul `platform` (Systemaufrufe nur dort, `docs/02-architecture.md`), `platform/CrashHandler.hpp`:
  `installCrashHandler(CrashSettings{directory, buildLine, logTail})`.
- **Windows:** `SetUnhandledExceptionFilter` und `MiniDumpWriteDump` aus `dbghelp` (Teil des Windows SDK, keine neue
  Bibliothek). Ausgabe: `crashes/gothar_<version>_<hash>_<zeit>.dmp` (Minidump mit Stapel und Threads, ohne ganzen
  Speicher) und daneben `.txt` mit Build-Zeile, Grund (Ausnahme-Code) und den letzten 200 Log-Zeilen.
- **Linux:** Signal-Handler (`SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGILL`) schreibt nur die `.txt` mit Rückverfolgung
  (`backtrace`, glibc) – ein Minidump-Format gibt es ohne Fremdbibliothek nicht (Entscheidung 7).
- **Auch:** `std::set_terminate` (ungefangene Ausnahme) und `G7_ASSERT`/`G7_VERIFY` in Release-Builds schreiben vor
  dem Abbruch denselben Bericht.
- **Verzeichnis:** Vorgabe `<Benutzerordner>/crashes` (dort, wo Spielstände und Einstellungen liegen), änderbar über
  `EngineConfig::crashDirectory` bzw. `--crash-dir`. Tests und CI setzen immer einen eigenen Ordner, nie den
  Benutzerordner (Regel: nichts unter `%APPDATA%\Gothar` aus Tests).
- **Nach einem Absturz:** Der nächste Start sieht neue Berichte und sagt es einmal (Entscheidung 3). Nichts wird gesendet,
  es gibt keinen Hintergrund-Upload und keine Abfrage.
- **Symbole:** Die `.pdb` kommen nicht ins ZIP, sondern als eigenes CI-Artefakt je Version (Entscheidung 6). Damit lässt
  sich ein Minidump aus der Hand des Projektinhabers später auswerten.
- **Alternativen, verworfen:** Crashpad/Breakpad (neue Bibliothek, Upload-Infrastruktur, für lokal zu groß);
  Windows Error Reporting allein (Berichte landen beim System, nicht beim Projekt).
- **Tests:** verstecktes `--crash-test=<art>` (Zugriffsfehler, Ausnahme, Assert) nur in Test- und Entwicklungs-Builds;
  ein Test startet `gothar` mit eigenem `--crash-dir` als Kindprozess und prüft `.dmp`/`.txt`. In CI auf Windows und
  Linux.

## Teil C – Paket (ZIP; Installer optional)
- **Werkzeug:** CMake `install()` und CPack mit dem ZIP-Generator (Teil von CMake, keine neue Bibliothek).
  Ziel `package`; Skript `tools/release/make_release` (Python) führt die Schritte nacheinander aus:
  1. Release-Build (`--preset release`), Figuren bauen (`g7_figures`).
  2. Lizenzprüfung (siehe unten) – bricht bei Fehlern ab.
  3. Kochen: `g7-cook --textures ktx2 --pack data.g7pak` über `assets/source` **und** `game/scripts` (Skripte ins
     Paket statt lose; die Engine liest sie schon über das VFS), gebaute Figuren eingeschlossen.
  4. `cpack -G ZIP` → `Gothar-<version>-<hash>-windows-x64.zip`.
  5. **Reiner Paket-Test:** ZIP in einen leeren Ordner entpacken, `gothar --smoke-test` mit
     `assets.dev_mounts = false`, dazu ein Lauf, der das Lager mit NPCs und Figuren lädt (`--frames=60`). Damit ist
     belegt, dass nichts mehr aus `assets/source` kommt.
- **Inhalt des ZIP:**
  `gothar.exe`, `config/engine.toml`, `shaders/`, `data.g7pak`, `LICENSES/` (Lizenztexte der Bibliotheken),
  `CREDITS.txt` (Pflichtangaben der Assets), `THIRD_PARTY_NOTICES.txt` (Bibliotheken mit Lizenz), `README.txt`
  (Start, Systemanforderungen, Ort der Spielstände und Absturzberichte). Keine Quellen, keine Tests, keine `.pdb`.
- **Lizenzprüfung (nur weitergabefähige Lizenzen):**
  - Maschinenlesbar neben `assets/LICENSES.md`: `assets/licenses.toml` mit Pfadmustern, Lizenz und Pflichtangabe je
    Quelle. `LICENSES.md` bleibt die lesbare Fassung; beide prüft CI auf Gleichstand.
  - Erlaubt: eigene Arbeit, CC0, CC BY (mit Angabe), DL-DE-BY 2.0 (LGL), ODbL (OSM, nur abgeleitete Werke im Paket,
    Angabe Pflicht). Alles andere bricht ab, ebenso jede Datei ohne passenden Eintrag.
  - Daraus entsteht `CREDITS.txt`: LGL („Datengrundlage: LGL, www.lgl-bw.de“), OSM („© OpenStreetMap-Mitwirkende“),
    freiwillige Nennungen (Kenney, Quaternius, MakeHuman, ambientCG) und, falls im Paket, Bewegungsdaten etwa aus
    der CMU-Datenbank mit deren Angabe. Heute nutzt kein Asset CMU-Daten; der Eintrag kommt mit dem ersten.
  - Bibliotheken: `THIRD_PARTY_NOTICES.txt` aus einer Liste in `tools/release/` (SDL3 zlib, Jolt MIT, Lua MIT, sol2
    MIT, EnTT MIT, fastgltf MIT, glm MIT, Dear ImGui MIT, nlohmann-json MIT, toml++ MIT, miniaudio Public Domain/
    MIT-0, stb Public Domain, zstd BSD, libktx Apache-2.0, msdfgen MIT, glad MIT). Die Liste prüft CI gegen
    `vcpkg.json`, damit keine Bibliothek ohne Eintrag dazukommt.
- **Installer:** optional über CPack (NSIS oder WiX, beides externe Programme, keine Bibliothek) – Entscheidung 1.
- **CI:** Ein Workflow auf Tags `v*` baut das ZIP und die Symbole als Artefakte. Er veröffentlicht nichts
  automatisch (Entscheidung 8).
- **Tests:** Lizenzprüfung (pytest in `tools/release`), CPack-Inhalt (Liste der Dateien im ZIP), der reine Paket-Test
  in CI auf Windows.

## Teil D – Gekochtes Format für Figuren und Animationen (`.g7skin`/`.g7anim`)
- **Roadmap:** „spätestens vor dem ersten reinen `.g7pak`-Release“. ADR 0019 sagt: der Cooker kopiert glTF, ein eigenes
  Format kommt, „wenn Ladezeit oder Größe es verlangen“.
- **Einschätzung:** Für ein reines `.g7pak` ist es **keine Voraussetzung**. Der Loader liest glTF aus Bytes über das VFS,
  und die Texturen der Figuren werden als eigene Dateien über das VFS aufgelöst. Ob das im Paket wirklich trägt, zeigt
  der reine Paket-Test aus Teil C.
- **Entschieden (Entscheidung 9):** `.g7skin`/`.g7anim` gehört **nicht** in diesen Plan.
  - Teil C beweist mit dem Paket-Test, dass Figuren und Clips aus `data.g7pak` laden. Scheitert er, wird Teil D
    Pflicht und kommt vor das erste Release.
  - Ob es sich lohnt, entscheidet die Messung im Performance-Plan (`docs/m17-perf`: Ladezeit Leonberg, Anteil der
    glTF-Figuren und -Clips). Das Format kommt dann mit eigener ADR.
  - Siehe Entscheidung 9.
- **Nachtrag (2026-11-09):** Teil C hatte eine Lücke, der Paket-Test hat Figuren nicht wirklich geprüft (#410). Danach
  hat der Projektinhaber das Format als eigenen Plan freigegeben (`asset.md`, „Plan M17: gekochte Figuren“, ADR 0025).
  Umgesetzt: Das Paket enthält `.g7skin`/`.g7anim`, und der strenge Paket-Test lädt alle Lager-Figuren daraus.

## Reihenfolge und Umfang
| Teil | Inhalt | Umfang |
|---|---|---|
| A | Versions-/Build-Info, `--version`, Hauptmenü, Log, Spielstand | klein |
| B | Absturzberichte Windows (Minidump) und Linux (Text) | mittel |
| C | Paket: Install-Regeln, CPack ZIP, Lizenzprüfung, Credits, reiner Paket-Test, CI-Artefakte | mittel bis groß |
| D | nur falls der Paket-Test es verlangt: `.g7skin`/`.g7anim` | (offen) |

A zuerst (B und C brauchen die Build-Zeile), dann B, dann C. Neue Bibliotheken: keine – CPack, dbghelp und `backtrace`
gehören zu CMake, Windows SDK bzw. glibc. Deshalb braucht es keine neue ADR; käme doch eine Bibliothek dazu (etwa für
Minidumps unter Linux), dann mit ADR „Vorgeschlagen“.

## Entscheidungen des Projektinhabers (2026-11-07, alle wie vorgeschlagen)
1. **ZIP oder Installer?** **Entschieden:** zuerst nur ZIP (entpacken, starten, Spielstände im Benutzerordner). Ein Installer
   (NSIS) später, wenn das Spiel öffentlich verteilt wird.
2. **Ordner der Absturzberichte?** **Entschieden:** `<Benutzerordner>/crashes`, neben `saves`, auf Windows also unter
   `%APPDATA%\Gothar\crashes`; dort schreibt nur das Spiel selbst, Tests nie.
3. **Hinweis nach einem Absturz?** **Entschieden:** Beim nächsten Start eine Zeile im Hauptmenü („Ein Absturzbericht liegt
   in …“, Ordner öffnen per Klick) und ein Log-Eintrag; kein Dialog, nichts wird gesendet.
4. **Wo steht die Version?** **Entschieden:** Hauptmenü unten rechts klein und blass `0.4.0 (abc1234)`, dazu Log und
   `--version`; im Spiel selbst nicht.
5. **Versionsschema?** **Entschieden:** `0.<meilenstein>.<patch>` bis zum ersten öffentlichen Release, also Meilenstein D =
   `0.4.0`; Git-Tag `v0.4.0` setzt die Version, der Hash kommt immer dazu.
6. **Symbole (`.pdb`)?** **Entschieden:** nicht im ZIP, sondern als CI-Artefakt je Version, 90 Tage aufbewahrt; für
   veröffentlichte Versionen dauerhaft im Release-Anhang.
7. **Linux?** **Entschieden:** Windows-ZIP ist das Release. Linux als `.tar.gz` nur als CI-Artefakt, ohne Zusage, mit
   Text-Absturzbericht ohne Minidump.
8. **Veröffentlichen?** **Entschieden:** CI baut auf Tags nur Artefakte. Ein GitHub-Release legt der Projektinhaber von Hand
   an (das Repo ist öffentlich, das ZIP wäre es dann auch).
9. **`.g7skin`/`.g7anim` jetzt?** **Entschieden:** nein (Teil D). Erst wenn der reine Paket-Test scheitert oder die
   Ladezeit-Messung im Performance-Plan es verlangt.
10. **Mitwirkende im Spiel?** **Entschieden:** ein Menüpunkt „Mitwirkende“ im Hauptmenü, der `CREDITS.txt` zeigt
    (Pflichtangaben LGL und OSM zuerst). Die Datei liegt zusätzlich im ZIP.
