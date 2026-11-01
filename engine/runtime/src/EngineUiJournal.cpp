// Character, diary and map screens in the engine (M14 part E): data from the hero and the Story table, the
// map item's picture (owner decision F9: a town map from welt, no player position), the keys status/log/map.

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Log.hpp>
#include <g7/render/TextureUpload.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/JournalScreens.hpp>

#include <format>

namespace g7
{
ui::StatusView Engine::statusView()
{
    ui::StatusView view;
    if (!m_hero || !m_gameUi)
    {
        return view;
    }
    const gameplay::Character& hero = *m_hero;
    const ui::Strings& strings = m_gameUi->strings();
    const auto label = [&](std::string_view prefix, std::string_view name)
    {
        const std::string key = std::string(prefix) + toUpper(name);
        return strings.contains(key) ? std::string(strings.get(key)) : std::string(name);
    };
    const std::string language = strings.language();
    view.name = contentText(hero.name());
    if (!hero.guild().empty() && hero.guild() != "none")
    {
        // The guild's name from Guilds (data/guilds.lua), in the UI language.
        const script::Value guilds = m_scripts ? m_scripts->global("Guilds") : script::Value();
        const std::string_view name = guilds[hero.guild()]["name"].asString();
        view.guild = name.empty() ? hero.guild() : contentText(name);
    }
    view.general = {
        {std::string(strings.get("UI_STATUS_LEVEL")), std::to_string(hero.level())},
        {std::string(strings.get("UI_STATUS_EXPERIENCE")),
         std::format("{} / {}", ui::formatNumber(hero.experience(), language),
                     ui::formatNumber(xpForLevel(hero.level() + 1), language))},
        {std::string(strings.get("UI_STATUS_LEARN_POINTS")), std::to_string(hero.learnPoints())},
    };
    view.attributes = {
        {label("UI_REQUIRE_", "hp_max"),
         std::format("{} / {}", hero.attribute("hp"), hero.attribute("hp_max"))},
        {label("UI_REQUIRE_", "mana_max"),
         std::format("{} / {}", hero.attribute("mana"), hero.attribute("mana_max"))},
        {label("UI_REQUIRE_", "str"), std::to_string(hero.attribute("str"))},
        {label("UI_REQUIRE_", "dex"), std::to_string(hero.attribute("dex"))},
    };
    for (const std::string_view talent : gameplay::kTalents)
    {
        const i32 level = hero.talent(talent);
        view.talents.emplace_back(label("UI_TALENT_", talent), level > 0 ? std::to_string(level) : "–");
    }
    for (const std::string_view type : gameplay::kDamageTypes)
    {
        if (type != "fall")
        {
            view.protection.emplace_back(label("UI_DAMAGE_", type), std::to_string(hero.protection(type)));
        }
    }
    return view;
}

ui::DiaryView Engine::diaryView() const
{
    const DiaryData panel = diaryData();
    ui::DiaryView view;
    view.chapter = panel.chapter;
    const auto copy = [](const std::vector<DiaryData::Topic>& from, std::vector<ui::DiaryView::Topic>& to)
    {
        for (const DiaryData::Topic& q : from)
        {
            to.push_back({q.name, q.entries});
        }
    };
    copy(panel.running, view.running);
    copy(panel.done, view.done);
    copy(panel.failed, view.failed);
    copy(panel.notes, view.notes);
    return view;
}

std::optional<std::pair<std::string, std::string>> Engine::heroMapItem() const
{
    if (!m_hero || !m_scripts)
    {
        return std::nullopt;
    }
    for (const gameplay::ItemStack& stack : m_hero->inventory(itemLookup()))
    {
        const script::Instance* item = m_scripts->findInstance("Item", stack.item);
        if (item != nullptr && !item->fields["map"].asString().empty())
        {
            return std::pair{contentText(item->fields["name"].asString()),
                             std::string(item->fields["map"].asString())};
        }
    }
    return std::nullopt;
}

Result<void> Engine::openMap(std::string title, std::string image)
{
    if (!m_vfs.exists(image))
    {
        return Error{"map image not found: " + image};
    }
    m_map = MapRequest{std::move(title), std::move(image)};
    return {};
}

void Engine::syncJournalScreens()
{
    const auto keep =
        [&](std::string_view name, bool open, const std::function<std::unique_ptr<ui::Screen>()>& make)
    {
        ui::Screen* screen = m_screens.find(name);
        if (!open && screen != nullptr)
        {
            screen->close();
        }
        else if (open && (screen == nullptr || screen->closed()))
        {
            m_screens.push(make());
        }
    };
    keep("status", m_statusOpen && m_hero != nullptr,
         [this]
         {
             return std::make_unique<ui::StatusScreen>([this] { return statusView(); },
                                                       [this] { m_statusOpen = false; });
         });
    keep("diary", m_diaryOpen,
         [this]
         {
             return std::make_unique<ui::DiaryScreen>([this] { return diaryView(); },
                                                      [this] { m_diaryOpen = false; });
         });

    // The map: its picture is loaded when it opens (and kept while the same map is shown).
    auto* map = dynamic_cast<ui::MapScreen*>(m_screens.find("map"));
    if (!m_map && map != nullptr)
    {
        map->close();
    }
    if (m_map && (map == nullptr || map->closed()))
    {
        if (m_mapTexturePath != m_map->image)
        {
            m_mapTexture.reset();
            m_mapTexturePath = m_map->image;
            auto bytes = m_vfs.read(m_map->image);
            auto image = bytes ? asset::decodeImage(bytes.value(), m_map->image)
                               : Result<asset::ImageData>(bytes.error());
            if (!image)
            {
                G7_LOG_WARN("ui", "map {}: {}", m_map->image, image.error().message);
            }
            else
            {
                m_mapSize =
                    Vec2(static_cast<f32>(image.value().width), static_cast<f32>(image.value().height));
                if (m_device)
                {
                    auto texture = render::createTexture(
                        *m_device, asset::textureFromImage(std::move(image).value(), true), true);
                    if (texture)
                    {
                        m_mapTexture = std::move(texture).value();
                    }
                }
            }
        }
        m_screens.push(std::make_unique<ui::MapScreen>(m_map->title, m_mapTexture ? &*m_mapTexture : nullptr,
                                                       m_mapSize, [this] { m_map.reset(); }));
    }
}

void Engine::journalKeys()
{
    // The keys that open these screens close them again (Gothic: B, N, M), though a screen holds the game
    // keys.
    ui::Screen* top = m_screens.top();
    const auto pressed = [&](platform::Action action) { return m_actions.pressed(m_input, action); };
    if (top != nullptr && top->name() == "status" && pressed(platform::Action::Status))
    {
        m_statusOpen = false;
    }
    else if (top != nullptr && top->name() == "diary" && pressed(platform::Action::Log))
    {
        m_diaryOpen = false;
    }
    else if (top != nullptr && top->name() == "map" && pressed(platform::Action::Map))
    {
        m_map.reset();
    }
}

void Engine::openMapOfHero()
{
    if (const auto map = heroMapItem())
    {
        if (auto opened = openMap(map->first, map->second); !opened)
        {
            G7_LOG_WARN("ui", "{}", opened.error().message);
        }
    }
    else if (m_gameUi)
    {
        showScreenMessage(uiText("UI_MAP_NONE"));
    }
}

void Engine::bindJournalFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"status_open", "status_open(open?: boolean)",
             "Öffnet (oder mit `false` schließt) den Charakterbildschirm wie die Taste status (B).",
             "Bildschirm", [this](std::span<const script::Value> a) -> Result<script::Value>
             {
                 m_statusOpen = a.empty() || a[0].asBool(true);
                 return script::Value{};
             }});
    vm.bind(
        {"map_open", "map_open(image: string, title?: string)",
         "Zeigt eine Karte (Bild im VFS, z. B. `ui/maps/leonberg.png`) wie ein Karten-Item (Konsole, Tests).",
         "Bildschirm", [this](std::span<const script::Value> a) -> Result<script::Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"argument 1 must be the image path"};
             }
             auto opened = openMap(a.size() > 1 ? std::string(a[1].asString()) : std::string(),
                                   std::string(a[0].asString()));
             if (!opened)
             {
                 return opened.error();
             }
             return script::Value{};
         }});
}
} // namespace g7
