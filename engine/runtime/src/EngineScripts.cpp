// Scripts in the engine (M7 part C): the Lua VM of the game session loads game/scripts (VFS "scripts/"), gets
// the engine's functions (insert, teleport, time, where) and events (world_loaded), runs its timers in the
// fixed step, reloads changed scripts in development builds and has a console (action "console", ^).

#include "PlayerFigure.hpp"

#include <g7/asset/Procedural.hpp>
#include <g7/core/Log.hpp>
#include <g7/gameplay/ScriptContent.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/script/ScriptVm.hpp>
#include <g7/world/StartPoints.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>

namespace g7
{
namespace
{
constexpr std::string_view kScriptRoot = "scripts";
constexpr i32 kScriptCopyPriority = -20; // the copy next to the game (gothar_data)
constexpr i32 kScriptDevPriority = -10;  // the repository's game/scripts (development builds) wins over it
constexpr std::string_view kDefaultNpcFigure = "characters/figures/laborer.figure.toml";
constexpr std::string_view kHumanGraph = "data/anim/human.animgraph.toml";
constexpr usize kConsoleLines = 200;
constexpr f64 kReloadCheckSeconds = 1.0;
/// Hot reload of scripts: on in development (debug) builds, off in release builds ([assets] hot_reload).
#ifdef NDEBUG
constexpr bool kScriptHotReloadDefault = false;
#else
constexpr bool kScriptHotReloadDefault = true;
#endif

} // namespace

void Engine::mountScripts()
{
    // In development builds the repository's game/scripts alone: the copy next to the game keeps files
    // deleted or renamed since (or from another branch), and every script found is loaded. Otherwise the copy
    // (gothar_data).
#if defined(G7_DEV_SCRIPT_ROOT)
    std::error_code devError;
    if (m_config.settings.get<bool>("assets.dev_mounts", true) &&
        std::filesystem::is_directory(fs::fromUtf8(G7_DEV_SCRIPT_ROOT), devError))
    {
        (void)m_vfs.mount(fs::fromUtf8(G7_DEV_SCRIPT_ROOT), kScriptDevPriority, std::string(kScriptRoot));
        return;
    }
#endif
    const fs::Path copy = fs::baseDirectories().gameDir / "scripts";
    std::error_code ec;
    if (std::filesystem::is_directory(copy, ec))
    {
        (void)m_vfs.mount(copy, kScriptCopyPriority, std::string(kScriptRoot));
    }
}

Result<void> Engine::initScripts()
{
    script::ScriptConfig config;
    config.readFile = [this](std::string_view path) -> Result<std::string>
    {
        auto bytes = m_vfs.read(std::string(kScriptRoot) + "/" + std::string(path));
        if (!bytes)
        {
            return bytes.error();
        }
        return std::string(bytes.value().begin(), bytes.value().end());
    };
    config.listFiles = [this]
    {
        std::vector<std::string> paths;
        for (const asset::VfsFileInfo& file : m_vfs.list(kScriptRoot, ".lua"))
        {
            paths.push_back(file.path.substr(kScriptRoot.size() + 1));
        }
        return paths;
    };
    config.print = [this](std::string_view line)
    {
        G7_LOG_INFO("script", "{}", line);
        consolePrint(std::string(line));
    };
    auto vm = script::ScriptVm::create(std::move(config));
    if (!vm)
    {
        return vm.error();
    }
    m_scripts = std::make_unique<script::ScriptVm>(std::move(vm).value());
    gameplay::defineContentKinds(*m_scripts);
    bindEngineFunctions();
    bindHeroFunctions();
    bindMobFunctions();
    bindUseFunctions();
    bindNpcFunctions();
    bindAiFunctions();
    bindPerceptionFunctions();
    bindDialogFunctions();
    bindTradeFunctions();
    loadVoiceLines();
    bindCombatFunctions();
    bindDiaryFunctions();
    m_scripts->loadAll();
    loadPerceptionSettings(); // data/perception.lua (M9 part C)
    loadDialogPresentation(); // data/dialog.lua (M10 part B)
    loadTradeSettings();      // data/trade.lua (M10 part C)
    loadCombat();             // data/combat.lua (M11)
    buildHero();
    for (const script::ScriptError& e : m_scripts->errors())
    {
        consolePrint("! " + e.text());
    }
    m_scriptStamp = scriptStamp();
    return {};
}

std::string Engine::scriptStamp() const
{
    // Newest change of a script on disk (development: hot reload); empty when they come from archives.
    std::string stamp;
    for (const asset::VfsFileInfo& file : m_vfs.list(kScriptRoot, ".lua"))
    {
        if (const auto disk = m_vfs.diskPath(file.path))
        {
            std::error_code ec;
            const auto time = std::filesystem::last_write_time(*disk, ec);
            stamp += std::format("{}:{};", file.path, ec ? 0 : time.time_since_epoch().count());
        }
    }
    return stamp;
}

void Engine::reloadScripts()
{
    // Definitions anew; the story variables stay (what a script sets at load is overwritten by the old
    // values).
    const script::Value story = m_scripts ? m_scripts->story() : script::Value();
    if (auto loaded = initScripts(); !loaded)
    {
        G7_LOG_WARN("engine", "script reload failed: {}", loaded.error().message);
        return;
    }
    const script::Table* old = story.asTable();
    const script::Value current = m_scripts->story();
    if (old != nullptr && current.asTable() != nullptr)
    {
        auto merged = std::make_shared<script::Table>(*current.asTable());
        for (const auto& [name, value] : old->fields)
        {
            merged->fields[name] = value;
        }
        (void)m_scripts->setStory(script::Value(std::move(merged)));
    }
    loadPerceptionSettings();
    loadDialogPresentation();
    loadTradeSettings();
    m_scripts->emit("scripts_reloaded");
    G7_LOG_INFO("engine", "scripts reloaded");
    consolePrint("(scripts reloaded)");
}

void Engine::updateScripts(f64 realSeconds)
{
    if (!m_scripts || !m_config.settings.get<bool>("assets.hot_reload", kScriptHotReloadDefault))
    {
        return;
    }
    m_scriptReloadTimer += realSeconds;
    if (m_scriptReloadTimer < kReloadCheckSeconds)
    {
        return;
    }
    m_scriptReloadTimer = 0.0;
    if (std::string stamp = scriptStamp(); stamp != m_scriptStamp)
    {
        reloadScripts();
    }
}

void Engine::bindEngineFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;

    vm.bind({"insert", "insert(instance: string, count?: integer) -> boolean",
             "Setzt ein Item (vor der Spielfigur auf den Boden, `count` Stück nebeneinander) oder einen NPC "
             "(vor die "
             "Spielfigur, ihr zugewandt) in die Welt. Ohne `mesh` erhält ein Item einen Platzhalter nach "
             "`category`.",
             "Welt", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"expects (instance: string, count?: integer)"};
                 }
                 const i64 count = a.size() > 1 ? a[1].asInteger(1) : 1;
                 if (auto inserted =
                         insertInstance(a[0].asString(), static_cast<u32>(std::clamp<i64>(count, 1, 50)));
                     !inserted)
                 {
                     return inserted.error();
                 }
                 return Value(true);
             }});
    vm.bind(
        {"teleport", "teleport(start: string) | teleport(x: number, y: number, z: number)",
         "Setzt die Spielfigur (bzw. ohne Spielfigur die Kamera) auf einen Startpunkt der Welt oder an eine "
         "Position in Metern.",
         "Welt", [this](std::span<const Value> a) -> Result<Value>
         {
             Vec3 feet(0.0f);
             f32 yaw = m_movement.yaw();
             if (a.size() == 1 && a[0].isString())
             {
                 auto start = world::findStartPoint(m_scene, a[0].asString());
                 if (!start)
                 {
                     return Error{std::format("no start point '{}'", a[0].asString())};
                 }
                 const Mat4 m = m_scene.worldMatrix(start.value());
                 feet = Vec3(m[3]);
                 const Vec3 ahead = glm::normalize(Vec3(m * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                 yaw = std::atan2(-ahead.x, -ahead.z);
             }
             else if (a.size() == 3 && a[0].isNumber() && a[1].isNumber() && a[2].isNumber())
             {
                 feet = Vec3(static_cast<f32>(a[0].asNumber()), static_cast<f32>(a[1].asNumber()),
                             static_cast<f32>(a[2].asNumber()));
             }
             else
             {
                 return Error{"expects (start: string) or (x, y, z)"};
             }
             if (m_player.valid())
             {
                 teleportPlayer(feet + Vec3(0.0f, 0.05f, 0.0f), yaw);
             }
             else
             {
                 m_camera.transform.position = feet + Vec3(0.0f, 1.7f, 0.0f);
             }
             return Value(true);
         }});
    vm.bind({"time", "time(hour: integer, minute?: integer)",
             "Stellt die Uhrzeit des Spiels (der Tag bleibt).", "Welt",
             [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isNumber() || a[0].asInteger() < 0 || a[0].asInteger() > 23 ||
                     (a.size() > 1 && (a[1].asInteger() < 0 || a[1].asInteger() > 59)))
                 {
                     return Error{"expects (hour 0-23, minute? 0-59)"};
                 }
                 m_gameTime.setTime(m_gameTime.day(), static_cast<u32>(a[0].asInteger()),
                                    static_cast<u32>(a.size() > 1 ? a[1].asInteger() : 0));
                 return Value();
             }});
    vm.bind({"where", "where() -> {x, y, z, yaw, world, time, day}",
             "Position (Meter) und Blickrichtung (Grad) der Spielfigur bzw. der Kamera, die Welt, der "
             "Spieltag (ab 1) und die "
             "Uhrzeit.",
             "Welt", [this](std::span<const Value>) -> Result<Value>
             {
                 const Vec3 p = m_player.valid() ? m_player.feet() : m_camera.transform.position;
                 const auto minute = static_cast<u32>(m_gameTime.minuteOfDay());
                 const auto rounded = [](f32 v, f64 scale)
                 { return std::round(static_cast<f64>(v) * scale) / scale; };
                 return script::makeTable(
                     {}, {{"x", rounded(p.x, 100.0)},
                          {"y", rounded(p.y, 100.0)},
                          {"z", rounded(p.z, 100.0)},
                          {"yaw", rounded(glm::degrees(m_movement.yaw()), 10.0)},
                          {"world", m_worldPath},
                          {"time", std::format("{:02}:{:02}", minute / 60, minute % 60)},
                          {"day", static_cast<i64>(m_gameTime.day()) + 1}}); // the first day is 1
             }});
    // Events the engine emits (documentation only).
    vm.bind({"world_loaded",
             "on(\"world_loaded\", fn(world: string))",
             "Nachdem eine Welt geladen ist (auch nach einem Weltwechsel); `world` ist ihr Pfad.",
             "Ereignisse",
             {}});
    vm.bind(
        {"scripts_reloaded",
         "on(\"scripts_reloaded\", fn())",
         "Nachdem geänderte Skripte neu geladen wurden (Entwicklung); die Story-Variablen bleiben erhalten.",
         "Ereignisse",
         {}});
}

Result<void> Engine::insertInstance(std::string_view name, u32 count)
{
    // In front of the player (or the camera), on the ground.
    const Vec3 origin =
        m_player.valid() ? m_player.feet() : m_camera.transform.position - Vec3(0.0f, 1.7f, 0.0f);
    Vec3 ahead = m_player.valid() ? gameplay::forwardOf(m_movement.yaw())
                                  : m_camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
    ahead.y = 0.0f;
    ahead = glm::length(ahead) > 1e-3f ? glm::normalize(ahead) : Vec3(0.0f, 0.0f, -1.0f);
    const Vec3 side(-ahead.z, 0.0f, ahead.x);
    const auto ground = [&](Vec3 p)
    {
        if (m_physics.valid())
        {
            // From just above the player's height (under a roof indoors); where the ground in front rises
            // more than that (a hill side, welt 2026-10-04) the ray starts inside it and finds nothing: from
            // above.
            const physics::LayerMask world = physics::layerBit(physics::Layer::World);
            auto hit = m_physics.raycast(p + Vec3(0.0f, 1.5f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 30.0f, world);
            if (!hit)
            {
                hit = m_physics.raycast(p + Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 40.0f, world);
            }
            if (hit)
            {
                p.y = hit->position.y;
            }
        }
        return p;
    };

    if (const script::Instance* item = m_scripts->findInstance("Item", name))
    {
        // Item vobs at runtime ids: in focus, picked up with the action key (M8).
        const f32 yaw = std::atan2(ahead.x, ahead.z);
        for (u32 i = 0; i < count; ++i)
        {
            const Vec3 at =
                ground(origin + ahead * 1.2f + side * (0.25f * (static_cast<f32>(i) - 0.5f * (count - 1))));
            if (auto spawned = spawnItem(name, 1, at, yaw); !spawned)
            {
                return spawned.error();
            }
        }
        ++m_insertedItems;
        G7_LOG_INFO("engine", "inserted {} x {} ({})", count, name, item->fields["name"].asString());
        return {};
    }
    if (const script::Instance* npc = m_scripts->findInstance("Npc", name))
    {
        for (u32 i = 0; i < count; ++i)
        {
            const Vec3 at =
                ground(origin + ahead * 2.5f + side * (1.0f * (static_cast<f32>(i) - 0.5f * (count - 1))));
            const Vec3 toPlayer = origin - at;
            if (auto spawned = spawnNpc(name, at, std::atan2(-toPlayer.x, -toPlayer.z)); !spawned)
            {
                return spawned.error();
            }
        }
        G7_LOG_INFO("engine", "inserted {} x {} ({})", count, name, npc->fields["name"].asString());
        return {};
    }
    return Error{std::format("unknown instance \"{}\" (no Item or Npc of that name)", name)};
}

std::string Engine::figureFromSet(std::string_view set, std::string_view npc)
{
    if (!m_figureSets)
    {
        auto bytes = m_vfs.read("data/figure_sets.toml");
        auto parsed =
            bytes ? Config::parse(std::string_view(reinterpret_cast<const char*>(bytes.value().data()),
                                                   bytes.value().size()),
                                  "data/figure_sets.toml")
                  : Result<Config>(bytes.error());
        m_figureSets = parsed ? std::move(parsed).value() : Config{};
    }
    const auto figures = m_figureSets->get<std::vector<std::string>>(std::format("sets.{}", set), {});
    if (figures.empty())
    {
        G7_LOG_WARN("engine", "{}: no figure set \"{}\" in data/figure_sets.toml", npc, set);
        return {};
    }
    // Stable: the same NPC (instance and number, "npc_citizen#3") gets the same figure every time.
    const std::string key =
        std::format("{}#{}", npc,
                    std::count_if(m_creatures.begin(), m_creatures.end(),
                                  [&](const auto& c) { return c->species.starts_with(npc); }));
    return figures[std::hash<std::string>{}(key) % figures.size()];
}

physics::CharacterDesc Engine::creatureBody(std::string_view species)
{
    physics::CharacterDesc desc; // human (physics.md)
    if (species.empty())
    {
        return desc;
    }
    if (!m_creatureBodies)
    {
        auto bytes = m_vfs.read("data/creatures.toml");
        auto parsed =
            bytes ? Config::parse(std::string_view(reinterpret_cast<const char*>(bytes.value().data()),
                                                   bytes.value().size()),
                                  "data/creatures.toml")
                  : Result<Config>(bytes.error());
        if (!parsed)
        {
            G7_LOG_WARN("engine", "data/creatures.toml: {} (animals get the human capsule)",
                        parsed.error().message);
        }
        m_creatureBodies = parsed ? std::move(parsed).value() : Config{};
    }
    const std::string key(species);
    desc.radius = static_cast<f32>(m_creatureBodies->get<f64>(key + ".radius", desc.radius));
    desc.height = static_cast<f32>(m_creatureBodies->get<f64>(key + ".height", desc.height));
    desc.stepHeight = static_cast<f32>(m_creatureBodies->get<f64>(key + ".step_height", desc.stepHeight));
    desc.maxSlopeDegrees =
        static_cast<f32>(m_creatureBodies->get<f64>(key + ".max_slope_degrees", desc.maxSlopeDegrees));
    return desc;
}

Result<u32> Engine::spawnNpc(std::string_view name, const Vec3& at, f32 yaw)
{
    const script::Instance* npc = m_scripts ? m_scripts->findInstance("Npc", name) : nullptr;
    if (npc == nullptr)
    {
        return Error{std::format("no Npc \"{}\"", name)};
    }
    // Animals are Npcs too (M9 part D, like Gothic's monsters): `species` picks their figure and graph.
    const std::string species =
        npc->fields["species"].isString() ? std::string(npc->fields["species"].asString()) : std::string();
    // The figure: its own manifest, or one of a set for nameless people (figure_set, data/figure_sets.toml),
    // chosen by the NPC's name so that it looks the same each time; the default one if neither is there.
    std::string figure(npc->fields["figure"].isString() ? npc->fields["figure"].asString() : "");
    if (figure.empty() && npc->fields["figure_set"].isString())
    {
        figure = figureFromSet(npc->fields["figure_set"].asString(), name);
    }
    if (figure.empty() || !m_vfs.exists(figure))
    {
        if (!figure.empty())
        {
            G7_LOG_WARN("engine", "{}: no figure {}, the default one stands in", name, figure);
        }
        figure = std::string(kDefaultNpcFigure);
    }
    auto spawned =
        species.empty() ? spawnAnimated(name, figure, kHumanGraph, at, yaw) : spawnCreature(species, at, yaw);
    if (!spawned)
    {
        return Error{std::format("{}: {}", name, spawned.error().message)};
    }
    Creature* c = creature(spawned.value());
    // Its name for scripts: the instance; the second and further ones of the same instance "name#2" ...
    // (packs).
    u32 same = 0;
    for (const auto& other : m_creatures)
    {
        const std::string_view base = std::string_view(other->species).substr(0, other->species.find('#'));
        same += other.get() != c && other->character && base == name ? 1u : 0u;
    }
    c->species = same == 0 ? std::string(name) : std::format("{}#{}", name, same + 1);
    // A capsule to walk with (M9): NPCs collide and follow the ground like the hero.
    if (m_physics.valid())
    {
        physics::CharacterDesc body = creatureBody(species);
        body.userData = 0;
        if (auto controller = physics::CharacterController::create(m_physics, body, at))
        {
            c->body = std::move(controller).value();
        }
    }
    // Its values and inventory (pickpocketing, M8 part D); a broken instance only loses those.
    if (auto character = gameplay::Character::fromInstance(*npc, itemLookup()))
    {
        c->character = std::make_unique<gameplay::Character>(std::move(character).value());
    }
    else
    {
        G7_LOG_WARN("engine", "{}", character.error().message);
    }
    // Its senses (M9 part C): the defaults of the scripts, `senses` of the instance overrides them.
    c->sight = m_perception.sight;
    c->sightCos = std::cos(glm::radians(m_perception.angle * 0.5f));
    if (const script::Table* senses = npc->fields["senses"].asTable())
    {
        c->sight = static_cast<f32>(senses->field("sight").asNumber(c->sight));
        c->sightCos = std::cos(
            glm::radians(static_cast<f32>(senses->field("angle").asNumber(m_perception.angle)) * 0.5f));
        c->hearing = static_cast<f32>(senses->field("hearing").asNumber(1.0));
    }
    c->perceptionTimer = static_cast<f32>(c->id % 5) * 0.04f; // staggered
    // Its daily routine (M9 part B), if the instance names one.
    if (npc->fields["routine"].isString())
    {
        c->routine = std::string(npc->fields["routine"].asString());
    }
    return spawned.value();
}

void Engine::setConsoleOpen(bool open) noexcept
{
    m_consoleOpen = open;
    m_consoleFocus = open;
}

void Engine::consolePrint(std::string line)
{
    m_consoleLines.push_back(std::move(line));
    if (m_consoleLines.size() > kConsoleLines)
    {
        m_consoleLines.erase(m_consoleLines.begin(),
                             m_consoleLines.begin() +
                                 static_cast<std::ptrdiff_t>(m_consoleLines.size() - kConsoleLines));
    }
}

Result<script::Value> Engine::runConsoleLine(std::string_view line)
{
    consolePrint(std::format("> {}", line));
    if (!m_scripts)
    {
        consolePrint("! no scripts");
        return Error{"no scripts"};
    }
    if (m_consoleHistory.empty() || m_consoleHistory.back() != line)
    {
        m_consoleHistory.emplace_back(line);
    }
    auto result = m_scripts->runString(line, "console");
    if (!result)
    {
        consolePrint("! " + result.error().message);
        return result;
    }
    if (!result.value().isNil())
    {
        consolePrint(result.value().toString());
    }
    return result;
}

const std::vector<std::string>& Engine::consoleLines() const noexcept
{
    return m_consoleLines;
}

const script::ScriptVm* Engine::scripts() const noexcept
{
    return m_scripts.get();
}

std::string Engine::scriptApiMarkdown() const
{
    return m_scripts ? m_scripts->apiMarkdown() : std::string();
}

void Engine::consoleUi()
{
    ui::ConsolePanel panel;
    panel.lines = m_consoleLines;
    panel.history = m_consoleHistory;
    panel.focus = m_consoleFocus;
    m_consoleFocus = false;
    m_debugUi.consolePanel(panel);
    if (!panel.submitted.empty())
    {
        (void)runConsoleLine(panel.submitted);
    }
    if (!panel.open)
    {
        m_consoleOpen = false;
    }
}
} // namespace g7
