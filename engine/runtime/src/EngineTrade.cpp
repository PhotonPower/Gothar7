// Trading (M10 part C, owner decision E6): barter with a trader like Gothic 1 - his goods against yours, paid
// in the currency item (Gulden). The trader sells at the full value, buys at half of it (Trade in
// data/trade.lua). Opened by an Info with `trade = true` after its lines; closing it returns to the
// dialogue's menu.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/gameplay/Character.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace g7
{
void Engine::loadTradeSettings()
{
    m_tradeSettings = TradeSettings{};
    const script::Value table = m_scripts ? m_scripts->global("Trade") : script::Value();
    if (table.asTable() == nullptr)
    {
        return;
    }
    if (table["currency"].isString())
    {
        m_tradeSettings.currency = std::string(table["currency"].asString());
    }
    m_tradeSettings.sellFactor = static_cast<f32>(table["sell_factor"].asNumber(m_tradeSettings.sellFactor));
    m_tradeSettings.buyFactor = static_cast<f32>(table["buy_factor"].asNumber(m_tradeSettings.buyFactor));
}

i64 Engine::itemValue(std::string_view item) const
{
    const script::Instance* i = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
    return i != nullptr ? i->fields["value"].asInteger(0) : 0;
}

i64 Engine::tradePrice(std::string_view item, bool heroBuys) const
{
    const f32 factor = heroBuys ? m_tradeSettings.sellFactor : m_tradeSettings.buyFactor;
    // The trader rounds in his favour: up when he sells, down when he buys; never below 1 for a thing of
    // value.
    const f64 price = static_cast<f64>(itemValue(item)) * factor;
    const i64 rounded = heroBuys ? static_cast<i64>(std::ceil(price)) : static_cast<i64>(std::floor(price));
    return itemValue(item) > 0 ? std::max<i64>(rounded, 1) : 0;
}

gameplay::Character* Engine::trader()
{
    Creature* c = m_trade ? creature(m_trade->npc) : nullptr;
    return c != nullptr ? c->character.get() : nullptr;
}

Result<void> Engine::openTrade()
{
    if (!m_dialog || !m_hero)
    {
        return Error{"trading needs a dialogue"};
    }
    Creature* c = creature(m_dialog->npc);
    if (c == nullptr || !c->character)
    {
        return Error{"nobody to trade with"};
    }
    m_trade.emplace();
    m_trade->npc = m_dialog->npc;
    return {};
}

void Engine::closeTrade()
{
    m_trade.reset();
    if (m_dialog)
    {
        m_dialog->menu.clear(); // the topics again
    }
}

Result<void> Engine::tradeBuy(std::string_view item, u32 count)
{
    gameplay::Character* seller = trader();
    if (seller == nullptr || !m_hero)
    {
        return Error{"no trade open"};
    }
    if (item == m_tradeSettings.currency || seller->itemCount(item) < count)
    {
        return Error{std::format("the trader has no {} x {}", count, item)};
    }
    const i64 price = tradePrice(item, true) * count;
    if (m_hero->itemCount(m_tradeSettings.currency) < price)
    {
        notice("Nicht genug Gulden.");
        return Error{std::format("costs {} {}", price, m_tradeSettings.currency)};
    }
    (void)seller->removeItem(item, count);
    m_hero->addItem(item, count);
    (void)m_hero->removeItem(m_tradeSettings.currency, static_cast<u32>(price));
    seller->addItem(m_tradeSettings.currency, static_cast<u32>(price));
    if (m_scripts)
    {
        const script::Value args[] = {m_dialog ? m_dialog->npcName : std::string(), std::string(item),
                                      static_cast<i64>(count), price};
        m_scripts->emit("item_bought", args);
    }
    return {};
}

Result<void> Engine::tradeSell(std::string_view item, u32 count)
{
    gameplay::Character* buyer = trader();
    if (buyer == nullptr || !m_hero)
    {
        return Error{"no trade open"};
    }
    if (item == m_tradeSettings.currency || m_hero->itemCount(item) < count)
    {
        return Error{std::format("you have no {} x {}", count, item)};
    }
    for (usize s = 0; s < static_cast<usize>(gameplay::EquipSlot::Count); ++s)
    {
        if (m_hero->equipped(static_cast<gameplay::EquipSlot>(s)) == item && m_hero->itemCount(item) <= count)
        {
            notice("Das trage ich gerade.");
            return Error{std::format("{} is equipped", item)};
        }
    }
    const i64 price = tradePrice(item, false) * count;
    if (buyer->itemCount(m_tradeSettings.currency) < price)
    {
        notice("So viel Gulden hat er nicht.");
        return Error{std::format("the trader cannot pay {} {}", price, m_tradeSettings.currency)};
    }
    (void)m_hero->removeItem(item, count);
    buyer->addItem(item, count);
    (void)buyer->removeItem(m_tradeSettings.currency, static_cast<u32>(price));
    m_hero->addItem(m_tradeSettings.currency, static_cast<u32>(price));
    if (m_scripts)
    {
        const script::Value args[] = {m_dialog ? m_dialog->npcName : std::string(), std::string(item),
                                      static_cast<i64>(count), price};
        m_scripts->emit("item_sold", args);
    }
    return {};
}

void Engine::tradeUi()
{
    gameplay::Character* seller = trader();
    if (seller == nullptr || !m_hero)
    {
        return;
    }
    const auto items = itemLookup();
    ui::TradePanel panel;
    const script::Instance* npc = m_scripts->findInstance("Npc", m_dialog ? m_dialog->npcName : "");
    panel.title = std::format("Handel mit {}", npc != nullptr ? npc->fields["name"].asString() : "?");
    panel.currency = "Gulden";
    const auto rows =
        [&](const gameplay::Character& who, bool heroBuys, std::vector<ui::TradePanel::Row>& out)
    {
        for (const gameplay::ItemStack& stack : who.inventory(items))
        {
            if (stack.item == m_tradeSettings.currency)
            {
                continue;
            }
            const script::Instance* item = m_scripts->findInstance("Item", stack.item);
            out.push_back({stack.item,
                           item != nullptr ? std::string(item->fields["name"].asString()) : stack.item,
                           stack.count, tradePrice(stack.item, heroBuys)});
        }
    };
    rows(*seller, true, panel.trader);
    rows(*m_hero, false, panel.hero);
    panel.traderMoney = seller->itemCount(m_tradeSettings.currency);
    panel.heroMoney = m_hero->itemCount(m_tradeSettings.currency);
    m_debugUi.tradePanel(panel);
    if (panel.buy >= 0 && static_cast<usize>(panel.buy) < panel.trader.size())
    {
        (void)tradeBuy(panel.trader[static_cast<usize>(panel.buy)].item, 1);
    }
    if (panel.sell >= 0 && static_cast<usize>(panel.sell) < panel.hero.size())
    {
        (void)tradeSell(panel.hero[static_cast<usize>(panel.sell)].item, 1);
    }
    if (panel.close)
    {
        closeTrade();
    }
}

void Engine::bindTradeFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    const auto result = [](Result<void> r) -> Result<Value>
    {
        if (!r)
        {
            return r.error();
        }
        return Value(true);
    };
    vm.bind(
        {"trade_open", "trade_open()",
         "Im Dialog: öffnet den Handel mit dem NPC, sobald die Zeilen gesagt sind (wie `trade = true` an der "
         "Info).",
         "Dialoge", [this](std::span<const Value>) -> Result<Value>
         {
             if (!m_dialog)
             {
                 return Error{"only in a dialogue"};
             }
             m_dialog->tradeRequested = true;
             return Value();
         }});
    vm.bind({"trade_buy", "trade_buy(item: string, count?: integer) -> boolean",
             "Im Handel: kauft vom Händler (zum vollen Wert mal Trade.sell_factor).", "Dialoge",
             [this, result](std::span<const Value> a) -> Result<Value>
             {
                 return result(tradeBuy(a.empty() ? "" : a[0].asString(),
                                        static_cast<u32>(a.size() > 1 ? a[1].asInteger(1) : 1)));
             }});
    vm.bind({"trade_sell", "trade_sell(item: string, count?: integer) -> boolean",
             "Im Handel: verkauft an den Händler (zum Wert mal Trade.buy_factor).", "Dialoge",
             [this, result](std::span<const Value> a) -> Result<Value>
             {
                 return result(tradeSell(a.empty() ? "" : a[0].asString(),
                                         static_cast<u32>(a.size() > 1 ? a[1].asInteger(1) : 1)));
             }});
    vm.bind({"trade_close", "trade_close()", "Schließt den Handel; der Dialog geht weiter.", "Dialoge",
             [this](std::span<const Value>) -> Result<Value>
             {
                 closeTrade();
                 return Value();
             }});
    vm.bind({"trade_price", "trade_price(item: string, buying: boolean) -> integer",
             "Der Preis eines Stücks: kauft der Held (`true`) bzw. verkauft er.", "Dialoge",
             [this](std::span<const Value> a) -> Result<Value>
             { return Value(tradePrice(a.empty() ? "" : a[0].asString(), a.size() > 1 && a[1].asBool())); }});
    vm.bind({"npc_item_count", "npc_item_count(npc: string, item: string) -> integer",
             "Wie viele Stück eines Gegenstands der NPC hat.", "NPCs",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 const auto id = a.empty() ? std::nullopt : npcByInstance(a[0].asString());
                 Creature* c = id ? creature(*id) : nullptr;
                 if (c == nullptr || !c->character || a.size() < 2)
                 {
                     return Error{"expects (npc in this world, item)"};
                 }
                 return Value(static_cast<i64>(c->character->itemCount(a[1].asString())));
             }});
    vm.bind({"item_bought",
             "on(\"item_bought\", fn(npc: string, item: string, count: integer, price: integer))",
             "Der Held hat beim Händler gekauft.",
             "Ereignisse",
             {}});
    vm.bind({"item_sold",
             "on(\"item_sold\", fn(npc: string, item: string, count: integer, price: integer))",
             "Der Held hat an den Händler verkauft.",
             "Ereignisse",
             {}});
}
} // namespace g7
