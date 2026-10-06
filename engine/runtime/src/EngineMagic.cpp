// Magic (M12, EngineMagic.cpp): spells from runes and scrolls (part B: what may be cast; casting follows in
// part C). Owner decisions Z1-Z3: runes need the spell's circle, scrolls not; both cost mana, which does not
// come back by itself.

#include <g7/gameplay/Magic.hpp>
#include <g7/runtime/Engine.hpp>

#include <format>

namespace g7
{
std::optional<gameplay::SpellInfo> Engine::spellOfItem(std::string_view item) const
{
    const script::Instance* def = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
    const std::string_view spell = def != nullptr ? def->fields["spell"].asString() : std::string_view();
    const script::Instance* s = spell.empty() ? nullptr : m_scripts->findInstance("Spell", spell);
    return s != nullptr ? std::optional<gameplay::SpellInfo>(gameplay::spellInfo(*s)) : std::nullopt;
}

void Engine::bindMagicFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind(
        {"cast_check", "cast_check(item: string, stages?: integer) -> string | nil",
         "Ob der Held die Rune bzw. Spruchrolle wirken kann (M12, Z1-Z3): nil, sonst der Grund (fehlender "
         "Kreis, zu wenig Mana, nicht im Inventar).",
         "Magie", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"expects (item, stages?)"};
             }
             const auto spell = spellOfItem(a[0].asString());
             if (!spell)
             {
                 return Error{std::format("{} is no rune or scroll", a[0].asString())};
             }
             if (!m_hero || m_hero->itemCount(a[0].asString()) == 0)
             {
                 return Value(std::string("Das hast du nicht."));
             }
             const script::Instance* item = m_scripts->findInstance("Item", a[0].asString());
             const bool scroll = item->fields["category"].asString() == "scroll";
             const auto blocked = gameplay::castBlocked(
                 *m_hero, *spell, scroll,
                 a.size() > 1 ? static_cast<u32>(std::max<i64>(0, a[1].asInteger())) : 0u);
             return blocked ? Value(*blocked) : Value();
         }});
}
} // namespace g7
