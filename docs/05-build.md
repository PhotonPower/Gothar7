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
Blender ≥ 4.2 für das Gebäude-Add-on. Einrichtung: `tools/worldgen/README.md`.

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

## Konfigurationsdateien
| Datei | Inhalt |
|---|---|
| `game/config/engine.toml` → `<build>/game/config/engine.toml` | Standardwerte: `[window]`, `[input]` (Schema, Stick-Totzone), `[bindings.classic]`, `[bindings.modern]`; wird beim Bauen neben die Executable kopiert |
| `config.toml` im Benutzerverzeichnis (Windows `%APPDATA%\Gothar\Gothar`) | optionale eigene Einstellungen; überschreibt `engine.toml` Schlüssel für Schlüssel |

Kommandozeilen-Schalter haben Vorrang vor beiden Dateien.

## Kommandozeile des Spiels
| Schalter | Wirkung |
|---|---|
| `--verbose` | Log-Level Debug |
| `--smoke-test` | 10 Frames headless mit fester Frame-Zeit (deterministisch: 10 Ticks), dann Ende (CI) |
| `--frames=N` | nach N Frames beenden (auch mit Fenster; CI mit `SDL_VIDEO_DRIVER=offscreen`) |
| `--max-fps=N` | Bildrate begrenzen (überschreibt `[window] max_fps`; 0 = unbegrenzt) |
| `--fullscreen` | randloses Vollbild in Desktop-Auflösung |
| `--no-render` | Fenster ohne OpenGL (Systeme ohne GL-Treiber, Windows-CI, `nodeps`-Build) |
| `--editor` | Editor-Modus (ab M4) |
| `--world=<name>` | Startwelt (ab M4) |

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
| Dear ImGui (+ ImGuizmo) | `imgui`, `imguizmo` | ui/render | M2/M4 |
| fastgltf | `fastgltf` | asset/tools | M2/M3 |
| stb (image, truetype) | `stb` | asset/ui | M2 |
| KTX | `ktx` | asset/tools | M3 |
| EnTT | `entt` | world (öffentlich) | M4 |
| nlohmann-json | `nlohmann-json` | world (Weltformat) | M4 |
| Jolt Physics | `joltphysics` | physics | M5 |
| Lua 5.4 + sol2 | `lua`, `sol2` | script | M7 |
| miniaudio | `miniaudio` | audio | M13 |
| Tracy | `tracy` | core | M17 |
