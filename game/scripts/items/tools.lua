-- Werkzeug (Beispielinhalt M8).
Item "it_lockpick" {
    name = "Dietrich",
    mesh = "items/it_lockpick.glb",
    category = "misc",
    value = 10,
}

Item "it_key_hut_door" {
    name = "Hüttenschlüssel",
    mesh = "items/it_key.glb",
    category = "key",
    value = 0,
}

-- Ein Schlüssel ohne bestimmtes Schloss (Inhalte, Händler); jedes Schloss hat sonst seinen eigenen Schlüssel.
Item "it_key" {
    name = "Schlüssel",
    mesh = "items/it_key.glb",
    category = "key",
    value = 0,
}

-- Gegenstände der Tagesabläufe (M9): Die NPCs nehmen sie bei der Animation in die Hand (npc_play).
Item "it_broom" {
    name = "Besen",
    mesh = "items/it_broom.glb",
    category = "misc",
    value = 2,
}

Item "it_mug" {
    name = "Krug",
    mesh = "items/it_mug.glb",
    category = "misc",
    value = 3,
}

Item "it_axe" {
    name = "Axt",
    mesh = "items/it_axe.glb",
    category = "misc",
    value = 15,
}

-- Die Währung (M10, Entscheidung des Projektinhabers E6): Gulden, im Handel und beim Lehrer.
Item "it_gulden" {
    name = "Gulden",
    category = "misc",
    value = 1,
}
