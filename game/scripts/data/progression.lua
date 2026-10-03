-- Stufen und Lernpunkte (wie Gothic 1): Stufe n braucht insgesamt 500 * n * (n + 1) / 2 Erfahrung
-- (Stufe 1: 500, Stufe 2: 1500, Stufe 3: 3000 ...), jede Stufe bringt 10 Lernpunkte.
Progression = {
    learn_points_per_level = 10,
    xp_for_level = function(level)
        return 500 * level * (level + 1) // 2
    end,
}
