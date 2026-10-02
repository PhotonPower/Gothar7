# 0003 – Grafik-API: OpenGL 4.5/4.6 hinter einer RHI

- **Status:** Akzeptiert (2026-10-02, Mindestversion 4.5)
- **Datum:** 2026-10-02
- **Phase:** M2

## Kontext
Ein Gothic-artiges Spiel braucht keine Spitzen-Grafik, aber stimmungsvolle Beleuchtung, Schatten,
Nebel, viel Vegetation. Entwicklungsgeschwindigkeit ist wichtiger als maximale Performance.

## Optionen
1. **OpenGL 4.6 (DSA)** – schnell produktiv, gut debugbar (RenderDoc), läuft auf Win/Linux; kein macOS-Support (4.1 max), Treiberqualität variiert.
2. **Vulkan** – modern, explizit, beste Performance; sehr viel Boilerplate, langsamer Start.
3. **bgfx / Diligent / sokol** – Abstraktion über mehrere APIs; Fremd-Abstraktion, eigene Lernkurve.
4. **SDL_GPU (SDL3)** – modern, plattformübergreifend; noch jung, weniger Referenzmaterial.

## Entscheidung
OpenGL 4.6 Core mit DSA, gekapselt in einer eigenen dünnen RHI, deren Konzepte (Pipeline-State,
Command-Liste) Vulkan-nah sind. Ein Vulkan- oder SDL_GPU-Backend bleibt als Option für M17.

## Konsequenzen
Kein macOS. Renderer-Code außerhalb der RHI darf keine GL-Aufrufe enthalten.

## Umsetzung (M2)
- **Mindestversion 4.5 Core** (DSA ist Kern ab 4.5); angefordert wird 4.6, Rückfall auf 4.5. 4.6-Funktionen
  (SPIR-V, anisotrope Filterung im Kern) nur optional nutzen. Grund: ältere Intel-GPUs und Tests in der
  Linux-CI mit Mesa llvmpipe (Software-Rasterizer) unter Xvfb.
- **Loader: glad** (vcpkg-Port `glad`, Feature `gl-api-46`, privat in `render`). Der Port erzeugt das
  Kompatibilitätsprofil; die Engine fordert trotzdem einen Core-Kontext an und nutzt nur Core-Funktionen.
  Das Preset `nodeps` hat kein glad (Generator braucht Python) – `render` baut dann ohne GL-Backend.
- Kontext-Erzeugung/Puffertausch/VSync in `platform::GlContext` (SDL), alle GL-Aufrufe in `render`.
- GL-Debug-Output: im Debug-Build synchron, Meldungen im Log (`render`), Wiederholungen gedrosselt.
- CI: GPU-Tests tragen das CTest-Label `gpu`; Linux führt sie unter Xvfb + llvmpipe aus, Windows-Runner
  (ohne OpenGL) schließen sie aus und testen das Fenster mit `--no-render`.
