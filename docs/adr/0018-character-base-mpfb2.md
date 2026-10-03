# 0018 – Ausgangskörper für Figuren: MPFB2 (MakeHuman Plugin for Blender)

- **Status:** Akzeptiert (2026-10-03, Entscheidung Projektinhaber)
- **Datum:** 2026-10-03
- **Phase:** F3

## Kontext
F3 braucht Grundkörper (2 Geschlechter × 3 Staturen), separate Köpfe mit Morph-Targets (Viseme, Blinzeln,
Ausdrücke; `characters-pipeline.md` §6) und eine Basis für Kleidung. Alles muss auf dem Referenz-Rig sitzen
(`docs/modules/animation.md`; Geometrie seit F1 aus dem Quaternius-Rig, T-Pose, 1,83 m) und stilisiert
nachbearbeitet werden. Randbedingungen:
- **Das Repo ist öffentlich.** Alles, was eingecheckt wird, muss frei weitergebbar sein (CC0 oder eigene Arbeit).
- Kein Code mit Copyleft-Lizenz im Repo (Repo-Lizenz MIT).
- Keine Inhalte aus Gothic (ADR 0008).
- Gestaltung (Stilisierung, Proportionen, Kleidungslinien) entscheidet der Projektinhaber; diese ADR betrifft
  nur das **Werkzeug und die Rechtslage**.

### Lizenzlage MPFB2 (belegt)
- MakeHuman-Community, „License“ (static.makehumancommunity.org/about/license.html):
  „The source code of MPFB is shared under GPL and the source code of MakeHuman is shared under AGPL.“ –
  „All core assets are shared under Creative Commons, CC0. The effective consequence of this is that you are
  free to do as you see fit with the asset or derivates of the assets.“
- Für ältere MakeHuman-Exporte gibt es zusätzlich die „MakeHuman License Exception“ (scancode-licensedb:
  `make-human-exception`): „As a special and limited exception, the copyright holders of the MakeHuman assets
  grants the option to use CC0 1.0 Universal“, wenn „the assets were bundled in an export that was made using
  the file export functionality inside an OFFICIAL and UNMODIFIED version of MakeHuman“. Für MPFB2 mit den
  CC0-Core-Assets ist diese Ausnahme nicht nötig, sie zeigt aber die Absicht der Rechteinhaber.
- **Folgerung:** Ein mit MPFB2 aus **Core-/System-Assets** erzeugter und von uns nachbearbeiteter Körper ist CC0
  bzw. unsere eigene Arbeit und darf ins öffentliche Repo. **Community-Asset-Pakete** (Kleidung, Haare, Haut)
  haben teils eigene Lizenzen (z. B. CC-BY) und werden nur nach Einzelprüfung verwendet und in
  `assets/LICENSES.md` eingetragen.
- Das Plugin selbst (GPLv3) wird **nur lokal in Blender installiert**. Kein MPFB2-Code wird ins Repo kopiert,
  vendort oder in unsere Werkzeuge eingebettet. `tools/chargen` ruft MPFB2 höchstens über dessen
  Blender-Operatoren in einer lokalen Blender-Installation auf (wie Blender selbst); der Aufruf ist optional und
  nicht Teil von CI.

## Optionen
1. **MPFB2 (MakeHuman Plugin for Blender)**
   - Vorteile: parametrische Körper (Geschlecht, Alter, Muskeln, Gewicht, Proportionen) → Staturen und viele
     Gesichter aus einem Werkzeug; saubere, animationstaugliche Topologie; Core-Assets CC0; läuft direkt in
     Blender 4.5 (unser Hauptwerkzeug); große Auswahl an Augen-, Zahn- und Zungen-Meshes.
   - Nachteile: realistischer Grundstil, braucht stilisierte Nacharbeit (§1 „Stil vor Realismus“); eigenes
     MakeHuman-Skelett → Übertragung auf das Referenz-Rig nötig (Gewichte neu bzw. per Datentransfer, Proportionen
     an die Referenz angleichen); Gesichts-Morph-Targets in unserem Namensschema müssen wir selbst erzeugen;
     Werkzeug GPL (nur lokal, s. o.).
2. **Quaternius „Universal Base Characters“ (+ „Modular Outfits“)** (CC0)
   - Vorteile: CC0; **bereits auf demselben Rig** wie unsere Referenz (Quaternius-Rig) → kein Umriggen;
     spieltaugliche Low-Poly-Topologie (~13 k Dreiecke); 6 Körper (m/w × 3 Proportionen), 20 Frisuren; zu den
     F2-Animationen passend.
   - Nachteile: fester, recht „glatter“ Stil, wenig Spielraum für eigenständige Gothic-artige Gesichter; Kopf
     nicht als separates Mesh mit Visemen/Ausdrücken; Download auf itch.io nur von Hand (Cloudflare,
     „Preis frei wählbar“); weniger Varianz.
3. **Komplett eigene Modelle** (Sculpting/Box-Modeling in Blender, eigene Basis-Meshes)
   - Vorteile: volle Kontrolle über Stil und Topologie; keine Fremdlizenzen.
   - Nachteile: sehr hoher Zeitaufwand, Fachkönnen für Gesichter/Anatomie nötig; bremst F3 und den Vertical Slice
     (M10) erheblich.
4. **Kombination:** Quaternius-Körper/-Kleidung als schnelle Basis auf dem fertigen Rig, MPFB2 für Köpfe und
   Varianz (Gesichter, Staturen), beides stilisiert nachbearbeitet.
   - Vorteile: schnell spielbar (Option 2) und trotzdem individuelle Köpfe (Option 1).
   - Nachteile: zwei Quellen mit unterschiedlicher Topologie/Stilistik → Anpassungsaufwand an Hals und Proportionen.

## Entscheidung
Option 1, **MPFB2 als Werkzeug für Ausgangskörper und Köpfe**, ausschließlich mit Core-/System-
Assets (CC0), Plugin nur lokal installiert, kein GPL-Code im Repo. Quaternius „Universal Base Characters“
(Option 2) bleibt die Rückfallebene, falls Stilisierung und Umriggen der MPFB2-Körper zu aufwendig werden;
die Wahl darf in F3b nach einem ersten Testkörper auf Option 4 wechseln.

Angenommen vom Projektinhaber am 2026-10-03 mit diesen Bedingungen. Stilisierung und Proportionen (F3b) bleiben seine Entscheidungen.

## Konsequenzen
- **Lizenzen:** Jede MPFB2-Ableitung bekommt einen Eintrag in `assets/LICENSES.md` (Quelle, verwendete Asset-Pakete,
  CC0). Community-Pakete nur nach Einzelprüfung.
- **Werkzeuge (F3):** `tools/chargen` bekommt Schritte, um einen MPFB2-Körper auf das Referenz-Rig zu bringen
  (Proportionen angleichen, Gewichte übertragen, Morph-Targets nach §6 benennen bzw. erzeugen) – als Blender-Skripte
  in unserem Code, die MPFB2-Ergebnisse (Meshes) verarbeiten, nicht MPFB2-Code enthalten.
- **CI:** unverändert; MPFB2 wird nie in CI benötigt (geprüft werden nur die erzeugten `.glb`).
- **Einrichtung:** Installation des Plugins (Blender-Extension) und Pfad in `docs/05-build.md`, sobald angenommen.
- **Offen für F3b:** Stilisierungsgrad, Proportionen, Gesichter – Gestaltungsentscheidungen des Projektinhabers
  (Stil-Referenzblatt mit W5).
