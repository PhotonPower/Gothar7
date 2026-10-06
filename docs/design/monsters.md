# Monster-Arten (F5)

Eigene Gegnerarten von Gothar – Namen und Gestalt frei erfunden, keine Gothic-Kreaturen (ADR 0008), mit Bezug
auf die Gegend um Leonberg und das Lager. Vorschläge mit Konzept-Blockouts von `figuren` (2026-10-06); der
Projektinhaber hat **alle vier** ausgewählt. Reihenfolge nach Aufwand: Schinder → Quaderbuckel → Glemsmahr →
Bergleu. Technik: `characters-pipeline.md` §7 (Rig-Vertrag) und §7.4 (Werkzeug `gothar-chargen creature`).
Neue Rigs (Glemsmahr, Bergleu) sind Verträge mit engine und werden vorher über den Koordinator abgestimmt.

| Art (ID) | Rolle | Größe | Gefahr | Skelett | Stand |
|---|---|---|---|---|---|
| **Schinder** (`schinder`) | Aasfresser im Rudel | 0,8 m Schulter | gering, im Rudel mittel | vom Wolf abgeleitet | Mesh, Rig, 15 Clips (2026-10-07) |
| **Quaderbuckel** (`quaderbuckel`) | gepanzerter Steinbruch-Bewohner | 1,8 m lang, 0,84 m hoch | mittel | vom Keiler abgeleitet + Stirnschild | Mesh, Rig, 17 Clips (2026-10-07) |
| **Glemsmahr** (`glemsmahr`) | Nachtjäger im Glemswald | geduckt 1,5 m, aufgerichtet 2,3 m | hoch (früh) | neues Rig (~45 Knochen) | offen |
| **Bergleu** (`bergleu`) | Boss: das Wappentier vom Engelberg | 1,6 m Schulter, 3,2 m lang | sehr hoch | neues Katzen-Rig (~50 Knochen) | offen |

## Schinder

Aasfresser vom Schindanger – Hyänen- bzw. Dachs-Gestalt: hohe Schultern, abfallender Rücken, kahler fleckiger
Hals und Kopf, dunkler Borstenkamm, quer gestreifte Flanken, buschiger Schwanz mit dunkler Spitze.

- **Lebensraum:** Schindanger, Abfallgruben vor dem Lager, Wegränder nach Kämpfen; Rudel aus 2–4 Tieren.
- **Verhalten:** Kreist um Leichen und Verwundete (`s_sneak`), ruft das Rudel (`t_call`). Greift nur an, wenn es in
  der Überzahl ist oder der Held unter der Hälfte seiner Lebenspunkte liegt; duckt sich, wenn es unterlegen ist
  (`s_cower`), und flieht, sobald ein Rudeltier fällt. Hingeworfenes Fleisch lenkt es ab (`s_eat`).
- **Gefahr / Rolle:** frühes Spiel, gering allein, mittel im Rudel. Beute: Fell, Zähne.
- **Tempo (mit engine, 2026-10-07):** Gehen 1,3, Rennen 6,5, Schleichen 0,7 m/s.
- **Technik:** 26 Knochen (Wolf-Benennung plus `spine_02`, `neck_02`, `jaw`, `ear_l/r`), lod0 7834 Dreiecke
  (Körper, Augen, Zähne), Fell 1024² mit Normal-Map 512²; Beschreibung `data/monsters/schinder.creature.toml`.
- **Für später notiert** (Projektinhaber): Der Kopf wirkt noch etwas nagetierhaft (runde Augen, kastenförmige
  Schnauze, hohles offenes Maul) – Feinschliff, wenn das Werkzeug für Quaderbuckel bzw. Glemsmahr ohnehin
  erweitert wird.

## Quaderbuckel

Gepanzerter Bewohner der alten Sandsteinbrüche: Rücken aus Platten, die wie behauene Sandsteinquader aussehen.
Friedlich, bis man ihm zu nahe kommt; warnt mit Stampfen und Knirschen, dann Rammstoß, der umwirft. Von vorn zieht
er den Kopf unter den Stirnschild – Klingen prallen ab; Flanke und Bauch sind verwundbar, stumpfe Waffen und Magie
wirken besser. Beute: Panzerplatten (Schild-Handwerk), Fleisch.

- **Tempo (mit engine, 2026-10-07):** Gehen 0,8, Rennen 3,5 (langsamer als der Held: man kann ihm davonlaufen),
  Anrennen `s_charge` 4,5 m/s am Ort (ohne Root Motion), beim Aufprall `t_attack_1` (Rammstoß, `hit_start`/`hit_end`;
  das Umwerfen setzt engine).
- **Stirnschild (Vertrag mit engine, 2026-10-07):** Knochen `brow_shield` unter `chest` (nicht unter `neck_01`):
  Translation gibt es nur auf root und pelvis, der Kopf taucht durch Beugen des Halses unter den stehenden Schild
  (`t_block_in`, `s_block`, `t_block_out`). Dreht der Kopf zur Seite (`t_attack_2`), dreht der Schild nur etwas mit.
  `socket_shield` ist Kind von `brow_shield` (Funken und Klang); ob ein Treffer „von vorn“ kommt, prüft engine über
  den Winkel (±60°), kein eigener Kollisionskörper.
- **Warnen:** `t_warn` (Stampfen, Plattenknirschen `sound:quaderbuckel_grind`) vor dem Angriff.
- **Technik:** 29 Knochen, lod0 7956 Dreiecke: Körper, 60 starre Sandstein-Platten (Dachziegel-Reihen, die beim
  Biegen überlappen), Stirnschild aus drei Blöcken, Steinknauf am Schwanz, kleine tiefliegende Augen, Maul mit
  Gaumen, Zunge, dunklem Rachen und Hornkanten; Beschreibung `data/monsters/quaderbuckel.creature.toml`.

## Glemsmahr

Hagerer Nachtjäger mit überlangen Armen, läuft auf den Knöcheln, flaches Gesicht mit großen, im Licht leuchtenden
Augen. Nur nachts im Wald; tagsüber schläft er in Baumhöhlen und Ruinen. Einzelgänger, folgt dem Helden außer
Sicht, greift von hinten an und springt zurück ins Dunkel; scheut Fackeln und Feuer. Lehrt „Nachts nicht ohne
Licht in den Wald“. Beute: Augen (Alchemie). Sonderclips: Schleichen, Aufrichten, Sprung, Zurückweichen vor Licht.

## Bergleu

Das verwilderte Wappentier Leonbergs („Löwenberg“): alt, vernarbt, mit dunkler Mähne, Säbelzähnen und kurzen
steinernen Hornstümpfen; einmalig in der Welt, haust in einer Höhle im alten Bruch oberhalb der Stadt.
Prankenhieb, Sprung, Schwanzschlag; das Gebrüll lässt den Helden taumeln und Begleiter fliehen; unter halben
Lebenspunkten rasend. Quest-Boss (verschollene Jäger, Kopfgeld der Stadt). Trophäe: Mähne.
