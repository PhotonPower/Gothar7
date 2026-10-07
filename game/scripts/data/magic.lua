-- Magie (M12, Entscheidungen des Projektinhabers): Mana erholt sich nicht von selbst (Z1: Tränke, Schlaf, Stufe);
-- Kreise 1-6 kosten beim Lehrer so viele Lernpunkte (Z2); Runen brauchen den Kreis, Spruchrollen nicht (Z3).
Magic = {
    circle_lp = { 10, 15, 20, 25, 30, 35 },
    -- Wirken (M12 Teil C1, Z5): eine Aufladestufe je charge_seconds; Projektile fliegen gerade mit projectile_speed
    -- (m/s); Zielzauber erreichen ein Ziel bis target_range (m).
    charge_seconds = 1.0,
    projectile_speed = 30,
    target_range = 25,
}
