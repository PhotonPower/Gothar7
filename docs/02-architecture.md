# 02 – Architektur

## Überblick

```
┌──────────────────────────────────────────────────────────────────────┐
│ game/ (gothar.exe)          tools/ (g7-cook, Editor-Modus)           │
├──────────────────────────────────────────────────────────────────────┤
│ runtime      Engine: Init-Reihenfolge, Hauptschleife, Modus (Spiel/Editor)
├──────────────────────────────────────────────────────────────────────┤
│ L4  save     ui                                                       │
│ L3  gameplay                                                          │
│ L2  ai                                                                │
│ L1  world    animation    audio    render                             │
│ L0' asset    physics      script   platform                           │
│ L0  core                                                              │
└──────────────────────────────────────────────────────────────────────┘
```

### Abhängigkeitsregeln (verbindlich)

| Modul | darf abhängen von |
|---|---|
| core | – (nur Standardbibliothek) |
| platform | core |
| asset | core |
| physics | core |
| script | core |
| render | core, platform, asset |
| audio | core, asset |
| animation | core, asset |
| world | core, asset, render, physics |
| ai | core, world, script |
| gameplay | core, world, animation, script, ai |
| ui | core, platform, render |
| save | core, world, script, gameplay |
| runtime | alle |

- **Keine Zyklen.** Braucht ein unteres Modul Informationen von oben, geschieht das über
  Interfaces/Callbacks, die das obere Modul registriert (z. B. `ai` ruft Skript-Zustände über
  `script`-Funktionsreferenzen auf, nicht über `gameplay`).
- Drittbibliotheken sind **Implementierungsdetail** ihres Moduls (`PRIVATE` linken). Ausnahmen
  (z. B. `glm` in der öffentlichen Mathe-API, `entt` in `world`) sind in ADRs festgehalten.
- Neue Abhängigkeiten zwischen Modulen → diese Tabelle **und** die `CMakeLists.txt` anpassen.

## Laufzeitmodell

### Entities & Daten (ADR 0005)

- Die Welt ist eine **EnTT-Registry** in `world`. Jedes Vob ist eine Entity.
- Komponenten sind **reine Daten** (`Transform`, `MeshRenderer`, `RigidBody`, `Npc`, `Inventory`,
  `Mob`, `Light`, `SoundEmitter`, `Trigger` …).
- Logik lebt in **Systemen** (freie Funktionen oder Klassen mit `update(World&, f64 dt)`),
  gruppiert nach Modul.
- Hierarchien (Waffe in der Hand, Fackel am Gürtel) über `Parent`/`Children`-Komponenten
  und Bone-Attachments aus `animation`.
- Dauerhafte IDs (`VobId`, 64 Bit) für Speicherstände und Skript-Referenzen – Entity-Handles
  sind nur zur Laufzeit gültig.

### Hauptschleife

```
while (!quit) {
    platform.pollEvents()            // Eingabe -> Aktionen
    steps = fixedStep.advance(dt)    // feste Simulationsrate, Standard 60 Hz
    repeat steps:
        script.tick()                // Timer, verzögerte Aufrufe
        ai.update()                  // Wahrnehmung, Zustände, Routinen, Pfade
        gameplay.update()            // Kampf, Interaktion, Inventar-Logik
        animation.update()           // Pose-Berechnung, Root-Motion, Events
        physics.step()               // Charakter-Controller, Rigidbodies
        world.update()               // Spielzeit, Wetter, Trigger
    audio.update(listener)
    render.drawFrame(alpha)          // interpoliert zwischen den letzten zwei Zuständen
    ui.draw()
}
```

- Simulation ist **deterministisch pro Schritt** (keine Abhängigkeit von Frame-Dauer).
- Rendern interpoliert Transformationen mit `FixedStep::alpha()`.
- Spielzeit (Uhrzeit der Welt) ist von Echtzeit entkoppelt und kann beschleunigt werden (Schlafen).

### Threading

- Phase 1: **Single-threaded** Simulation + Rendering, Asset-Laden auf Worker-Threads.
- Später (M17): Job-System in `core` (Task-Graph), parallele Animation/KI-Wahrnehmung,
  Render-Command-Aufzeichnung getrennt von Submission.
- Regel: Systeme greifen nur im Hauptthread auf die Registry zu, außer explizit als Job markiert.

### Initialisierung & Shutdown

Reihenfolge = Abhängigkeitsreihenfolge (core → platform → asset → physics → script → render →
audio → animation → world → ai → gameplay → ui → save). Shutdown in umgekehrter Reihenfolge.
Jedes Subsystem hat `init(const Config&) -> Result<void>` und `shutdown()`.

### Speicher

- Standard-Allokator + `std::unique_ptr`/Container als Default.
- Frame-Allocator (Linear) für temporäre Daten pro Frame, Pools für häufige Kleinobjekte – erst
  einführen, wenn Profiling es rechtfertigt.
- Rohzeiger sind **nicht besitzend**.

### Fehlerbehandlung

- Programmierfehler: `G7_ASSERT` (Debug) / `G7_VERIFY` (immer).
- Erwartbare Fehler (Datei fehlt, Skriptfehler): `g7::Result<T>`.
- Keine Exceptions über Modulgrenzen. Drittbibliotheken, die werfen, werden an der Grenze gefangen.

### Konfiguration

- `config/engine.toml` (Auflösung, Sichtweite, Lautstärken, Tastenbelegung) – ab M1.
- Kommandozeilen-Schalter überschreiben Konfiguration (`--verbose`, `--world=…`, `--editor`).

## Daten- und Skriptfluss

```
assets/source ──g7-cook──► assets/cooked/*.g7pak ──asset──► render / audio / animation / world
game/scripts/*.lua ───────► script (Lua-VM) ──Instanzen──► gameplay (Items, NPCs, Infos)
                                         └──Zustände/Routinen──► ai
worlds/*.g7world (Editor) ─► world (Vobs, Wegnetz, Zonen)
```

- **Inhalt gehört in Skripte und Daten**, Mechanik in C++. Faustregel: Wenn ein Designer es
  ändern will (Werte, Texte, Verhalten eines bestimmten NPCs) → Skript. Wenn es schnell oder
  allgemein sein muss (Pfadsuche, Kollision, Animation) → C++.

## Modi

- **Spiel-Modus**: normaler Ablauf.
- **Editor-Modus** (`--editor`): gleiche Engine, Simulation pausierbar, ImGui-Werkzeuge,
  Weltdateien schreibbar.
- **Headless** (`--smoke-test`, Tests): ohne Fenster/Audio, begrenzte Frames.
