# gameplay

**Zweck:** Spielregeln und -systeme. Werte und Formeln möglichst in Skripten; C++ stellt Mechanik bereit.

## Bewegung & Kamera (M5 Teil C, umgesetzt) – `Movement.hpp`
```cpp
namespace g7::gameplay {
struct MovementSettings { runSpeed, walkSpeed, sneakSpeed, backwardSpeed, strafeSpeed, acceleration, deceleration,
                          turnSpeedDegrees, mouseTurnPerPixel, stepHeight, maxSlopeDegrees, stickToFloor; CameraSettings camera;
                          static Result<MovementSettings> parse(std::string_view toml, std::string_view source); };
struct MoveInput { f32 forward, strafe, turn, mouseTurn; bool walk, sneak; };   // aus den Aktionen (runtime)
class PlayerMovement { Vec3 step(const MoveInput&, f32 seconds, const MovementSettings&); void reset(f32 yaw); f32 yaw() const; };
class ThirdPersonCamera { void reset(feet, yaw, const CameraSettings&);
    void update(seconds, feet, yaw, mousePitchPixels, const CameraSettings&, const Obstruction&);   // je Bild
    Vec3 position() const; Quat rotation() const; };   // Obstruction = sphereCast der Engine
}
```
- **Werte: `assets/source/data/movement.toml`** (Spielgefühl; Startwerte vom Projektinhaber freigegeben).
  Die Engine lädt die Datei über den `AssetManager` (`[game] movement`, Hot-Reload im laufenden Spiel).
  Fehlende Schlüssel behalten die Vorgabe; falsche Typen oder Werte sind Fehler, dann gelten die eingebauten
  Vorgaben mit einer Warnung.
- **Gangart wie Gothic** (Entscheidung Projektinhaber): Standard ist **Rennen** (4,0 m/s). Gehalten
  `walk` (Shift) heißt Gehen (1,6), `sneak` Schleichen (1,1). Rückwärts (1,4) und seitwärts (2,0) sind nie
  schneller als die Gangart, diagonal nicht schneller als der schnellere Anteil. Beschleunigen mit 12 m/s²,
  Bremsen mit 16 m/s².
- **Drehen:** klassisch A/D mit 180°/s, die Maus in beiden Schemata (0,15°/Pixel). Gieren 0 = Blick nach −Z.
- **Kamera:**
  - Hinter der Figur, Blickpunkt 1,55 m über den Füßen, Abstand 3 m, Grundneigung 12° nach unten.
  - Die Maus neigt zwischen −40° und +60°.
  - Position und Gieren folgen exponentiell gedämpft (0,12 s bzw. 0,25 s; kein Überschwingen; Drehen den
    kurzen Weg).
  - **Wände:** Ein `sphereCast` (r 0,2 m) vom Blickpunkt nach hinten verkürzt den Abstand, nie unter 0,6 m.
  - Modi (Kampf, Dialog, Schwimmen) folgen mit ihren Phasen als weitere Datensätze.
- **Engine (`runtime/src/EnginePlayer.cpp`):**
  - Die Spielfigur entsteht beim Laden einer Welt auf dem Startpunkt (`--start` bzw. kleinste id; Füße = `pos`,
    nur das Gieren).
  - Keine Figur gibt es ohne Startpunkt, mit `--editor` und mit `--benchmark`.
  - Die Bewegung läuft je festem Schritt.
  - Die Darstellung interpoliert die Füße zwischen den Schritten; die Kamera wird je Bild nachgeführt.
  - Trigger melden die Figur (Hüfthöhe) statt der Kamera.
  - **F3** (`debug_fly`) schaltet auf die freie Debug-Kamera und zurück.
  - Im Spielbetrieb fängt die Maus ein (nicht bei Pause oder offenem Debug-Fenster F1).
  - Bis M6 wird die Platzhalterfigur `characters/figures/placeholder_mannequin.glb` (T-Pose) gezeichnet.
  - F2 zeigt den Zylinder, Bodennormale, Zustand und Tempo.

## Charakter (M8)
- Attribute: `hp`, `hpMax`, `mana`, `manaMax`, `str`, `dex`; Erfahrung, Stufe, Lernpunkte.
- Talente mit Stufen: `melee_1h`, `melee_2h`, `bow`, `crossbow`, `sneak`, `picklock`, `pickpocket`, `acrobatics`, `magic_circle`.
- Schutzwerte je Schadensart: `edge`, `blunt`, `point`, `fire`, `magic`, `fall`.
- Gilden + Gilden-Einstellungstabelle (in Lua). Der Spieler ist ein `Npc` mit `PlayerController` statt `Brain`.

## Items & Inventar (M8)
- Item-Instanz (aus Skript) + Laufzeit-Stack (Menge). Kategorien siehe Roadmap.
- Inventar ohne Gewichtslimit (wie Gothic), sortiert nach Kategorie.
- Ausrüstungs-Slots: Nahkampf, Fernkampf, Rüstung, Ring×2, Amulett, Gürtel(optional), 7 Rune/Spruch-Plätze.
- Benutzen: `onUse`-Skriptfunktion (Trank → Leben, Schriftstück → Dokument-UI).

## Fokus (M8)
Kandidaten im Kegel vor der Kamera/Figur, Priorität NPC > Mob > Item, Distanz- und Sichtprüfung,
Hysterese gegen Flackern. Im Kampf: Gegner-Fokus mit Ziel-Lock.

## Mob-Interaktion (M8)
- Mob-Definition (Daten): Typ, Zustände (`S0`→`S1`…), Animationen je Übergang, Benutzer-Slots, benötigtes Item,
  erzeugtes Item, Skript-Hook (`onUse`, `onStateChange`), Besitzer, Schloss (Kombination z. B. `LRRL`).
- Ablauf: hingehen → an Slot ausrichten → Einstiegs-Animation → Zustände → Ausstieg.

## Dialog & Quests (M10)
- `Info` (siehe script.md): Auswahl der verfügbaren Infos = Bedingung erfüllt ∧ (permanent ∨ nicht gesagt), sortiert nach Priorität; `important` startet Dialog automatisch, wenn der NPC den Spieler wahrnimmt.
- Dialog-Ablauf als Sequenz: `say` (Sprache + Untertitel + Gesten + Lippensync), `choices`, `trade`, `teach`, `end`.
- Dialog-Kamera: Schuss/Gegenschuss je Sprecher.
- Tagebuch: Topics mit Status, Einträge mit Zeitstempel.

## Kampf (M11)
- Waffenmodus-Wechsel (Ziehen/Wegstecken als Animation mit Event `item_to_hand`).
- Nahkampf: Angriff startet Clip; zwischen `hit_start`/`hit_end` Shapecast der Waffe; Kombo-Eingabe
  nur im `combo_window`; Anzahl Kombo-Schläge und Tempo abhängig von Talentstufe.
- Parade/Block, Seitwärtsschritt, Rückwärtsschritt.
- Schaden = max(Waffenschaden + Attributbonus − Schutz, Minimum); kritisch nach Talent; Faustkampf/menschliche Gegner → bewusstlos.
- Fernkampf: Zielmodus, Projektil (Ballistik), Treffer-Chance/-Streuung nach Talent.

## Magie (M12)
Rune (unendlich) vs. Spruchrolle (verbraucht), Mana-Kosten, Kreise; Zauber als Skript + Effekt-Daten
(`invest` zum Aufladen, `cast`, Projektil/Fläche/Verwandlung/Kontrolle/Beschwörung).

## Wirtschaft
Handel: Händler-Inventar, Preisfaktor Verkauf (z. B. 0,5), Währung als Item (`ItMi_Ore`).
