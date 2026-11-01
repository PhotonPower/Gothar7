// The item screens in the engine (M14 part D, F4/F5): inventory, trading and containers as game screens,
// their data from the hero, the trader, the chest or the lying body, and the 3D item pictures of the cells
// (inventory_views.toml v2, rendered into atlas tiles).

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/ItemScreen.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace g7
{
namespace
{
constexpr u32 kCellTilePixels = 128; ///< cell pictures (drawn at about 90-200 px)
constexpr u32 kCellColumns = 8;      ///< 64 different items at once
constexpr u32 kLargeTilePixels = 384;
constexpr std::string_view kViewsPath = "data/inventory_views.toml";

std::string listOf(const ui::Strings& strings, std::string_view prefix, const script::Value& table)
{
    std::string out;
    if (const script::Table* t = table.asTable())
    {
        for (const auto& [name, value] : t->fields)
        {
            if (value.asInteger() == 0)
            {
                continue;
            }
            const std::string key = std::string(prefix) + toUpper(name);
            out +=
                std::format("{}{} {}", out.empty() ? "" : ", ",
                            strings.contains(key) ? std::string(strings.get(key)) : name, value.asInteger());
        }
    }
    return out;
}
} // namespace

std::vector<std::string> Engine::itemDetails(std::string_view item) const
{
    std::vector<std::string> lines;
    if (!m_gameUi || !m_scripts)
    {
        return lines;
    }
    const ui::Strings& strings = m_gameUi->strings();
    const script::Instance* instance = m_scripts->findInstance("Item", item);
    if (instance == nullptr)
    {
        return lines;
    }
    const script::Value& fields = instance->fields;
    if (const std::string damage = listOf(strings, "UI_DAMAGE_", fields["damage"]); !damage.empty())
    {
        lines.push_back(strings.format("UI_ITEM_DAMAGE", {{"list", damage}}));
    }
    if (const std::string protection = listOf(strings, "UI_DAMAGE_", fields["protection"]);
        !protection.empty())
    {
        lines.push_back(strings.format("UI_ITEM_PROTECTION", {{"list", protection}}));
    }
    if (const std::string requires_ = listOf(strings, "UI_REQUIRE_", fields["requires"]); !requires_.empty())
    {
        lines.push_back(strings.format("UI_ITEM_REQUIRES", {{"list", requires_}}));
    }
    if (fields["value"].asInteger() > 0)
    {
        lines.push_back(strings.format(
            "UI_ITEM_VALUE", {{"value", ui::formatNumber(fields["value"].asInteger(), strings.language())}}));
    }
    return lines;
}

ui::ItemScreenView Engine::itemScreenView(ui::ItemScreenMode mode)
{
    ui::ItemScreenView view;
    if (!m_hero || !m_gameUi)
    {
        return view;
    }
    const ui::Strings& strings = m_gameUi->strings();
    const gameplay::ItemLookup items = itemLookup();
    const auto entries = [&](const gameplay::Character* who, bool heroSide, std::vector<ui::ItemEntry>& out)
    {
        if (who == nullptr)
        {
            return;
        }
        for (const gameplay::ItemStack& stack : who->inventory(items))
        {
            if (mode == ui::ItemScreenMode::Trade && stack.item == m_tradeSettings.currency)
            {
                continue; // the money is in the footer
            }
            const auto info = items(stack.item);
            ui::ItemEntry e;
            e.item = stack.item;
            e.name = info ? contentText(info->name) : stack.item;
            e.category = info ? info->category : "misc";
            e.count = stack.count;
            if (heroSide)
            {
                for (usize s = 0; s < static_cast<usize>(gameplay::EquipSlot::Count); ++s)
                {
                    e.equipped =
                        e.equipped || m_hero->equipped(static_cast<gameplay::EquipSlot>(s)) == stack.item;
                }
            }
            e.details = itemDetails(stack.item);
            if (e.equipped)
            {
                e.details.insert(e.details.begin(), std::string(strings.get("UI_ITEM_EQUIPPED")));
            }
            if (mode == ui::ItemScreenMode::Trade)
            {
                e.price = tradePrice(stack.item, !heroSide);
            }
            out.push_back(std::move(e));
        }
    };
    entries(m_hero.get(), true, view.hero.items);
    view.hero.title = strings.format("UI_INVENTORY_TITLE", {{"name", contentText(m_hero->name())}});
    view.hero.footer = strings.format(
        "UI_MONEY",
        {{"amount", ui::formatNumber(m_hero->itemCount(m_tradeSettings.currency), strings.language())}});
    view.message = m_inventoryMessage;
    if (mode == ui::ItemScreenMode::Trade)
    {
        gameplay::Character* seller = trader();
        entries(seller, false, view.other.items);
        const script::Instance* npc = m_scripts->findInstance("Npc", m_dialog ? m_dialog->npcName : "");
        view.other.title = npc != nullptr ? contentText(npc->fields["name"].asString()) : std::string();
        if (seller != nullptr)
        {
            view.other.footer = strings.format(
                "UI_MONEY", {{"amount", ui::formatNumber(seller->itemCount(m_tradeSettings.currency),
                                                         strings.language())}});
        }
    }
    else if (mode == ui::ItemScreenMode::Container)
    {
        const Creature* looted = m_lootTarget ? creature(*m_lootTarget) : nullptr;
        if (looted != nullptr && looted->character)
        {
            entries(looted->character.get(), false, view.other.items);
            view.other.title = std::format("{} ({})", contentText(looted->character->name()),
                                           strings.get(looted->fighter.state() == gameplay::FightState::Dead
                                                           ? "UI_LOOT_DEAD"
                                                           : "UI_LOOT_UNCONSCIOUS"));
        }
        else if (m_mobUse && m_mobUse->containerOpen)
        {
            const MobRuntime& chest = m_mobs.at(m_mobUse->vob.value);
            view.other.title = contentText(chest.name);
            for (const auto& [item, count] : chest.contents)
            {
                const auto info = items(item);
                ui::ItemEntry e;
                e.item = item;
                e.name = info ? contentText(info->name) : item;
                e.category = info ? info->category : "misc";
                e.count = count;
                e.details = itemDetails(item);
                view.other.items.push_back(std::move(e));
            }
        }
    }
    return view;
}

void Engine::syncItemScreens()
{
    if (!m_gameUi)
    {
        return;
    }
    const bool container = (m_mobUse && m_mobUse->containerOpen) || (m_lootTarget && m_inventoryOpen);
    const bool trading = m_trade.has_value() && m_dialog.has_value();
    const bool inventory = m_inventoryOpen && !container && !trading && m_hero;
    const auto want = [&](ui::ItemScreenMode mode, bool open)
    {
        const std::string_view name = mode == ui::ItemScreenMode::Trade       ? "trade"
                                      : mode == ui::ItemScreenMode::Container ? "container"
                                                                              : "inventory";
        ui::Screen* screen = m_screens.find(name);
        if (!open)
        {
            if (screen != nullptr)
            {
                screen->close();
            }
            return;
        }
        if (screen != nullptr && !screen->closed())
        {
            return;
        }
        ui::ItemScreen::Actions actions;
        actions.view = [this, mode] { return itemScreenView(mode); };
        actions.use = [this](const ui::ItemEntry& e) { useInventoryItem(e.item); };
        actions.drop = [this](const ui::ItemEntry& e)
        {
            auto dropped = dropItem(e.item, 1);
            m_inventoryMessage = dropped ? std::string() : dropped.error().message;
        };
        actions.transfer = [this, mode](bool fromOther, const ui::ItemEntry& e, u32 count)
        { transferItem(mode, fromOther, e.item, count); };
        actions.maxTransfer = [this, mode](bool fromOther, const ui::ItemEntry& e) -> u32
        {
            if (mode != ui::ItemScreenMode::Trade || e.price <= 0)
            {
                return e.count;
            }
            // Buying: as many as the hero can pay; selling: as many as the trader can pay.
            const gameplay::Character* payer = fromOther ? m_hero.get() : trader();
            const i64 money = payer != nullptr ? payer->itemCount(m_tradeSettings.currency) : 0;
            return static_cast<u32>(std::clamp<i64>(money / e.price, 0, e.count));
        };
        actions.close = [this, mode]
        {
            if (mode == ui::ItemScreenMode::Trade)
            {
                closeTrade();
            }
            else if (mode == ui::ItemScreenMode::Container && m_mobUse && m_mobUse->containerOpen)
            {
                mobCommand(MobCommand::Leave);
            }
            else
            {
                setInventoryOpen(false);
                m_lootTarget.reset();
            }
        };
        actions.preview = [this](const std::string& item, bool large, bool spinning)
        { return itemPreview(item, large, spinning); };
        m_previewDrawn.clear(); // the pictures of a newly opened screen are fresh
        m_screens.push(std::make_unique<ui::ItemScreen>(mode, std::move(actions),
                                                        ui::itemTabs(m_gameUi->screenLayout())));
    };
    want(ui::ItemScreenMode::Trade, trading);
    want(ui::ItemScreenMode::Container, container);
    want(ui::ItemScreenMode::Inventory, inventory);
}

void Engine::useInventoryItem(const std::string& item)
{
    if (!m_hero)
    {
        return;
    }
    gameplay::Character& hero = *m_hero;
    const gameplay::ItemLookup items = itemLookup();
    for (usize s = 0; s < static_cast<usize>(gameplay::EquipSlot::Count); ++s)
    {
        if (hero.equipped(static_cast<gameplay::EquipSlot>(s)) == item)
        {
            hero.unequip(static_cast<gameplay::EquipSlot>(s));
            m_inventoryMessage.clear();
            return;
        }
    }
    const auto info = items(item);
    const std::string category = info ? info->category : "misc";
    if (gameplay::Character::slotFor(category))
    {
        auto slot = hero.equip(item, items);
        m_inventoryMessage.clear();
        if (!slot)
        {
            // Not strong or skilled enough (the requirements of the item), else just "cannot".
            const script::Instance* def = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
            const std::string needs =
                def != nullptr ? listOf(m_uiStrings, "UI_REQUIRE_", def->fields["requires"]) : std::string();
            m_inventoryMessage =
                needs.empty() ? uiText("UI_CANNOT_DO") : uiText("UI_ITEM_REQUIRES", {{"list", needs}});
        }
        return;
    }
    const u64 notices = m_noticeCount;
    auto used = useItem(item);
    m_inventoryMessage = used ? std::string() : failureMessage(notices);
}

std::string Engine::failureMessage(u64 noticesBefore) const
{
    // The engine's errors are for the log and Lua; the player reads the notice the action gave, if any.
    return m_noticeCount != noticesBefore ? m_notice : uiText("UI_CANNOT_DO");
}

void Engine::transferItem(ui::ItemScreenMode mode, bool fromOther, const std::string& item, u32 count)
{
    const u64 notices = m_noticeCount;
    Result<void> done{};
    if (mode == ui::ItemScreenMode::Trade)
    {
        done = fromOther ? tradeBuy(item, count) : tradeSell(item, count);
    }
    else if (const Creature* looted = m_lootTarget ? creature(*m_lootTarget) : nullptr; looted != nullptr)
    {
        if (fromOther)
        {
            auto taken = loot(looted->species, item, count);
            done = taken ? Result<void>{} : Result<void>(taken.error());
        }
        else
        {
            notice(uiText("UI_LOOT_TAKE_ONLY"));
            done = Error{"only taking"};
        }
    }
    else if (m_mobUse && m_mobUse->containerOpen)
    {
        const world::VobId vob = m_mobUse->vob;
        done = fromOther ? takeFromMob(vob, item, count) : putIntoMob(vob, item, count);
    }
    m_inventoryMessage = done ? std::string() : failureMessage(notices);
}

std::optional<ui::CellFrame> Engine::itemCellFrame(std::string_view item, const LoadedModel& model)
{
    if (const auto it = m_cellFrames.find(item); it != m_cellFrames.end())
    {
        return it->second;
    }
    if (!m_inventoryViewsLoaded)
    {
        m_inventoryViewsLoaded = true;
        if (auto bytes = m_vfs.read(kViewsPath); bytes)
        {
            auto views = ui::InventoryViews::load(
                std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()),
                kViewsPath);
            if (views)
            {
                m_inventoryViews = std::move(views).value();
            }
            else
            {
                G7_LOG_WARN("ui", "{} (inventory cells use the default view)", views.error().message);
            }
        }
    }
    std::optional<ui::CellFrame> frame;
    const asset::MeshData* data = model.source.valid() ? model.source.get() : nullptr;
    if (model.source.valid() && data == nullptr)
    {
        return std::nullopt; // still loading: try again next time
    }
    {
        // LOD 0 only (the contract): the vertices its submeshes index.
        std::vector<u8> used(data != nullptr ? data->vertices.size() : 0, 0);
        bool any = false;
        for (const asset::Submesh& submesh :
             data != nullptr ? data->submeshes : std::vector<asset::Submesh>{})
        {
            if (submesh.lod != 0)
            {
                continue;
            }
            for (u32 i = submesh.firstIndex;
                 i < submesh.firstIndex + submesh.indexCount && i < data->indices.size(); ++i)
            {
                if (data->indices[i] < used.size())
                {
                    used[data->indices[i]] = 1;
                    any = true;
                }
            }
        }
        std::vector<Vec3> positions;
        for (usize i = 0; data != nullptr && i < data->vertices.size(); ++i)
        {
            if (!any || used[i] != 0)
            {
                positions.push_back(data->vertices[i].position);
            }
        }
        if (data == nullptr)
        {
            // A placeholder without CPU data: the corners of its bounds.
            for (int i = 0; i < 8; ++i)
            {
                positions.emplace_back((i & 4) ? model.bounds.max.x : model.bounds.min.x,
                                       (i & 2) ? model.bounds.max.y : model.bounds.min.y,
                                       (i & 1) ? model.bounds.max.z : model.bounds.min.z);
            }
        }
        const script::Instance* instance = m_scripts ? m_scripts->findInstance("Item", item) : nullptr;
        const std::string mesh =
            instance != nullptr ? std::string(instance->fields["mesh"].asString()) : model.name;
        const std::string category = instance != nullptr && instance->fields["category"].isString()
                                         ? std::string(instance->fields["category"].asString())
                                         : std::string("misc");
        const ui::InventoryView view =
            m_inventoryViews ? m_inventoryViews->resolve(mesh, category) : ui::InventoryView{};
        const f32 fov = m_inventoryViews ? m_inventoryViews->fovDegrees() : 30.0f;
        if (auto computed = ui::computeCell(positions, view, fov); computed)
        {
            frame = computed.value();
        }
        else
        {
            G7_LOG_WARN("ui", "inventory cell of {}: {}", item, computed.error().message);
        }
    }
    m_cellFrames.emplace(std::string(item), frame);
    return frame;
}

std::optional<ui::PreviewImage> Engine::itemPreview(const std::string& item, bool large, bool spinning)
{
    if (!m_device || !m_gameUi)
    {
        return std::nullopt;
    }
    if (!m_cellPreviews)
    {
        auto cells = ui::ItemPreviews::create(*m_device, *m_shaders, kCellTilePixels, kCellColumns);
        auto big = ui::ItemPreviews::create(*m_device, *m_shaders, kLargeTilePixels, 1);
        if (!cells || !big)
        {
            G7_LOG_ERROR("ui", "item pictures: {}", cells ? big.error().message : cells.error().message);
            return std::nullopt;
        }
        m_cellPreviews = std::move(cells).value();
        m_bigPreview = std::move(big).value();
    }
    const LoadedModel* model = itemModel(item);
    if (model == nullptr)
    {
        return std::nullopt;
    }
    const auto frame = itemCellFrame(item, *model);
    if (!frame)
    {
        return std::nullopt;
    }
    const ui::PreviewLight& light = m_gameUi->style().itemPreview;
    const f32 angle = spinning ? m_spinAngle : 0.0f;
    if (large)
    {
        m_bigPreview->render(*m_device, m_meshRenderer, 0, model->mesh, model->materials, *frame, angle,
                             light);
        return ui::PreviewImage{&m_bigPreview->texture(), m_bigPreview->uv(0)};
    }
    auto tile = m_previewTiles.find(item);
    if (tile == m_previewTiles.end())
    {
        if (m_previewTiles.size() >= m_cellPreviews->capacity())
        {
            m_previewTiles.clear(); // full: start over (more than 64 different items on screen is rare)
            m_previewDrawn.clear();
        }
        tile = m_previewTiles.emplace(item, static_cast<u32>(m_previewTiles.size())).first;
    }
    if (spinning || !m_previewDrawn.contains(item))
    {
        m_cellPreviews->render(*m_device, m_meshRenderer, tile->second, model->mesh, model->materials, *frame,
                               angle, light);
        // A turning picture must be redrawn upright when it stops turning.
        if (spinning)
        {
            m_previewDrawn.erase(item);
        }
        else
        {
            m_previewDrawn.insert(item);
        }
    }
    return ui::PreviewImage{&m_cellPreviews->texture(), m_cellPreviews->uv(tile->second)};
}

void Engine::advanceItemSpin(f32 seconds)
{
    const f32 period = m_gameUi ? std::max(0.5f, m_gameUi->style().itemSpinSeconds) : 6.0f;
    m_spinAngle = std::fmod(m_spinAngle + seconds * 2.0f * std::numbers::pi_v<f32> / period,
                            2.0f * std::numbers::pi_v<f32>);
}
} // namespace g7
