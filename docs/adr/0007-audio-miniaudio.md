# 0007 – Audio: miniaudio

- **Status:** Vorgeschlagen (vor M13 bestätigen)
- **Datum:** 2026-10-02
- **Phase:** M13

## Optionen
1. **miniaudio** – Single-Header, Public Domain/MIT-0, Mixer-Graph, 3D-Spatialization, OGG/WAV/MP3; kein HRTF.
2. **OpenAL Soft** – HRTF, Standard-API; LGPL, Decoder separat.
3. **FMOD / Wwise** – professionell, Werkzeuge für dynamische Musik; Lizenzkosten/-bedingungen.
4. **SoLoud** – einfache API; weniger aktiv.

## Entscheidung
miniaudio. Das dynamische Musiksystem wird selbst gebaut (Taktgrenzen-Übergänge über Sample-genaues Scheduling).
