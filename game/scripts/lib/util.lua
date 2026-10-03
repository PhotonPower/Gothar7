-- Kleine Hilfen für alle Skripte (lib/ lädt vor allem anderen).
local util = {}

--- Begrenzt `x` auf [lo, hi].
function util.clamp(x, lo, hi)
    if x < lo then return lo end
    if x > hi then return hi end
    return x
end

--- "08:30" -> 510 (Minuten seit Mitternacht).
function util.minutes(clock)
    local h, m = clock:match("^(%d+):(%d+)$")
    return tonumber(h) * 60 + tonumber(m)
end

return util
