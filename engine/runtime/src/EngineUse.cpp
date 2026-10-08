// Using items and pickpocketing (M8 part D): food and potions work at the "use" event of t_eat / t_drink and
// are used up, documents show their text (t_read_scroll); only standing (owner's decision 2026-10-04).
// Pickpocketing as in Gothic 1: only with the talent, sure with enough dexterity (Npc.pickpocket_dex),
// noticed otherwise.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <format>

namespace g7
{
namespace
{
constexpr f32 kUseFallbackSeconds = 0.8f;     ///< a use without its clip in the graph
constexpr f32 kStandingSpeed = 0.5f;          ///< m/s: below this the hero counts as standing
constexpr u64 kCreatureFocusBit = 1ull << 62; ///< as in EngineItems.cpp
} // namespace

bool Engine::heroStanding() const
{
    return m_player.valid() && m_player.state() == physics::MoveState::Ground &&
           m_swimmer.mode() == gameplay::WaterMode::Land && !m_climb && !m_mobUse && !m_pickup &&
           !m_itemUse && !m_pickpocket &&
           glm::length(Vec3(m_player.velocity().x, 0.0f, m_player.velocity().z)) < kStandingSpeed;
}

Result<void> Engine::useItem(std::string_view item)
{
    if (!m_hero || m_hero->itemCount(item) == 0)
    {
        return Error{std::format("the hero has no \"{}\"", item)};
    }
    const script::Instance* def = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
    if (def == nullptr)
    {
        return Error{std::format("unknown item \"{}\"", item)};
    }
    const std::string category(def->fields["category"].asString());
    if (category == "torch")
    {
        // Lights it, or puts it out and away (owner decision, as Gothic 1).
        if (m_torch)
        {
            putTorchAway();
        }
        else
        {
            lightTorch(item);
        }
        return {};
    }
    // Drinks among the food (tag "drink": beer, wine) are drunk, not eaten.
    bool drink = false;
    if (const script::Table* tags = def->fields["tags"].asTable())
    {
        drink =
            std::ranges::any_of(tags->array, [](const script::Value& t) { return t.asString() == "drink"; });
    }
    const char* state = category == "food" && !drink                 ? "use_eat"
                        : category == "food" || category == "potion" ? "use_drink"
                        : category == "document"                     ? "use_read"
                                                                     : nullptr;
    if (state == nullptr)
    {
        return Error{std::format("\"{}\" cannot be used", item)};
    }
    if (!heroStanding())
    {
        notice("Nicht jetzt.");
        return Error{"not now"};
    }
    ItemUse use;
    use.item = std::string(item);
    use.category = category;
    use.state = state;
    if (m_figure && m_figure->animator.hasState(state))
    {
        m_figure->animator.enter(state, 0.15f);
        use.animated = true;
    }
    m_itemUse = std::move(use);
    return {};
}

void Engine::applyItemUse(ItemUse& use)
{
    use.applied = true;
    const script::Instance* def = m_scripts ? m_scripts->findInstance("Item", use.item) : nullptr;
    if (def == nullptr || !m_hero || m_hero->itemCount(use.item) == 0)
    {
        return; // gone meanwhile
    }
    if (use.category == "document")
    {
        m_document = Document{std::string(def->fields["name"].asString()),
                              std::string(def->fields["text"].asString())};
    }
    if (const script::Table* effects = def->fields["effects"].asTable())
    {
        for (const auto& [attribute, amount] : effects->fields)
        {
            (void)m_hero->setAttribute(attribute,
                                       m_hero->attribute(attribute) + static_cast<i32>(amount.asInteger()));
        }
    }
    if (const script::Table* boost = def->fields["boost"].asTable())
    {
        // For a while (the speed potion): a new one starts the time again, it does not add up.
        const auto factor =
            static_cast<f32>(boost->fields.contains("speed") ? boost->fields.at("speed").asNumber(1.0) : 1.0);
        const auto secs = static_cast<f32>(
            boost->fields.contains("seconds") ? boost->fields.at("seconds").asNumber(0.0) : 0.0);
        if (factor > 0.0f && secs > 0.0f)
        {
            m_speedBoost = SpeedBoost{factor, secs};
        }
    }
    if (use.category == "food" || use.category == "potion")
    {
        (void)m_hero->removeItem(use.item);
    }
    if (m_scripts)
    {
        if (const script::FunctionRef f = def->fields["on_use"].asFunction(); f.valid())
        {
            const script::Value args[] = {use.item};
            (void)m_scripts->call(f, args);
        }
        const script::Value args[] = {use.item};
        m_scripts->emit("item_used", args);
    }
}

void Engine::fixedUpdateItemUse(f32 seconds)
{
    if (m_itemUse)
    {
        ItemUse& use = *m_itemUse;
        use.time += seconds;
        const bool event = std::find(m_mobEvents.begin(), m_mobEvents.end(), "use") != m_mobEvents.end();
        const bool ended = use.animated ? (!m_figure || m_figure->animator.state() != use.state ||
                                           m_figure->animator.stateEnded())
                                        : use.time >= kUseFallbackSeconds;
        if (!use.applied && (event || ended || (!use.animated && use.time >= 0.5f * kUseFallbackSeconds)))
        {
            applyItemUse(use);
        }
        if (use.applied && ended)
        {
            if (use.animated && m_figure)
            {
                m_figure->animator.enter("move", 0.25f);
            }
            m_itemUse.reset();
        }
    }
    if (m_pickpocket)
    {
        PendingPickpocket& p = *m_pickpocket;
        p.time += seconds;
        const bool ended = p.animated ? (!m_figure || m_figure->animator.state() != "pickpocket" ||
                                         m_figure->animator.stateEnded())
                                      : p.time >= kUseFallbackSeconds;
        if (ended)
        {
            finishPickpocket(p);
            if (p.animated && m_figure)
            {
                m_figure->animator.enter("move", 0.25f);
            }
            m_pickpocket.reset();
        }
    }
}

std::optional<Engine::Document> Engine::document() const
{
    return m_document;
}

void Engine::closeDocument() noexcept
{
    m_document.reset();
}

Result<void> Engine::pickpocketFocus()
{
    if (!m_focus || m_focus->kind != gameplay::FocusKind::Npc || !m_hero)
    {
        return Error{"no NPC in focus"};
    }
    Creature* c = creature(static_cast<u32>(m_focus->id & ~kCreatureFocusBit));
    if (c == nullptr || !c->character)
    {
        return Error{"nobody to rob"};
    }
    if (m_hero->talent("pickpocket") < 1)
    {
        notice("Das kann ich nicht.");
        return Error{"needs the talent pickpocket"};
    }
    if (c->pickpocketed)
    {
        notice("Da ist nichts mehr zu holen.");
        return Error{"already tried"};
    }
    if (!heroStanding())
    {
        notice("Nicht jetzt.");
        return Error{"not now"};
    }
    PendingPickpocket p;
    p.creature = c->id;
    if (m_figure && m_figure->animator.hasState("pickpocket"))
    {
        m_figure->animator.enter("pickpocket", 0.15f);
        p.animated = true;
    }
    m_pickpocket = p;
    return {};
}

void Engine::finishPickpocket(const PendingPickpocket& p)
{
    Creature* c = creature(p.creature);
    if (c == nullptr || !c->character || !m_hero)
    {
        return;
    }
    c->pickpocketed = true; // once per NPC, like Gothic
    const script::Instance* npc = m_scripts ? m_scripts->findInstance("Npc", c->species) : nullptr;
    const script::Value settings = m_scripts ? m_scripts->global("Pickpocketing") : script::Value();
    const i64 needed = npc != nullptr && npc->fields["pickpocket_dex"].isNumber()
                           ? npc->fields["pickpocket_dex"].asInteger()
                           : settings["default_dex"].asInteger(30);
    const std::string name = npc != nullptr ? std::string(npc->fields["name"].asString()) : c->species;
    if (m_hero->attribute("dex") < needed)
    {
        notice(std::format("{} hat es bemerkt!", name));
        if (m_scripts)
        {
            const script::Value args[] = {c->species};
            m_scripts->emit("pickpocket_failed", args);
            const script::Value seen[] = {c->species, std::string(), i64{0}};
            witnessed("assess_theft", seen, c->species); // the victim notices, and whoever sees it (M9)
        }
        return;
    }
    // Something not worn: one piece of a random stack.
    std::vector<std::string> loot;
    for (const gameplay::ItemStack& stack : c->character->inventory(itemLookup()))
    {
        u32 worn = 0;
        for (usize s = 0; s < static_cast<usize>(gameplay::EquipSlot::Count); ++s)
        {
            worn += c->character->equipped(static_cast<gameplay::EquipSlot>(s)) == stack.item ? 1 : 0;
        }
        if (stack.count > worn)
        {
            loot.push_back(stack.item);
        }
    }
    if (loot.empty())
    {
        notice(std::format("{} hat nichts bei sich.", name));
        return;
    }
    // A quest item the scripts name comes first (Gothic: each NPC's pickpocket item; M10).
    if (const std::string_view wanted = npc != nullptr ? npc->fields["pickpocket_item"].asString() : "";
        !wanted.empty() && std::ranges::find(loot, wanted) != loot.end())
    {
        loot = {std::string(wanted)};
    }
    const f32 roll = m_random ? m_random() : std::uniform_real_distribution<f32>(0.0f, 1.0f)(m_rng);
    const std::string item =
        loot[std::min(loot.size() - 1, static_cast<usize>(roll * static_cast<f32>(loot.size())))];
    (void)c->character->removeItem(item);
    m_hero->addItem(item);
    const script::Instance* def = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
    notice(std::format("{} gestohlen.", def != nullptr ? def->fields["name"].asString() : item));
    if (m_scripts)
    {
        const script::Value args[] = {c->species, item};
        m_scripts->emit("pickpocket", args);
    }
}

std::optional<std::vector<gameplay::ItemStack>> Engine::creatureInventory(u32 id) const
{
    const Creature* c = creature(id);
    if (c == nullptr || !c->character)
    {
        return std::nullopt;
    }
    return c->character->inventory(itemLookup());
}

void Engine::documentUi()
{
    if (!m_document)
    {
        return;
    }
    ui::DocumentPanel panel;
    panel.title = m_document->title;
    panel.text = m_document->text;
    m_debugUi.documentPanel(panel);
    if (!panel.open)
    {
        m_document.reset();
    }
}

gameplay::MovementSettings Engine::boostedMovement() const
{
    gameplay::MovementSettings s = m_movementSettings;
    if (m_speedBoost && !m_transform)
    {
        const f32 f = m_speedBoost->factor;
        s.walkSpeed *= f;
        s.runSpeed *= f;
        s.sneakSpeed *= f;
        s.strafeSpeed *= f;
        s.backwardSpeed *= f;
    }
    return s;
}

void Engine::fixedUpdateBoost(f32 seconds)
{
    if (!m_speedBoost)
    {
        return;
    }
    m_speedBoost->seconds -= seconds;
    if (m_speedBoost->seconds <= 0.0f)
    {
        m_speedBoost.reset();
        if (m_scripts)
        {
            m_scripts->emit("boost_ended");
        }
    }
}

void Engine::bindUseFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"hero_boost", "hero_boost() -> {speed, seconds} | nil",
             "Die laufende Wirkung eines Tempo-Tranks (`boost` am Item): Faktor und verbleibende Sekunden; "
             "nil ohne.",
             "Held", [this](std::span<const Value>) -> Result<Value>
             {
                 if (!m_speedBoost)
                 {
                     return Value();
                 }
                 return script::makeTable({}, {{"speed", static_cast<f64>(m_speedBoost->factor)},
                                               {"seconds", static_cast<f64>(m_speedBoost->seconds)}});
             }});
    vm.bind({"boost_ended",
             "on(\"boost_ended\", fn())",
             "Die Wirkung eines Tempo-Tranks ist vorbei.",
             "Ereignisse",
             {}});
    vm.bind(
        {"use_item", "use_item(item: string)",
         "Der Held benutzt ein Item aus dem Inventar (Nahrung, Trank, Schriftstück) – nur im Stand, sonst "
         "„Nicht jetzt.“. Die Wirkung (`effects`) kommt beim Event `use` des Clips.",
         "Held", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"argument 1 must be an item"};
             }
             if (auto used = useItem(a[0].asString()); !used)
             {
                 return used.error();
             }
             return Value();
         }});
    vm.bind({"owned_by", "owned_by(vob: string) -> string | nil",
             "Wem ein Item- oder Mob-Vob dieser Welt gehört (Npc oder Gilde); nil, wenn niemandem.", "Mobs",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"argument 1 must be a vob name"};
                 }
                 const entt::entity e = m_scene.findByName(StringId(a[0].asString()));
                 if (e == entt::null)
                 {
                     return Error{std::format("no vob \"{}\" in this world", a[0].asString())};
                 }
                 std::string owner;
                 if (const world::ItemRef* item = m_scene.get<world::ItemRef>(e))
                 {
                     owner = item->owner;
                 }
                 else if (const auto mob = m_mobs.find(m_scene.idOf(e).value); mob != m_mobs.end())
                 {
                     owner = mob->second.owner;
                 }
                 return owner.empty() ? Value() : Value(owner);
             }});
    vm.bind({"item_used",
             "on(\"item_used\", fn(item: string))",
             "Der Held hat ein Item benutzt (nach seiner Wirkung).",
             "Ereignisse",
             {}});
    vm.bind({"pickpocket",
             "on(\"pickpocket\", fn(npc: string, item: string))",
             "Der Held hat einem NPC etwas aus der Tasche gezogen.",
             "Ereignisse",
             {}});
    vm.bind({"pickpocket_failed",
             "on(\"pickpocket_failed\", fn(npc: string))",
             "Der NPC hat den Taschendiebstahl bemerkt (die Reaktion folgt mit M9).",
             "Ereignisse",
             {}});
    vm.bind({"theft",
             "on(\"theft\", fn(owner: string, item: string, count: integer))",
             "Der Held hat fremden Besitz genommen (Item-Vob mit owner, Truhe eines anderen).",
             "Ereignisse",
             {}});
}
} // namespace g7
