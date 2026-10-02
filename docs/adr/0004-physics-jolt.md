# 0004 – Physik: Jolt Physics

- **Status:** Vorgeschlagen (vor M5 bestätigen)
- **Datum:** 2026-10-02
- **Phase:** M5

## Optionen
1. **Jolt Physics** – modern, schnell, multithreaded, hervorragender Charakter-Controller (`CharacterVirtual`), MIT-Lizenz, in Horizon Forbidden West eingesetzt.
2. **PhysX 5** – ausgereift, BSD-3; größer, schwerer zu integrieren.
3. **Bullet** – bekannt; Entwicklung weitgehend eingeschlafen, Charakter-Controller schwach.
4. **Eigene Kollision** (wie ZenGin) – volle Kontrolle; hoher Aufwand, fehleranfällig.

## Entscheidung
Jolt Physics, privat im Modul `physics`.
