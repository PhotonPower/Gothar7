-- Sinne der NPCs (M9 Teil C). Die Engine liest diese Tabelle beim Laden der Skripte; ein Npc kann mit
-- `senses = { sight = 30, angle = 120, hearing = 1.5 }` abweichen.
Perception = {
    sight = 25,          -- Meter, so weit sieht ein NPC den Spieler am Tag
    angle = 100,         -- Grad, Breite des Sichtkegels
    sneak_factor = 0.5,  -- schleicht der Spieler, sieht man ihn nur halb so weit
    night_factor = 0.6,  -- nachts (21-6 Uhr) ebenso kürzer
    near_distance = 20,  -- näher: 5 Blicke je Sekunde, weiter weg: einer
    forget_seconds = 10, -- so lange aus den Augen, dann ist er "neu" (assess_player)
    room_distance = 8,   -- so nah bemerkt ein Besitzer den Spieler in seinem Bereich auch ohne Sicht
    noise = {            -- Hörweite je Geräusch in Metern
        run = 8,
        lockpick = 6,
    },
}
