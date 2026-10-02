# 0011 – Plattformschicht: SDL3

- **Status:** Akzeptiert
- **Datum:** 2026-10-02
- **Phase:** M1

## Kontext
Die Engine braucht Fenster, Tastatur/Maus/Gamepad-Eingabe, einen OpenGL-Kontext (M2) und
Benutzerverzeichnisse für Saves/Config – auf Windows (Hauptplattform) und Linux. Plattform-Aufrufe
sind auf das Modul `platform` beschränkt (`docs/02-architecture.md`).

## Optionen
1. **SDL3** – Fenster, Eingabe inkl. Gamepad-Datenbank, GL-Kontext, `SDL_GetPrefPath`, Offscreen-Videotreiber
   für Tests; zlib-Lizenz, in vcpkg vorhanden.
2. **GLFW** – schlank und GL-nah; schwächere Gamepad-Unterstützung, keine Benutzerpfade.
3. **SFML** – bringt viel Ungenutztes mit (Grafik, Netzwerk), weniger GL-nah.
4. **Eigene Win32/X11/Wayland-Schicht** – volle Kontrolle, sehr viel Aufwand und Fehlerquellen.

## Entscheidung
SDL3, **PRIVATE** im Modul `platform`. Kein anderes Modul bindet SDL-Header ein; die öffentliche
API (`g7::platform::Window`, `userDataDirectory`, später `Input`) ist SDL-frei (PImpl).
Das Preset `nodeps` baut SDL 3.4.16 statisch per `FetchContent`.

## Konsequenzen
- Tests und CI ohne Display nutzen den SDL-Videotreiber `offscreen` (`SDL_VIDEO_DRIVER=offscreen`).
- vcpkg-Port ohne Standard-Features, unter Linux nur `x11` und `wayland`: Die Standard-Features `dbus`/`ibus`
  ziehen `libsystemd` samt langer Abhängigkeitskette nach. IME-Unterstützung (ibus) und D-Bus-Funktionen
  (z. B. Bildschirmschoner-Sperre) bei Bedarf später ergänzen.
- Die Linux-CI braucht Entwicklungspakete für X11/Wayland, damit vcpkg SDL3 bauen kann.
- Unter Windows liefert vcpkg `SDL3.dll`; sie wird neben die Executables kopiert und muss beim Packaging (M17) mit.
