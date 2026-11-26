# Messung 2026-11-26 – Basis für das Job-System (perf.md Teil C, C0)

Vor dem Job-System: Wie viel Hauptthread-Zeit kosten die Kandidaten aus `perf.md` Teil C (Animation, Wahrnehmung,
Culling)? Die Zahlen entscheiden, welche Schritte C2–C4 kommen.

- **Build:** Preset `profile` (RelWithDebInfo, `G7_PROFILING`, Zonen im Bericht), main 862c7ca1, RTX 3080 Laptop,
  1600 × 900, Schatten an. Leonberg aus `leonberg-main/862c7ca1`.
- **Lauf:** `gothar --perf=markt,gasse,stube,markt_voll`, je Uhrzeit 600 Bilder nach dem Anlauf, ohne Bildratengrenze
  (≈ 220–280 Bilder/s). Die Simulation läuft mit festen 60 Hz: Nur etwa jedes vierte Bild hat einen Schritt
  (`Engine::fixedUpdate` 0,2–0,3 Aufrufe je Bild). **Je Schritt** = Zeit je Bild ÷ Aufrufe. Bei 60 Bildern/s fällt
  genau ein Schritt in jedes Bild.
- Zusätzlich lief ein Lauf (markt, markt_voll) mit drei vorübergehenden Zonen, die danach entfernt wurden:
  Culling bzw. Zeichnen je Schattenkaskade, Pose und Skinning-Palette je Figur und Bild.

## Bild und Simulation

| Szenario | Uhrzeit | Bild Mittel | p99 | GPU | NPCs simuliert/gesamt | Simulation je Schritt | davon Kreaturen (KI + Animation) |
|---|---|---|---|---|---|---|---|
| markt | 10:00 | 4,09 | 5,57 | 3,63 | 12/30 | 0,61 | 0,42 |
| markt | 21:00 | 4,19 | 5,66 | 3,74 | 13/30 | 0,75 | 0,53 |
| gasse | 10:00 | 3,59 | 4,59 | 3,18 | 2/30 | 0,21 | 0,09 |
| stube | 10:00 | 3,62 | 4,26 | 3,29 | 5/30 | 0,28 | 0,19 |
| markt_voll | 10:00 | 4,64 | 6,64 | 4,22 | 32/90 | 2,35 | 2,16 |

(ms)

## Kandidaten (Hauptthread)

| Kandidat | Zone | markt 10:00 | markt_voll 10:00 | Budget | Urteil |
|---|---|---|---|---|---|
| Animation abtasten und blenden | `animation` (je Figur) | 0,18 ms je Schritt | **1,21 ms je Schritt**, 14 Aufrufe je Bild | ≤ 1,5 ms | lohnt |
| übrige Kreaturen-Schleife (Look-At, Gesicht, Bewegung, Wege) | `creatures` ohne `animation`, `ai` | 0,22 ms je Schritt | 0,90 ms je Schritt | – | Look-At und Gesicht mit der Animation |
| Pose zwischen den Schritten und Skinning-Palette | vorübergehend „figure pose“, einmal je Figur und Bild | 0,10 ms (15 Figuren) | **0,51 ms** (p95 0,86; 63 Figuren) | (Animation) | lohnt |
| KI-Wahrnehmung | `ai::perception` | 0,001 ms je Schritt | 0,035 ms je Schritt | (KI ≤ 1,5) | lohnt nicht |
| KI ohne Wahrnehmung | `ai` | 0,02 ms je Schritt | 0,06 ms je Schritt | ≤ 1,5 ms | – |
| Culling Hauptpass | `render::culling` | 0,07 ms | 0,09 ms | ≤ 1,0 ms (mit Draw-Listen) | lohnt nicht |
| Culling der Schattenkaskaden | vorübergehend „cascade cull“, alle Kaskaden | 0,10 ms | 0,12 ms | (dto.) | lohnt nicht |
| Zeichnen der Kaskaden (GL) | vorübergehend „cascade draw“ | 0,34 ms | 0,40 ms | – | nicht parallelisierbar (ein GL-Kontext) |

- **Animation insgesamt** in markt_voll bei 60 Bildern/s: 1,2 ms (Schritt, ohne Look-At und Gesicht) + 0,5 ms
  (Pose/Palette) ≈ **1,7 ms**.
  Das liegt über dem Richtwert von 1,5 ms. Im normalen Markt sind es etwa 0,3 ms. Beides ist reine Rechnung je Figur
  (eigene Daten, keine Skripte bis auf die Clip-Events) und lässt sich gut verteilen. Erwartet mit 7 Arbeitern:
  markt_voll −1,2 bis −1,5 ms je Bild, Markt −0,2 bis −0,3 ms.
- **Culling** (Hauptpass und Kaskaden zusammen 0,2 ms) und **Wahrnehmung** (0,035 ms je Schritt) sind so klein, dass
  das Verteilen auf Threads kaum etwas spart. Aufteilen und Zusammenführen kosten in derselben Größenordnung.
- Den größten CPU-Anteil im Bild hat das Absenden der Draws (Kaskaden 0,3–0,4 ms, Figuren in den Kaskaden). Es bleibt
  am einen GL-Kontext und ist kein Fall für das Job-System.

## Entscheidung (nach der Regel des Koordinators: was sich nicht lohnt, entfällt)

- **C1** Job-System in `core`: ja.
- **C2** Animation parallel: ja, beide Teile – das Abtasten je Schritt (Animator, Look-At, Gesicht) und die Pose je Bild
  mit Skinning-Palette (`prepareFigurePose`, vor dem Zeichnen für alle sichtbaren Figuren).
- **C3** Culling parallel: **entfällt** (0,2 ms gesamt).
- **C4** Wahrnehmung parallel: **entfällt** (0,035 ms je Schritt).
- Neu bewerten, wenn Leonberg deutlich mehr Häuser oder NPCs bekommt (Welt-Streaming, mehr Bewohner). Die
  Messung wird dann mit denselben Szenarien wiederholt.
