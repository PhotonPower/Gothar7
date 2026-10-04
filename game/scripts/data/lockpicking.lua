-- Schlösser knacken (wie Gothic 1, Entscheidung Projektinhaber 2026-10-04): links/rechts drehen nach der
-- Kombination des Schlosses; ein falscher Schritt setzt auf den Anfang zurück und bricht den Dietrich mit
-- dieser Wahrscheinlichkeit, je nach Talent `picklock` (Einträge für Talent 0, 1, 2). Knacken geht auch ohne
-- Talent.
Lockpicking = {
    item = "it_lockpick",
    break_chance = { 0.50, 0.25, 0.05 },
}
