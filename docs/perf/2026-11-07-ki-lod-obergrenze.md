# Messung 2026-11-07 – KI-LOD mit Obergrenze nach Anzahl

- **Änderung:** `[ai] max_simulated = 32`. Voll simuliert werden höchstens die 32 nächsten NPCs; wer im Kampf oder
  Dialog ist, bleibt immer simuliert (`docs/modules/ai.md`, KI-LOD).
- **Build:** Preset `release`, Branch `feature/m17-ai-lod-cap` (auf `feature/m17-perf-scenarios`, main eingemergt)
- **Welt und Hardware:** wie bei der ersten Messung (`2026-11-07-erste-messung.md`): Leonberg-Snapshot `acfcc3dc`,
  RTX 3080 Laptop, 1600×900
- **Aufruf:** `gothar --perf=markt,markt_voll` und zweimal `--perf=markt_voll`

## markt_voll (Markt + 60 NPCs), 10:00

| | Mittel ms | p50 | p95 | p99 | schlechtester | GPU ms | Draws | NPCs simuliert/gesamt |
|---|---|---|---|---|---|---|---|---|
| vorher (ohne Obergrenze) | 5,08 | 4,16 | 8,54 | 10,04 | 37,49 | 3,24 | 3050 | 73 / 85 |
| nachher, Lauf 1 | 4,24 | 3,95 | 6,42 | 7,06 | 8,61 | 3,23 | 2496 | 32 / 85 |
| nachher, Lauf 2 | 3,93 | 3,74 | 6,18 | 6,62 | 11,91 | 3,04 | 2523 | 32 / 85 |

## markt (unverändert, Kontrolle)

| Uhrzeit | Mittel ms | p95 | p99 | NPCs simuliert/gesamt |
|---|---|---|---|---|
| 10:00 | 3,86 (vorher 3,72) | 5,46 | 6,57 | 12 / 25 |
| 21:00 | 4,01 (vorher 3,82) | 5,57 | 6,86 | 13 / 25 |

## Befund

- Die Obergrenze hält: 32 von 85 simuliert. p95 sinkt um etwa 25 %, p99 um etwa 30 %, die Ausreißer verschwinden.
- Die GPU-Zeit bleibt gleich: Nicht simulierte NPCs werden weiter gezeichnet. Die Draws sinken vermutlich, weil
  herausfallende NPCs, die gerade gingen, sofort an ihrem Ziel stehen, und das liegt teils außer Sicht.
- Nicht simulierte NPCs wurden nicht animiert und standen in ihrer letzten Pose, mit der Obergrenze auch in
  Sichtweite (im Stresslauf auf dem Markt selbst). Behoben im Folge-PR, siehe unten.
- markt liegt im Rahmen der Messschwankung, dort greift die Obergrenze nicht (25 NPCs).

## Nachtrag: keine Statuen (Animation ohne KI)

NPCs, die die Obergrenze herausnimmt, die aber sichtbar (im Kamera-Frustum) und näher als `simulation_distance` sind,
spielen ihre aktuelle Schleife bzw. Idle-Animation weiter, ohne KI, Wege und Wahrnehmung (Branch
`feature/m17-lod-idle-anim`).

| markt_voll, 10:00 | Mittel ms | p50 | p95 | p99 | schlechtester | GPU ms | NPCs simuliert/gesamt |
|---|---|---|---|---|---|---|---|
| Obergrenze, ohne Animation (2 Läufe) | 4,24 / 3,93 | 3,95 / 3,74 | 6,42 / 6,18 | 7,06 / 6,62 | 8,61 / 11,91 | 3,23 / 3,04 | 32 / 85 |
| mit Animation (2 Läufe) | 4,17 / 4,17 | 3,68 / 3,80 | 6,89 / 6,69 | 7,25 / 7,18 | 8,40 / 8,64 | 3,05 / 3,06 | 32 / 85 |

Die Animation der sichtbaren, nicht simulierten NPCs kostet p95 etwa 0,3–0,5 ms. Im Mittel ändert sich nichts. Alles
bleibt weit unter dem Budget (vor der Obergrenze: p99 10,04 ms).
