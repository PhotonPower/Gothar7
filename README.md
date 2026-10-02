# Gothar

Eine eigene **C++20-Game-Engine** und ein Open-World-Action-RPG im Geist von **Gothic 1** –
lebendige Welt, NPCs mit Tagesabläufen, Gilden, Dialoge, direkter Nahkampf, dynamische Musik.

> Status: **Phase M0 – Fundament.** Siehe [Roadmap](docs/03-roadmap.md).

## Schnellstart
```bash
# einmalig: vcpkg installieren und VCPKG_ROOT setzen (siehe docs/05-build.md)
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/game/gothar --verbose
```

## Technik (geplant)
C++20 · CMake + vcpkg · SDL3 · OpenGL 4.6 · EnTT · Jolt Physics · Lua 5.4 (sol2) · miniaudio · Dear ImGui · glTF

## Aufbau
| Ordner | Inhalt |
|---|---|
| `engine/` | Engine-Module (`core`, `platform`, `asset`, `render`, `audio`, `physics`, `animation`, `world`, `script`, `ai`, `gameplay`, `ui`, `save`, `runtime`) |
| `game/` | Spiel-Executable und Lua-Skripte |
| `tools/` | Asset-Cooker, Editor |
| `tests/` | Unit- und Szenario-Tests |
| `docs/` | [Dokumentation](docs/README.md): Vision, Architektur, Roadmap, Modul-Specs, ADRs |
| `assets/` | Eigene / frei lizenzierte Inhalte |

## Mit Claude Code entwickeln
Im Repository `claude` starten – [`CLAUDE.md`](CLAUDE.md) enthält alle Regeln und Einstiegspunkte.
Mit `/naechster-schritt` plant Claude die nächste Aufgabe aus der Roadmap.

## Rechtliches
Der Code steht unter der [MIT-Lizenz](LICENSE). „Gothic“ ist eine Marke von THQ Nordic; dieses
Projekt ist nicht mit THQ Nordic oder Piranha Bytes verbunden und verwendet keine Original-Inhalte.
