# core

**Zweck:** Fundament ohne Abhängigkeiten zu anderen Modulen. Alles, was jedes Modul braucht.

## Bestand (M0)
- `Types.hpp` – `i32`, `u64`, `f32` …
- `Log.hpp` – kanalbasiertes Logging (`G7_LOG_INFO("render", "...", args)`), threadsicher, `std::format`
- `Assert.hpp` – `G7_ASSERT` (Debug), `G7_VERIFY` (immer)
- `Result.hpp` – `Result<T>` / `Result<void>` mit `Error{message}`
- `Clock.hpp` – `Stopwatch`, `FixedStep` (Akkumulator für feste Simulationsrate; `droppedSeconds()` meldet bei
  Hängern verworfene Zeit, die Engine loggt sie mit `--verbose` als `frame hitch`), `FramePacer`
  (Frame-Limit ohne Drift: Fristen rücken um ein festes Intervall vor, nach einem Hänger > 1 Intervall
  wird neu synchronisiert statt nachgeholt; Zeit wird injiziert, daher ohne echte Uhr testbar)
- `Version.hpp`
- `FileSystem.hpp` – Namespace `g7::fs`:
  - `readFile(path) -> Result<std::vector<u8>>`, `readText(path) -> Result<std::string>` (binär, keine Zeilenende-Umwandlung)
  - `writeFile`/`writeText` sowie `writeFileAtomic`/`writeTextAtomic` (Temp-Datei `<ziel>.tmp` + `rename`,
    nie halb geschriebene Saves/Configs)
  - `createDirectories`, `exists`, `lastWriteTime` (Hot-Reload-Polling)
  - `fromUtf8`/`toUtf8`: Pfade an der API sind UTF-8 (unter Windows sonst ANSI-Codepage)
  - `BaseDirectories{gameDir, userDir}` als dokumentiertes Subsystem-Singleton (`setBaseDirectories`
    beim Start), `gamePath("assets/...")`, `userPath("saves/...")`.
    `game/src/main.cpp` setzt `gameDir` = Verzeichnis der Executable und `userDir` =
    `platform::userDataDirectory("Gothar", "Gothar")` (SDL_GetPrefPath; Rückfall `gameDir/userdata`) –
    OS-Aufrufe gehören nicht in core.
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

- `Geometry.hpp` – `Plane` (Abstand, aus Punkt/Normale oder Koeffizienten), `Sphere`, `AABB` (`center`, `extents`,
  `transformed`), `Frustum::fromViewProjection` (6 Ebenen nach innen, für 0..1-Tiefe inkl. Reverse-Z;
  `contains`, `intersects(Sphere/AABB)` konservativ), `perspectiveReverseZ(fovY, aspect, near, far)` (ADR 0002).
- `StringId.hpp` – `StringId`: 64-Bit-FNV-1a über ASCII-kleingeschriebene Bytes, also **immer
  case-insensitive** (`StringId("WP_HC_CAMPFIRE") == "wp_hc_campfire"_sid`), wie Gothic-Namen.
  - `StringId(name)` (Laufzeit), `"NAME"_sid` (`consteval`, in `g7::literals`; für `switch`/`static_assert`),
    `fromHash`, `hashOf`, `hash()`, `valid()` (Default = ungültig, Hash 0), Vergleichsoperatoren
  - `G7_STRINGID_NAMES` (Standard: an ohne `NDEBUG`): zur Laufzeit erzeugte IDs tragen ihren Klartext in eine
    threadsichere Tabelle ein (dokumentiertes Subsystem-Singleton); `name()` liefert die zuerst registrierte
    Schreibweise, Kollisionen lösen Log-Fehler + `G7_ASSERT` aus. Literale werden nicht registriert.
  - `std::hash` und `std::formatter` (Klartext oder `#<16 Hex-Ziffern>`)
- `StringUtil.hpp` – ASCII-Groß-/Kleinschreibung (andere Bytes, z. B. UTF-8-Umlaute, bleiben unverändert):
  `toLowerAscii`/`toUpperAscii`, `toLower`/`toUpper`, `equalsIgnoreCase`, `startsWithIgnoreCase`,
  transparente `IgnoreCaseHash`/`IgnoreCaseEqual` für `std::unordered_map<std::string, T, …>`.
- `Config.hpp` – `Config` (ADR 0010; toml++ PRIVATE hinter PImpl, header-only mit `TOML_EXCEPTIONS=0`):
  - `Config::parse(text, sourceName)`, `Config::load(path)` → `Result<Config>`; Syntaxfehler als
    `"<quelle>:<zeile>:<spalte>: <beschreibung>"`. `save(path)` schreibt atomisch, `toToml()`.
  - Schlüssel sind Punkt-Pfade (`"audio.volume.music"`). Werttypen (Concept `ConfigValue`): `bool`, `i64`, `f64`,
    `std::string`, `std::vector<std::string>`, `std::vector<f64>`; der Typ wird immer explizit angegeben:
    `get<i64>("render.fps_limit", 0)`, `find<T>(key) -> std::optional<T>`, `set<T>(key, value)`, `contains(key)`.
  - Array-Elemente per Index im Pfad: `"object[2].mesh"` (Tabellen-Arrays `[[object]]`); `arraySize(key)` liefert die
    Länge (0 ohne Array). Zahlen-Arrays akzeptieren auch Ganzzahlen (`[1, 2.5, 3]`).
  - `get` liefert bei fehlendem Schlüssel den Default, bei falschem Typ den Default + Warnung im Log;
    `f64` akzeptiert auch Ganzzahlen. `set` legt Zwischentabellen an und ersetzt vorhandene Werte.
  - `keys(table)` – direkte Unterschlüssel, sortiert (z. B. alle Aktionen in `[bindings]`).
  - `merge(overrides)` – rekursiv; Grundlage für Standard-Config ← Benutzer-Config ← Kommandozeile (ab M1).

- `Profiler.hpp` – Profiler-Hook:
  - Makros `G7_PROFILE_SCOPE("name")`, `G7_PROFILE_FUNCTION()`, `G7_PROFILE_FRAME()`; nur aktiv mit
    CMake-Option `G7_PROFILING=ON` (Standard OFF → kompilieren zu nichts). Zonennamen müssen statische
    Lebensdauer haben (String-Literale, `__func__`) – dieselbe Regel wie bei Tracy.
  - Dahinter vorerst ein eingebauter Sammler (`g7::profiler`, dokumentiertes Subsystem-Singleton, threadsicher):
    `ScopedZone`, `markFrame()`, `lastFrame() -> std::vector<ZoneStats{name, calls, totalMs, maxMs}>`
    (inklusive Zeiten, sortiert nach Gesamtzeit). Grundlage für das Debug-Overlay in M2; ab M17 Tracy
    hinter denselben Makros.
  - `Engine::run` markiert Frames und misst die Zonen `Engine::frame`, `Engine::fixedUpdate`, `Engine::render`.

## Später
- `Ray` (Editor-Auswahl, M4).
- Job-System (M17), Frame-/Pool-Allokatoren bei Bedarf, Zufallszahlen (deterministisch, seedbar – wichtig für Saves/Tests).
