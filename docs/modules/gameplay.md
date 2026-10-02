# gameplay

**Zweck:** Spielregeln und -systeme. Werte und Formeln möglichst in Skripten; C++ stellt Mechanik bereit.

## Charakter (M8)
- Attribute: `hp`, `hpMax`, `mana`, `manaMax`, `str`, `dex`; Erfahrung, Stufe, Lernpunkte.
- Talente mit Stufen: `melee_1h`, `melee_2h`, `bow`, `crossbow`, `sneak`, `picklock`, `pickpocket`, `acrobatics`, `magic_circle`.
- Schutzwerte je Schadensart: `edge`, `blunt`, `point`, `fire`, `magic`, `fall`.
- Gilden + Gilden-Einstellungstabelle (in Lua). Der Spieler ist ein `Npc` mit `PlayerController` statt `Brain`.

## Items & Inventar (M8)
- Item-Instanz (aus Skript) + Laufzeit-Stack (Menge). Kategorien siehe Roadmap.
- Inventar ohne Gewichtslimit (wie Gothic), sortiert nach Kategorie.
- Ausrüstungs-Slots: Nahkampf, Fernkampf, Rüstung, Ring×2, Amulett, Gürtel(optional), 7 Rune/Spruch-Plätze.
- Benutzen: `onUse`-Skriptfunktion (Trank → Leben, Schriftstück → Dokument-UI).

## Fokus (M8)
Kandidaten im Kegel vor der Kamera/Figur, Priorität NPC > Mob > Item, Distanz- und Sichtprüfung,
Hysterese gegen Flackern. Im Kampf: Gegner-Fokus mit Ziel-Lock.

## Mob-Interaktion (M8)
- Mob-Definition (Daten): Typ, Zustände (`S0`→`S1`…), Animationen je Übergang, Benutzer-Slots, benötigtes Item,
  erzeugtes Item, Skript-Hook (`onUse`, `onStateChange`), Besitzer, Schloss (Kombination z. B. `LRRL`).
- Ablauf: hingehen → an Slot ausrichten → Einstiegs-Animation → Zustände → Ausstieg.

## Dialog & Quests (M10)
- `Info` (siehe script.md): Auswahl der verfügbaren Infos = Bedingung erfüllt ∧ (permanent ∨ nicht gesagt), sortiert nach Priorität; `important` startet Dialog automatisch, wenn der NPC den Spieler wahrnimmt.
- Dialog-Ablauf als Sequenz: `say` (Sprache + Untertitel + Gesten + Lippensync), `choices`, `trade`, `teach`, `end`.
- Dialog-Kamera: Schuss/Gegenschuss je Sprecher.
- Tagebuch: Topics mit Status, Einträge mit Zeitstempel.

## Kampf (M11)
- Waffenmodus-Wechsel (Ziehen/Wegstecken als Animation mit Event `item_to_hand`).
- Nahkampf: Angriff startet Clip; zwischen `hit_start`/`hit_end` Shapecast der Waffe; Kombo-Eingabe
  nur im `combo_window`; Anzahl Kombo-Schläge und Tempo abhängig von Talentstufe.
- Parade/Block, Seitwärtsschritt, Rückwärtsschritt.
- Schaden = max(Waffenschaden + Attributbonus − Schutz, Minimum); kritisch nach Talent; Faustkampf/menschliche Gegner → bewusstlos.
- Fernkampf: Zielmodus, Projektil (Ballistik), Treffer-Chance/-Streuung nach Talent.

## Magie (M12)
Rune (unendlich) vs. Spruchrolle (verbraucht), Mana-Kosten, Kreise; Zauber als Skript + Effekt-Daten
(`invest` zum Aufladen, `cast`, Projektil/Fläche/Verwandlung/Kontrolle/Beschwörung).

## Wirtschaft
Handel: Händler-Inventar, Preisfaktor Verkauf (z. B. 0,5), Währung als Item (`ItMi_Ore`).
