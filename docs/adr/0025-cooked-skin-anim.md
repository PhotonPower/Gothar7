# 0025 – Gekochte Figuren und Animationen: `.g7skin` und `.g7anim`

- **Status:** Angenommen (Projektinhaber, 2026-11-08)
- **Datum:** 2026-11-08
- **Phase:** M17 (Plan in `docs/modules/asset.md`, „Plan M17: gekochte Figuren und Animationen“)

## Kontext
ADR 0019 (eigene Animations-Laufzeit) hat festgelegt, dass Figuren und Clips zunächst aus glTF laden und ein eigenes
gekochtes Format folgt, wenn Ladezeit oder Paketgröße es verlangen. Die Roadmap verlangt es spätestens vor dem ersten
reinen `.g7pak`-Release.

Heute gilt:
- `g7-cook` macht aus geskinnten `.glb` nur statische `.g7mesh` und überspringt reine Clip-Sets.
- Das reine Paket aus #384 enthält deshalb keine Figuren.
- In Leonberg ist „figures and clips“ mit 1,4 s der größte Posten bis zum ersten Bild
  (`docs/perf/2026-11-07-erste-messung.md`).

Bedingungen:
- Figuren werden zur Laufzeit aus Teilen zusammengesetzt (`asset::assembleFigure`). Dessen Daten (Hals-Ringe,
  verdeckte Dreiecke) zeigen per [glTF-Primitive, Vertex] in die Teile. Die Reihenfolge der Vertices muss also
  erhalten bleiben.
- `animation` und `render` arbeiten mit `SkinnedModelData` und `AnimationSetData` und sollen unverändert bleiben.
- Keine neue Abhängigkeit ohne Not. Formate nach dem Muster von ADR 0016 (`.g7mesh`, `.g7pak`).

## Optionen
1. **glTF unverändert ins Archiv.**
   - Kein neues Format, der Loader liest glTF schon aus Bytes.
   - Aber fastgltf-Parsen und Umrechnen bei jedem Start bleiben, die Ladezeit sinkt nicht.
   - Laufzeitfehler in Teilen fallen erst im Spiel auf.
   - Als Übergang (Plan Teil 0) sinnvoll.
2. **Eigene Binärformate `.g7skin`/`.g7anim`** (eine Datei je Quell-`.glb`, Abbild der CPU-Strukturen).
   - Einlesen ohne Parser, Prüfung beim Kochen, keine neue Bibliothek.
   - Aber zwei Formate mehr mit Versionen und Tests, und der Cooker muss bei Vertragsänderungen (§6.1/§6.2)
     mitziehen.
3. **ozz-animation** (Offline-Konvertierung, komprimierte Spuren).
   - In ADR 0019 schon verworfen (eigene Laufzeit). Würde eine Bibliothek nur für das Format einführen.
4. **Allgemeine Serialisierung (FlatBuffers, cereal).**
   - Schema-Werkzeug bzw. neue Abhängigkeit für zwei einfache Formate. Kein Vorteil gegenüber Option 2.

## Entscheidung
Option 2, mit Option 1 als Übergang, bis Option 2 im Paket ist.

- **`.g7skin`** (Kopf `G7SK`, Version u32, little-endian, Abschnitte mit Länge):
  - Skelett (Namen, Eltern, Ruhepose, `rootParent`) und inverse Bind-Matrizen.
  - Teile mit Vertices, Joints (u16×4), Gewichten, Indizes, Submeshes, Morph-Targets (dicht, mit Namen) und der
    Primitive-Tabelle.
  - Materialien mit Bildverweisen als VFS-Pfaden, Bounds und den Zusammenbau-Daten (`asset.extras.gothar`, §6.2).
  - Vertex- und Dreiecksreihenfolge wie aus glTF geladen.
- **`.g7anim`** (Kopf `G7AN`, Version u32): Skelett des Sets und Clips mit Spuren (Knochenname, Kanal,
  Schritt/linear, Zeiten, Werte als f32). Quantisierung kommt erst in einer späteren Version.
  `<set>.events.toml` bleibt eine eigene Datei.
- **Cooker:** geskinntes `.glb` → `.g7skin`, Animationen → `.g7anim`, kein statisches `.g7mesh` für Skins,
  `kCookerVersion` 5.
- **Laden:** „Gekochtes gewinnt“ wie bei `.g7mesh` (`x.glb` → `x.g7skin`/`x.g7anim`). Rückfall auf glTF, wenn die
  gekochte Datei fehlt, eine fremde Version hat oder beschädigt ist. Inhalte nennen weiter `.glb`.
- Der Leser prüft alle Grenzen und Indizes. Erwartbare Fehler kommen als `g7::Result`.

## Konsequenzen
- Positiv:
  - Figuren und Clips im reinen Paket.
  - Schnelleres Laden (Messung im Plan, Teil A/F).
  - Fehlerhafte Teile scheitern schon beim Kochen.
  - `animation`/`render` bleiben unverändert.
- Negativ:
  - Zwei Formate mehr. Jede Änderung an §6.1/§6.2 (Morph-Liste, `extras.gothar`) braucht eine neue Format- bzw.
    Cooker-Version.
  - Gekochte Dateien in `assets/cooked` verdecken lose `.glb` wie heute schon bei `.g7mesh`. Wer an Figuren arbeitet,
    kocht neu oder arbeitet ohne gekochten Ordner.
- Vertrag mit figuren: Er bleibt unverändert (glTF, `figure.toml`, `events.toml`). Neu ist nur, dass der Cooker
  ungültige Teile ablehnt.
