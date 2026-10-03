#include <g7/core/Log.hpp>
#include <g7/physics/Character.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/walk/Autopilot.hpp>
#include <g7/world/Components.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Triggers.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <format>
#include <fstream>
#include <set>

namespace g7::walk
{
namespace
{
using Json = nlohmann::ordered_json;

constexpr f32 kActionDistance = 2.0f; ///< a jump fires this close to its point; a climb waits for the wall
constexpr f64 kAtWallSeconds = 0.25;  ///< a climb fires once the player stood still this long in reach
constexpr f64 kStuckSeconds = 2.0;    ///< no progress for this long: stuck
constexpr f32 kProgress = 0.2f;       ///< metres closer that count as progress
constexpr f32 kReportedFall = 1.0f;   ///< falls from this height on are logged

/// Centimetres are enough; in double so the log shows 40.95, not 40.950000762939453.
f64 round2(f64 x)
{
    return std::round(x * 100.0) / 100.0;
}

Json vec(const Vec3& v)
{
    const auto r = [](f32 x) { return round2(x); };
    return Json::array({r(v.x), r(v.y), r(v.z)});
}

/// What the player does right now, as the log names it.
std::string stateOf(const Engine& engine)
{
    if (engine.playerClimbing())
    {
        return "climb";
    }
    switch (engine.playerWaterMode())
    {
    case gameplay::WaterMode::Swim:
        return "swim";
    case gameplay::WaterMode::Dive:
        return "dive";
    default:
        break;
    }
    switch (engine.player()->state())
    {
    case physics::MoveState::Slide:
        return "slide";
    case physics::MoveState::Air:
        return "air";
    default:
        return "ground";
    }
}
} // namespace

struct Autopilot::Impl
{
    Route route;
    fs::Path outDir;
    std::ofstream log;
    usize index = 0;
    bool started = false;
    bool finished = false;
    bool actionDone = false;
    f64 progressTime = 0.0;
    f32 progressDistance = 1e30f;
    std::string state = "ground";
    std::string worldPath;
    std::set<u64> insideTriggers;
    u64 landingTick = 0;
    Vec3 lastFeet{0.0f};
    f32 travelled = 0.0f;
    f32 nextShotAt = 0.0f;
    u32 autoShots = 0;
    // Summary
    u32 reached = 0;
    std::vector<std::string> skipped;
    u32 stuck = 0;
    u32 falls = 0;
    f32 highestFall = 0.0f;
    f32 fallDamage = 0.0f;

    void write(Engine& engine, std::string_view event, Json fields = Json::object())
    {
        Json line;
        line["event"] = event;
        line["t"] = std::round(engine.simulationTime() * 100.0) / 100.0;
        for (auto& [key, value] : fields.items())
        {
            line[key] = value;
        }
        log << line.dump() << '\n';
        log.flush();
    }

    void screenshot(Engine& engine, const std::string& name)
    {
        if (engine.renderDevice() == nullptr)
        {
            return; // --no-render: no pictures
        }
        engine.requestScreenshot(outDir / fs::fromUtf8(name + ".png"));
    }

    void reachedPoint(Engine& engine, const RoutePoint& point, bool teleported)
    {
        ++reached;
        write(engine, "reached",
              {{"name", point.name},
               {"pos", vec(engine.player()->feet())},
               {"state", stateOf(engine)},
               {"teleport", teleported}});
        if (point.screenshot)
        {
            screenshot(engine, point.name);
        }
        next(engine);
    }

    void next(Engine& engine)
    {
        ++index;
        actionDone = false;
        progressTime = engine.simulationTime();
        progressDistance = 1e30f;
    }

    void finish(Engine& engine, bool timedOut)
    {
        if (finished)
        {
            return;
        }
        finished = true;
        for (usize i = index; i < route.points.size(); ++i)
        {
            skipped.push_back(route.points[i].name);
        }
        if (timedOut)
        {
            write(engine, "timeout",
                  {{"point", index < route.points.size() ? route.points[index].name : ""}});
        }
        Json summary;
        summary["points"] = route.points.size();
        summary["reached"] = reached;
        summary["skipped"] = skipped;
        summary["stuck"] = stuck;
        summary["falls"] = falls;
        summary["highestFall"] = round2(highestFall);
        summary["fallDamage"] = round2(fallDamage);
        summary["drownDamage"] = round2(engine.drownDamage());
        summary["seconds"] = std::round(engine.simulationTime() * 10.0) / 10.0;
        summary["metres"] = std::round(travelled);
        summary["timedOut"] = timedOut;
        std::ofstream(outDir / "walk_summary.json") << summary.dump(2) << '\n';
        G7_LOG_INFO("walk", "route done: {} of {} points reached, {} stuck, {} falls ({} m highest)", reached,
                    route.points.size(), stuck, falls, highestFall);
        engine.setPlayerInputOverride(std::nullopt);
        engine.requestQuit();
    }

    /// Name of the vob in front of the player that stops it (for "stuck").
    std::string blocker(Engine& engine, const Vec3& direction)
    {
        const Vec3 chest = engine.player()->feet() + Vec3(0.0f, 0.9f, 0.0f);
        const auto hit = engine.physics().sphereCast(chest, 0.25f, direction, 1.5f,
                                                     physics::layerBit(physics::Layer::World));
        if (!hit)
        {
            const auto low =
                engine.physics().sphereCast(engine.player()->feet() + Vec3(0.0f, 0.3f, 0.0f), 0.25f,
                                            direction, 1.5f, physics::layerBit(physics::Layer::World));
            if (!low)
            {
                return "unknown";
            }
            return low->userData == 0 ? "terrain" : vobName(engine, low->userData);
        }
        return hit->userData == 0 ? "terrain" : vobName(engine, hit->userData);
    }

    static std::string vobName(Engine& engine, u64 id)
    {
        const entt::entity e = engine.scene().findById(world::VobId{id});
        const world::Vob* vob = e != entt::null ? engine.scene().get<world::Vob>(e) : nullptr;
        return vob != nullptr && !vob->nameText.empty() ? vob->nameText : std::format("vob {}", id);
    }

    void observe(Engine& engine)
    {
        // State changes, landings, triggers, world changes, distance for regular screenshots.
        const std::string now = stateOf(engine);
        if (now != state)
        {
            const auto swimming = [](const std::string& s) { return s == "swim" || s == "dive"; };
            if (now == "slide" || state == "slide")
            {
                write(engine, now == "slide" ? "slide_start" : "slide_end",
                      {{"pos", vec(engine.player()->feet())}});
            }
            if (swimming(now) != swimming(state))
            {
                write(engine, swimming(now) ? "swim_start" : "swim_end",
                      {{"pos", vec(engine.player()->feet())}});
            }
            if (now == "climb")
            {
                write(engine, "climb", {{"pos", vec(engine.player()->feet())}});
            }
            state = now;
        }
        if (const auto& landing = engine.lastLanding(); landing && landing->tick != landingTick)
        {
            landingTick = landing->tick;
            if (landing->height >= kReportedFall)
            {
                ++falls;
                highestFall = std::max(highestFall, landing->height);
                fallDamage += landing->damage;
                write(engine, "fall",
                      {{"height", round2(landing->height)},
                       {"damage", round2(landing->damage)},
                       {"water", landing->intoWater},
                       {"pos", vec(landing->feet)}});
            }
        }
        std::set<u64> inside;
        engine.scene().each<world::Vob, world::TriggerVolume>(
            [&](entt::entity, const world::Vob& vob, const world::TriggerVolume&)
            {
                if (engine.triggers().isInside(vob.id, Engine::kCameraProbe))
                {
                    inside.insert(vob.id.value);
                    if (!insideTriggers.contains(vob.id.value))
                    {
                        write(engine, "trigger", {{"name", vob.nameText}});
                    }
                }
            });
        insideTriggers = std::move(inside);
        if (engine.worldPath() != worldPath)
        {
            if (!worldPath.empty())
            {
                write(engine, "world_change", {{"world", engine.worldPath()}});
            }
            worldPath = engine.worldPath();
            insideTriggers.clear();
        }
        const Vec3 feet = engine.player()->feet();
        const f32 moved = glm::length(Vec2(feet.x - lastFeet.x, feet.z - lastFeet.z));
        if (moved < 5.0f) // not a teleport or level change
        {
            travelled += moved;
        }
        lastFeet = feet;
        if (route.screenshotEveryM > 0.0f && travelled >= nextShotAt)
        {
            nextShotAt = travelled + route.screenshotEveryM;
            const std::string name = std::format("auto_{:04}", ++autoShots);
            write(engine, "screenshot", {{"file", name + ".png"}, {"metres", std::round(travelled)}});
            screenshot(engine, name);
        }
    }

    void step(Engine& engine)
    {
        const physics::CharacterController* player = engine.player();
        if (player == nullptr)
        {
            G7_LOG_ERROR("walk", "no player in this world (a start point is needed)");
            write(engine, "error", {{"message", "no player"}});
            finish(engine, false);
            return;
        }
        if (!started)
        {
            started = true;
            worldPath = engine.worldPath();
            lastFeet = player->feet();
            progressTime = engine.simulationTime();
            write(engine, "start",
                  {{"world", worldPath}, {"pos", vec(player->feet())}, {"points", route.points.size()}});
        }
        observe(engine);
        if (index >= route.points.size())
        {
            finish(engine, false);
            return;
        }
        if (engine.simulationTime() > route.timeLimit)
        {
            finish(engine, true);
            return;
        }
        const RoutePoint& point = route.points[index];
        const Vec3 feet = player->feet();
        if (point.teleport && stateOf(engine) == "air")
        {
            return; // let a fall end (and be logged) before putting the player elsewhere
        }
        if (point.teleport)
        {
            f32 y = 0.0f;
            if (point.y)
            {
                y = *point.y;
            }
            else if (const auto ground = engine.physics().raycast(Vec3(point.pos.x, 5000.0f, point.pos.y),
                                                                  Vec3(0.0f, -1.0f, 0.0f), 10000.0f,
                                                                  physics::layerBit(physics::Layer::World)))
            {
                y = ground->position.y;
            }
            else
            {
                write(engine, "teleport_failed", {{"name", point.name}, {"reason", "no ground below"}});
                skipped.push_back(point.name);
                next(engine);
                return;
            }
            engine.teleportPlayer(Vec3(point.pos.x, y + 0.02f, point.pos.y), engine.playerMovement().yaw());
            lastFeet = Vec3(point.pos.x, y, point.pos.y);
            reachedPoint(engine, point, true);
            return;
        }
        const Vec2 toPoint = point.pos - Vec2(feet.x, feet.z);
        const f32 distance = glm::length(toPoint);
        if (distance <= point.radius)
        {
            reachedPoint(engine, point, false);
            return;
        }
        // Progress or stuck.
        if (distance < progressDistance - kProgress)
        {
            progressDistance = distance;
            progressTime = engine.simulationTime();
        }
        else if (engine.simulationTime() - progressTime > kStuckSeconds && !engine.playerClimbing())
        {
            ++stuck;
            const Vec3 direction = glm::normalize(Vec3(toPoint.x, 0.0f, toPoint.y));
            write(engine, "stuck",
                  {{"name", point.name},
                   {"pos", vec(feet)},
                   {"vob", blocker(engine, direction)},
                   {"state", stateOf(engine)}});
            skipped.push_back(point.name);
            next(engine);
            return;
        }
        if (!engine.playerClimbing())
        {
            engine.steerPlayer(std::atan2(-toPoint.x, -toPoint.y));
        }
        gameplay::MoveInput input;
        input.forward = 1.0f;
        const Gait gait = point.gait.value_or(route.gait);
        input.walk = gait == Gait::Walk;
        input.sneak = gait == Gait::Sneak && engine.playerWaterMode() == gameplay::WaterMode::Land;
        const bool atWall = engine.simulationTime() - progressTime > kAtWallSeconds;
        const bool fire = point.action == RoutePoint::Action::Jump
                              ? distance < kActionDistance
                              : distance < kActionDistance + 0.5f && atWall;
        if (point.action != RoutePoint::Action::None && !actionDone && fire)
        {
            actionDone = true;
            input.jump = true;
            write(engine, point.action == RoutePoint::Action::Jump ? "jump" : "climb_try",
                  {{"name", point.name}, {"pos", vec(feet)}});
        }
        engine.setPlayerInputOverride(input);
    }
};

Autopilot::Autopilot(Route route, fs::Path outDir) : m_impl(std::make_unique<Impl>())
{
    m_impl->route = std::move(route);
    m_impl->outDir = std::move(outDir);
    std::error_code ec;
    std::filesystem::create_directories(m_impl->outDir, ec);
    m_impl->log.open(m_impl->outDir / "walk.jsonl", std::ios::trunc);
    if (!m_impl->log)
    {
        G7_LOG_ERROR("walk", "cannot write {}", fs::toUtf8(m_impl->outDir / "walk.jsonl"));
    }
}

Autopilot::~Autopilot() = default;

void Autopilot::update(Engine& engine, f64, bool, bool)
{
    if (!m_impl->finished)
    {
        m_impl->step(engine);
    }
}

void Autopilot::ui(Engine&, ui::DebugUi&)
{
}

bool Autopilot::finished() const noexcept
{
    return m_impl->finished;
}
} // namespace g7::walk
