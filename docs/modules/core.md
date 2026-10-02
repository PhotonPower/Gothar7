# core

**Zweck:** Fundament ohne Abhängigkeiten zu anderen Modulen. Alles, was jedes Modul braucht.

## Bestand (M0)
- `Types.hpp` – `i32`, `u64`, `f32` …
- `Log.hpp` – kanalbasiertes Logging (`G7_LOG_INFO("render", "...", args)`), threadsicher, `std::format`
- `Assert.hpp` – `G7_ASSERT` (Debug), `G7_VERIFY` (immer)
- `Result.hpp` – `Result<T>` / `Result<void>` mit `Error{message}`
- `Clock.hpp` – `Stopwatch`, `FixedStep` (Akkumulator für feste Simulationsrate)
- `Version.hpp`
- `FileSystem.hpp` – Namespace `g7::fs`:
  - `readFile(path) -> Result<std::vector<u8>>`, `readText(path) -> Result<std::string>` (binär, keine Zeilenende-Umwandlung)
  - `writeFile`/`writeText` sowie `writeFileAtomic`/`writeTextAtomic` (Temp-Datei `<ziel>.tmp` + `rename`,
    nie halb geschriebene Saves/Configs)
  - `createDirectories`, `exists`
  - `fromUtf8`/`toUtf8`: Pfade an der API sind UTF-8 (unter Windows sonst ANSI-Codepage)
  - `BaseDirectories{gameDir, userDir}` als dokumentiertes Subsystem-Singleton (`setBaseDirectories`
    beim Start), `gamePath("assets/...")`, `userPath("saves/...")`.
    In M0 setzt `game/src/main.cpp` `gameDir` = Verzeichnis der Executable, `userDir` = `gameDir/userdata`.
    Ab M1 setzt `platform` das Benutzerverzeichnis über `SDL_GetPrefPath`, weil OS-Aufrufe nicht in core gehören.
  - Fehler als `Result` mit Pfad und Ursache in der Meldung; keine Exceptions.
- `Math.hpp` – glm (ADR 0002, öffentliche Abhängigkeit) hinter Aliassen:
  - `Vec2/3/4`, `IVec2/3`, `Quat`, `Mat3`, `Mat4`; Standardkonstruktion = 0 bzw. Identität (`GLM_FORCE_CTOR_INIT`)
  - Koordinatensystem: rechtshändig, **+Y oben**, **−Z vorwärts**, +X rechts, Meter;
    Konstanten `kWorldUp`, `kWorldForward`, `kWorldRight`, `kPi`, `kEpsilon`
  - `toRadians`/`toDegrees`, `nearlyEqual` (f32, Vec3, Quat – q und −q gelten als gleich)
  - `quatFromEuler(pitch, yaw, roll)` (Bogenmaß, q = yaw · pitch · roll; positives Yaw dreht nach links,
    positives Pitch nach oben), `lookRotation(forward, up)` (robust bei senkrechtem Blick)
  - `std::format`-Unterstützung: Vektoren als `(x, y, z)`, Quaternionen als `quat(w, x, y, z)`,
    Format-Spezifikationen gelten je Komponente (`{:.2f}`)
- `Transform.hpp` – `Transform{position, rotation, scale}` (Matrix = T · R · S):
  - `toMatrix()`, `fromMatrix()` (ohne Scherung; Spiegelung als negatives x-Scale)
  - `parent * child` (lokal → Welt), `inverse()`, `transformPoint`, `transformDirection`,
    `forward()/right()/up()`
  - `interpolate(a, b, t)` – lerp/slerp (kürzester Weg) für das Rendern zwischen Simulationsschritten
  - Verkettung ist nur bei gleichmäßiger Skalierung exakt (nicht-gleichmäßig + Rotation bräuchte Scherung).

## Geplant (M0)
- **StringId**: 32/64-Bit-Hash (FNV-1a) für Namen (`"WP_HC_CAMPFIRE"`), Debug-Builds behalten
  den Klartext für Ausgaben; case-insensitive Variante (Gothic-Namen sind case-insensitive).
- **Config**: TOML lesen, typisierte Abfragen mit Defaults.
- **Profiler-Makros**: `G7_PROFILE_SCOPE("name")` – zunächst leer, ab M17 Tracy.

## Später
- Geometrie-Primitive `AABB`, `Sphere`, `Ray`, `Plane`, `Frustum` (M2, zusammen mit Kamera und Culling).
- Job-System (M17), Frame-/Pool-Allokatoren bei Bedarf, Zufallszahlen (deterministisch, seedbar – wichtig für Saves/Tests).
