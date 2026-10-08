# 0007 – Audio: miniaudio

- **Status:** Angenommen (Projektinhaber, 2026-10-08)
- **Datum:** 2026-10-02
- **Phase:** M13

## Optionen
1. **miniaudio** – Single-Header, Public Domain/MIT-0, Mixer-Graph, 3D-Spatialization, OGG/WAV/MP3; kein HRTF.
2. **OpenAL Soft** – HRTF, Standard-API; LGPL, Decoder separat.
3. **FMOD / Wwise** – professionell, Werkzeuge für dynamische Musik; Lizenzkosten/-bedingungen.
4. **SoLoud** – einfache API; weniger aktiv.

## Entscheidung
miniaudio (MIT-0/Public Domain), privat im Modul `audio`. OGG Vorbis dekodiert stb_vorbis (Port `stb`, schon für
stb_image im Projekt), über miniaudios Decoder-Anbindung. Das dynamische Musiksystem wird selbst gebaut
(Taktgrenzen-Übergänge über Sample-genaues Scheduling).

## Weitere Entscheidungen des Projektinhabers (2026-10-08, M13)
- Klänge und Musik: jetzt selbst erzeugte Platzhalter (`tools/audio`); Effekte nach und nach aus geprüften CC0-Paketen
  mit Nachweis in `assets/LICENSES.md`; echte Musik später vom Projektinhaber bzw. beauftragt. KI-erzeugte Musik nur
  nach erneuter Rückfrage.
- Kein HRTF, kein Doppler; Verdeckung als einfacher Tiefpass (Raycast).
- Musikzustände wie Gothic: Std, Thr (ein Feind will angreifen), Fgt (Kampf), 5 s Hysterese, Übergänge auf Taktgrenzen.
- Lautstärken in `engine.toml` `[audio]`; das Optionsmenü folgt mit M14.
- Headless und in CI ohne Audiogerät: miniaudio ohne Device, die Engine zieht die Frames selbst (deterministische Tests).
