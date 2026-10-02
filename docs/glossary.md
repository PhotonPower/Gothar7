# Glossar

| Begriff | Bedeutung in Gothar |
|---|---|
| **Vob** | „Virtual Object“ – jedes platzierte Objekt in der Welt (Entity mit Transform). In Gothar eine EnTT-Entity mit `VobComponent`. |
| **Mob** | Interaktives Vob (Truhe, Bett, Amboss, Tür, Hebel). Hat Interaktionszustände und Slots für die Benutzer-Position. |
| **NPC** | Nicht-Spieler-Charakter inkl. Monster. Der Spieler ist technisch ebenfalls ein NPC mit Eingabe statt KI. |
| **Instanz** | In Skripten definierte Vorlage (Item, NPC, Info …), aus der zur Laufzeit Objekte erzeugt werden. |
| **Wegnetz (Waynet)** | Graph aus **Wegpunkten** (WP) und Kanten für Pfadsuche; dazu **Freepoints** (FP) als lose Aufenthaltsorte (z. B. „FP_SIT_CAMPFIRE“). |
| **Routine / Tagesablauf (TA)** | Liste von Zeitfenstern pro NPC: `von–bis → Zustand + Wegpunkt`. |
| **Zustand (ZS)** | KI-Zustand mit `begin`/`loop`/`end`-Funktion in Skripten (z. B. `ZS_Sleep`, `ZS_Smalltalk`). |
| **Wahrnehmung (Perception)** | Ereignisse, die ein NPC sieht oder hört (Spieler gesehen, Kampflärm, Waffe gezogen, Diebstahl …), auf die Skripte reagieren. |
| **Einstellung (Attitude)** | Haltung eines NPC zum Spieler: freundlich, neutral, unfreundlich, feindlich (dauerhaft + temporär). |
| **Gilde** | Fraktionszugehörigkeit; Gilden-Tabelle definiert Einstellungen zwischen Gilden. |
| **Info** | Ein Dialogeintrag eines NPCs: Bedingung, Text in der Auswahl, Ausführungsfunktion, Flags (wichtig, permanent). |
| **Fokus** | Aktuell anvisiertes Objekt (NPC/Item/Mob) des Spielers. |
| **Talent** | Lernbare Fähigkeit mit Stufen (Einhand 1/2, Schlösser öffnen …). |
| **Lernpunkte (LP)** | Währung zum Lernen bei Lehrern, gewonnen durch Stufenaufstieg. |
| **Spielzeit** | Simulierte Uhrzeit (Standard: 1 Spielstunde ≈ 5–6 Echtminuten, konfigurierbar). |
| **Kapitel** | Story-Abschnitt, schaltet Weltzustände und NPC-Routinen um. |
| **Musik-Zone** | Bereich mit Musik-Thema; Zustände Standard/Bedrohung/Kampf × Tag/Nacht. |
| **.g7pak** | Archivformat der Engine für gekochte Assets. |
| **Cooking** | Umwandlung von Quell-Assets (glTF, PNG …) in Laufzeitformate. |
