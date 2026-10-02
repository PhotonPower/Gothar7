# 0009 – Spiel-UI: eigenes leichtgewichtiges System

- **Status:** Vorgeschlagen (vor M14 bestätigen)
- **Datum:** 2026-10-02
- **Phase:** M14

## Optionen
1. **Eigenes Retained-Mode-UI** auf dem Renderer – genau zugeschnitten (Gothic-UI ist schlicht), volle Kontrolle über Stil; Aufwand für Layout/Text.
2. **RmlUi** (HTML/CSS-artig) – mächtig, Designer-freundlich; zusätzliche Abhängigkeit, Integrationsaufwand.
3. **Dear ImGui auch fürs Spiel** – schnell; schwer stilisierbar, Immediate-Mode passt nicht zu Animationen/Gamepad.

## Entscheidung
Vorschlag Option 1; Neubewertung von RmlUi zu Beginn von M14. ImGui bleibt Debug/Editor vorbehalten.
