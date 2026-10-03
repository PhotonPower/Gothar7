# 0004 – Physik: Jolt Physics

- **Status:** Akzeptiert (2026-10-03, Entscheidung Projektinhaber)
- **Datum:** 2026-10-02
- **Phase:** M5

## Optionen
1. **Jolt Physics** – modern, schnell, multithreaded, hervorragender Charakter-Controller (`CharacterVirtual`), MIT-Lizenz, in Horizon Forbidden West eingesetzt.
2. **PhysX 5** – ausgereift, BSD-3; größer, schwerer zu integrieren.
3. **Bullet** – bekannt; Entwicklung weitgehend eingeschlafen, Charakter-Controller schwach.
4. **Eigene Kollision** (wie ZenGin) – volle Kontrolle; hoher Aufwand, fehleranfällig.

## Entscheidung
Jolt Physics, privat im Modul `physics`.

## Umsetzung
- Version **5.6.0** (MIT): vcpkg-Port `joltphysics`; das `nodeps`-Preset holt dasselbe Release per FetchContent
  (Tag `v5.6.0`, SHA256 `6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e`).
- Die API von `physics` enthält keine Jolt-Typen (PImpl); siehe `docs/modules/physics.md`.
