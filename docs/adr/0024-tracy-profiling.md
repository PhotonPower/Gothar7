# 0024 – Profiling mit Tracy

- **Status:** Vorgeschlagen
- **Datum:** 2026-11-07
- **Phase:** M17 (Teil A, `docs/modules/perf.md`)

## Kontext
Meilenstein D verlangt ≥ 60 FPS in Leonberg auf Zielhardware (RTX 3080 Laptop, Intel UHD ≥ 30 FPS; Entscheidung 1
des Performance-Plans). Dafür braucht es Zeiten je System und je Thread. Das Job-System (Teil C) bringt
Arbeiter-Threads, deren Zeiten nur mit einer Zeitleiste sichtbar werden.

Heute gibt es die Makros `G7_PROFILE_SCOPE`/`G7_PROFILE_FRAME` (`core/Profiler.hpp`) mit einem eingebauten Sammler,
der je Bild Summen bildet. Er ist nur mit `G7_PROFILING=ON` an und kennt vier Zonen. Er zeigt keine Zeitleiste,
keine GPU-Zeiten, keine Threads und keine Speicherzuteilungen. `docs/05-build.md` führt Tracy seit M0 als geplante
Abhängigkeit, eine ADR fehlt bisher.

## Optionen
1. **Tracy** (BSD-3, vcpkg `tracy`): Zonen mit Zeitleiste über Threads, `FrameMark`, GPU-Zonen für OpenGL
   (`TracyOpenGL`), Plots, Speicher, Sperren.
   - Der Client wird in das Spiel gelinkt, der Betrachter läuft getrennt und verbindet sich per Netzwerk.
   - Ohne `TRACY_ENABLE` sind alle Makros leer, und es kommt nichts ins Programm.
   - Gängig in Spiele-Engines, die Makros passen 1:1 auf die vorhandenen `G7_PROFILE_*`.
2. **Optick** (MIT): ähnliche Zeitleiste, GPU-Zeiten nur für D3D12 und Vulkan, nicht für OpenGL. Seit Jahren kaum
   gepflegt.
3. **Eingebauten Sammler ausbauen** (Zeitleiste im ImGui-Fenster, GPU-Abfragen selbst): keine Abhängigkeit, aber
   viel eigener Code für das, was Tracy fertig hat. Speicher- und Sperren-Analyse fehlen weiter.
4. **Nur Plattform-Profiler** (Visual Studio, `perf`, Nsight): ohne Code, aber ohne Zonen nach Systemen, ohne
   gemeinsame Sicht auf CPU und GPU und auf Linux und Windows verschieden.

## Entscheidung (Vorschlag)
**Option 1: Tracy**, mit diesen Grenzen:
- **Nur im neuen Preset `profile`:** RelWithDebInfo mit `G7_PROFILING=ON` und `TRACY_ENABLE`. Debug, Release und das
  Paket enthalten kein Tracy und öffnen keinen Port (Entscheidung 2 des Performance-Plans).
- **Abhängigkeit** `tracy` in `vcpkg.json`, öffentlich nur in `core` hinter `G7_PROFILING`. Die Makros in
  `Profiler.hpp` bilden auf `ZoneScopedN`, `FrameMark` und Thread-Namen ab. GPU-Zonen (`TracyGpuZone`) nur in
  `render`. Keine Tracy-Makros außerhalb von `core` und `render`.
- **Version:** die aktuelle vcpkg-Version beim Umsetzen. Betrachter und Client müssen zur selben Protokoll-Version
  gehören, deshalb nennt `docs/05-build.md` die passende Betrachter-Version.
- **Der eingebaute Sammler bleibt** für das Overlay (F1) und für Tests ohne Tracy. Beide hängen an denselben Makros.
- **Erst nach Bestätigung** dieser ADR kommt Tracy in `vcpkg.json` und `05-build.md` (Regel aus `CLAUDE.md`).

## Konsequenzen
- Neues Preset `profile`; CI baut es nicht (Tracy bringt dort keinen Nutzen, die Makros sind sonst leer).
- `core` bekommt eine öffentliche Abhängigkeit, solange `G7_PROFILING` an ist (wie glm, nur bedingt).
- Messungen für `docs/perf/` laufen mit dem Preset `profile`, Zahlen ohne Tracy mit `release` (`--perf=`-Szenarien,
  Teil B). Sie bleiben vergleichbar, weil Tracy nur misst, wenn ein Betrachter verbunden ist.
- Lizenz BSD-3: Weil Tracy nicht ins Paket kommt, braucht `THIRD_PARTY_NOTICES.txt` keinen Eintrag. Der Test
  gegen `vcpkg.json` (`tests/cook/test_release.cpp`) verlangt ihn trotzdem; Tracy steht dort dann unter
  „Nur in den Werkzeugen“.
- Alternative bei Ablehnung: Option 3 mit begrenztem Umfang (Zeitleiste der letzten Bilder im Overlay), ohne GPU-
  und Speicher-Analyse.
