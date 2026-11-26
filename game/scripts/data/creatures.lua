-- Tiere (M9 Teil D): wie sie leben und auf den Spieler und andere Tiere reagieren. Entscheidungen des
-- Projektinhabers (2026-10-04): Wolf und Keiler drohen ab etwa 12 m einige Sekunden, dann greifen sie an (bis zum
-- Kampf in M11: verfolgen); der Laufvogel ist neutral und greift erst unter 4 m an. Wolfsrudel 2-4 mit Anführer.
-- Wölfe sind nachts unterwegs und schlafen tags, Keiler und Laufvogel umgekehrt; zwischendurch fressen sie.
-- body: der Körper als Trefferziel (M11 Waffen-Sweep, Annäherung der Kampf-KI): eine Kapsel, liegend entlang des
-- Körpers oder stehend; Länge von Ende zu Ende, offset = Mitte ab den Füßen (rechts, oben, vorn). Werte aus figurens
-- [rig.collision] (characters-pipeline.md §7.1).
Creatures = {
    wolf = {
        territory = 25,         -- m um den Wegpunkt des Tagesablaufs
        threaten_distance = 12, -- näher: drohen (Knurren)
        threaten_seconds = 3,   -- so lange, dann Angriff
        eat_chance = 0.25,      -- je Streifzug: erst fressen
        prey = { laufvogel = true },
        pack_gait = "trot",  -- Rudel folgt dem Anführer im Trab (figuren #200: Trab 3 m/s)
        chase_gait = "trot", -- verfolgen: nah im Trab, ab 8 m rennend (Gangarten: Gehen 1,2 / Trab 3 / Rennen 6)
        body = { shape = "lying", radius = 0.32, length = 1.22, offset = { 0, 0.48, 0.48 } },
    },
    keiler = {
        territory = 20,
        threaten_distance = 12, -- Scharren
        threaten_seconds = 3,
        eat_chance = 0.4,
        body = { shape = "lying", radius = 0.42, length = 1.74, offset = { 0, 0.42, 0.18 } },
    },
    laufvogel = {
        territory = 20,
        attack_distance = 4, -- neutral, erst so nah greift er an
        eat_chance = 0.4,
        predators = { wolf = true },
        flee_distance = 12, -- vor Räubern
        body = { shape = "upright", radius = 0.24, length = 1.66, offset = { 0, 0.83, 0.37 } },
    },
    -- Dungeon (D6, docs/design/monsters.md). Ruhende Monster (zs_dormant) bemerken den Helden mit halber
    -- Reichweite; wake_seconds: so lange dauert das Erwachen (t_wake), bevor sie handeln.
    gatterbock = { -- Steinwächter neben einem Gatter: steht als Statue, langsam und wuchtig
        territory = 6,        -- langsam (0,9 / 2,2 m/s): man kann ihm davonlaufen
        attack_distance = 25, -- erwacht: greift an, was er sieht
        wake_seconds = 1.6,
        body = { shape = "upright", radius = 0.28, length = 2.5, offset = { 0, 1.25, 0.01 } },
    },
    mauerschlupfer = { -- liegt in einer Mauernische, stiehlt Glänzendes (Gegenstände mit tags = { "shiny" })
        territory = 10,
        attack_distance = 25,
        wake_seconds = 0.8,
        steals = true, -- erwacht: schnappt sich etwas Glänzendes und flieht; ohne Beute greift er an
        flee_seconds = 10,
        body = { shape = "lying", radius = 0.25, length = 0.8, offset = { 0, 0.49, 0.36 } },
    },
    mauerkriecher = { -- sitzt in Ritzen, kommt bei Licht und Lärm heraus (t_emerge), greift im Rudel an
        territory = 12,
        attack_distance = 25,
        wake_seconds = 0.9,
        pack_gait = "run", -- schnell (1,2 / 5,0 m/s): das Rudel folgt dem Anführer laufend
        chase_gait = "run",
        body = { shape = "lying", radius = 0.21, length = 0.99, offset = { 0, 0.16, -0.02 } },
        -- Ein schwerer Treffer (ab 25 % des Lebens) rollt ihn ein: 4 s still, der Panzer lässt nur ein Viertel durch;
        -- von vorn (bis 45° neben der Blickrichtung) trifft man den weichen Bauch voll (Projektinhaber 2026-11-24).
        curl = { share = 0.25, seconds = 4.0, damage_factor = 0.25, belly_degrees = 45 },
    },
    kristallkrebs = { -- liegt zwischen den Kristallen der Grotte, läuft seitwärts, hält fest, wirft Zauber zurück
        territory = 8,
        attack_distance = 20,
        wake_seconds = 1.2,
        chase_gait = "run", -- seitwärts 0,8 / 2,5 m/s
        move_axis = "x",
        body = { shape = "lying", radius = 0.4, length = 1.0, axis = "x", offset = { 0, 0.44, 0 } },
        -- Ein Angriff von dreien ist ein Griff: die Schere hält den Helden bis zum Event release (höchstens 2,5 s),
        -- wenn er beim Event grab bis 2,2 m vor ihm steht.
        grab = { chance = 0.33, seconds = 2.5, reach = 2.2 },
        reflect = { share = 0.5 }, -- die Hälfte eines Zaubers des Helden geht an ihn zurück (Event reflect)
        splinter = { item = "it_kristall", max = 3 }, -- stumpfe Treffer schlagen Kristalle ab (Beute)
    },
    kellermutter = { -- Boss: Stammmutter der Kriecher über dem Hort, drei Phasen (ai/kellermutter.lua)
        territory = 15,
        attack_distance = 25,
        wake_seconds = 2.0,
        chase_gait = "run", -- 0,6 / 1,5 m/s
        body = { shape = "lying", radius = 0.9, length = 5.0, offset = { 0, 1.0, 0.4 } },
        -- Phasen nach dem Anteil des Lebens: unter rage tobt sie (t_rage) und ruft alle call_seconds die Brut
        -- (t_call, Event spawn_brood: count Mauerkriecher, höchstens max zugleich); unter guard schleppt sie sich
        -- zum Hort und schützt ihn (s_guard), die Schwachstelle am Brutsack ist offen.
        phases = { rage = 2 / 3, guard = 1 / 3 },
        brood = { npc = "mon_mauerkriecher", count = 3, max = 6, call_seconds = 15 },
        -- Offene Schwachstelle (Brutsack hinten): Treffer von hinten (bis 60° neben der Gegenrichtung) zählen
        -- doppelt; ab stagger_share des Lebens taumelt sie (t_stagger).
        weak = { degrees = 60, factor = 2.0, stagger_share = 0.05 },
    },
    beinhauswart = { -- wartet an einer Gruft (zs_dormant), blendet mit der grünen Laterne, schlägt mit dem Spaten
        territory = 12,
        attack_distance = 20,
        wake_seconds = 1.4,
        threaten_distance = 8, -- nach dem Erwachen und alle flash.every Sekunden: Laterne entgegenstoßen
        threaten_seconds = 1.2,
        chase_gait = "run", -- 0,8 / 3,2 m/s: langsamer als der rennende Held
        body = { shape = "upright", radius = 0.3, length = 2.0, offset = { 0, 1.0, 0 } },
        -- Event flash von t_threaten: Wer bis distance m vor ihm steht und zu ihm schaut (bis degrees neben der
        -- Blickrichtung), sieht seconds lang grünweiß und taumelt. Nur mit der Laterne in der Hand; bei
        -- lantern_drop des Lebens lässt er sie fallen (Rasend-Phase: danach nur noch der Spaten).
        flash = { distance = 7, degrees = 60, seconds = 1.5, colour = { 0.75, 1.0, 0.8 }, every = 10 },
        lantern = "it_lantern_beinhaus",
        lantern_drop = 0.5,
    },
}

--- Die Werte des Tieres aus Creatures; nil für Menschen (Npc ohne `species`).
function animal(npc)
    local n = instance("Npc", npc)
    return n and n.species and Creatures[n.species] or nil
end
