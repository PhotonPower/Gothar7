# Messung Vegetation, Teil V (2026-11-23)

Teil V des Gras-Plans (`render.md` „Plan M17: Grasfelder und Vegetations-Instancing“): erst messen, ob die CPU-Seite
der ~5100 Pflanzen-Vobs in Leonberg zählt. Nur dann kommen statische Instanz-Puffer je Modell und Zelle; sonst nur
das Schatten-Kriterium und dieser Bericht.

- **Aufbau:** Preset `profile` (RelWithDebInfo, `G7_PROFILING`, Tracy ohne verbundenen Client), RTX 3080 Laptop,
  1600 × 900, `gothar --perf=feld,markt`, je Uhrzeit 600 Bilder, Gras `medium` (Vorgabe). Leonberg aus
  `leonberg-main/a2800a49`. Stand: G3 (#444) mit bzw. ohne Schatten-Kriterium.
- **Störung:** Während der Läufe liefen ein `gothar.exe` und `gothar-chargen.exe` anderer Sitzungen. Die CPU-Zonen
  streuen deshalb stark (die unveränderte Zone `render::culling` schwankt zwischen den Läufen um den Faktor 2); die
  GPU-Zeiten und die Zählungen tragen die Aussage.

## CPU je Bild (Mittel, ms) – vor dem Schatten-Kriterium

| Szenario | Uhrzeit | Bild | `render::culling` | `render::shadow cascade` (Summe) | `render::scene` gesamt |
|---|---|---|---|---|---|
| feld | 10:00 | 3,16 | 0,041 | 0,47 | 1,12 |
| feld | 19:30 | 3,68 | 0,037 | 0,68 | 1,29 |
| markt | 10:00 | 4,52 | 0,080 | 1,01 | 2,77 |
| markt | 21:00 | 5,06 | 0,089 | 1,18 | 3,02 |

- Das **Sammeln der sichtbaren Objekte** (`render::culling`: Raster-Abfrage, Abstand, Größe, LOD, Draw-Liste des
  Hauptpasses) kostet im Feld **0,04 ms**, auf dem Markt 0,08–0,09 ms. Budget „Culling und Draw-Listen“ (perf.md):
  1,0 ms – also 4–9 %.
- Die Schatten-Kaskaden (sammeln und abschicken, 2,75 Kaskaden je Bild mit `shadow_far_cadence`) liegen bei
  0,5–1,2 ms; davon ist der größere Teil das Abschicken der Multi-Draws und der Figuren, nicht das Durchlaufen der
  Pflanzen.
- Draws: feld ~505–521, markt ~976–1000 (Multi-Draw: ein Befehl je Material-Gruppe, nicht je Pflanze).

## Entscheidung zu Teil V

**Instanz-Puffer je Modell und Zelle lohnen sich nicht** und bleiben zurückgestellt: Selbst wenn sie die Arbeit je
Pflanze ganz beseitigten, sparten sie im Feld unter 0,05 ms im Hauptpass und einen Teil der Schatten-Sammlung – bei
einem Bild von 3–5 ms CPU und 16,7 ms Budget. Die GPU (1,5–2,6 ms gesamt) ist durch die Zahl der Draws nicht
begrenzt. Wieder aufnehmen, wenn ein Szenario das Budget „Culling und Draw-Listen“ reißt.

## Schatten-Kriterium (umgesetzt)

Kleine Deko (größenabhängig ausblendbar, größte Ausdehnung unter 1,5 m: Büschel, Unkraut, niedrige Büsche, Eimer)
wirft Sonnenschatten nur noch in den ersten zwei Kaskaden (`render::castsShadowInCascade`). Weiter weg wäre ihr
Schatten ein, zwei Texel einer groben Kaskade.

| Szenario | Uhrzeit | GPU `scene` vorher | nachher | Dreiecke vorher | nachher |
|---|---|---|---|---|---|
| feld | 10:00 | 1,293 | 1,224 | 787 000 | 775 000 |
| feld | 19:30 | 1,872 | 1,837 | 1 155 000 | 1 138 000 |
| markt | 10:00 | 2,348 | 2,239 | 1 497 000 | 1 437 000 |
| markt | 21:00 | 2,436 | 2,353 | 1 567 000 | 1 495 000 |

Gewinn 0,04–0,11 ms GPU (3–5 % der Szene), 1–4 % Dreiecke. Die CPU-Zahlen des zweiten Laufs sind durch die
Fremdlast verrauscht (auch unveränderte Zonen wurden teurer) und taugen nicht für einen Vergleich; weniger Objekte
in zwei Kaskaden können die CPU nur entlasten.

Die Intel-UHD-Messung entfällt (Projektinhaber 2026-11-24); die Vorgabe `auto` = `low` für Gras auf Intel bleibt.
