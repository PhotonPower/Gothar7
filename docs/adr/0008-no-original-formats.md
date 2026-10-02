# 0008 – Keine Unterstützung von Original-Gothic-Dateiformaten im Spiel

- **Status:** Vorgeschlagen
- **Datum:** 2026-10-02
- **Phase:** –

## Kontext
Mit ZenKit (MIT) gäbe es eine Bibliothek, um ZEN/MRM/MDS/DAT/VDF zu lesen; OpenGothic zeigt, dass
eine Neuimplementierung, die Originaldaten lädt, möglich ist.

## Optionen
1. **Eigene Formate, eigene Inhalte** – rechtlich sauber, Freiheit beim Design; Gothic-Inhalte stehen nicht als Testdaten zur Verfügung.
2. **Original-Daten laden** (wie OpenGothic) – sofort reichhaltige Testwelt; Engine wird an ZenGin-Eigenheiten gebunden, Spiel nur mit Original-Kopie lauffähig, eigenes Spiel nicht das Ziel.

## Entscheidung
Option 1. Ein optionales, separates Import-Werkzeug (z. B. `tools/zen-import` mit ZenKit) kann
später für Forschung entstehen – es ist kein Teil des Spiels, und importierte Daten werden nie committet.
