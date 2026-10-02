# 0016 – Asset-Cooker: Texturen (KTX2/UASTC), Pak-Kompression (zstd), Meshformat

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** M3

## Kontext
`g7-cook` wandelt Quell-Assets (`assets/source`, glTF/PNG) in Laufzeitformate um und packt sie in `.g7pak`
(`docs/06-asset-pipeline.md`). Zur Laufzeit soll nur noch Gekochtes geladen werden (M3-Abnahme). Offen waren:
- **Texturen:** ADR 0014 hat KTX2 (libktx) für M3 vorgesehen. Ziel sind vorkomprimierte BC7- (Farbe) und
  BC5-Texturen (Normalen) mit Mipmaps, weil PNG-Dekodieren und Mipmaps auf der GPU für große Welten zu langsam
  und zu speicherhungrig sind. Die Zielhardware ist Desktop mit OpenGL ≥ 4.5; BC7 ist dort Kernbestandteil.
- **Archive:** `.g7pak` v1 speichert unkomprimiert. `kPakFlagCompressed` ist reserviert.
- **Meshes:** glTF zur Laufzeit zu parsen kostet Zeit und braucht fastgltf in der Laufzeit.

In vcpkg verfügbar sind `ktx` (4.4.2, enthält den Basis-Universal-Encoder und -Transcoder), `zstd`, `lz4`,
`meshoptimizer` und `basisu`. Ein eigenständiger BC7-Encoder (bc7enc_rdo, Compressonator, ISPC) ist **nicht** in vcpkg.

## Optionen
**Texturen**
1. **KTX2 + UASTC (libktx):** Der Cooker kodiert nach UASTC mit zstd-Superkompression. Beim Laden wandelt der
   Asset-Worker in BC7 (Farbe) bzw. BC5 (Normalen) um. Dafür reicht eine Abhängigkeit, die Dateien sind kompakt,
   und später sind auch andere GPU-Formate erreichbar. Nachteil: eine kurze Umwandlung beim Laden und eine
   Qualität knapp unter direktem BC7.
2. **KTX2 mit direkt kodiertem BC7/BC5:** bc7enc_rdo im Cooker, libktx nur als Container. Das bringt die beste
   Qualität ohne Umwandlung beim Laden, aber einen Encoder außerhalb von vcpkg (FetchContent) als zusätzliche
   Abhängigkeit.
3. **DDS mit eigenem Schreiber:** keine Bibliothek nötig, aber ein proprietäres Containerformat und alle
   Encoder-Fragen bleiben offen.

**Pak-Kompression**
1. **zstd:** gutes Verhältnis, schnelles Entpacken, ohnehin über libktx im Projekt.
2. **LZ4:** noch schneller beim Entpacken, aber deutlich größere Dateien.
3. **Keine Kompression:** einfach, verschenkt aber Platz bei Meshes, Welten und Skripten.

**Meshes**
1. **Eigenes Binärformat `.g7mesh`:** im Kern eine Ablage von `MeshData`, wird ohne Umrechnung geladen.
2. **glTF zur Laufzeit beibehalten:** kein Cooker-Aufwand, aber langsamer und fastgltf bleibt in der Laufzeit.

## Entscheidung
- **Texturen: Option 1, KTX2 + UASTC** über **libktx** (vcpkg `ktx`). Zur Laufzeit wird nach BC7/BC5 umgewandelt;
  libktx ist `PRIVATE` in `asset` und in `g7-cook` eingebunden. Normal-Maps werden als zweikanalige Daten
  kodiert und nach BC5 umgewandelt. Reichen Qualität oder Ladezeit nicht, ist Option 2 der Ausweg; das
  Containerformat KTX2 bleibt dabei gleich.
- **Pak-Kompression: zstd** (vcpkg `zstd`), pro Eintrag mit Pak-Version 2 und `kPakFlagCompressed`.
  Bereits komprimierte Formate (`.ktx2`, `.ogg`, `.png`, `.jpg`) werden roh gespeichert.
- **Meshes: eigenes Format `.g7mesh`** (Version 1). Es enthält Header, Vertices im `asset::Vertex`-Layout
  (48 Byte), u32-Indizes, Submeshes, Materialien und Bildverweise als VFS-Pfade auf gekochte Dateien.
  `g7-cook` liest glTF mit fastgltf (ADR 0013); die Laufzeit lädt nur noch `.g7mesh`.
  **meshoptimizer** (Vertex-Cache-Optimierung, LODs) wird erst entschieden, wenn LODs gebraucht werden.

## Konsequenzen
- Umsetzung in Schritten:
  1. `g7-cook` v1 mit glTF → `.g7mesh`, Bilder unverändert, Packen.
  2. KTX2: Kodieren im Cooker, Umwandeln in `asset`, Hochladen der komprimierten Formate in `render`.
  3. zstd in `.g7pak` v2.
  4. Manifest mit inkrementellem Kochen.
- Neue Abhängigkeiten `ktx` und `zstd` kommen mit den Schritten 2 und 3 in `vcpkg.json`, ebenso in die
  Bibliothekstabelle von `docs/05-build.md`.
- `render` braucht Upload-Pfade für komprimierte Texturen (`GL_COMPRESSED_RGBA_BPTC_UNORM`/`_SRGB`,
  `GL_COMPRESSED_RG_RGTC2`) mit vorberechneten Mipmaps.
- Der eingebaute `MeshData`-Lader des `AssetManager` unterscheidet `.g7mesh` und glTF an der Endung.
  glTF-Laden zur Laufzeit bleibt für die Entwicklung (`--view-mesh`, lose Dateien), das Spiel lädt nur Gekochtes.
