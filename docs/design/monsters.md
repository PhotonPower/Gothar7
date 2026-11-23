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
| **Glemsmahr** (`glemsmahr`) | Nachtjäger im Glemswald | geduckt 1,5 m, aufgerichtet ~2,1 m | hoch (früh) | neues Rig (37 Knochen) | Mesh, Rig, 19 Clips (2026-10-07) |
| **Bergleu** (`bergleu`) | Boss: das Wappentier vom Engelberg | 1,6 m Schulter, 3,2 m lang | sehr hoch | neues Katzen-Rig (41 Knochen) | Mesh, Rig, 19 Clips (2026-10-08) |

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
- **Kopf-Feinschliff** (2026-10-07, auf Hinweis des Projektinhabers „noch etwas nagetierhaft“): Augen kleiner und
  tiefer unter einem Oberlid, rundlichere Schnauze mit überhängenden Lefzen und rundem Nasenspiegel, Maul mit
  Gaumen, Zunge und dunklem Rachen (wie beim Quaderbuckel).

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
Licht in den Wald“. Beute: Augen (Alchemie).

- **Rig (Vertrag mit engine, 2026-10-07):** 37 Knochen mit Sockets – Rumpf `spine_01..03`, `neck_01/02`, `head`,
  `jaw`, `ear_l/r`; Arme als Vorderbeine (`front_shoulder` → `front_upper` → `front_lower` → `front_foot` = Hand,
  dazu `front_finger_1/2/3`, ein Glied je Finger); zehengängige Hinterbeine (`back_upper` → `back_lower` →
  `back_foot` = Mittelfuß → `back_toes`); kein Schwanz. Sockets `socket_mouth`, `socket_eyes` (Leuchten),
  `socket_hand_l/r` (Krallen). Eine Kapsel nach der geduckten Gestalt.
- **Tempo:** Knöchelgang 1,4, Galopp 6,5, Anschleichen 0,9 m/s (Root Motion). `t_leap` (~3 m) und `t_jump_back`
  (~2,5 m) laufen am Ort, engine fährt die Kapsel; Events `leap_start`, `leap_land`, beim Sprung `hit_start`/`hit_end`
  um die Landung. Aufrecht (`t_rise`, `s_upright`, `t_lower`) nur zum Drohen und Wittern, gekämpft wird auf vier
  Gliedmaßen. `t_recoil` vor Licht und Feuer mit `recoil` und `sound:glemsmahr_hiss`.
- **Aussehen (nach Rückmeldung des Projektinhabers):** sehnige Glieder mit knotigen Ellbogen, Knien und Handgelenken,
  Muskelsträngen, Schulterblättern, Rippen und Hüftknochen unter blassgrauer, kahler Haut; schädelhaftes,
  eingefallenes Gesicht mit Wangenknochen, kleine Augen tief in den Höhlen mit fahl-gelblichem Leuchten (emissiv),
  lange, zurückgelegte Ohren, breites Maul mit Nadelzähnen; lod0 7636 Dreiecke.

## Bergleu

Das verwilderte Wappentier Leonbergs („Löwenberg“): alt, vernarbt, mit dunkler Mähne, Säbelzähnen und kurzen
steinernen Hornstümpfen; einmalig in der Welt, haust in einer Höhle im alten Bruch oberhalb der Stadt.
Prankenhieb, Sprung, Schwanzschlag; das Gebrüll lässt den Helden taumeln und Begleiter fliehen; unter halben
Lebenspunkten rasend. Quest-Boss (verschollene Jäger, Kopfgeld der Stadt). Trophäe: Mähne.

- **Stil:** ein natürlicher, alter Höhlenlöwe – keine Ähnlichkeit mit Wappen- oder Logo-Löwen bestehender Marken; die
  eigenen Merkmale sind die steinernen Hornstümpfe (groß, hellgrau), Narben und Säbelzähne. Mähne aus langen, flach
  nach hinten fallenden Strähnen (Variante B, vom Projektinhaber gewählt 2026-10-08).
- **Rig (Vertrag mit engine, 2026-10-08):** 41 Knochen mit Sockets – `spine_01..03`, `neck_01/02`, `head`, `jaw`,
  `ear_l/r`, Mähne `mane_back`, `mane_l/r` (nur in den Clips bewegt, kein Nachschwingen in der Engine), `tail_01..05`,
  zehengängige Beine mit Schulterblättern (`front_shoulder`) und Zehen. Sockets `socket_mouth`, `socket_paw_l/r`
  (Prankenhieb), `socket_tail` (Schwanzschlag). Eine Kapsel nach Schulterhöhe und Breite; Treffer prüft engine über eine
  Strecke entlang des Rumpfs.
- **Kampf:** Pirschgang 1,5, Galopp 8,0, Anpirschen 1,0 m/s; `t_pounce` (~5 m) am Ort mit `leap_start`/`leap_land` und
  Treffern um die Landung; `t_tail_lash`; `t_roar` (Event `roar`: Held taumelt, Begleiter fliehen); `t_rage` leitet die
  zweite Phase ein, danach spielt engine die normalen Clips mit Rate ~1,3; `t_stagger` nach schwerem Treffer bzw.
  pariertem Prankenhieb.
- **Technik:** lod0 11096 Dreiecke (Boss-Budget bis 12 k), Beschreibung `data/monsters/bergleu.creature.toml`.

### Gatterbock (gebaut 2026-11-23)
Steinwächter der Erbauer, 2,5 m: Rumpf und Glieder aus ungleichen, verwitterten Sandsteinquadern auf einem dunklen Eisengerüst, von
Eisenbändern gehalten, Fäuste und Füße aus Eisen, gebogene Eisenhörner, zwei Glutaugen unter der Stirn.
- **Verhalten (für engine):** steht als Statue neben einem Gatter (`s_dormant`, `zs_dormant`) und erwacht (`t_wake`),
  wenn das Gatter geöffnet oder die Platte betreten wird; langsam (Gehen 0,9, Trab 2,2 m/s – man kann ihm davonlaufen),
  wuchtig: `t_attack_1` schmettert beide Fäuste vorn nieder (umwerfen), `t_attack_2` ist eine Rückhand von links nach
  rechts. Klingen prallen ab, stumpfe Waffen und Magie wirken (Werte in Lua).
- **Technik:** menschenähnliches Rig (25 Knochen, `characters-pipeline.md` §7.1), Steinblöcke als starre Platten an
  ihren Knochen (er knickt nur in den Gelenken wie eine gegliederte Statue). v2 (Projektinhaber 2026-11-23): Blöcke
  ungleich groß, behauen und verwittert (gefaste, abgeschlagene Kanten, Risse, Moos und Schmutz in den Fugen),
  Eisen rostig; lod0 6288 Dreiecke; Beschreibung `data/monsters/gatterbock.creature.toml`.

### Beinhauswart (gebaut 2026-11-23)
Hagerer, gebeugter Totengräber unklarer Art, 2,0 m bis zur Kapuze: graues Leichentuch mit tiefer Kapuze, darin ein
knöchernes Gesicht mit zwei grünen Lichtpunkten, knöcherne Hände; rechts ein Grabspaten, links eine eiserne Laterne
mit grünem Licht. Lebendig, kein Untoter.
- **Verhalten (für engine):** wartet auf den Spaten gestützt an einer Gruft (`s_dormant`, `zs_dormant`) oder geht
  seine Runde zwischen den Nischen (Gehen 0,8 m/s) und zündet mit der Laterne Lichter an (`s_eat`). Sieht er den
  Helden, stößt er ihm die Laterne entgegen (`t_threaten`, Event `flash` = Blenden, kurzer Schrecken) und schlägt mit
  dem Spaten: `t_attack_1` hackt von oben, `t_attack_2` fegt flach in Kniehöhe (Treffpunkt `socket_spade`). Läuft
  3,2 m/s (langsamer als der rennende Held). Die Laterne ist ein eigenes Item `it_lantern_beinhaus` (grünes Licht am
  Knoten `socket_light`); engine hängt sie per `npc_hold` an `socket_lantern` und lässt sie in der Rasend-Phase
  fallen (Lebenspunkte; mit engine 2026-11-24).
- **Technik:** menschenähnliches Rig wie der Gatterbock (§7.1) mit den Trägern `spade` (Spaten als starre Platten)
  und `lantern` (ohne Geometrie, trägt nur `socket_lantern`); Leichentuch als Körperform; lod0 4712 Dreiecke;
  Beschreibung `data/monsters/beinhauswart.creature.toml`.
