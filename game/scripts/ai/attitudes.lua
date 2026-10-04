-- Einstellungen der NPCs zum Spieler (M9 Teil C2, Gothic: freundlich, neutral, verärgert, feindlich).
-- Reihenfolge: vorübergehend (wird nach einer Weile vergessen) vor dauerhaft (Story, wird gespeichert) vor der
-- Gilden-Tabelle `Attitudes` (data/guilds.lua) gegen die Gilde des Helden.

AttitudeSettings = {
    forget_seconds = 300, -- so lange bleibt eine vorübergehende Einstellung (Spielzeit-Sekunden)
}

local kinds = { friendly = true, neutral = true, angry = true, hostile = true }
local temporary = {} -- npc -> { attitude = ..., timer = ... }

local function check(a)
    if not kinds[a] then
        error("attitude must be friendly, neutral, angry or hostile, not " .. tostring(a))
    end
end

--- Die Einstellung des NPCs zum Spieler: "friendly", "neutral", "angry" oder "hostile".
function npc_attitude(npc)
    if temporary[npc] then
        return temporary[npc].attitude
    end
    local attitudes = Story.attitudes
    if attitudes and attitudes[npc] then
        return attitudes[npc]
    end
    local own = instance("Npc", npc)
    local hero_guild = hero().guild
    if own and own.guild and hero_guild and hero_guild ~= "" then
        return attitude(own.guild, hero_guild)
    end
    return "neutral"
end

--- Dauerhafte Einstellung (Story: wird mit dem Spielstand gespeichert); nil löscht sie.
function set_attitude(npc, a)
    if a ~= nil then
        check(a)
    end
    Story.attitudes = Story.attitudes or {}
    Story.attitudes[npc] = a
end

--- Vorübergehende Einstellung, vergessen nach `seconds` (Vorgabe AttitudeSettings.forget_seconds).
function set_temp_attitude(npc, a, seconds)
    check(a)
    if temporary[npc] then
        cancel(temporary[npc].timer)
    end
    local timer = after(seconds or AttitudeSettings.forget_seconds, function()
        temporary[npc] = nil
    end)
    temporary[npc] = { attitude = a, timer = timer }
end
