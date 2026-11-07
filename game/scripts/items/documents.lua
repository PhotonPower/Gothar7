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

-- Kapitel 2 (M15 Teil E): der Brief an den Rat von Leonberg.
Item "it_letter_council" {
    name = "Versiegelter Brief an den Rat",
    category = "document",
    value = 0,
    text = "An den Rat der Stadt Leonberg,\n\ndas Lager am Waldrand bittet um Salz und Eisen für den Winter. Der Überbringer ist ein Mann, dem man trauen kann.\n\n- Der Vogt",
}
