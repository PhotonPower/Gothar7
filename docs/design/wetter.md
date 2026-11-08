# Plan M17: Wetter und Wind

Auftrag des Projektinhabers (über den Koordinator, 2026-11-07), Roadmap M17 „Wetter: Regen (Partikel + nasse
Oberflächen), Gewitter, Wind für Vegetation“:
- Regen mit Partikeln und nassen Oberflächen, Pfützen optional;
- Gewitter mit Blitz und Donner, der Donner verzögert;
- Wind für Vegetation und für Kleidung bzw. Fahnen;
- Wetterwechsel über den Tag.

Stand: **Plan freigegeben** (Koordinator, 2026-11-07). Der Projektinhaber hat alle zehn Vorschläge zum Spielgefühl
übernommen; Teil K entfällt. Umsetzung je Teil ein PR: E1 → R → N → G → W; **alle Teile umgesetzt** (W nach
welts Markierungen #396; Mechanik und Werte in `docs/modules/render.md` „Wind“). Abweichung vom Entwurf unten: das
Windgewicht kommt nicht aus der Vertexfarbe, sondern aus der Höhe im Modell und der Markierung am Material (Vertrag
in `coordination.md`).

## Ausgangslage
- **Tageszeit:** `world::DayCycle` liest `data/environment.toml` mit Schlüsselbildern je Uhrzeit: Sonne, Mond, Ambient,
  Nebel, Himmel, Bloom und seit M17 P2 die Farbkorrektur (`grading_*`). `[zone.<wert>]` überschreibt die
  Farbkorrektur je Zone. Ein Spieltag dauert 96 Echtminuten (`[time] minute_seconds = 4`).
- **Partikel (M12):** Emitter als Daten (`data/fx/*.toml`), auf der CPU simuliert, mit Instancing gezeichnet. Für
  Feuer und Zauber gemacht (hunderte Teilchen), nicht für Regen (zehntausende).
- **Audio (M13):** Ambiente je Zone (`data/ambient.toml`) mit Schleife und Einzelklängen sowie Tag und Nacht. Dazu
  ein Tiefpass je Klang und seit W3 je Bus (`setBusMuffle`). Innenräume (`indoor`-Zonen) haben ein eigenes Ambiente,
  das Draußen läuft gedämpft dahinter.
- **Wasser und Post (M17):** Bloom, Farbkorrektur je Tageszeit und Zone, Wasser mit SSR, Unterwasser, SSAO. Die
  Entscheidung 2 zu P2 sieht für Gewitter schon „grau-grün“ vor.
- **Speichern (M15):** `save::GameState` (JSON) mit Spielzeit und Welt-Deltas. Das Wetter gehört dazu.
- **Vegetation:** welt baut Modelle und Pflanzungen (W6, PR 1 #352 Modelle gemergt, PR 2 #370 Gärten offen).
  Wind-Animation und Instancing stehen in der Roadmap als eigener M17-Punkt „Vegetation“. Den Wind für die
  Vegetation plane ich ein, **sobald welts PR 3 da ist** (Vorgabe des Koordinators).
- **Was es noch nicht gibt:** Specular-Licht (die Beleuchtung ist Lambert plus Hemisphären-Ambient), eine
  Draufsicht-Tiefe (welche Stellen unter einem Dach liegen), Wind.

## Grundsätze
- **Mechanik in C++, Werte als Daten:** Wetterlagen, Häufigkeiten, Stärken und Übergänge stehen in
  `data/weather.toml`. Die Stimmung je Lage (Licht, Nebel, Farbkorrektur) steht dort als Überschreibung der
  Tageszeit, genau wie `[zone.*]` in `environment.toml`.
- **Ein Wetterzustand für die Welt**, in `world` (neu `world::Weather`, neben `DayCycle`). Er liefert je Frame einen
  `WeatherSample`: Regen 0..1, Wolken 0..1, Wind (Richtung, Stärke, Böen), Nässe 0..1, Blitz. Render, Audio, Partikel
  und KI lesen nur diesen Zustand.
- **Deterministisch:** Der Ablauf hängt von der Spielzeit und einem Startwert ab. Speichern und Laden ergibt
  dasselbe Wetter, Tests sind wiederholbar (`--seed`).
- **Abschaltbar:** `[render] weather = true`. Aus heißt: kein Regen, keine Nässe, keine Blitze zu sehen. Die
  Lage läuft aber weiter (Spielstand, Ton, Skripte), damit Speichern und Laden gleich bleiben. Dazu
  `[render] rain_quality = low|medium|high`.
- **Innen trocken:** Unter Dächern regnet es nicht und nichts wird nass. Eine Draufsicht-Tiefe um die Kamera
  entscheidet das, siehe Teil R. In Innenräumen (`indoor`-Zonen) klingt der Regen gedämpft „auf dem Dach“.
- **Keine neue Bibliothek, keine ADR:** Alles sind Shader und Daten hinter der vorhandenen RHI.

## Teile (je ein PR, in dieser Reihenfolge)
1. **E1 – Wetterlagen, Ablauf, Stimmung, Lua, Speichern** (noch ohne Regen-Darstellung)
   - `data/weather.toml`:
     - Lagen `[state.<name>]`, z. B. `klar`, `bewoelkt`, `regen`, `gewitter`, `nebel`. Jede Lage hat `rain`,
       `clouds`, `wind`, `gusts`, `fog_factor`, `light_factor` und `grading_*` als Überschreibung der Tageszeit.
     - Ablauf `[schedule]`: Wahrscheinlichkeiten je Lage und Tageszeit, Dauer `min`/`max` in Spielstunden,
       Übergang in Spielminuten, erlaubte Folgen. Gewitter z. B. nur aus Regen oder bewölkt.
   - `world::Weather` würfelt die nächste Lage zur nächsten Wechselzeit und blendet über die Übergangszeit.
     `DayCycle::evaluate` mischt die Lage in Licht, Nebel, Himmel und Farbkorrektur (Wolken dunkeln die Sonne,
     Himmel und Nebel werden grauer).
   - Lua:
     - `set_weather(name, minutes?)` (sofort oder mit Übergang);
     - `weather() -> {name, rain, wind, next}`;
     - `lock_weather(bool)` (eine Story-Szene hält die Lage);
     - Ereignis `on("weather_changed", fn(old, new))`.
     - Generiert in `docs/script-api.md`.
   - Konsole: `weather regen`. Startparameter: `--weather=regen`, für Prüfbilder.
   - Speichern: Lage, Ziel, Fortschritt des Übergangs, nächste Wechselzeit und der Zufallszustand kommen in
     `GameState`. Das Format ist additiv nach ADR 0021, die Versionsnummer bleibt.
   - Tests:
     - CPU: Ablauf deterministisch, Wahrscheinlichkeiten über viele Tage, Übergang monoton, Sperre durch `lock_weather`.
     - Laden und Speichern: dasselbe Wetter danach.
     - Mischung in `DaySample`.
2. **R – Regen**
   - **Regenschleier:** eigener Renderer statt der CPU-Partikel.
     - Ein Zylinder aus Strichen um die Kamera (Radius etwa 25 m), Position je Instanz im Vertex-Shader aus
       Instanznummer und Zeit, ohne CPU-Simulation.
     - Schräg nach dem Wind, Länge nach der Fallgeschwindigkeit.
     - Anzahl nach `rain` und `rain_quality`: low 4 000, medium 12 000, high 24 000.
     - Additiv sehr schwach, damit er nicht „modern“ glänzt.
   - **Draufsicht-Tiefe** (Regen-Verdeckung): eine Tiefenkarte von oben über 64 × 64 m um die Kamera, 512².
     - Gezeichnet mit dem Tiefen-Vorpass-Weg aus P3 (Schatten-Shader, eigene Pipeline).
     - Neu nur, wenn die Kamera mehr als 8 m weiterzieht oder die Welt sich ändert.
     - Regentropfen unter einem Dach werden verworfen, unter Vordächern und in Toren bleibt es trocken.
   - **Spritzer:** kurze Partikel an zufälligen Bodenpunkten im Umkreis von 10 m, aus der Draufsicht-Tiefe; bei
     `low` aus.
   - **Unter Wasser** kein Regen, aber Ringe auf der Wasseroberfläche (`water.frag`, Normalenstörung nach `rain`).
   - **Ton:**
     - Regen-Schleife `amb_rain` als eigene Ebene über dem Zonen-Ambiente, Lautstärke nach `rain`.
     - In `indoor`-Zonen die Variante `amb_rain_roof`, gedämpft. Unter Wasser über den Bus-Tiefpass.
     - Wind-Schleife `amb_wind` (gibt es schon) nach `wind`.
     - Platzhalter-Klänge erzeugt wie die übrigen `amb_*`, eigene Aufnahmen später (Lizenz CC0/eigen).
   - **Tests:**
     - GPU: kein Regen unter einem Dach (Draufsicht-Tiefe), Dichte folgt `rain`, `weather = false` zeichnet nichts.
     - Audio: Regen-Ebene nach `rain`, innen gedämpft.
3. **N – Nasse Oberflächen, Pfützen (optional)**
   - **Nässe 0..1** steigt bei Regen langsam (etwa 10 Spielminuten bis nass) und trocknet danach langsamer
     (etwa 1 Spielstunde). Sie gilt nur draußen, über die Draufsicht-Tiefe.
   - **In `common/lighting.glsl`:**
     - Albedo dunkler, wie bei nassem Stein und Holz: ×0,6 bei voller Nässe, abhängig von der Material-Rauheit.
       Diese steht noch nicht in den Materialien, Vorschlag ist eine Vorgabe je Material-Gruppe.
     - Ein Glanz: Fresnel zum Himmel (Horizont- und Zenitfarbe wie beim Wasser) und Sonnenglanz, nur auf nach oben
       zeigenden Flächen stark.
   - **Pfützen** (Stufe `high`):
     - Eine Rauschmaske auf flachen, nach oben zeigenden Flächen (Gelände, Pflaster), die mit der Nässe wächst.
     - Dort spiegelnd wie Wasser, mit Himmel und bei `high` auch SSR aus W2. Dazu Regenringe.
   - **Tests:**
     - GPU: nass ist dunkler und glänzt nach oben, unter einem Dach bleibt es trocken.
     - Pfützen nur auf flachen Flächen; `weather = false` bleibt trocken.
4. **G – Gewitter**
   - **Blitz:**
     - Ein kurzes Aufhellen von 0,1–0,3 s, zwei bis drei Zuckungen.
     - Ein gerichtetes Licht aus der Blitzrichtung, Himmel und Nebel heller, Bloom tut den Rest.
     - Optional ein sichtbarer Blitzstrahl am Horizont als prozeduraler Streifen im Himmel-Shader.
   - **Donner:**
     - Entfernung des Blitzes zufällig 300–3000 m, Verzögerung = Entfernung / 343 m/s, also 1–9 s.
     - Lautstärke und Tiefpass nach Entfernung: nahe Blitze krachen hell, ferne grollen dumpf.
     - Klänge `thunder_near`, `thunder_far`.
   - Häufigkeit je Gewitter-Lage aus `weather.toml`. Farbkorrektur grau-grün, wie in Entscheidung 2 zu P2 schon
     vorgesehen.
   - **Tests:**
     - CPU: Verzögerung aus der Entfernung, Blitzfolge deterministisch.
     - GPU: der Blitz hellt das Bild für die Dauer auf.
     - Audio: der Donner startet zur richtigen Mixer-Zeit (`play` mit `delaySeconds`).
5. **W – Wind**
   - **Globaler Wind** aus der Wetterlage: Richtung (dreht langsam), Grundstärke und Böen (Rauschen über Zeit und
     Ort). Er steht im Lighting-Block bzw. in einem eigenen kleinen Block für alle Shader.
   - **Partikel:** Rauch und Funken (M12) treiben mit dem Wind, mit einem Faktor je Emitter (`wind` in
     `data/fx/*.toml`).
   - **Vegetation:**
     - Biegen im Vertex-Shader: Stamm wenig, Äste und Blätter mehr, Gras stark, mit Phasenversatz je Instanz.
     - Das braucht ein **Gewicht je Vertex**. Vorschlag für den Vertrag mit welt: Vertexfarbe Alpha = Biegegewicht
       (0 am Fuß, 1 an den Spitzen). Fehlt sie, gilt die Höhe über dem Modellfuß relativ zur Modellhöhe.
     - Kennzeichnung der Modelle als Vegetation über die Kategorie bzw. den Material-Namen, im Vertrag festzulegen.
     - **Erst nach welts PR 3** (W6 Vegetation). Bis dahin bleibt der Vertrag ein Vorschlag in `coordination.md`.
     - Das Instancing der Vegetation (Roadmap „Vegetation“) ist ein eigener Punkt. Der Wind wirkt auch ohne es.
   - **Fahnen, Wimpel, Zunftschilder:** dasselbe Biegen mit dem Gewicht aus der Vertexfarbe. Schilder schwingen
     als Ganzes um ihre Aufhängung, mit Gewicht nach unten zunehmend. Betrifft welt (Schilder #261) und figuren
     (Fahnen als Gegenstände).
   - **Kleidung:** nicht, wie in Gothic 1 (Entscheidung 7). Möglich wäre später ein leichtes
     Schwingen von Umhang- und Rock-Knochen, sofern das Referenz-Skelett sie hat (Vertrag mit figuren,
     `animation.md`).
   - **Tests:**
     - CPU: Wind deterministisch, Böen im Rahmen.
     - GPU: die Spitze eines Testmodells bewegt sich mit dem Wind, der Fuß nicht; ohne Wind steht alles still.
6. **K – NPCs bei Regen – entfällt** (Entscheidung 6: NPCs stellen sich nicht unter). Der Entwurf zur Erinnerung:
   - Ein Zustand `ZS_SHELTER`: Wer draußen eine Routine ohne festen Ort hat, z. B. Herumstehen, sucht bei Regen den
     nächsten überdachten Freepoint oder geht nach Hause.
   - Wer arbeitet (Händler am Stand, Wache), bleibt.
   - Inhalt in Lua, Mechanik (überdacht ja/nein aus der Draufsicht-Tiefe bzw. `indoor`) in C++.
   - Wahrnehmung: Regen senkt die Hörweite leicht (Entscheidung offen).

## Budget je Teil (1600×900, RTX 3080 Laptop, Release; Intel UHD als Ziel der Stufe „low“)
| Teil | Budget | Stufe low |
|---|---|---|
| E1 Wetterlage (CPU) | ≤ 0,05 ms | gleich |
| R Regenschleier (high 24 000 Striche) | ≤ 0,5 ms | 4 000 Striche ≤ 0,4 ms |
| R Draufsicht-Tiefe (nur bei Bewegung > 8 m) | ≤ 0,8 ms im Frame der Erneuerung, sonst 0 | 256², ≤ 0,5 ms |
| R Spritzer | ≤ 0,15 ms | aus |
| N Nässe in den Shadern | ≤ 0,2 ms | gleich |
| N Pfützen (high, mit SSR) | ≤ 0,5 ms | aus |
| G Blitz und Donner | ≤ 0,05 ms | gleich |
| W Wind (Vertex-Biegen, Partikel) | ≤ 0,3 ms bei welts Vegetationsdichte | ≤ 0,3 ms |
| **Summe Regen + Gewitter, high** | **≤ 2,0 ms** | ≤ 1,2 ms |

Gemessen wird mit den Passzeiten aus P1 (neue Pässe `rain`, `rain_map`). Überschreitet ein Teil sein Budget,
wird er heruntergestuft (weniger Striche, kleinere Karte, seltener erneuert), nicht das Budget erhöht.

## Optionen (Reiter „Grafik“, gespeichert wie bisher nur bei Abweichung)
| Option | Schlüssel | Werte | Vorgabe |
|---|---|---|---|
| Wetter | `render.weather` | an / aus | an |
| Regenqualität | `render.rain_quality` | niedrig / mittel / hoch (Pfützen nur hoch) | mittel |
| Wind | `render.wind` | an / aus (Vegetation und Fahnen stehen still) | an |

## Schnittstellen und Abhängigkeiten
- **welt:**
  - Vertrag für das Wind-Gewicht der Vegetation, Fahnen und Schilder (Vorschlag oben).
  - Alles erst nach welts PR 3; ich schlage den Vertrag dann in `coordination.md` vor.
- **figuren:**
  - Fahnen als Gegenstände mit Gewicht in der Vertexfarbe.
- **Audio:** neue Klänge `amb_rain`, `amb_rain_roof`, `thunder_near`, `thunder_far` (zunächst Platzhalter).
- **Speichern:** additiv in `GameState` (ADR 0021).

## Entscheidungen zum Spielgefühl (Projektinhaber, 2026-11-07: alle zehn wie vorgeschlagen; rückbaubar in `weather.toml`)
1. **Häufigkeit:** an etwa jedem dritten Spieltag Regen, Gewitter an etwa jedem zehnten (nur im Sommerhalbjahr,
   solange es keine Jahreszeiten gibt: immer möglich). Nebel am Morgen an etwa jedem vierten Tag.
2. **Dauer:**
   - Regen 1–3 Spielstunden, also 4–12 Echtminuten.
   - Gewitter 20–60 Spielminuten innerhalb eines Regens.
   - Morgennebel bis etwa 9 Uhr.
   - Übergänge 15 Spielminuten (1 Echtminute).
3. **Stärke:** meist mäßiger Landregen (`rain` 0,5), selten Starkregen (0,9). Kein Wetter, das die Sicht unter
   etwa 80 m drückt; Gothic ist dunkel genug.
4. **Wann:** Regen zu jeder Tageszeit. Gewitter bevorzugt am Nachmittag und Abend. Kein Wetterwechsel während
   Dialogen, Zwischensequenzen und gesperrter Story-Szenen (`lock_weather`).
5. **Wind:** immer eine leichte Brise (Gras und Blätter bewegen sich etwas). Bei Regen mehr, beim Gewitter böig;
   Rauch treibt mit.
6. **NPCs bei Regen:** **nein**, wie in Gothic 1, wo der Regen das Verhalten nicht änderte. Teil K entfällt.
7. **Kleidung im Wind:** nein, wie in Gothic 1. Fahnen, Wimpel und Schilder bewegen sich.
8. **Blitz und Donner:** deutlich, aber nicht grell; Blitz eher als Aufleuchten des Himmels, selten ein
   sichtbarer Strahl. Nahe Donnerschläge laut, die meisten ferner Grollen.
9. **Pfützen:** nur in der Stufe „hoch“, auf Pflaster und Wegen. Sie bleiben nach dem Regen etwa eine Spielstunde.
10. **Wetter und Welt:** dieselbe Wetterlage für Leonberg und das Testlager. Höhlen und Innenräume bleiben trocken
    und hören den Regen gedämpft.

## Offene technische Punkte
- **Material-Rauheit für die Nässe** (Teil N): Die Materialien haben keine Rauheit. Vorschlag ist eine Vorgabe je
  Material-Gruppe (Stein, Holz, Putz, Stoff) über den Material-Namen, bis welt und figuren sie im glTF setzen.
- **Specular:** Für Nässe und Pfützen kommt ein einfacher Glanz in `lighting.glsl` dazu. Ohne Nässe bleibt er aus,
  das Bild bleibt also bytegleich.
