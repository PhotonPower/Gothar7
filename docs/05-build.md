| `--max-fps=N` | Bildrate begrenzen (überschreibt `[window] max_fps`; 0 = unbegrenzt) |
# 05 – Bauen & Entwicklungsumgebung

## Voraussetzungen
- CMake ≥ 3.25, Ninja
- Compiler: **Windows**: Visual Studio 2022 (MSVC 19.38+) · **Linux**: GCC 13+ oder Clang 17+
- **vcpkg** (Manifest-Modus) mit gesetzter Umgebungsvariable `VCPKG_ROOT`
- Grafiktreiber mit OpenGL 4.5 oder neuer (ab M2; 4.6 bevorzugt)
- **Linux**: Entwicklungspakete für SDL3 (X11/Wayland), z. B. unter Ubuntu:
  `sudo apt install pkg-config autoconf autoconf-archive automake libtool libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libudev-dev`
  (dieselbe Liste wie in `.github/workflows/ci.yml`; dort zusätzlich `xvfb libgl1-mesa-dri` für die GPU-Tests)

### vcpkg einrichten (einmalig)
```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg      # Windows z. B. C:\dev\vcpkg
~/vcpkg/bootstrap-vcpkg.sh                                     # Windows: bootstrap-vcpkg.bat
export VCPKG_ROOT=~/vcpkg                                      # Windows: setx VCPKG_ROOT C:\dev\vcpkg
```

### Hinweis: vcpkg aus Visual Studio
Die „x64 Native Tools Command Prompt“ setzt `VCPKG_ROOT` oft auf das mit Visual Studio gelieferte
vcpkg (`...\Microsoft Visual Studio\<ver>\Community\VC\vcpkg`). Das funktioniert, weil `vcpkg.json`
eine `builtin-baseline` enthält. Wer das eigene vcpkg nutzen will: `set VCPKG_ROOT=C:\dev\vcpkg`.

### Versionen der Abhängigkeiten
`builtin-baseline` in `vcpkg.json` legt die Port-Versionen fest (reproduzierbare Builds).
Aktualisieren: `%VCPKG_ROOT%\vcpkg x-update-baseline` im Repo-Ordner, danach neu konfigurieren.

### Python (Welt-Spur)
Für `tools/worldgen/` wird Python ≥ 3.11 benötigt (`winget install Python.Python.3.12`) sowie
Blender ≥ 4.2 für das Gebäude-Add-on, empfohlen **4.5 LTS** (`winget install BlenderFoundation.Blender.LTS.4.5`;
Pfad in `tools/worldgen/config/local.toml`). Einrichtung: `tools/worldgen/README.md`.
Python-Pakete (z. B. numpy) stehen in `tools/worldgen/pyproject.toml` und werden mit pip/uv installiert,
nicht über vcpkg.

Für Einzelbilder aus den 360°-Videos (`gothar-worldgen facade frames`, W4) braucht man außerdem
**ffmpeg ≥ 6**, getestet mit **9.0** (`winget install Gyan.FFmpeg`; Linux: Paket `ffmpeg`). ffmpeg ist wie
Blender ein externes Programm und keine Python-Abhängigkeit.
- Suchreihenfolge: `--ffmpeg`, Umgebungsvariable `G7_FFMPEG`, `paths.ffmpeg` in `local.toml`, `PATH`,
  danach der winget-Paketordner. Der winget-Befehlsalias wirkt erst in neuen Shells.
- `ffprobe` wird neben ffmpeg gesucht.
- Tests, die ffmpeg brauchen, werden übersprungen, wenn es fehlt.

### Python (Figuren-Spur)
Für `tools/chargen/` (Rig-Validator, Blender-Export) wird ebenfalls Python ≥ 3.11 benötigt; einzige
Laufzeit-Abhängigkeit ist **numpy** (`tools/chargen/pyproject.toml`, Installation mit pip/uv). Blender **4.5 LTS**
braucht man für `build-rig`, `build-placeholder`, `export` und das Prüfen von `.blend` (Suche: `--blender`,
`G7_BLENDER`, `PATH`, Standard-Installationsordner). Einrichtung: `tools/chargen/README.md`.

**MPFB2** (MakeHuman Plugin for Blender, ADR 0018) – nur lokal, für Ausgangskörper und Köpfe (F3); nicht in CI:
```cmd
"C:\Program Files\Blender Foundation\Blender 4.5\blender.exe" --online-mode --command extension sync
"C:\Program Files\Blender Foundation\Blender 4.5\blender.exe" --online-mode --command extension install mpfb --enable
```
Dazu die Asset-Pakete (alle CC0) von static.makehumancommunity.org/assets/assetpacks/ – **„MakeHuman system
assets“** (Haut, Augen, Brauen, Wimpern, Zähne, Zunge), **Shirts 01, Pants 01, Shoes 01, Hair 01** – laden (schnell über
`files.makehumancommunity.org/asset_packs/<paket>/<paket>_cc0.zip`; der Spiegel `files2` ist sehr langsam), für die
Gesichts-Morphs außerdem **Visemes 02** und **Faceunits 01** (`files.makehumancommunity.org/functional/<paket>.zip`),
für Grundkörper und Bärte **Underwear 01** und **Bodyparts 05** (nur die in `assets/LICENSES.md` genannten Teile) –
und
entweder in Blender über MPFB → „Apply assets“ → „Library settings“ → „Install asset pack“ einspielen oder direkt
nach `%APPDATA%\Blender Foundation\Blender\4.5\extensions\.user\blender_org\mpfb\data` entpacken.
Rohdateien bleiben unter `DATA_ROOT\characters\mpfb` (nicht im Repo). Verwenden: **Core/System + einzeln geprüfte CC0-Pakete (Liste in `assets/LICENSES.md`)**;
Community-Pakete erst nach Lizenzprüfung (ADR 0018). Das Plugin (GPLv3) wird nie ins Repo kopiert.

## Bauen
```bash
cmake --preset debug            # konfiguriert, installiert Abhängigkeiten aus vcpkg.json
cmake --build --preset debug
ctest --preset debug            # Unit-Tests
./build/debug/game/gothar --verbose
```
Unter Windows die Befehle in der **„x64 Native Tools Command Prompt for VS 2022“** ausführen
(damit Ninja den MSVC-Compiler findet) – oder den Ordner direkt in Visual Studio / CLion / VS Code
(CMake Tools) öffnen; die Presets werden erkannt.
ctest bricht jede Test-Suite nach 300 s ab (GPU-Suite 900 s), damit ein hängender Test die Ausführung rot macht
statt sie zu blockieren; anpassbar mit `-DG7_TEST_TIMEOUT=<sekunden>`.

Ohne vcpkg (nur Kern, ohne Tests): `cmake --preset nodeps && cmake --build --preset nodeps` – ohne glad,
das Spiel dort mit `--no-render` starten.

## Presets
| Preset | Zweck |
|---|---|
| `debug` | Entwicklung, Assertions an |
| `release` | `RelWithDebInfo`, Profiling/Performance |
| `coverage` | Debug + `G7_COVERAGE` (nur GCC/Clang), für die Abdeckungsmessung |
| `nodeps` | Schnelltest ohne vcpkg |

## CMake-Optionen
| Option | Standard | Bedeutung |
|---|---|---|
| `G7_BUILD_TESTS` | ON | Unit-Tests |
| `G7_BUILD_TOOLS` | ON | g7-cook, Editor |
| `G7_WARNINGS_AS_ERRORS` | OFF | in CI empfohlen ON |
| `G7_PROFILING` | OFF | aktiviert die `G7_PROFILE_*`-Zonen (eingebauter Sammler, ab M17 Tracy) |
| `G7_COVERAGE` | OFF | Testabdeckung (`--coverage`, nur GCC/Clang); Auswertung mit gcovr |
| `G7_DEV_ASSETS` | ON | Entwicklungs-Builds mounten `<repo>/assets/source`, `assets/cooked` und `assets/cooked/*.g7pak` (`[assets] dev_mounts`); für Auslieferungen OFF |

## Konfigurationsdateien
| Datei | Inhalt |
|---|---|
| `game/config/engine.toml` → `<build>/game/config/engine.toml` | Standardwerte: `[window]`, `[camera]` (FOV, Near/Far, Maus, Fluggeschwindigkeit), `[render]` (Shader-Hot-Reload, Shader-Verzeichnis, Anisotropie, Schatten, Tonemapping, Belichtung, Nebel, Debug-Overlay, Debug-UI), `[assets]` (VFS-Mounts, Hot-Reload), `[input]` (Schema, Stick-Totzone), `[bindings.classic]`, `[bindings.modern]`; wird bei jedem Build neben die Executable kopiert (Target `gothar_data`) |
| `config.toml` im Benutzerverzeichnis (Windows `%APPDATA%\Gothar\Gothar`) | optionale eigene Einstellungen; überschreibt `engine.toml` Schlüssel für Schlüssel |
| `engine/render/shaders/` → `<build>/game/shaders/` | Engine-Shader (GLSL); werden bei jedem Build neben die Executable kopiert (Target `gothar_data`, auch wenn nur ein Shader geändert wurde) – Änderungen dort gehen beim nächsten Build verloren, zum Bearbeiten `[render] shader_dir` auf die Quellen zeigen lassen |

Kommandozeilen-Schalter haben Vorrang vor beiden Dateien.

## Kommandozeile des Spiels
| Schalter | Wirkung |
|---|---|
| `--verbose` | Log-Level Debug |
| `--smoke-test` | 10 Frames headless mit fester Frame-Zeit (deterministisch: 10 Ticks), dann Ende (CI) |
| `--frames=N` | nach N Frames beenden (auch mit Fenster; CI mit `SDL_VIDEO_DRIVER=offscreen`) |
| `--max-fps=N` | Bildrate begrenzen (überschreibt `[window] max_fps`; 0 = unbegrenzt) |
| `--fullscreen` | randloses Vollbild in Desktop-Auflösung |
| `--view-mesh=<pfad>` | Modell (`.gltf`/`.glb`/`.g7mesh`) am Ursprung anzeigen, Debug-Kamera richtet sich danach aus; VFS-Pfad oder Datei auf der Festplatte (wie `--scene`) |
| `--world=<pfad>` | Welt `.g7world` laden (VFS-Pfad wie `testworld/camp.g7world` oder Datei auf der Festplatte); hat Vorrang vor `--scene`/`--view-mesh` |
| `--editor` | Editor-Modus (Simulation pausiert, Editor-Fenster; docs/modules/tools.md) |
| `--time=HH:MM` | Spielzeit beim Start (Vorgabe `[time] start`, 08:00) |
| `--start=<name>` | Startpunkt der Welt (Vob-Typ `start`, Groß-/Kleinschreibung egal); ohne Angabe der mit der kleinsten id |
| `--save-world=<datei>` | nach dem Laden die Welt bzw. Testszene als `.g7world` speichern (stabil, ein Vob pro Zeile) |
| `--scene=<pfad>` | Testszene laden (TOML, siehe „Testszenen“): VFS-Pfad wie `testscene/scene.toml` oder Datei auf der Festplatte (deren Ordner wird unter `local/` gemountet); ersetzt `--view-mesh` |
| `--viewpoint=N` | mit Viewpoint N der Szene starten (Standard 0) |
| `--benchmark` | VSync und Frame-Limit aus, jeden Viewpoint der Szene 300 Frames lang ansteuern (die ersten 30 zum Einschwingen), Frame-Zeiten (Mittel, p95, p99, schlechtester) sowie Draws, Pufferbindungen, Pipeline-Wechsel und Dreiecke je Viewpoint loggen, dann beenden |
| `--screenshot=<datei.png>` | letztes Bild als PNG speichern (mit `--frames` oder `--benchmark`) |
| `--no-ground` | Bodenplatte unter dem Modell bzw. der Szene weglassen |
| `--no-sun` | ohne Sonnenlicht (Punktlichter allein beurteilen) |
| (Taste F1) | ImGui-Debugfenster ein/aus (Aktion `debug_ui`): Leistung, Kamera, Render-Einstellungen live |
| (Taste F2) | Debug-Overlay ein/aus (Aktion `debug_draw`): FPS, Draw-Calls, Achsen, Raster, Bounds, Lichtradien |
| `--no-render` | Fenster ohne OpenGL (Systeme ohne GL-Treiber, Windows-CI, `nodeps`-Build) |
| `--editor` | Editor-Modus (ab M4) |
| `--world=<name>` | Startwelt (ab M4) |

## Testszenen (`--scene`)
Vorläufiges Szenenformat bis zum Weltformat und Editor in M4 (`runtime/SceneFile.hpp`). Die M2-Abnahmeszene liegt in
`assets/source/testscene/scene.toml` (Kenney-Modelle, CC0, siehe `assets/LICENSES.md`):
```
build\debug\game\gothar.exe --scene=testscene/scene.toml
build\release\game\gothar.exe --scene=testscene/scene.toml --benchmark
```
```toml
ground = { size = 640.0, color = [0.34, 0.31, 0.24] }   # Bodenplatte (optional), Farbe linear
[environment]            # optional: sun_direction/sun_color/sun_intensity, ambient_sky/ambient_ground,
fog_start = 20.0         #           fog_color, fog_start, fog_density überschreiben die Abendstimmung
[prefab.hut]             # wiederverwendbare Gruppe; Teile relativ zum Objekt (mit dessen Skalierung)
[[prefab.hut.part]]
mesh = "town/wall-wood.glb"
rotation_y = 90
[[object]]               # entweder mesh = "…" oder prefab = "…"
prefab = "hut"
position = [13.0, 0.0, 0.0]   # Meter
rotation_y = 0                # Grad um +Y
scale = 3                     # gleichmäßig
[[light]]                # Punktlicht (Fackel): position, radius, color (linear), intensity
position = [0.0, 1.0, 0.0]
radius = 12
[[viewpoint]]            # Start (--viewpoint) und --benchmark: position, yaw (0 = Blick nach -Z, + = links), pitch
position = [16.0, 7.0, 20.0]
yaw = 38
```
Modellpfade sind relativ zur Szenendatei (VFS-Pfade, `..` erlaubt). Fehler nennen Datei und Eintrag, z. B.
`scene.toml: 'object[3].position' must be a list of 3 numbers`.

## CI (GitHub Actions)
`.github/workflows/ci.yml`: Jobs `build (ubuntu-24.04)`, `build (windows-2022)` und `coverage` bei Push auf `main` und bei
Pull Requests. GPU-Tests laufen unter Linux mit Xvfb + Mesa llvmpipe, Windows schließt sie aus (`-LE gpu`).
**vcpkg-Binärcache:** `VCPKG_BINARY_SOURCES=files,<workspace>/vcpkg-binary-cache` + `actions/cache` – jeder Lauf
speichert einen neuen Eintrag, PRs lesen die Einträge von `main`. Ohne Treffer baut vcpkg alle Abhängigkeiten neu
(SDL3, simdjson, fastgltf … – mehrere Minuten pro Job). Das Repository ist öffentlich; Actions-Minuten auf
Standard-Runnern sind damit kostenlos.

## Neue Abhängigkeit hinzufügen
1. ADR schreiben/aktualisieren (warum diese Bibliothek?).
2. Port in `vcpkg.json` → `dependencies`.
3. `find_package(...)` in der `CMakeLists.txt` des **Moduls**, `PRIVATE_LIBS` in `g7_add_module`.
4. Tabelle „Drittbibliotheken“ in `docs/02-architecture.md` bzw. Modul-Doku ergänzen.

## Geplante Drittbibliotheken
| Bibliothek | vcpkg-Port | Modul | Phase |
|---|---|---|---|
| doctest | `doctest` | tests | M0 |
| glm | `glm` | core (öffentlich) | M0 |
| toml++ | `tomlplusplus` | core | M0 |
| SDL3 | `sdl3` | platform | M1 |
| glad (GL 4.6) | `glad` | render | M2 |
| Dear ImGui (+ ImGuizmo) | `imgui` (ADR 0015), `imguizmo` | ui | M2/M4 |
| fastgltf | `fastgltf` | asset/tools | M2/M3 |
| stb (image, truetype) | `stb` | asset/ui | M2 |
| KTX (libktx, UASTC → BC7/BC5) | `ktx` (ADR 0016) | asset/tools | M3 |
| zstd | `zstd` (ADR 0016) | asset/tools | M3 |
| EnTT 3.16 (MIT) | `entt` (ADR 0005) | world (öffentlich; Registry nicht in der API) | M4 |
| nlohmann-json 3.12 (MIT) | `nlohmann-json` (ADR 0017) | world (privat, Weltformat `.g7world`) | M4 |
| Jolt Physics | `joltphysics` | physics | M5 |
| Lua 5.4 + sol2 | `lua`, `sol2` | script | M7 |
| miniaudio | `miniaudio` | audio | M13 |
| Tracy | `tracy` | core | M17 |
