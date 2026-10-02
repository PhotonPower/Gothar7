# 0001 – C++20, CMake und vcpkg

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** M0

## Kontext
Die Engine soll auf Windows (Hauptplattform) und Linux bauen, mit verbreiteten Werkzeugen und
reproduzierbaren Abhängigkeiten.

## Optionen
1. **CMake + vcpkg (Manifest)** – Industriestandard, IDE-Unterstützung, Binär-Cache; vcpkg-Erstinstallation langsam.
2. **CMake + CPM/FetchContent** – keine externe Installation; lange Builds großer Bibliotheken, kein Binär-Cache.
3. **Premake/Meson/Bazel** – weniger IDE-/Bibliotheksunterstützung.

Sprache: C++20 (Concepts, `std::format`, `std::span`, Ranges) wird von MSVC/GCC 13/Clang 17 vollständig genug unterstützt; C++23 (`std::expected`) noch nicht überall robust.

## Entscheidung
C++20, CMake ≥ 3.25 mit Presets, vcpkg im Manifest-Modus. Jedes Engine-Modul ist eine
statische Bibliothek (`g7_add_module`).

## Konsequenzen
- Eigener `Result<T>`-Typ statt `std::expected` (Umstieg später einfach).
- Entwickler brauchen `VCPKG_ROOT`; `nodeps`-Preset für schnelle Kern-Builds.
