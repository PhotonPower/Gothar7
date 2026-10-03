# 0019 – Animations-Laufzeit: eigene Implementierung

- **Status:** Akzeptiert (2026-10-03, Entscheidung Projektinhaber)
- **Datum:** 2026-10-03
- **Phase:** M6

## Kontext
M6 braucht Skelettanimation für Menschen und Monster:
- Clips abspielen, überblenden, Oberkörper-Layer, additive Clips
- Events nach dem Vertrag `events.toml` (characters-pipeline.md §3)
- Root Motion für Klettern und Interaktionen
- ein datengetriebener Zustandsautomat

Die Quelldaten sind glTF (Skins und Animationen), die wir mit fastgltf (ADR 0013) ohnehin laden. Das Referenz-Rig hat 60
Knochen (`kMaxBones` 128); erwartet werden einige Dutzend animierte Figuren gleichzeitig.

## Optionen
1. **Eigene Laufzeit** im Modul `animation`:
   - Sampling linear bzw. slerp, Blending, Masken, Events, Root Motion und Zustandsautomat sind überschaubar.
   - Keine neue Abhängigkeit; Daten bleiben glTF bzw. später ein eigenes gekochtes Format nach ADR 0016.
   - Nachteil: Optimierungen wie komprimierte Tracks und SIMD-Sampling müssen wir selbst bauen, wenn nötig.
2. **ozz-animation** (MIT, vcpkg):
   - Schnell und erprobt, SoA/SIMD.
   - Aber eigene Binärformate und ein Offline-Konvertierungsschritt (glTF → ozz) im Cooker.
   - Eigene Skelett- und Clip-Typen neben unseren; Zustandsautomat und Events müssten wir trotzdem selbst schreiben.
3. **Eine Middleware** (z. B. eine Animations-Engine mit Editor): überdimensioniert, Lizenz bzw. Weitergabe unklar.

## Entscheidung
Eigene Laufzeit (Option 1), Entscheidung des Projektinhabers. Bei 60 Knochen × einigen Dutzend Figuren reicht CPU-Sampling;
das Skinning läuft auf der GPU.

## Konsequenzen
- **Modul `animation`:** Skelett, Clip, Pose, Sampling, Blending, Layer, Events, Root Motion, Zustandsautomat
  (`docs/modules/animation.md`).
- **Laden:** `asset` lädt glTF-Skins und -Animationen (fastgltf, privat).
- **Cooker:** kopiert die glTF-Dateien zunächst nur. Ein eigenes Format (`.g7skel`/`.g7anim`) kommt, wenn Ladezeit
  oder Größe es verlangen.
- **Leistung:** Animations-LOD (seltener aktualisieren in der Ferne) und eine Parallelisierung über Figuren (M17)
  sind vorgesehen. Messen, bevor optimiert wird.
