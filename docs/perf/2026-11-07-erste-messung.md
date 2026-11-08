# Messung 2026-11-07 – erste Werte der Mess-Szenarien

- **Build:** Preset `release` (RelWithDebInfo, ohne `G7_PROFILING`, also ohne Zonen), Basis main `d3e8c2f5` plus
  Branch `feature/m17-perf-scenarios`
- **Welt:** Leonberg aus dem Snapshot `leonberg-main/acfcc3dc` (welt)
- **Hardware:** NVIDIA GeForce RTX 3080 Laptop GPU, 1600×900, Fenster, VSync aus, Schatten an
- **Aufruf:** `build\release\game\gothar.exe --perf=all --perf-out=<ordner>` (je Lauf 5 s Anlauf, 600 Bilder)
- Intel UHD (Entscheidung 1) ist noch nicht gemessen.

## Bildzeit (ms) und Szene

| Szenario | Uhrzeit | Mittel | p50 | p95 | p99 | GPU | Draws | Dreiecke | Lichter | NPCs simuliert/gesamt |
|---|---|---|---|---|---|---|---|---|---|---|
| markt | 10:00 | 3,72 | 3,67 | 4,94 | 6,51 | 2,50 | 973 | 1,59 M | 33 | 12 / 25 |
| markt | 21:00 | 3,82 | 3,73 | 5,25 | 5,65 | 2,69 | 992 | 1,80 M | 33 | 13 / 25 |
| gasse | 10:00 | 3,41 | 3,39 | 4,30 | 7,24 | 2,43 | 783 | 1,17 M | 23 | 2 / 25 |
| gasse | 21:00 | 3,34 | 3,31 | 3,91 | 4,64 | 2,48 | 734 | 1,13 M | 23 | 2 / 25 |
| stube | 10:00 | 2,46 | 2,41 | 2,67 | 3,67 | 2,07 | 328 | 0,29 M | 33 | 5 / 25 |
| stube | 21:00 | 2,52 | 2,40 | 3,42 | 3,99 | 2,03 | 369 | 0,34 M | 33 | 5,1 / 25 |
| markt_voll (+60 NPCs) | 10:00 | 5,08 | 4,16 | 8,54 | 10,04 | 3,24 | 3050 | 3,83 M | 33 | 73 / 85 |

GPU = Summe der Timer-Abfragen je Durchgang (scene, water, effects, bloom, post), geglättet. Der Szene-Durchgang
(Schatten, Hauptpass) macht davon rund 90 % aus.

## Ladezeit (Start bis erstes Bild in Leonberg)

| Phase | ms | fertig nach ms |
|---|---|---|
| assets (VFS, Asset-Manager) | 82 | 82 |
| scripts | 15 | 246 |
| window, OpenGL | 272 | 519 |
| shaders | 38 | 556 |
| scene rendering (targets, shadows, sky) | 25 | 581 |
| world file (read, parse) | 187 | 769 |
| world (vobs, waynet, terrain) | 368 | 1137 |
| houses and models (instances, collision) | 895 | 2032 |
| figures and clips (world_loaded) | 1420 | 3814 |
| **first frame** | | **3850** |

Der erste Lauf nach dem Bauen brauchte 6,2 s bis zum ersten Bild (kalter Datei-Cache).

## Befunde

- Alle drei Szenarien liegen weit unter dem Budget (16,6 ms Bild, GPU ≤ 10 ms). Auch der Stresslauf hält p99 bei
  10 ms.
- **Stress „Markt voll“:** 73 von 85 NPCs werden simuliert, das Budget sieht ≤ 25 vor. Die KI-LOD begrenzt nach
  Entfernung, nicht nach Anzahl: Kandidat für Teil C bzw. D (eine Obergrenze nach Nähe).
  Draws verdreifachen sich (Figuren), p95 verdoppelt sich.
- **Laden:** Figuren und Clips sind mit 1,4 s der größte Posten, danach Häuser und Modelle mit 0,9 s. Das spricht für
  `.g7skin`/`.g7anim` (release.md, Teil D), sobald Leonberg mehr Bewohner bekommt.
- Zonen je System fehlen hier (Release ohne `G7_PROFILING`). Mit dem Preset `profile` (PR #387) stehen sie im
  selben Bericht.
- Ein zweiter Lauf zur selben Zeit war rund 3× langsamer (p99 bis 120 ms). Vermutlich lief parallel GPU-Last einer
  anderen Sitzung. Messungen deshalb nur auf ruhiger Maschine vergleichen.
