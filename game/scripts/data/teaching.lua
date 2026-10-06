-- Lernen beim Lehrer (M10 Teil C, Entscheidung des Projektinhabers E7): Attribut +1 kostet 1 Lernpunkt, +5 kostet
-- 5; Talente 5-20 Lernpunkte; dazu Gulden je Lehrer (`gulden` im Angebot). Die Angebote stehen beim Lehrer
-- (dialogs/), teach_menu baut daraus die Antworten im Dialog.

Teaching = {
    attribute_names = { str = "Stärke", dex = "Geschick", mana_max = "Mana" },
    talent_names = { magic_circle = "Kreis der Magie", picklock = "Schlösser öffnen", pickpocket = "Taschendiebstahl", smithing = "Schmieden",
                     sneak = "Schleichen", ["1h"] = "Einhand" },
    limit = 100, -- höchster Wert eines Attributs beim Lehrer
}

local function label(offer)
    local name = offer.attribute and (Teaching.attribute_names[offer.attribute] or offer.attribute)
        or (Teaching.talent_names[offer.talent] or offer.talent)
    local what = offer.attribute and string.format("%s +%d", name, offer.amount or 1)
        or string.format("%s (Stufe %d)", name, offer.level or 1)
    local cost = string.format("%d LP", offer.lp)
    if (offer.gulden or 0) > 0 then
        cost = cost .. string.format(", %d Gulden", offer.gulden)
    end
    return string.format("%s (%s)", what, cost)
end

--- Bringt dem Helden ein Angebot bei; false und ein Grund, wenn es nicht geht.
function teach(offer)
    local h = hero()
    if h.learn_points < offer.lp then
        return false, "lp"
    end
    if item_count("it_gulden") < (offer.gulden or 0) then
        return false, "gulden"
    end
    if offer.attribute then
        local now = stat(offer.attribute)
        if now + (offer.amount or 1) > Teaching.limit then
            return false, "limit"
        end
        set_stat(offer.attribute, now + (offer.amount or 1))
    else
        if talent(offer.talent) >= (offer.level or 1) then
            return false, "known"
        end
        set_talent(offer.talent, offer.level or 1)
    end
    set_learn_points(h.learn_points - offer.lp)
    if (offer.gulden or 0) > 0 then
        remove_item("it_gulden", offer.gulden)
    end
    return true
end

--- Im Dialog: die Angebote des Lehrers als Antworten, dazu "Zurück". `replies` sind seine Sätze für
--- ok, lp (zu wenig Lernpunkte), gulden, limit, known.
function teach_menu(npc, offers, replies)
    for _, offer in ipairs(offers) do
        choice(label(offer), function()
            local ok, why = teach(offer)
            say(npc, ok and replies.ok or replies[why])
            teach_menu(npc, offers, replies)
        end)
    end
    choice("Zurück.", function() end)
end
