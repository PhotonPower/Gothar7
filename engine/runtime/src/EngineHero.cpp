// The hero's character (M8 part A): attributes, talents, experience, inventory and equipment of the Npc
// `pc_hero` from the scripts, and the script functions that read and change them (group "Held" in
// docs/script-api.md).

#include <g7/core/Log.hpp>
#include <g7/gameplay/Character.hpp>
#include <g7/runtime/Engine.hpp>

#include <format>

namespace g7
{
namespace
{
constexpr std::string_view kHeroInstance = "pc_hero";
} // namespace

gameplay::ItemLookup Engine::itemLookup() const
{
    return [this](std::string_view item) -> std::optional<gameplay::ItemInfo>
    {
        const script::Instance* instance = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
        return instance ? std::optional(gameplay::itemInfo(*instance)) : std::nullopt;
    };
}

void Engine::buildHero()
{
    if (m_hero || !m_scripts)
    {
        return; // a reload keeps the hero as it is
    }
    const script::Instance* hero = m_scripts->findInstance("Npc", kHeroInstance);
    if (hero == nullptr)
    {
        G7_LOG_WARN("engine", "no Npc \"{}\" in the scripts: the hero has no character values",
                    kHeroInstance);
        return;
    }
    auto character = gameplay::Character::fromInstance(*hero, itemLookup());
    if (!character)
    {
        G7_LOG_WARN("engine", "hero: {}", character.error().message);
        consolePrint("! hero: " + character.error().message);
        return;
    }
    m_hero = std::make_unique<gameplay::Character>(std::move(character).value());
    G7_LOG_INFO("engine", "hero {}: level {}, {} items", m_hero->name(), m_hero->level(),
                m_hero->inventory(itemLookup()).size());
}

const gameplay::Character* Engine::hero() const noexcept
{
    return m_hero.get();
}

i32 Engine::xpForLevel(i32 level)
{
    // Progression.xp_for_level from the scripts; without it Gothic 1's rule.
    const script::Value progression = m_scripts ? m_scripts->global("Progression") : script::Value();
    if (const script::FunctionRef f = progression["xp_for_level"].asFunction(); f.valid())
    {
        const script::Value args[] = {static_cast<i64>(level)};
        if (auto r = m_scripts->call(f, args); r && r.value().isNumber())
        {
            return static_cast<i32>(r.value().asInteger());
        }
    }
    return 500 * level * (level + 1) / 2;
}

void Engine::bindHeroFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    const auto hero = [this]() -> Result<gameplay::Character*>
    {
        if (!m_hero)
        {
            return Error{"no hero (Npc \"pc_hero\" missing)"};
        }
        return m_hero.get();
    };
    const auto text = [](std::span<const Value> a, usize i) -> Result<std::string>
    {
        if (a.size() <= i || !a[i].isString())
        {
            return Error{std::format("argument {} must be a string", i + 1)};
        }
        return std::string(a[i].asString());
    };

    vm.bind({"give_item", "give_item(item: string, count?: integer) -> integer",
             "Gibt dem Helden `count` (Vorgabe 1) Stück eines Items; gibt die neue Anzahl zurück.", "Held",
             [this, hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto item = text(a, 0);
                 if (!h || !item)
                 {
                     return !h ? h.error() : item.error();
                 }
                 if (!itemLookup()(item.value()))
                 {
                     return Error{std::format("unknown item \"{}\"", item.value())};
                 }
                 const i64 count = a.size() > 1 ? a[1].asInteger(1) : 1;
                 if (count < 1)
                 {
                     return Error{"count must be at least 1"};
                 }
                 h.value()->addItem(item.value(), static_cast<u32>(count));
                 return Value(static_cast<i64>(h.value()->itemCount(item.value())));
             }});
    vm.bind({"remove_item", "remove_item(item: string, count?: integer) -> boolean",
             "Nimmt dem Helden `count` Stück weg; `false` (und nichts weggenommen), wenn er weniger hat. "
             "Ausgerüstetes wird dabei abgelegt.",
             "Held", [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto item = text(a, 0);
                 if (!h || !item)
                 {
                     return !h ? h.error() : item.error();
                 }
                 return Value(h.value()->removeItem(item.value(),
                                                    static_cast<u32>(a.size() > 1 ? a[1].asInteger(1) : 1)));
             }});
    vm.bind({"item_count", "item_count(item: string) -> integer", "Wie viele Stück eines Items der Held hat.",
             "Held", [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto item = text(a, 0);
                 if (!h || !item)
                 {
                     return !h ? h.error() : item.error();
                 }
                 return Value(static_cast<i64>(h.value()->itemCount(item.value())));
             }});
    vm.bind({"inventory", "inventory() -> {{item, count, name, category}}",
             "Das Inventar des Helden, nach Kategorie sortiert (Waffen zuerst, wie in Gothic), ohne "
             "Gewichtsgrenze.",
             "Held", [this, hero](std::span<const Value>) -> Result<Value>
             {
                 auto h = hero();
                 if (!h)
                 {
                     return h.error();
                 }
                 std::vector<Value> list;
                 const gameplay::ItemLookup items = itemLookup();
                 for (const gameplay::ItemStack& stack : h.value()->inventory(items))
                 {
                     const auto info = items(stack.item);
                     list.push_back(
                         script::makeTable({}, {{"item", stack.item},
                                                {"count", static_cast<i64>(stack.count)},
                                                {"name", info ? info->name : stack.item},
                                                {"category", info ? info->category : std::string("misc")}}));
                 }
                 return script::makeTable(std::move(list));
             }});
    vm.bind(
        {"equip", "equip(item: string) -> string",
         "Rüstet ein Item aus dem Inventar aus; gibt den Platz zurück (`melee`, `ranged`, `armor`, `helmet`, "
         "`ring1`/`ring2`, `amulet`, `belt`, `rune1`–`rune7`). Fehler, wenn Bedingungen (`requires`) fehlen.",
         "Held", [this, hero, text](std::span<const Value> a) -> Result<Value>
         {
             auto h = hero();
             auto item = text(a, 0);
             if (!h || !item)
             {
                 return !h ? h.error() : item.error();
             }
             auto slot = h.value()->equip(item.value(), itemLookup());
             if (!slot)
             {
                 return slot.error();
             }
             return Value(std::string(gameplay::slotName(slot.value())));
         }});
    vm.bind({"unequip", "unequip(slot: string)", "Legt ab, was auf dem Platz `slot` ausgerüstet ist.", "Held",
             [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto slot = text(a, 0);
                 if (!h || !slot)
                 {
                     return !h ? h.error() : slot.error();
                 }
                 const auto s = gameplay::slotFromName(slot.value());
                 if (!s)
                 {
                     return Error{std::format("unknown slot \"{}\"", slot.value())};
                 }
                 h.value()->unequip(*s);
                 return Value();
             }});
    vm.bind({"equipped", "equipped(slot: string) -> string | nil",
             "Was auf dem Platz `slot` ausgerüstet ist.", "Held",
             [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto slot = text(a, 0);
                 if (!h || !slot)
                 {
                     return !h ? h.error() : slot.error();
                 }
                 const auto s = gameplay::slotFromName(slot.value());
                 if (!s)
                 {
                     return Error{std::format("unknown slot \"{}\"", slot.value())};
                 }
                 const std::string& item = h.value()->equipped(*s);
                 return item.empty() ? Value() : Value(item);
             }});
    vm.bind({"stat", "stat(name: string) -> integer",
             "Ein Attribut des Helden (`hp`, `hp_max`, `mana`, `mana_max`, `str`, `dex`) oder ein Schutzwert "
             "(`protection_edge`, `_blunt`, `_point`, `_fire`, `_magic`, `_fall`, mit Ausrüstung).",
             "Held", [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto name = text(a, 0);
                 if (!h || !name)
                 {
                     return !h ? h.error() : name.error();
                 }
                 if (name.value().starts_with("protection_"))
                 {
                     return Value(
                         static_cast<i64>(h.value()->protection(std::string_view(name.value()).substr(11))));
                 }
                 return Value(static_cast<i64>(h.value()->attribute(name.value())));
             }});
    vm.bind({"set_stat", "set_stat(name: string, value: integer)",
             "Setzt ein Attribut des Helden; `hp`/`mana` bleiben zwischen 0 und dem Maximum.", "Held",
             [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto name = text(a, 0);
                 if (!h || !name || a.size() < 2 || !a[1].isNumber())
                 {
                     return !h ? h.error() : !name ? name.error() : Error{"expects (name, value: integer)"};
                 }
                 if (auto set = h.value()->setAttribute(name.value(), static_cast<i32>(a[1].asInteger()));
                     !set)
                 {
                     return set.error();
                 }
                 return Value();
             }});
    vm.bind({"talent", "talent(name: string) -> integer",
             "Stufe eines Talents des Helden (0 = nicht gelernt): `melee_1h`, `melee_2h`, `bow`, `crossbow`, "
             "`sneak`, `picklock`, `pickpocket`, `acrobatics`, `magic_circle`.",
             "Held", [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto name = text(a, 0);
                 if (!h || !name)
                 {
                     return !h ? h.error() : name.error();
                 }
                 return Value(static_cast<i64>(h.value()->talent(name.value())));
             }});
    vm.bind({"set_talent", "set_talent(name: string, level: integer)",
             "Setzt die Stufe eines Talents des Helden.", "Held",
             [hero, text](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 auto name = text(a, 0);
                 if (!h || !name || a.size() < 2 || !a[1].isNumber())
                 {
                     return !h ? h.error() : !name ? name.error() : Error{"expects (name, level: integer)"};
                 }
                 if (auto set = h.value()->setTalent(name.value(), static_cast<i32>(a[1].asInteger())); !set)
                 {
                     return set.error();
                 }
                 return Value();
             }});
    vm.bind({"add_xp", "add_xp(amount: integer) -> integer",
             "Gibt dem Helden Erfahrung; gibt die Zahl der neuen Stufen zurück. Jede Stufe bringt "
             "`Progression.learn_points_per_level` Lernpunkte und löst `level_up` aus.",
             "Held", [this, hero](std::span<const Value> a) -> Result<Value>
             {
                 auto h = hero();
                 if (!h || a.empty() || !a[0].isNumber())
                 {
                     return !h ? h.error() : Error{"expects (amount: integer)"};
                 }
                 const script::Value progression = m_scripts->global("Progression");
                 const i32 points = static_cast<i32>(progression["learn_points_per_level"].asInteger(10));
                 const u32 gained = h.value()->addExperience(
                     static_cast<i32>(a[0].asInteger()), [this](i32 level) { return xpForLevel(level); },
                     points);
                 for (u32 i = 0; i < gained; ++i)
                 {
                     const script::Value args[] = {
                         static_cast<i64>(h.value()->level() - static_cast<i32>(gained - 1 - i))};
                     m_scripts->emit("level_up", args);
                 }
                 return Value(static_cast<i64>(gained));
             }});
    vm.bind({"hero", "hero() -> {name, guild, level, xp, next_xp, learn_points}",
             "Name, Gilde, Stufe, Erfahrung, Erfahrung bis zur nächsten Stufe und Lernpunkte des Helden.",
             "Held", [this, hero](std::span<const Value>) -> Result<Value>
             {
                 auto h = hero();
                 if (!h)
                 {
                     return h.error();
                 }
                 const gameplay::Character& c = *h.value();
                 return script::makeTable({}, {{"name", c.name()},
                                               {"guild", c.guild()},
                                               {"level", static_cast<i64>(c.level())},
                                               {"xp", static_cast<i64>(c.experience())},
                                               {"next_xp", static_cast<i64>(xpForLevel(c.level() + 1))},
                                               {"learn_points", static_cast<i64>(c.learnPoints())}});
             }});
    vm.bind({"drop_item", "drop_item(item: string, count?: integer)",
             "Legt `count` (Vorgabe 1) Stück aus dem Inventar des Helden vor ihm auf den Boden; "
             "Ausgerüstetes wird "
             "dabei abgelegt.",
             "Held", [this, text](std::span<const Value> a) -> Result<Value>
             {
                 auto item = text(a, 0);
                 if (!item)
                 {
                     return item.error();
                 }
                 const i64 count = a.size() > 1 ? a[1].asInteger(1) : 1;
                 if (count < 1)
                 {
                     return Error{"count must be at least 1"};
                 }
                 if (auto dropped = dropItem(item.value(), static_cast<u32>(count)); !dropped)
                 {
                     return dropped.error();
                 }
                 return Value();
             }});
    vm.bind({"item_taken",
             "on(\"item_taken\", fn(item: string, count: integer))",
             "Der Held hat einen Gegenstand aus der Welt aufgehoben (Aktionstaste auf ein Item im Fokus).",
             "Ereignisse",
             {}});
    vm.bind({"level_up",
             "on(\"level_up\", fn(level: integer))",
             "Der Held hat eine neue Stufe erreicht.",
             "Ereignisse",
             {}});
}
} // namespace g7
