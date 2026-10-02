# 0010 – Konfigurationsformat: TOML mit toml++

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** M0

## Kontext
Engine- und Benutzereinstellungen (Auflösung, Sichtweite, Lautstärken, Tastenbelegung) werden
von Hand bearbeitet und vom Optionsmenü (M14) geschrieben. Das Format braucht Kommentare,
Typen (bool, Zahl, String, Liste) und verschachtelte Abschnitte. Die Engine wirft keine
Exceptions über Modulgrenzen.

## Optionen
1. **TOML mit toml++** – header-only, C++17/20, gepflegt, TOML 1.0 vollständig, auch ohne Exceptions nutzbar.
2. **TOML mit toml11** – ähnlich ausgereift, aber stärker auf Exceptions ausgelegt.
3. **JSON (nlohmann-json, ab M4 ohnehin da)** – keine Kommentare, Anführungszeichen-Pflicht; schlecht für handbearbeitete Dateien.
4. **Eigenes INI-Format** – Wartungsaufwand, keine Typen, keine Listen.

## Entscheidung
TOML mit toml++, **PRIVATE** in core hinter der PImpl-Klasse `g7::Config`. toml++ wird mit
`TOML_EXCEPTIONS=0` (und header-only) eingebunden; Parse-Fehler werden zu `g7::Result`.
Das Preset `nodeps` lädt toml++ v3.4.0 per `FetchContent`.

## Konsequenzen
- Kein anderes Modul sieht toml++; ein späterer Wechsel betrifft nur `Config.cpp`.
- Nur die Typen bool, i64, f64, String und String-Liste sind über die API erreichbar; weitere
  (z. B. Datum) bei Bedarf ergänzen.
- `config/engine.toml` und das Überschreiben durch Benutzer-Config/Kommandozeile folgen in M1.
