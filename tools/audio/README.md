# audio – Platzhalter-Klänge und -Musik

Entscheidung des Projektinhabers (M13, ADR 0007): Zuerst erzeugt die Engine-Spur eigene Platzhalter. Effekte kommen
nach und nach aus geprüften CC0-Paketen (Nachweis in `assets/LICENSES.md`), echte Musik später vom Projektinhaber bzw.
beauftragt. KI-erzeugte Musik nur nach erneuter Rückfrage.

```bash
cd tools/audio
uv venv && uv pip install -e ".[dev]"
.venv/Scripts/gothar-audio placeholders          # alle nach assets/source/sounds/<name>.wav
.venv/Scripts/gothar-audio placeholders heal     # nur einen
.venv/Scripts/gothar-audio music                 # Musik-Segmente und Stinger nach assets/source/music/<name>.wav
.venv/Scripts/python -m pytest && .venv/Scripts/ruff check . && .venv/Scripts/ruff format --check .
```

Die Klänge sind synthetisch und deterministisch (fester Zufalls-Seed je Name), 22 050 Hz, mono, 16 bit. Die Namen
entsprechen den Anim-Events `sound:<name>` und den Einträgen in `assets/source/data/sounds.toml`; echte Klänge ersetzen
später die Dateien unter gleichem Namen.

Musik (M13 Teil C, `src/gothar_audio/music.py`): je Segment eine ganze Zahl Takte im Tempo seines Themas
(`data/music.toml`). Die Tempi 96, 120, 80 und 150 BPM legen jeden Takt auf ganze Frames bei 22,05 und 48 kHz.
Nachklänge laufen an den Anfang um, deshalb schließt die Schleife ohne Knacken. Dazu die Stinger `quest`,
`level_up`, `death` und `chapter`.
