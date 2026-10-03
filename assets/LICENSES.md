# Lizenzen und Quellen

Jede Fremdquelle, die in Assets oder abgeleitete Daten einfließt, wird hier eingetragen.

## Geodaten
| Quelle | Lizenz | Pflichtangabe | Verwendung |
|---|---|---|---|
| LGL Baden-Württemberg – DGM1, LoD2, DOP | Datenlizenz Deutschland – Namensnennung 2.0 | „Datengrundlage: LGL, www.lgl-bw.de“ | Gelände und Baukörper des Spielorts Leonberg, einschließlich aller daraus abgeleiteten Dateien unter `assets/source/worlds/leonberg/` (Weltdateien `*.g7world`, Heightmap, Gebäude-`.glb` in `generated/`) und der gekochten Fassungen |
| OpenStreetMap | ODbL 1.0 | „© OpenStreetMap-Mitwirkende“ | Straßen, Plätze, Nutzung; abgeleitet: Splat-Karten in `assets/source/worlds/leonberg/generated/` |

## Modelle, Texturen, Sounds
| Asset | Quelle | Lizenz | Pflichtangabe |
|---|---|---|---|
| `assets/source/testscene/town/*` (Wände, Dächer, Laterne, Stände, Karren, Zaun, Bäume, Felsen; `Textures/colormap.png`) | Kenney „Fantasy Town Kit“ 2.0, kenney.nl/assets/fantasy-town-kit | CC0 1.0 (Lizenztext `town/License.txt`) | keine (Nennung „Kenney“ freiwillig) |
| `assets/source/characters/rig/human_reference.*` (Gelenkpositionen und Knochenachsen des Referenz-Rigs; Geometrie der Gliederpuppe ist eigene Arbeit) | Quaternius „Universal Animation Library 2“ [Standard], quaternius.com bzw. opengameart.org/content/universal-animation-library-2 | CC0 1.0 | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/characters/figures/placeholder_mannequin.*` (Platzhalterfigur: Mannequin-Mesh mit Gewichten, auf das Referenz-Rig umgebunden) | Quaternius „Universal Animation Library 2“ [Standard], opengameart.org/content/universal-animation-library-2 | CC0 1.0 | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/characters/anims/human/*` (Clips mit Quelle Q/Q→ in `docs/design/animation-list.md`; Herkunft je Clip in `tools/chargen/src/gothar_chargen/data/clips/<set>.toml`; umbenannt, Kanäle bereinigt, teils rückwärts/überblendet/verkettet) | Quaternius „Universal Animation Library“ 1 und 2 [Standard], opengameart.org/node/174563 und opengameart.org/content/universal-animation-library-2 | CC0 1.0 | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/characters/parts/farmer/*`, `figures/farmer.glb` (Körper/Kopf aus MPFB2-Basismesh mit MakeHuman-Makros, an das Referenz-Rig angepasst) | MakeHuman/MPFB2 Core-Assets (Basismesh, Rig „game_engine“); CC0 laut static.makehumancommunity.org/about/license.html („All core assets are shared under Creative Commons, CC0“) | CC0 1.0 | keine |
| `assets/source/characters/textures/skin/*`, `textures/face/*` (Haut, Augen, Brauen, Wimpern; verkleinert) | MakeHuman „System assets“ (`makehuman_system_assets_cc0.zip`); CC0 laut static.makehumancommunity.org/assets/assetpacks/makehuman_system_assets.html | CC0 1.0 | keine |
| `textures/cloth/elvs_crude_t-shirt_male_*`, Hemd-Geometrie | MakeHuman Asset-Paket „Shirts 01“ (Elvaerwyn, `shirts01_cc0.zip`); CC0 laut static.makehumancommunity.org/assets/assetpacks/shirts01.html | CC0 1.0 | keine |
| `textures/cloth/toigo_wool_pants_*`, Hosen-Geometrie | MakeHuman Asset-Paket „Pants 01“ (Toigo, `pants01_cc0.zip`); CC0 laut static.makehumancommunity.org/assets/assetpacks/pants01.html | CC0 1.0 | keine |
| `textures/cloth/culturalibre_male_boots_*`, Stiefel-Geometrie | MakeHuman Asset-Paket „Shoes 01“ (Culturalibre, `shoes01_cc0.zip`); CC0 laut static.makehumancommunity.org/assets/assetpacks/shoes01.html | CC0 1.0 | keine |
| `textures/hair/cortu_short_messy_hair_*`, Haar-Geometrie | MakeHuman Asset-Paket „Hair 01“ (Cortu, `hair01_cc0.zip`); CC0 laut static.makehumancommunity.org/assets/assetpacks/hair01.html | CC0 1.0 | keine |
| `assets/source/characters/monsters/wolf/*` (Mesh, Gewichte und Rig des Wolfs auf das Monster-Rig `wolf` umgebaut; Clips `wolf/s_idle`, `wolf/s_walk`, `wolf/s_run`, `wolf/t_turn_l/r` aus „Idle“/„Walking“ abgeleitet), `tools/chargen/src/gothar_chargen/data/monsters/wolf.toml` (Gelenkpositionen) | Quaternius „Animated Animales Low Poly“ (Animal Pack Vol.2), opengameart.org/content/animated-animales-low-poly | CC0 1.0 (laut OpenGameArt-Seite) | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/characters/monsters/keiler/*` (Mesh, Gewichte und Rig des Schweins auf das Monster-Rig `keiler` umgebaut, dunkel eingefärbt; Clips `keiler/s_idle`, `s_walk`, `s_run`, `t_turn_l/r`, `t_die` aus „Idle“/„Walk“/„Death“ abgeleitet), `tools/chargen/src/gothar_chargen/data/monsters/keiler.toml` (Gelenkpositionen) | Quaternius „Lowpoly Animated Farm Animal Pack“, opengameart.org/content/lowpoly-animated-farm-animal-pack | CC0 1.0 (laut OpenGameArt-Seite) | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/testscene/nature/*` (Bäume, Felsen, Büsche, Lagerfeuer, Holzstapel, Zelt) | Kenney „Nature Kit“ 2.1, kenney.nl/assets/nature-kit | CC0 1.0 (Lizenztext `nature/License.txt`) | keine (Nennung „Kenney“ freiwillig) |

## Engine-eingebettete Daten
| Daten | Quelle | Lizenz | Verwendung |
|---|---|---|---|
| 8×8-Bitmap-Schrift (ASCII 0x20–0x7E) | `font8x8_basic.h` von Daniel Hepper (github.com/dhepper/font8x8), nach IBM-PC-BIOS-Schriften | Public Domain | Debug-Text (`engine/render/src/DebugFont.hpp`) |
