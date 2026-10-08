# audio – Platzhalter-Klänge und -Musik

Entscheidung des Projektinhabers (M13, ADR 0007): Zuerst erzeugt die Engine-Spur eigene Platzhalter. Effekte kommen
nach und nach aus geprüften CC0-Paketen (Nachweis in `assets/LICENSES.md`), echte Musik später vom Projektinhaber bzw.
beauftragt. KI-erzeugte Musik nur nach erneuter Rückfrage.

```bash
cd tools/audio
uv venv && uv pip install -e ".[dev]"
.venv/Scripts/gothar-audio placeholders          # alle nach assets/source/sounds/<name>.wav
.venv/Scripts/gothar-audio placeholders heal     # nur einen
.venv/Scripts/python -m pytest && .venv/Scripts/ruff check . && .venv/Scripts/ruff format --check .
```

Die Klänge sind synthetisch und deterministisch (fester Zufalls-Seed je Name), 22 050 Hz, mono, 16 bit. Die Namen
entsprechen den Anim-Events `sound:<name>` und den Einträgen in `assets/source/data/sounds.toml`; echte Klänge ersetzen
später die Dateien unter gleichem Namen.
