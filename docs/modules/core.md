# core

**Zweck:** Fundament ohne Abhängigkeiten zu anderen Modulen. Alles, was jedes Modul braucht.

## Bestand (M0)
- `Types.hpp` – `i32`, `u64`, `f32` …
- `Log.hpp` – kanalbasiertes Logging (`G7_LOG_INFO("render", "...", args)`), threadsicher, `std::format`
- `Assert.hpp` – `G7_ASSERT` (Debug), `G7_VERIFY` (immer)
- `Result.hpp` – `Result<T>` / `Result<void>` mit `Error{message}`
- `Clock.hpp` – `Stopwatch`, `FixedStep` (Akkumulator für feste Simulationsrate)
- `Version.hpp`

## Geplant (M0)
- **Mathe** (glm, ADR 0002): `Vec2/3/4`, `Quat`, `Mat4`, `Transform{pos, rot, scale}` mit
  `toMatrix()`, `lerp`/`slerp` für Interpolation, `AABB`, `Sphere`, `Ray`, `Plane`, `Frustum`.
  Koordinatensystem: rechtshändig, **+Y oben**, −Z vorwärts, Meter.
- **Dateisystem**: `readFile(path) -> Result<std::vector<u8>>`, `readText`, `writeFileAtomic`,
  Basis-Pfade (Spielverzeichnis, Benutzer-Daten für Saves/Config).
- **StringId**: 32/64-Bit-Hash (FNV-1a) für Namen (`"WP_OC_CAMPFIRE"`), Debug-Builds behalten
  den Klartext für Ausgaben; case-insensitive Variante (Gothic-Namen sind case-insensitive).
- **Config**: TOML lesen, typisierte Abfragen mit Defaults.
- **Profiler-Makros**: `G7_PROFILE_SCOPE("name")` – zunächst leer, ab M17 Tracy.

## Später
- Job-System (M17), Frame-/Pool-Allokatoren bei Bedarf, Zufallszahlen (deterministisch, seedbar – wichtig für Saves/Tests).
