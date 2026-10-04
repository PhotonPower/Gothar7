-- Handel (M10 Teil C, Entscheidung des Projektinhabers E6): Tauschhandel wie Gothic 1 mit Gulden als Währung.
-- Der Händler verkauft zum vollen Wert und kauft zum halben (`value` der Items).
Trade = {
    currency = "it_gulden",
    sell_factor = 1.0, -- der Held kauft: Wert mal diesem Faktor (aufgerundet)
    buy_factor = 0.5,  -- der Held verkauft: Wert mal diesem Faktor (abgerundet)
}
