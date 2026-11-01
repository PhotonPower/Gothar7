-- Schriftstücke (Beispielinhalt M8): Lesen zeigt `text` und lässt das Schriftstück im Inventar.
Item "it_letter_farm" {
    name = "Brief an den Bauern",
    category = "document",
    value = 0,
    text = "Bauer,\n\ndie Ernte muss bis zum Vollmond eingebracht sein. Schick zwei Knechte zum Lager.\n\n- Der Vogt",
}

-- Karten (M14 Teil E, F9): Benutzen zeigt das Bild auf Pergament, ohne Spielerposition. Den Stadtplan erzeugt welt
-- aus den OSM-Daten (gothar-worldgen citymap, ui/maps/leonberg.toml); Agnes vom Krämerladen verkauft ihn.
Item "it_map_leonberg" {
    name = "Stadtplan von Leonberg",
    category = "document",
    value = 50,
    map = "ui/maps/leonberg.png",
}
