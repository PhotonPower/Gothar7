# Messung 2026-11-26 – Animation im Job-System (perf.md Teil C, C2)

Vorher/nachher für C2: Die Kreaturen werden in drei Phasen aktualisiert (seriell KI und Bewegungsabsicht, parallel
Animator/Look-At/Gesicht, seriell Clip-Events und Root-Motion), und die Pose mit Skinning-Palette entsteht vor dem
Zeichnen für alle Figuren, die gezeichnet werden können, parallel (`prepareCreaturePoses`).

- **Build:** Preset `profile` (RelWithDebInfo, `G7_PROFILING`), RTX 3080 Laptop, 16 logische Kerne (Job-System
  automatisch: 8 Threads = Hauptthread + 7 Arbeiter), 1600 × 900, Schatten an. Leonberg aus `leonberg-main/862c7ca1`.
- **Vorher:** Branch `feature/m17-jobs` (C1, b2dd6b93: Job-System vorhanden, aber ungenutzt – Verhalten wie main).
  **Nachher:** C2 mit `[jobs] threads = 0` (zwei Läufe, „nachher“ und „Wiederholung“) und mit `--set=jobs.threads=1`
  (gleicher Build, alles seriell).
- **Lauf:** `gothar --perf=markt,markt_voll --no-user-config`, je Uhrzeit 600 Bilder nach dem Anlauf, ohne
  Bildratengrenze. Die Simulation läuft mit 60 Hz, etwa jedes vierte Bild hat einen Schritt; **je Schritt** = Zeit je
  Bild ÷ Aufrufe von `Engine::fixedUpdate`. Keine parallele GPU-Last.
- **Zonen:** `creatures (ai, animation)` umfasst alle drei Phasen, `creatures: animation (parallel)` ist die
  Wandzeit der Parallel-Phase auf dem Hauptthread, `render::figure poses` der Pose-Vorpass (neu; vorher steckte die
  Pose in `render::figures`). Die Zonen `animation` und `job` laufen auch auf den Arbeitern und zählen deren Zeit mit –
  sie sind CPU-Zeit, keine Wandzeit.

## markt_voll 10:00 (32 von 90 NPCs simuliert, dazu sichtbare nur animierte)

| ms | vorher | nachher | Wiederholung | nachher, 1 Thread |
|---|---|---|---|---|
| Bild Mittel / p99 | 4,66 / 6,29 | 4,44 / 5,98 | 4,65 / 5,94 | 4,65 / 6,97 |
| GPU | 4,26 | 4,07 | 4,30 | 4,12 |
| `Engine::fixedUpdate` je Schritt | 2,25 | 1,36 | 1,36 | 2,31 |
| `creatures (ai, animation)` je Schritt | 2,06 | 1,14 | 1,14 | 2,10 |
| – Parallel-Phase (Wandzeit) je Schritt | (1,16 `animation`) | 0,41 | 0,40 | 1,40 |
| Pose + Palette je Bild | (in `render::figures` 0,93) | 0,19 | 0,19 | 0,49 |
| `render::figures` je Bild (ohne Pose) | – | 0,46 | 0,48 | 0,43 |

## markt 10:00 (12 von 30 NPCs simuliert)

| ms | vorher | nachher | Wiederholung | nachher, 1 Thread |
|---|---|---|---|---|
| Bild Mittel / p99 | 4,11 / 5,39 | 3,96 / 5,11 | 4,13 / 5,31 | 3,95 / 5,67 |
| `Engine::fixedUpdate` je Schritt | 0,58 | 0,53 | 0,52 | 0,60 |
| `creatures (ai, animation)` je Schritt | 0,41 | 0,33 | 0,33 | 0,41 |
| – Parallel-Phase (Wandzeit) je Schritt | (0,18 `animation`) | 0,11 | 0,11 | 0,21 |
| Pose + Palette je Bild | (in `render::figures` 0,19) | 0,07 | 0,07 | 0,09 |

(markt 21:00 entsprechend: Kreaturen 0,52 → 0,39 ms je Schritt; Pose-Vorpass 0,08 ms je Bild, mit 1 Thread 0,10.)

## Bewertung

- **markt_voll:** Die Kreaturen-Schleife sinkt von 2,1 auf 1,1 ms je Schritt (Animation 1,4 → 0,4 ms), der
  Pose-Vorpass von 0,49 auf 0,19 ms je Bild. Bei 60 Bildern/s (ein Schritt je Bild) spart der Hauptthread damit etwa
  **1,3 ms je Bild** – wie in C0 erwartet (−1,2 bis −1,5 ms). Animation (Schritt + Pose) liegt jetzt bei etwa 0,6 ms,
  deutlich unter dem Budget von 1,5 ms.
- **markt:** −0,1 ms je Schritt und −0,02 bis −0,1 ms je Bild; bei wenigen Figuren lohnt das Verteilen kaum, kostet
  aber auch nichts (mit 1 Thread gleich schnell wie vorher).
- **Bildzeit:** In diesen Szenarien bestimmt die GPU die Bildzeit (4,1–4,3 ms GPU bei 4,4–4,7 ms Bild); das Mittel
  ändert sich deshalb kaum (Streuung zwischen zwei Läufen ±0,2 ms). Der Gewinn ist Hauptthread-Zeit, die bei
  60-FPS-Begrenzung frei wird, und ein ruhigeres p99 im Stress (6,3 → 6,0 ms).
- **Wirkungsgrad:** Auf den Arbeitern braucht eine Figur etwa doppelt so lang wie seriell (Zone `animation` 2,2 statt
  1,2 ms CPU je Schritt: Hyperthreading-Paare, Takt, Speicher). Mit 8 Threads bleibt trotzdem Faktor 3,4 in der
  Wandzeit. Mehr Threads lohnen sich deshalb nicht.
- **1 Thread** (`[jobs] threads = 1`) ist so schnell wie vorher: Die Aufteilung in Phasen kostet nichts Messbares.
