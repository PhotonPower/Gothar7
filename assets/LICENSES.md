# Lizenzen und Quellen

Jede Fremdquelle, die in Assets oder abgeleitete Daten einfließt, wird hier eingetragen.

## Geodaten
| Quelle | Lizenz | Pflichtangabe | Verwendung |
|---|---|---|---|
| LGL Baden-Württemberg – DGM1, LoD2, DOP | Datenlizenz Deutschland – Namensnennung 2.0 | „Datengrundlage: LGL, www.lgl-bw.de“ | Gelände und Baukörper des Spielorts Leonberg |
| OpenStreetMap | ODbL 1.0 | „© OpenStreetMap-Mitwirkende“ | Straßen, Plätze, Nutzung |

## Modelle, Texturen, Sounds
| Asset | Quelle | Lizenz | Pflichtangabe |
|---|---|---|---|
| `assets/source/testscene/town/*` (Wände, Dächer, Laterne, Stände, Karren, Zaun, Bäume, Felsen; `Textures/colormap.png`) | Kenney „Fantasy Town Kit“ 2.0, kenney.nl/assets/fantasy-town-kit | CC0 1.0 (Lizenztext `town/License.txt`) | keine (Nennung „Kenney“ freiwillig) |
| `assets/source/characters/rig/human_reference.*` (Gelenkpositionen und Knochenachsen des Referenz-Rigs; Geometrie der Gliederpuppe ist eigene Arbeit) | Quaternius „Universal Animation Library 2“ [Standard], quaternius.com bzw. opengameart.org/content/universal-animation-library-2 | CC0 1.0 | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/characters/figures/placeholder_mannequin.*` (Platzhalterfigur: Mannequin-Mesh mit Gewichten, auf das Referenz-Rig umgebunden) | Quaternius „Universal Animation Library 2“ [Standard], opengameart.org/content/universal-animation-library-2 | CC0 1.0 | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/characters/anims/human/none.*` (Test-Clips `none/s_idle`, `s_walk`, `s_run`; umbenannt, Kanäle bereinigt) | Quaternius „Universal Animation Library“ [Standard], opengameart.org/node/174563 | CC0 1.0 | keine (Nennung „Quaternius“ freiwillig) |
| `assets/source/testscene/nature/*` (Bäume, Felsen, Büsche, Lagerfeuer, Holzstapel, Zelt) | Kenney „Nature Kit“ 2.1, kenney.nl/assets/nature-kit | CC0 1.0 (Lizenztext `nature/License.txt`) | keine (Nennung „Kenney“ freiwillig) |

## Engine-eingebettete Daten
| Daten | Quelle | Lizenz | Verwendung |
|---|---|---|---|
| 8×8-Bitmap-Schrift (ASCII 0x20–0x7E) | `font8x8_basic.h` von Daniel Hepper (github.com/dhepper/font8x8), nach IBM-PC-BIOS-Schriften | Public Domain | Debug-Text (`engine/render/src/DebugFont.hpp`) |
