-- Stimmen der Zurufe (Entscheidung Projektinhaber, 2026-10-05): eine Stimme je Gilde bzw. Gruppe, männlich und,
-- wo es Frauen gibt, weiblich (figure_sets.toml: Wachen und Jäger sind nur Männer). Ein NPC ruft mit der Stimme
-- seiner Gilde und seines Geschlechts (`Npc.gender`, Vorgabe "m"); `Npc.voice` überschreibt die Gruppe (z. B.
-- "craftsman" für den Schmied). Die Schlüssel der Zurufe vergibt `gothar-voice scan`:
-- svm_<stimme>_<m|f>_<anlass>_NN (docs/modules/audio.md „Sprache“).
Voices = {
    guard = { "m" },
    farmer = { "m", "f" },
    hunter = { "m" },
    outcast = { "m", "f" },
    citizen = { "m", "f" },
    craftsman = { "m", "f" },
}
