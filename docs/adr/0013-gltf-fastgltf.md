# 0013 – glTF-Import: fastgltf

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** M2 (Laufzeit-Import), M3 (`g7-cook`)

## Kontext
Modelle kommen als glTF 2.0 (`.gltf` + `.bin`/eingebettet, `.glb`) aus Blender & Co. In M2 lädt das Spiel
sie direkt, ab M3 kocht `g7-cook` sie in ein eigenes Laufzeitformat. Der Parser muss Accessoren
(Typen, Normalisierung, Strides, Sparse) korrekt auflösen.

## Optionen
1. **fastgltf** – C++17/20, sehr schnell (simdjson), gepflegt, Accessor-Werkzeuge, MIT; in vcpkg.
2. **cgltf** – ein C-Header, keine Abhängigkeiten, einfach; C-API, Accessor-Hilfen schlichter.
3. **tinygltf** – verbreitet; zieht nlohmann-json und stb nach, langsamer.

## Entscheidung
fastgltf, **PRIVATE im Modul `asset`** (`asset::loadGltf` liefert engine-eigene `MeshData`). Das Preset
`nodeps` holt fastgltf per `FetchContent` (simdjson lädt fastgltf selbst nach).

## Konsequenzen
- Kein anderes Modul sieht fastgltf; der Renderer arbeitet nur mit `asset::MeshData`.
- simdjson kommt als transitive Abhängigkeit mit.
- Statische Meshes zuerst (Knoten-Transformationen eingebacken); Skins/Animationen in M6 mit derselben Bibliothek.
