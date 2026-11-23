# Messung 2026-11-08 – wohin die Ladezeit der Figuren geht (Plan gekochte Figuren, Teil A)

- **Build:** Preset `release` (RelWithDebInfo), Basis main `335caa3b` plus Branch `feature/m17-skin-measure`
  (Aufteilung der Figuren-Ladezeit, siehe unten)
- **Welt:** Leonberg aus dem Snapshot `leonberg-main/1434b991` (welt), 25 Bewohner aus `world_loaded`
- **Hardware:** NVIDIA GeForce RTX 3080 Laptop GPU, 1600×900, Fenster
- **Aufruf:** `build\release\game\gothar.exe --no-user-config --perf=markt --perf-out=<ordner>`, drei Läufe
  (Lauf 1 mit `markt,markt_voll`). Parallel lief ein `gothar.exe` einer anderen Sitzung, aber kein Compiler.
- **Daten:** lose Quelldateien (`assets/source`, Entwicklungs-Mounts). Figurenteile und Clip-Sets als glTF, Bilder als
  PNG. Im Paket liegen die Bilder als KTX2 vor (siehe Befunde).

## Messweise
`loadAnimatedFigure` misst je Schritt die Wanduhr-Zeit und summiert sie über alle Figuren, die `world_loaded`
baut. Engine und `--perf` schreiben die Summen als eigene Ladephasen `figures: …` neben „figures and clips“,
das Log zeigt sie als Zeile `figures: N built in … ms`. Mit dem Preset `profile` stehen dieselben Schritte zusätzlich als
Tracy-Zonen bereit (`figure load`, `figure parts (glTF)`, `figure clip sets (glTF)`, `figure assemble`,
`figure images`, `figure upload`).

| Schritt | Inhalt |
|---|---|
| parts (glTF) | Figurenteile als `SkinnedModelData` laden (fastgltf + Umrechnung), warten |
| clip sets (glTF) | 16 Clip-Sets als `AnimationSetData` laden, warten (danach im Cache) |
| assemble | `asset::assembleFigure` (Hals, verdeckte Dreiecke, Paletten) |
| images | Texturen laden, warten |
| GPU upload | `SkinnedMesh` und Materialien |
| other | Graph, Skelett, Animator, Gangart |

## Ergebnis (ms, Leonberg, 25 Figuren)

| Lauf | figures and clips | Figuren gesamt | parts | clip sets | assemble | images | upload | other | erstes Bild |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1326 | 1268 | 256 | 459 | 32 | 204 | 159 | 157 | 5172 |
| 2 | 1322 | 1271 | 259 | 461 | 29 | 202 | 159 | 162 | 3745 |
| 3 | 1411 | 1353 | 266 | 502 | 32 | 216 | 169 | 168 | 2981 |

Die Zeit bis zum ersten Bild schwankt mit dem Fenster-/OpenGL-Start (2,5 s, 1,2 s bzw. 0,2 s). Der Posten der
Figuren bleibt stabil.

## Befunde
- **glTF-Parsen ist mehr als die Hälfte:** Teile und Clip-Sets zusammen 715–768 ms von 1268–1353 ms (56–57 %).
  - Die Clip-Sets fallen nur beim ersten Figurenaufbau an, danach liegen sie im Cache. Ihre ~460 ms sind also
    reines Einlesen von 15 MB glTF.
  - Die Teile kommen über alle Figuren verteilt dazu, je neuem Teil einmal.
- **Assemble ist klein** (~30 ms für 25 Figuren). Frage 4 (Figuren vorab zusammenbauen) ist damit erledigt: Ein Cache
  fertiger Figuren lohnt nicht.
- **Bilder** ~200 ms: in diesem Lauf PNG-Dekodierung. Im Paket sind es KTX2-Dateien (seit Teil 0, `preferCooked`),
  der Wert ist dort voraussichtlich kleiner. Eigener Posten, nicht Teil von `.g7skin`.
- **Upload** ~160 ms und **other** ~160 ms ändert das Format nicht.
- **Erwarteter Gewinn von `.g7skin`/`.g7anim`:** Bei 5–10× schnellerem Einlesen fallen von ~720–770 ms etwa
  600–700 ms weg. Leonberg hätte die Figuren dann in ~0,6 statt ~1,3 s. Nachgemessen wird mit denselben Ladephasen in
  Teil F.

## Folgen für den Plan
- Teile B–F wie entschieden. `.g7anim` bringt mit ~460 ms den größten Einzelposten, `.g7skin` ~260 ms.
- Danach wären Bilder (KTX2 im Paket messen) und Upload die nächsten Kandidaten, beide außerhalb dieses Plans.
