# Architecture Decision Records

Wichtige technische Entscheidungen werden hier festgehalten (Vorlage: [0000-template.md](0000-template.md)).
„Vorgeschlagen“ = Empfehlung, die **zu Beginn der jeweiligen Phase** bestätigt oder geändert wird.

| Nr. | Titel | Status | Phase |
|---|---|---|---|
| [0001](0001-cpp20-cmake-vcpkg.md) | C++20, CMake und vcpkg | Akzeptiert | M0 |
| [0002](0002-math-glm.md) | Mathe-Bibliothek: glm | Akzeptiert | M0 |
| [0003](0003-renderer-opengl.md) | OpenGL 4.5/4.6 hinter einer RHI | Akzeptiert | M2 |
| [0004](0004-physics-jolt.md) | Physik: Jolt Physics | Vorgeschlagen | M5 |
| [0005](0005-ecs-entt.md) | Szenenmodell: EnTT mit Vob-Konzept | Vorgeschlagen | M4 |
| [0006](0006-scripting-lua.md) | Skriptsprache: Lua 5.4 + sol2 | Vorgeschlagen | M7 |
| [0007](0007-audio-miniaudio.md) | Audio: miniaudio | Vorgeschlagen | M13 |
| [0008](0008-no-original-formats.md) | Keine Original-Gothic-Formate im Spiel | Vorgeschlagen | – |
| [0009](0009-game-ui.md) | Spiel-UI: eigenes System | Vorgeschlagen | M14 |
| [0010](0010-config-toml.md) | Konfigurationsformat: TOML mit toml++ | Akzeptiert | M0 |
| [0011](0011-platform-sdl3.md) | Plattformschicht: SDL3 | Akzeptiert | M1 |
| [0012](0012-geodata-sources.md) | Geodaten: LGL Open GeoData + OSM statt Google | Akzeptiert | W1 |
| [0013](0013-gltf-fastgltf.md) | glTF-Import: fastgltf | Akzeptiert | M2 |
| [0014](0014-images-stb.md) | Bilddekodierung: stb_image (KTX2 ab M3) | Akzeptiert | M2 |
| [0015](0015-debug-ui-imgui.md) | Debug-/Editor-UI: Dear ImGui mit eigenen Backends | Akzeptiert | M2 |
| [0016](0016-asset-cooker-formats.md) | Asset-Cooker: KTX2/UASTC, zstd-Paks, `.g7mesh` | Akzeptiert | M3 |
