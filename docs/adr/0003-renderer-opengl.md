# 0003 – Grafik-API: OpenGL 4.6 hinter einer RHI

- **Status:** Vorgeschlagen (vor M2 bestätigen)
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
