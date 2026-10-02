# audio

**Zweck:** Klangwelt. Bibliothek: **miniaudio** (ADR 0007), privat.

## Bestandteile
- **Mixer-Busse**: Master → Musik, Effekte, Sprache, Ambient, UI; Lautstärken aus Optionen; Ducking (Musik leiser bei Sprache).
- **3D-Sounds**: Emitter-Komponente, Abschwächungskurven, Doppler aus, Verdeckung per Raycast (Tiefpass).
- **Sound-Definitionen** (Daten): Variationen (zufällige Auswahl), Lautstärke/Tonhöhen-Streuung, Reichweite.
- **Ambient-Zonen**: Loop + zufällige Einzelgeräusche, Überblendung zwischen Zonen, Tag/Nacht-Varianten.
- **Sprache**: Datei je Text-Schlüssel (`DIA_Diego_Hello_11_01.ogg`), Dauer bestimmt Untertitel/Gesprächstempo; Lippensync aus Amplitude oder Phonem-Daten.

## Dynamisches Musiksystem (Gothic: DirectMusic)
- Musik-Zone (aus world) → **Thema** (z. B. `CAMP`, `FOREST`, `MINE`).
- Zustand: `Std` | `Thr` (Bedrohung: Feind nimmt Spieler wahr) | `Fgt` (Kampf) – ermittelt aus gameplay/ai.
- Tageszeit: `Day` | `Ngt`.
- Pro Thema × Zustand × Tageszeit eine Menge von Segmenten (Loops) mit Takt-/Tempo-Info.
- Übergänge **auf Taktgrenzen**, Überblendung oder Übergangs-Segment; Stinger (z. B. Quest gelöst).
- Hysterese: Kampf → Standard erst nach X Sekunden ohne Kampf.

```toml
# music/camp.toml
[theme.CAMP]
bpm = 96
[theme.CAMP.std.day]
segments = ["music/camp_day_a.ogg", "music/camp_day_b.ogg"]
[theme.CAMP.fgt]
segments = ["music/fight_generic_a.ogg"]
transition = "next_bar"
```
