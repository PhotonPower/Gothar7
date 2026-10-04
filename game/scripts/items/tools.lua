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
