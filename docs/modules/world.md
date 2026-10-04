# world

**Zweck:** Die Spielwelt: Entities (Vobs) und Komponenten, Weltdateien, Spielzeit, Umgebung
(Tag/Nacht, Wetter), Zonen, Wegnetz-Daten, Weltwechsel.

## Szene (ADR 0005: EnTT)
### Umgesetzt (M4) – `Components.hpp`, `Scene.hpp`
```cpp
namespace g7::world {
struct VobId { u64 value; bool valid() const; bool runtime() const; };   // 0 = keine; >= kRuntimeVobIdBase = Laufzeit
struct Vob { VobId id; StringId name; };   struct WorldTransform { Mat4 matrix; };   // lokal: g7::Transform
struct VobDesc { StringId name; Transform transform; VobId parent; VobId id /*fest beim Laden*/; bool runtime; };
class Scene {   // entt::registry privat (ADR 0005)
    Result<entt::entity> spawnVob(const VobDesc&);  void destroyVob(entt::entity);   // mit allen Nachkommen
    bool valid(e); usize vobCount(); entt::entity findById(VobId); entt::entity findByName(StringId); VobId idOf(e);
    template <C> C& set(e, C); const C* get(e) const; C* get(e); bool has(e); void remove(e);
    template <C..., Fn> void each(Fn);                     // fn(entity, C&...)
    void setTransform(e, const Transform& local);  Result<void> setParent(child, parent /*null = Wurzel*/);
    entt::entity parent(e); std::vector<entt::entity> children(e); Mat4 worldMatrix(e);
    void updateTransforms();                                // nur geänderte Teilbäume
    u64 nextVobId(); Result<void> setNextVobId(u64);        // steigt nur
    u64 nextRuntimeVobId(); Result<void> setNextRuntimeVobId(u64);   // für den Spielstand (M15)
};
}
```
- **IDs:** Welt-Vobs aus `nextVobId` (ab 1), Laufzeit-Vobs aus einem eigenen Zähler ab `kRuntimeVobIdBase`. Gelöschte IDs
  kommen nie wieder. Beim Laden kann `VobDesc::id` eine feste ID setzen; doppelte IDs, Welt-IDs im Laufzeitbereich,
  feste IDs für Laufzeit-Vobs und unbekannte Eltern sind **Fehler** (`Result`, Daten). Die Zähler stehen danach
  mindestens bei max(ID) + 1; `setNextVobId`/`setNextRuntimeVobId` lehnen kleinere Werte ab.
- **Komponenten:** `Vob`, `Transform` (lokal), `WorldTransform` hat jeder Vob; andere Module setzen eigene
  Komponenten über `set`/`get`/`each`. Transformänderungen nur über `setTransform`/`set<Transform>` (setzt die
  Änderungsmarke), `get<Transform>` ist nur lesend.
- **Hierarchie** (intern `detail::Hierarchy`, verkettete Kindliste): `setParent` behält die Weltlage (neues Lokal =
  inverse(Eltern-Welt) × Welt; ohne Scherung) und lehnt Zyklen ab; `destroyVob` löscht den Teilbaum.
  `updateTransforms` berechnet `WorldTransform` ab dem obersten geänderten Vob jedes Teilbaums neu.
- `World` (geplant, unten) bündelt `Scene`, Spielzeit, Umgebung und Wegnetz.

### Geplant
```cpp
namespace g7::world {
struct VobId { u64 value; };                      // persistent, unique per world, never reused (ADR 0005); 0 = none
inline constexpr u64 kRuntimeVobIdBase = 0x8000'0000'0000'0000; // vobs spawned at runtime (saved in the save game)
struct Vob        { VobId id; StringId name; };   // every placed object
struct Transform  { Vec3 position; Quat rotation; Vec3 scale{1}; };
struct WorldTransform { Mat4 matrix; };           // computed by hierarchy system
struct Parent     { entt::entity entity; StringId boneName; }; // optional bone attachment
struct MeshRenderer { asset::Handle<render::Mesh> mesh; u32 materialOverride; };
struct PointLightC  { Vec3 color; f32 range; f32 flicker; };
struct TriggerVolume{ Shape shape; StringId onEnter, onLeave; };   // script callbacks
struct Zone       { ZoneType type /*Music, Ambient, Indoor, Owned*/; StringId value; AABB bounds; };

class World {
public:
    // The entt::registry is not part of the API (ADR 0005): upper modules use these functions.
    entt::entity spawnVob(const VobDesc&);  void destroyVob(entt::entity);
    template <class C> C& set(entt::entity, C);  template <class C> C* get(entt::entity);  template <class C> void remove(entt::entity);
    template <class... C, class Fn> void each(Fn&&);   // iterate entities with all of C...
    VobId idOf(entt::entity) const;
    entt::entity findByName(StringId) const; entt::entity findById(VobId) const;
    GameTime& time(); Environment& environment(); const Waynet& waynet() const;
    void fixedUpdate(f64 dt);
};
}
```

## Weltformat `.g7world` (JSON, vom Editor geschrieben)
**Modul `world_format`** (seit M5): Lesen, Schreiben und Prüfen von `.g7world` (`WorldFile.hpp`), der
`terrain`-Block mit Höhenkodierung (`TerrainRef.hpp`) und die Vob-Komponenten (`Components.hpp`) – ohne Rendern,
Physik und EnTT, damit Werkzeuge (g7-cook) Welten lesen, ohne die Engine zu linken; nlohmann-json bleibt dort privat.
Die Header behalten den Pfad `g7/world/` und den Namensraum `g7::world` – am Include-Pfad ist das Modul also nicht
erkennbar. Zuordnung: **`world_format`** (Ziel `g7_world_format`): `WorldFile.hpp`, `TerrainRef.hpp`, `Components.hpp`;
**`world`** (Ziel `g7_world`): alle übrigen (`Scene.hpp`, `WorldScene.hpp`, `Terrain.hpp`, `Triggers.hpp`,
`StartPoints.hpp`, `GameTime.hpp`, `DayCycle.hpp` …). In `world` bleiben die Szene
(`spawnWorld`/`captureWorld` in `WorldScene.hpp`), `Heightfield` (`Terrain.hpp`), Trigger, Startpunkte, Spielzeit
und Tag/Nacht.
**VobId-Vertrag** (ADR 0005, `docs/coordination.md`): Jeder Vob hat eine `id` (u64, ≥ 1), eindeutig in der Welt und nie
wiederverwendet; `nextVobId` ist der nächste freie Wert und steigt nur. Editor und Welt-Assembler (W3) vergeben IDs
daraus; beim Zusammenführen von Teilwelten werden IDs neu vergeben. Laufzeit-Vobs liegen ab `kRuntimeVobIdBase`.

```json
{ "version": 1, "name": "testworld", "nextVobId": 103,
  "staticMeshes": ["meshes/world/terrain.g7mesh", "meshes/world/camp.g7mesh"],
  "vobs": [
    {"id":101,"type":"mesh","name":"CAMPFIRE_01","pos":[1,0,2],"rot":[0,0,0,1],"mesh":"meshes/props/campfire.g7mesh"},
    {"id":102,"type":"light","name":"CAMPFIRE_01_LIGHT","parent":101,"pos":[0,1,0],"rot":[0,0,0,1],
     "components":{"light":{"color":[1,0.6,0.3],"range":8,"intensity":3,"flicker":0.3}}}
  ],
  "waynet": { "points": [ { "name": "WP_CAMP_ENTRANCE", "pos": [0,0,0], "dir": [0,0,1] },
                          { "name": "WP_CAMP_FIRE", "pos": [1,0,2] } ],
              "edges": [ ["WP_CAMP_ENTRANCE", "WP_CAMP_FIRE"] ],
              "freepoints": [ { "name": "FP_SIT_CAMPFIRE_01", "pos": [1,0,3], "dir": [0,0,-1] } ] },
  "zones": [ { "type": "music", "value": "CAMP", "bounds": [[-20,-5,-20],[20,10,20]] } ] }
```

**Version 1 – verbindlich (umgesetzt in `world/WorldFile.hpp`, ADR 0017):**
- Pflicht: `version` (= 1; andere Versionen werden abgelehnt). Optional: `name`, `nextVobId` (wird beim Lesen auf
  mindestens max(id) + 1 angehoben), `staticMeshes` (VFS-Pfade), `vobs`, `waynet`, `zones`.
- Vob: `id` (Pflicht, ≥ 1, eindeutig), `type` = `empty` (Gruppe, Vorgabe) | `mesh` | `light` | `start` | `sound` |
  `trigger` | `mob` (unten), `name`, `parent` (ID; Eltern dürfen
  in der Datei nach den Kindern stehen), `pos` [x,y,z] (Meter, relativ zum Elternteil), `rot` Quaternion **[x,y,z,w]**,
  `scale` [x,y,z] (Vorgabe 1). `mesh`-Vobs: `mesh` (VFS-Pfad ab Wurzel, `.g7mesh`; ein `.glb`-Pfad lädt die gekochte
  `.g7mesh`, wenn vorhanden). `light`-Vobs: `components.light` mit `color` (linear), `range` (> 0), `intensity` (Vorgabe 3),
  `flicker` (0–1, Vorgabe 0).
- Weitere Vob-Typen (M4, Erweiterung von v1 nach dem Muster `components`, Abschnitt „Vob-Typen“ unten).
- `waynet`: Wegnetz, verbindlich ab 2026-10-04 (Abschnitt „Wegnetz“ unten); seit M8 Teil A gelesen und geprüft
  (`WorldFile::waynet`, `world::WaynetData`, Fehler mit Eintrag) und sortiert, ein Eintrag je Zeile geschrieben.
  `zones` wird bis zu seinem System unverändert gelesen und zurückgeschrieben. Unbekannte Schlüssel werden ignoriert (nicht
  zurückgeschrieben).
- **Schreiben** ist stabil: Kopf-Schlüssel je eine Zeile, dann **ein Vob pro Zeile** nach `id` sortiert, Zahlen auf
  1e-5 gerundet – gleiche Welt, gleiche Bytes; Laden und Speichern ändert nichts.
- Fehler nennen Datei und Eintrag (`camp.g7world: vobs[3].pos: must be a list of 3 numbers`); fehlende Eltern,
  Elternzyklen und doppelte IDs lassen die Szene beim Laden unverändert.

```cpp
Result<WorldFile> parseWorldFile(std::string_view json, std::string_view source);  Result<WorldFile> loadWorldFile(const asset::Vfs&, path);
std::string writeWorldFile(const WorldFile&);                                      // stabil
Result<void> spawnWorld(Scene&, const WorldFile&);  WorldFile captureWorld(const Scene&, std::string_view name);
// Komponenten: MeshRef { std::string path; }, LightSource { Vec3 color; f32 range, intensity, flicker; }
```
- **Engine (Vob-Typen):** `--start=<name>` bzw. der Startpunkt mit der kleinsten id setzt die Kamera; Trigger melden
  die Kamera (Log `trigger enter TRG_… -> FUNKTION`, ab M7 Lua); Debug-Draw (F2): Startpunkte grün mit Blickpfeil,
  Trigger blau (rot, solange die Kamera darin ist), Sound-Reichweite violett, Mob-Fokuspunkt mit Definition.
  Testwelt: START_LAGER, SND_CAMPFIRE, TRG_CAMP_GATE, Mob STOOL_CAMPFIRE.
- **Engine:** `--world=<vfs-pfad>` lädt eine Welt (Mesh-Vobs werden gerendert, Licht-Vobs zu Punktlichtern, bis zum
  Gelände eine Bodenplatte unter der Welt), `--save-world=<datei>` speichert die geladene Welt oder Testszene.
  Testwelt: `assets/source/testworld/camp.g7world` (das Lager der M2-Testszene, 174 Vobs).
- Eine gekochte Binärvariante folgt bei Bedarf (große Welten); das Textformat bleibt.

## Vob-Typen (v1, M4)
Für alle Typen gilt dieselbe Transform-Regel: `pos`/`rot`/`scale` relativ zum `parent`, ohne `parent` also
Weltkoordinaten. Namen sind Bezeichner (Gothic-Konvention, GROSS_MIT_UNTERSTRICH), keine Anzeigetexte – sichtbare
Texte kommen aus Inhalt/Definitionen (lokalisiert, M14). Konvention der Engine: rechtshändig, +Y oben, **−Z vorn**.

| Typ | Komponente (`components.…`) | Bedeutung |
|---|---|---|
| `start` | – | Startpunkt; `name` Pflicht und eindeutig (ohne Groß-/Kleinschreibung), sonst Ladefehler. `pos` = Füße. Ohne Spielfigur steht die Kamera **1,62 m** darüber (`kStartEyeHeight`, gemessene Augenhöhe der Figuren; bis M5 1,7 m), volle `rot`; die Spielfigur (M5) steht mit den Füßen dort und nimmt nur das Gieren. |
| `sound` | `sound`: `sound` (Pflicht, Name einer Sound-Definition), `range` m (> 0, Vorgabe 20), `volume` 0…1 (Vorgabe 1), `mode` `loop`|`random` (Vorgabe loop), `delay` [min, max] s (random; Vorgabe [5, 15]) | Geräuschquelle (wie zCVobSound); bis zur Audio-Phase nur Daten + Debug-Draw. |
| `trigger` | `trigger`: `shape` `box` (`halfExtents` [x,y,z] > 0, Vorgabe 1) | `sphere` (`radius` > 0, Vorgabe 1), `onEnter`/`onLeave` (Skriptfunktionsnamen, optional), `filter` `player`|`npc`|`any` (Vorgabe player), `once` (Vorgabe false), `target` (**reserviert**: Vob-ID oder Name, gelesen/geschrieben, noch ohne Wirkung) | Volumen, das Betreten/Verlassen meldet. Box dreht und skaliert mit dem Vob, Kugel: Radius × größte Skalierung. |
| `mob` | `mob`: `definition` (Pflicht, Name der Mob-Definition, M8) + `mesh` wie bei `mesh` | Interaktives Objekt (Bett, Truhe, Tür). `definition` ist eine `Mob`-Instanz der Skripte (Name, Typ aus `data/mobs.toml`, Schloss, Inhalt) oder ein bloßer Typ (`chest`); Fokusname aus der Instanz. Tür-Mobs: Ursprung an der Angel, das Türblatt entlang +X (gameplay.md „Mob-Interaktion“). |
| `item` (M8 Teil B, umgesetzt) | `item`: `instance` (Pflicht, Name einer `Item`-Instanz der Skripte), `count` (1…1 000 000, Vorgabe 1, wird bei 1 nicht geschrieben) | Gegenstand, der in der Welt liegt. Kein `mesh` in der Datei: Die Engine zeichnet das `mesh` des Items bzw. einen Platzhalter seiner Kategorie, ohne Kollision (wie Gothic), und nimmt ihn in den Fokus. Aufgehoben, verschwindet der Vob; zur Laufzeit gelegte Items (`insert`, `drop_item`) haben Laufzeit-IDs und werden nicht gespeichert (Spielstand mit M14). |
| `water` (M5 Teil E, umgesetzt) | `water`: `halfExtents` [x,y,z] > 0, `kind` (**reserviert**, z. B. Fluss/Sumpf) | Wasserkörper als Box um `pos`, nur um Y gedreht; **Oberfläche = Oberkante** (pos.y + hy). Schwimmen ab Wassertiefe > Hüfthöhe (0,9 m), Tauchen mit Luftvorrat. Boxen dürfen sich überlappen (Flussabschnitte, Stufen an den Überlappungen). Darstellung der Fläche mit M17, bis dahin Debug-Draw. Ein Gefälle-Feld ist für M17 vorgemerkt. Mit welt abgestimmt. Fehler: fehlende oder nicht positive `halfExtents`, Drehung nicht nur um Y, Skalierung ≠ 1. Abfrage: `world::WaterBodies` (`Water.hpp`): `surfaceAt(punkt)` = höchste Oberfläche der Boxen, deren Grundfläche den Punkt enthält und die von unter ihm bis höchstens 0,5 m unter ihn reichen (eine Brücke hoch über dem Fluss ist trocken). |

**Kategorie (`category`, M4 Sichtbarkeit):** `mesh`-Vobs haben optional `"category": "deco" | "gameplay"`, Vorgabe
`deco` – geschrieben wird es nur bei `gameplay` (bestehende Welten bleiben bytegleich). `deco` darf ausgeblendet werden,
wenn es auf dem Bildschirm klein wird (`size_cull`), `gameplay` nie wegen der Größe (Wegweiser, Questobjekte) – für
beide gilt die Sichtweite `view_distance` (engine.toml, render.md „Sichtbarkeit“). `mob`-Vobs sind immer `gameplay`;
`"category": "deco"` an einem Mob ist ein Ladefehler. Andere Typen haben kein Feld. Mit welt abgestimmt.

Beispiel (Leonberg, Ursprung = Marktbrunnen, Gelände dort y ≈ 0):
```json
{"id":1,"type":"start","name":"START_MARKTPLATZ","pos":[0,0,8],"rot":[0,0,0,1]}
{"id":2,"type":"start","name":"START_UEBERSICHT","pos":[0,40,60],"rot":[-0.258819,0,0,0.965926]}
```
- START_MARKTPLATZ: Füße 8 m südlich des Brunnens, Kamera ohne Figur bei (0, 1.62, 8), Blick nach Norden (−Z, Identität).
- START_UEBERSICHT: Kamera ohne Figur bei (0, 41.62, 60), um 30° nach unten geneigt: Drehung um +X um −30°,
  q = (sin(−15°), 0, 0, cos(15°)) = (−0.258819, 0, 0, 0.965926).
- Auswahl: `--start=<name>` (Groß-/Kleinschreibung egal; unbekannter Name = Fehler mit Liste), sonst der Startpunkt
  mit der **kleinsten id**. Ohne Startpunkt bleibt die bisherige Übersichtskamera.

```cpp
struct StartPoint {}; struct SoundEmitter { sound, range, volume, mode, delay }; struct MobRef { definition }; struct ItemRef { instance; count };
struct TriggerVolume { shape, halfExtents, radius, onEnter, onLeave, filter, once, targetId, targetName };
Result<entt::entity> findStartPoint(const Scene&, std::string_view name = {});          // StartPoints.hpp
class TriggerSystem { void setCallback(Callback); std::vector<TriggerEvent> update(const Scene&, span<const TriggerProbe>);
                      bool isInside(VobId trigger, VobId who) const; void reset(); };  // Triggers.hpp
```
- `TriggerSystem::update` prüft Proben (Spieler/NPC mit `VobId`, Position; in M4 die Kamera) gegen alle Trigger und
  meldet Änderungen **sortiert nach Trigger-ID, dann Proben-ID, Leave vor Enter** – deterministisch für Tests und
  Skripte. Eine fehlende Probe hat alle Trigger verlassen. `once`: nur das erste Update mit Eintritt meldet (alle
  Proben darin), danach nichts mehr bis `reset()`. `world` ruft keine Skripte: die Engine registriert den Callback.

## Wegnetz – `waynet`-Block (v1, Vertrag mit welt, 2026-10-04)
Wegpunkte, Kanten und Freepoints für Pfadsuche und Tagesabläufe (Glossar „Wegnetz“, `ai.md`). welt erzeugt einen
Vorschlag aus den Straßenachsen (`leonberg-pipeline.md` W-G), der Editor bessert nach (M16); die Engine liest und
schreibt den Block mit M8 Teil A, die Pfadsuche folgt mit M9.
```json
"waynet": {
  "points":     [ { "name": "WP_LEO_MARKT_01", "pos": [12.5, 3.1, -40.0], "dir": [0, 0, 1], "owner": "worldgen" },
                  { "name": "WP_LEO_MARKT_02", "pos": [20.0, 3.0, -41.5], "owner": "worldgen" },
                  { "name": "WP_LEO_KIRCHE_TUER", "pos": [30.2, 4.0, -38.0] } ],
  "edges":      [ ["WP_LEO_MARKT_01", "WP_LEO_MARKT_02", "worldgen"], ["WP_LEO_MARKT_02", "WP_LEO_KIRCHE_TUER"] ],
  "freepoints": [ { "name": "FP_SIT_LEO_BRUNNEN_01", "pos": [1.2, 3.0, 0.8], "dir": [0, 0, -1], "owner": "worldgen" } ]
}
```
- **Punkte** (`points`) und **Freepoints** (`freepoints`): `name` (Pflicht), `pos` (Pflicht, Meter, Weltkoordinaten;
  `y` = Bodenhöhe, also die Füße), `dir` (optional, waagrechte Blickrichtung für dort Stehende/Sitzende; wird
  normiert), `owner` (optional, siehe unten).
- **Namen:** Großbuchstaben, Ziffern und `_`; Punkte beginnen mit `WP_`, Freepoints mit `FP_`. Eindeutig über Punkte
  und Freepoints einer Welt. Bei Freepoints ist das zweite Glied der **Typ**: `FP_SIT_`, `FP_STAND_`,
  `FP_SMALLTALK_`, `FP_ROAM_`, `FP_SLEEP_`, `FP_CAMPFIRE_`; andere Typen sind erlaubt und werden bis zu ihrer
  Verwendung ignoriert.
- **Kanten** (`edges`): ungerichtet, `[name_a, name_b]` bzw. `[name_a, name_b, "worldgen"]`; beide Namen müssen
  Punkte (`WP_`) sein. Eine Kante auf einen fehlenden Punkt, doppelte Namen oder ungültige Namen sind Ladefehler mit
  Datei und Eintrag (`leonberg.g7world: waynet.edges[12]: unknown point "WP_X"`); doppelte Kanten werden
  zusammengefasst. Freepoints haben keine Kanten – die KI geht zum nächsten erreichbaren Punkt und von dort hin.
- **Eigentum** wie bei Vobs (`owner`): `"worldgen"` an einem Punkt, Freepoint oder als drittes Kantenelement heißt,
  der Generator ersetzt ihn bei jedem Lauf; ohne `owner` ist er von Hand bzw. im Editor gesetzt und bleibt.
  Verschiebt der Editor einen erzeugten Punkt, entfällt `owner` (er gehört dann dem Menschen). Der Generator
  vergibt keine Namen, die schon ein Hand-Punkt trägt, und lässt Kanten ohne `owner` stehen. Erzeugte Kanten dürfen
  an beliebigen Punkten enden (auch an Hand-Punkten); ein Punkt ohne `owner` mit einem Namen, den der Generator
  vergeben würde, gilt für ihn als vorhanden – er verbindet ihn, legt ihn aber nicht neu an.
- **Erzeugte Punkte dauerhaft entfernen** geht über die Annotationen des Generators (welt:
  `tools/worldgen/data/<ort>/waynet.json` mit `remove`/`add`); ein Löschen im Editor allein hält nur bis zum nächsten
  Generatorlauf. Der Editor (M16) weist darauf hin, statt es anders zu lösen.
- **Namen aus Ortsdaten** (welt): Umlaute umgeschrieben (Ä → AE, ß → SS); Türpunkte ohne Hausnummer tragen das
  Kürzel der LoD2-Gebäude-ID. Leonberg: etwa 1500–3000 Punkte.
- **Empfehlungen** (die Engine warnt mit M9, lehnt aber nicht ab): Abstand verbundener Punkte 5–20 m; die Gerade
  zwischen ihnen ist in Hüfthöhe frei (kein Haus, keine Mauer); Punkte stehen auf begehbarem Boden; vor jeder
  benutzbaren Haustür ein Punkt (welt: `WP_LEO_<STRASSE>_<NR>`), Freepoints auf Plätzen und an Bänken/Feuern.
- **Schreiben** ist stabil: je Punkt, Freepoint und Kante eine Zeile, Punkte und Freepoints nach Name, Kanten nach
  ihren Namen sortiert (die kleinere zuerst); Zahlen auf 1e-5 gerundet.
- Vorher stand hier eine Skizze mit Kanten per Index; sie wurde nie benutzt (das Spiel las den Block nicht).

## Generator-Kopf (`generator`, optional, M4)
```json
"generator": {"tool": "gothar-worldgen", "owned": [3, 4, [10, 935]]}
```
- Schreibt der Welt-Assembler (welt) in den Kopf (nach `nextVobId`): `owned` = die Vob-IDs, die er bei jedem Lauf
  neu schreibt (einzelne IDs oder geschlossene Bereiche [von, bis], aufsteigend); gelockte Gebäude, Startpunkte und
  reservierte IDs nicht. Von welt gegengelesen.
- **Nur ein Hinweis für den Editor** (`isGenerated`): er warnt beim Bearbeiten solcher Vobs. Die Engine schreibt den
  Kopf unverändert zurück; ein veraltetes oder kaputtes `owned` ist **nie** ein Ladefehler (Warnung, dann ohne
  Wirkung). Der Assembler entscheidet weiter selbst (vob_ids.json).

## Weltwechsel (M4)
**Levelwechsel-Trigger** (Vertrag, von welt gegengelesen): `trigger`-Vobs mit `components.trigger.changeWorld =
{"world": "<VFS-Pfad .g7world>", "start": "<Startpunkt-Name der Zielwelt>"}` (beide Pflicht). Wirkt nur auf den Spieler
(`filter` muss `player` sein, sonst Ladefehler); `onEnter`/`onLeave` bleiben daneben möglich.
```json
{"id":7,"type":"trigger","name":"TRG_TO_RATHAUS","pos":[12,0,-3],"components":{"trigger":{"shape":"box",
 "halfExtents":[1,1.5,0.3],"changeWorld":{"world":"worlds/leonberg/rathaus.g7world","start":"START_RATHAUS_EINGANG"}}}}
```
- `Engine::requestWorldChange(world, start)` (auch vom Trigger): ausgeführt **zwischen zwei Frames** – nach der
  Simulation, vor dem Rendern. Zielwelt und Startpunkt werden erst dann geprüft; fehlt eines: Warnung, man bleibt.
- Ablauf: Zustand der aktuellen Welt sichern → entladen (Szene, Instanzen, Raster, Gelände, Lichter, Trigger) →
  Zielwelt laden (aus dem gesicherten Zustand, wenn schon besucht, sonst aus der Datei) → Kamera (später Spieler) auf
  den Startpunkt → nicht mehr genutzte Modelle freigeben.
- **Rückkehr:** Die verlassene Welt ist so, wie man sie verlassen hat (verschobene/entfernte Vobs, verbrauchte
  `once`-Trigger) – in M4 im Speicher, später im Spielstand (save.md „Schnittstelle zum Weltwechsel“).
- **Kein Pingpong:** Nach der Ankunft gelten Trigger, in denen der Spieler schon steht, als betreten ohne Ereignis
  (`TriggerSystem::prime`); sie lösen erst nach Verlassen und Wiederbetreten aus. Liegt ein Startpunkt in einem
  Levelwechsel, warnt die Engine beim Laden. Startpunkte daher knapp hinter den Rück-Trigger setzen.
- **Global, nicht Teil einer Welt:** Spielzeit und Tag/Nacht (M4), später Story-Variablen und der Spieler selbst –
  sie laufen über Weltwechsel weiter.
- Testwelten: `testworld/camp.g7world` (TRG_TO_CAVE → START_HOEHLE, Ankunft START_LAGER_HOEHLE) und
  `testworld/cave.g7world` (Felsenkessel, TRG_TO_CAMP). Ladezeit wird geloggt; ein Ladebildschirm kommt mit der UI.

## Gelände – `terrain`-Block (v1.x, Vertrag mit welt)
Optional in `.g7world`; fehlt er, hat die Welt kein Gelände (v1 bleibt gültig). Mit welt abgestimmt (passt zu
`tools/worldgen` `terrain.json`).
```json
"terrain": {"version":1,"heightmap":"worlds/leonberg/terrain.r16","width":2000,"height":2000,"cellSize":1.0,
            "firstSample":[-999.5,-999.5],"minY":-50.991,"maxY":94.85}
```
- `version` (Pflicht, = 1), `heightmap`: VFS-Pfad einer **rohen `.r16`**: `width·height` Werte **uint16 Little Endian**,
  zeilenweise. `width`/`height` ≥ 2, `cellSize` > 0 (Meter zwischen Sample-Mitten), `firstSample` [x, z] der Mitte von
  Spalte 0 / Zeile 0, `maxY` > `minY`. Optional: `splat`, `holes` (unten).
- **Lage:** Sample (Spalte c, Zeile r) liegt bei x = firstSample.x + c·cellSize, z = firstSample.z + r·cellSize.
  **Zeile 0 = kleinstes z (Norden, −Z), Spalte 0 = kleinstes x (Westen, −X)**, Y oben. Die Fläche reicht von Sample-Mitte
  zu Sample-Mitte; dazwischen bilinear, außerhalb gilt die Randhöhe.
  Beispiel (2000×2000, 1 m, firstSample −999.5): (c 0, r 0) → (−999.5, −999.5); (1999, 0) → (999.5, −999.5);
  (0, 1999) → (−999.5, 999.5); (1000, 500) → (0.5, −499.5).
- **Höhe:** y = minY + v / 65535 · (maxY − minY). Kodierung v = round((y − minY) / (maxY − minY) · 65535)
  (Halbwerte beliebig gerundet), auf 0 … 65535 begrenzt.
  Beispiel (minY −50.991, maxY 94.85, Auflösung 2,2254 mm): y 0.0 → v 22913 → y −0.000616; y 21.9 → v 32754 →
  y 21.899457; minY → 0; maxY → 65535.
### Splat-Schichten (`splat`, optional, M4 Teil B)
```json
"splat": {"maps": ["worlds/leonberg/generated/splat0.png"],
          "layers": [{"name": "Wiese", "albedo": "worlds/leonberg/wiese.png", "tile": 4.0},
                     {"name": "Fels", "albedo": "worlds/leonberg/fels.png", "tile": 6.0, "normal": "…"}]}
```
- `layers`: 1 … 8 Schichten; `name` und `albedo` (VFS-Pfad, sRGB-Farbe) Pflicht, `tile` = Meter je Texturwiederholung
  (> 0, Vorgabe 4), `normal` (VFS-Pfad) **reserviert**: gelesen und geschrieben, noch nicht gezeichnet. Alle Albedos
  müssen gleich groß und gleich formatiert sein (sonst Ladewarnung mit Meldung, dann Neigungsfärbung).
- `maps`: genau ⌈Schichten / 4⌉ (1 oder 2) RGBA-Bilder, **lineare Daten**: Kanal k von Karte m = Gewicht der Schicht
  4m + k (Alpha ist ein Gewicht, keine Transparenz). Je Punkt auf Summe 1 normiert; Summe 0 → Schicht 0. Alle Karten
  gleich groß (W×H, beliebig).
- **Lage:** Pixel-Mitten liegen auf Samples – Pixel (0, 0) auf Sample (0, 0), Pixel (W−1, H−1) auf dem letzten Sample;
  Zeile 0 = Norden wie die Heightmap; dazwischen bilinear. Pixel (i, j) liegt bei
  x = firstSample.x + i·(width−1)·cellSize / (W−1), z = firstSample.z + j·(height−1)·cellSize / (H−1).
  Beispiel Leonberg (2000×2000, 1 m, firstSample −999.5) mit 1000×1000 Splat: Pixel (0, 0) → (−999.5, −999.5);
  (999, 999) → (999.5, 999.5); (500, 0) → x = −999.5 + 500·1999 / 999 = 1.0005. Bei Heightmap-Auflösung
  (2000×2000) fällt jedes Pixel genau auf ein Sample; da 1999 prim ist, ergibt jede andere Auflösung einen krummen
  Pixelabstand (z. B. 2,001 m bei 1000 px) – mit der Formel unproblematisch.

### Löcher (`holes`, optional)
- `"holes": "worlds/leonberg/generated/holes.r8"` – rohe 8-Bit-Maske **je Zelle** (nicht je Sample):
  (width−1)·(height−1) Bytes, zeilenweise, Zeile 0 = Norden. Zelle (c, r) ist das Quadrat zwischen Sample (c, r) und
  (c+1, r+1): x ∈ [firstSample.x + c·cellSize, firstSample.x + (c+1)·cellSize], z entsprechend. **0 = Loch** (Zelle
  fehlt, auch im Schatten und später für Kollision), sonst Boden (worldgen schreibt 255).
  Beispiel Leonberg: 1999·1999 = 3 996 001 Bytes; Zelle (0, 0) = x −999.5 … −998.5, z −999.5 … −998.5; Byte-Index von
  Zelle (c, r) = r·1999 + c; Zelle (1000, 500) = x 0.5 … 1.5, z −499.5 … −498.5.
- Falsche Länge ist ein Ladefehler mit Dateiname; eine Maske nur aus Nullen wird geladen, aber gewarnt (sie entfernt
  das ganze Gelände).

- `world::Heightfield` (`Terrain.hpp`): `load(vfs, ref)` (Höhen + Löcher), `heightAt(x, z)`, `normalAt(x, z)`,
  `sampleHeight(c, r)`, `isHole(x, z)` (letzte Sample-Linie gehört zur letzten Zelle; außerhalb false), `holes()`,
  `bounds()`, `renderDesc()`; `encodeHeight`/`decodeHeight`. Gerendert von `render::TerrainRenderer` (render.md); die
  Engine lädt Splat-Karten und Albedos über den AssetManager (gekocht als `.ktx2`, siehe 06-asset-pipeline.md).
- Testwelt: `assets/source/testworld/` – `terrain.r16` (257×257, 2 m, −20 … 60 m), `splat0.png` (129×129: Gras,
  Erde, Fels, Weg), `layer_*.png` (128², prozedural), `holes.r8` (Grube östlich des Lagers); alles aus
  `make_terrain.py`.
- Generierte Gelände-Daten der Welt-Spur liegen unversioniert unter `assets/source/worlds/<ort>/generated/`
  (im Dev-Build gemountet, von g7-cook mitgekocht).

## Spielzeit & Umgebung (M4, umgesetzt)
```cpp
class GameTime { advance(f64 seconds); setTime(day, hour, minute); advanceTo(hour, minute);  // GameTime.hpp
                 day(); minuteOfDay(); hourOfDay(); totalMinutes(); clock(); restore(clock);
                 setCallback(fn(const TimeEvent&)); setSecondsPerMinute(f64); };
struct TimeEvent { Kind Minute|Jumped; u64 from, to; };                      // Minuten seit Tag 0, 00:00
class DayCycle { static parse/load/fallback; DaySample evaluate(f32 hour, f32 fogDensity) const; };  // DayCycle.hpp
struct Orbit { sunrise, sunset, noonElevation, moonElevation; sunDirection(hour); moonDirection(hour); };
```
- **`GameTime`** ist **global** (die Engine hält sie, nicht die Welt): läuft im festen Zeitschritt und über
  Weltwechsel weiter. `[time] minute_seconds` (Vorgabe 4 → 24 h ≈ 96 Min), `[time] start` bzw. `--time=HH:MM`
  (Vorgabe 08:00).
- **Ereignisse, deterministisch:** je Spielminute ein `Minute`-Ereignis in aufsteigender Reihenfolge. **Sprünge**
  (`advanceTo` – Schlafen –, `setTime`, Laden oder mehr als 60 Minuten in einem Schritt, z. B. bei hohem Zeitraffer)
  sind **ein** `Jumped`-Ereignis {from, to} statt vieler Minuten: Zuhörer (Tagesabläufe, M9) setzen dann alles direkt
  in den Zustand zur neuen Uhrzeit, wie Gothic NPCs nach dem Schlafen in ihre Routine setzt.
- **`DayCycle`** liest die Kurven aus **`data/environment.toml`** (VFS, Inhalt: `assets/source/data/`, `[time]
  environment`): `[orbit]` (Sonnenauf-/-untergang, Mittags- und Mondhöhe) und `[[key]]` je Uhrzeit mit Sonnen- und
  Mondlicht, Ambient Himmel/Boden, Nebelfarbe (zugleich Horizont des Himmels) und Nebeldichte-Faktor, Zenitfarbe,
  Sternen. Farben in **sRGB 0..1**, intern linear; zwischen den Schlüsseln zyklisch und weich (smoothstep).
  Fehler nennen Datei und Schlüssel (`environment.toml: key[2].hour: …`). Fehlt die Datei: Warnung, feste
  Abenddämmerung wie vor M4.
- Sonne: Aufgang im Osten (+X), Mittag im Süden (+Z), Untergang im Westen; der Mond gegenüber. **Ein**
  Richtungslicht: tagsüber die Sonne, nachts der Mond (schwach, kühl, mit Schatten); am Horizont blenden beide aus.
- **Reserviert (M17):** `[zone]` für Überschreibungen je Zone bzw. Welt (Höhle, Sumpf; Farbkorrektur) – noch ohne Wirkung.
- Werte sind Spielgefühl und Vorgaben bis zur Abnahme durch den Projektinhaber (Screenshots zu 6 Uhrzeiten,
  Lager und Leonberg: `C:\GotharData\review\m4-daynight\`, nicht im Repo).
- Wetter (M17): Regenintensität als Zustandsautomat mit Übergängen.

## Gothic-Bezug
- Vob-Baum ≈ ZenGin `zCVob`-Hierarchie. Mobs, Trigger, Lichter, Startpunkte sind Vob-Typen.
- Weltwechsel: siehe Abschnitt „Weltwechsel“ (umgesetzt in M4); Zustand der verlassenen Welt bleibt (save.md).
