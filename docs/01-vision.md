# 01 – Vision & Gothic-1-Analyse

## Ziel

Gothar ist eine eigene C++20-Engine für ein **Open-World-Action-RPG im Geist von Gothic 1**
(Piranha Bytes, 2001): eine dichte, handgebaute Welt, in der NPCs ein glaubwürdiges Eigenleben
führen, der Spieler als Niemand beginnt und sich Respekt erst verdienen muss.

Die Engine wird **nicht** als Klon von ZenGin gebaut und lädt **keine** Original-Assets. Sie
reproduziert die *Funktionalität* und das *Spielgefühl* mit moderner Technik und eigenen Inhalten.

## Die Säulen von Gothic 1 (was die Engine können muss)

| Säule | Was es im Spiel bedeutet | Technische Konsequenz |
|---|---|---|
| **Lebendige Welt** | NPCs schlafen, essen, arbeiten, trinken am Lagerfeuer – nach Uhrzeit | Tagesabläufe (Routinen), Wegnetz, Spielzeit/Tag-Nacht, KI-Zustände |
| **Glaubwürdige Reaktionen** | „Was machst du in meiner Hütte?“, Waffe ziehen provoziert, Diebstahl wird bemerkt | Wahrnehmungssystem (Sehen/Hören), Einstellungen (Attitudes), Gilden-Beziehungen, Besitz von Objekten/Räumen |
| **Eine zusammenhängende Welt** | Minental ohne Ladezonen, sichtbare Orte in der Ferne | Große Außenwelt + Innenräume in einer Welt, Sichtweite/Nebel, LOD, Streaming (später) |
| **Fraktionen & Aufstieg** | Altes Lager, Neues Lager, Sumpflager; Aufnahme in eine Gilde | Gildensystem, skriptgesteuerte Story-Variablen, Kapitel |
| **Lernen statt Leveln** | Erfahrung → Stufen → Lernpunkte; Lehrer bringen Talente bei | Attribut-/Talentsystem, Dialog-gesteuertes Lernen |
| **Direkter Nahkampf** | Timing-basierte Kombos, Talentstufen ändern Animationen | Animations-Events, Kampf-Zustandsautomat, Treffer-Fenster |
| **Dialoge** | Wahl aus Dialogoptionen, vollvertont, Kamera schneidet | Info-System mit Bedingungen, Dialogkamera, Sprachausgabe + Untertitel |
| **Atmosphäre** | Dynamische Musik, Ambient-Sounds, Regen, Lagerfeuer | Musik-Zonen + Zustände, 3D-Audio, Wetter, Partikel |
| **Moddbarkeit** | Fast alles Inhaltliche liegt in Skripten | Skript-VM, Daten in Skripten, Hot-Reload, Editor |

## Feature-Katalog (Referenz Gothic 1)

**Spielfigur & Steuerung**
- Third-Person-Kamera (Verfolgerkamera, Kollision mit Wänden, eigene Modi für Kampf, Dialog, Schwimmen, Inventar)
- Gehen, Rennen, Schleichen, Seitwärtsschritte, Springen, an Kanten **hochziehen/klettern**, Leitern
- Schwimmen und Tauchen (mit Luftvorrat), Fallschaden
- **Fokus-System**: der Spieler visiert NPCs, Items und interaktive Objekte an, Name wird eingeblendet
- Aktionstaste + Richtungstasten als Steuerkonzept (später optional moderne Belegung)

**Interaktion mit der Welt („Mobs“)**
- Truhen (abschließbar, Dietrich-Minispiel mit Links/Rechts-Folge), Türen, Hebel, Schalter
- Betten (schlafen → Zeit vorspulen), Stühle/Bänke, Kessel, Amboss, Schleifstein, Bratspieß, Alchemietisch, Erzabbau
- Interaktionen sind zustandsbasierte Animationsfolgen (Hin-, Benutzen-, Weg-Animation)
- Items aufheben, ablegen; benutzen (essen, trinken, lesen)

**Charakter**
- Attribute: Stärke, Geschick, Mana, Lebenspunkte (+ Maxima)
- Talente: Einhand/Zweihand (Stufen), Bogen/Armbrust, Schleichen, Schlösser öffnen, Taschendiebstahl, Magiekreise, Akrobatik
- Erfahrung, Stufenaufstieg, Lernpunkte; Lehrer-NPCs
- Gilde bestimmt Rüstung, Zugang, Reaktionen anderer

**NPCs & KI**
- Tagesabläufe: Zeitfenster → Zustand (schlafen, essen, Wache, Schmieden, Smalltalk) an Wegpunkt
- Wahrnehmung: sehen, hören (Kampflärm, Schritte), Waffe gezogen, Diebstahl, Betreten privater Räume
- Einstellung zum Spieler (freundlich / neutral / unfreundlich / feindlich) + temporäre Einstellung
- Hilfe für Kameraden (Gruppenverhalten), Warnungen vor Angriff, Flucht bei niedrigem Leben
- Bewusstlos statt tot im Faustkampf / bei menschlichen Gegnern → Plündern möglich
- Monster-KI: Revier, Rudel, Fressen/Schlafen-Routinen, Fliehen vor stärkeren Gegnern

**Dialoge & Quests**
- Dialoge als Liste von „Infos“ pro NPC mit Bedingung, Wichtig-Flag (NPC spricht an), Permanent-Flag
- Unterauswahlen (Choices), Handel, Lernen über Dialog
- Tagebuch: Aufträge (laufend/erfolgreich/gescheitert) + Notizen
- Kapitel-Struktur, globale Story-Variablen

**Kampf & Magie**
- Nahkampf mit Kombos, Ausweichen/Parieren, Talentstufen
- Fernkampf (Bogen, Armbrust) mit Zielhilfe
- Schadensarten (Klinge, Stumpf, Spitze, Feuer, Magie, …) und Rüstungsschutz pro Art
- Magie: Runen (dauerhaft) und Spruchrollen (einmalig), Kreise; Projektil-, Flächen-, Verwandlungs-,
  Kontroll- und Beschwörungszauber

**Wirtschaft**
- Handel mit Händlern (Erz als Währung), Wertverhältnisse, Tauschbildschirm
- Taschendiebstahl

**Welt & Atmosphäre**
- Tag/Nacht mit Sonne/Mond/Sternen, Himmelsfarben nach Uhrzeit, Nebel
- Regen, Gewitter (später), Wasserflächen
- Dynamische Musik nach Ort und Lage (normal / Bedrohung / Kampf, Tag/Nacht)
- Ambient-Sounds nach Zone, Lagerfeuer-/Fackellicht
- Mehrere Welten (Außenwelt, separate Dungeons) mit Übergängen

**System**
- Speichern/Laden an jeder Stelle, Schnellspeichern
- Menüs, Optionen, Untertitel, Lokalisierung (DE/EN)

## Nicht-Ziele (vorerst)

- Mehrspieler
- Kompatibilität zu Original-Gothic-Dateien (ZEN/MRM/MDS/DAT) – höchstens als optionales,
  separates Import-Werkzeug für Forschung, siehe ADR 0008
- Konsolen-Ports, VR
- Prozedurale Welt – die Welt ist handgebaut

## Name

**Gothar** ist der Projekt- und Spielname (vorher Arbeitstitel „Gothic7“). Das interne Kürzel
der Engine im Code bleibt `g7` (Namespace, Target-Präfix, Dateiformate).

## Rechtlicher Rahmen

„Gothic“ ist eine Marke von THQ Nordic; alle Original-Inhalte sind urheberrechtlich geschützt.
Das Projekt verwendet ausschließlich eigene oder frei lizenzierte Inhalte. Vor einer
Veröffentlichung sollte der Name „Gothar“ in den Markenregistern (DPMA, EUIPO) geprüft werden.
Beispiele in der Doku verwenden bewusst eigene Namen statt Gothic-Figuren und -Orten.

Hinweis: Mit **OpenGothic** (Engine-Neuimplementierung) und **ZenKit** (Dateiformat-Bibliothek)
existieren offene Projekte, die als Lernreferenz für das Verhalten von Gothic dienen können.
Code wird von dort nicht ohne Lizenzprüfung übernommen.
