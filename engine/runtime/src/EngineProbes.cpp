// Reflection probes of the engine (M17 windows part F2, render.md "Spiegelnde Fenster"): placed along the
// waynet when a world is loaded (worlds without one: a grid over the scene), each with an influence box out
// to the facades around it; one face drawn a frame, nearest probe first; all anew every 10 game minutes and
// when the weather changes the sky.

#include <g7/core/Log.hpp>
#include <g7/core/Profiler.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <cmath>

namespace g7
{
namespace
{
constexpr f32 kBoxReach = 25.0f;    // m: how far the influence box reaches at most (no facade hit)
constexpr f32 kBoxAbove = 8.0f;     // m above the probe
constexpr f32 kGridSpacing = 20.0f; // m: probes of worlds without a waynet
constexpr f32 kRedrawHours = 10.0f / 60.0f;
constexpr f32 kRedrawSky = 0.05f;  // change of sky and sun colour (linear) that redraws every probe
constexpr u32 kFreshProbes = 4;    // a new world: these nearest probes are drawn at once
constexpr f32 kSmallObject = 1.5f; // m: smaller things are left out of the faces
} // namespace

void Engine::placeReflectionProbes()
{
    G7_PROFILE_SCOPE("render::probe placement");
    const std::vector<render::IndoorVolume>& rooms = indoorRooms();
    const auto outdoors = [&](const Vec3& p) { return render::indoorAmount(p, rooms, 0.0f) < 0.5f; };
    std::vector<Vec3> junctions;
    std::vector<std::pair<Vec3, Vec3>> segments;
    const auto& points = m_waynet.points();
    for (u32 i = 0; i < points.size(); ++i)
    {
        const auto neighbours = m_waynet.neighbours(i);
        if (neighbours.size() >= 3 && outdoors(points[i].position))
        {
            junctions.push_back(points[i].position);
        }
        for (const u32 j : neighbours)
        {
            if (i < j && outdoors(points[i].position) && outdoors(points[j].position))
            {
                segments.emplace_back(points[i].position, points[j].position);
            }
        }
    }
    std::vector<Vec3> positions = render::placeReflectionProbes(junctions, segments);
    if (points.empty() && m_sceneBounds.max.x >= m_sceneBounds.min.x)
    {
        // No waynet (test scenes): a grid over the scene, on the ground found below.
        for (f32 x = m_sceneBounds.min.x; x <= m_sceneBounds.max.x; x += kGridSpacing)
        {
            for (f32 z = m_sceneBounds.min.z; z <= m_sceneBounds.max.z; z += kGridSpacing)
            {
                const Vec3 top(x, m_sceneBounds.max.y + 1.0f, z);
                if (const auto hit = m_physics.raycast(top, Vec3(0.0f, -1.0f, 0.0f),
                                                       m_sceneBounds.max.y - m_sceneBounds.min.y + 2.0f,
                                                       physics::layerBit(physics::Layer::World));
                    hit && outdoors(hit->position))
                {
                    positions.push_back(hit->position + Vec3(0.0f, render::ProbePlacement{}.height, 0.0f));
                }
            }
        }
    }
    // Influence boxes: out to the first wall along the axes (the facades of the street), from the ground up.
    std::vector<render::ReflectionProbe> probes;
    probes.reserve(positions.size());
    const physics::LayerMask world = physics::layerBit(physics::Layer::World);
    const auto reach = [&](const Vec3& from, const Vec3& direction)
    {
        const auto hit = m_physics.raycast(from, direction, kBoxReach, world);
        return hit ? std::max(hit->distance, 0.5f) : kBoxReach;
    };
    for (const Vec3& p : positions)
    {
        const f32 below = render::ProbePlacement{}.height + 0.5f;
        AABB box;
        box.min = Vec3(p.x - reach(p, Vec3(-1.0f, 0.0f, 0.0f)), p.y - below,
                       p.z - reach(p, Vec3(0.0f, 0.0f, -1.0f)));
        box.max = Vec3(p.x + reach(p, Vec3(1.0f, 0.0f, 0.0f)),
                       p.y + std::min(reach(p, Vec3(0.0f, 1.0f, 0.0f)), kBoxAbove),
                       p.z + reach(p, Vec3(0.0f, 0.0f, 1.0f)));
        probes.push_back({p, box});
    }
    G7_LOG_INFO("engine", "reflection probes: {} ({})", probes.size(),
                points.empty() ? "grid, no waynet" : "along the waynet");
    m_probes.schedule().setProbes(std::move(probes));
}

void Engine::updateReflectionProbes(const render::ShadowFrame* shadows)
{
    G7_PROFILE_SCOPE("render::reflection probes");
    m_probeFacesDrawn = 0;
    m_environment.probes = {};
    m_environment.probeParams = Vec4(0.0f);
    m_meshRenderer.setReflectionProbes(nullptr);
    m_meshRenderer.setScreenHistory(nullptr, nullptr);
    m_environment.ssr = false;
    const bool stage = m_device != nullptr && (m_windowReflections == render::WindowReflections::Medium ||
                                               m_windowReflections == render::WindowReflections::High);
    if (!stage)
    {
        m_probes = {}; // the atlas's memory back
        m_probesDirty = true;
        return;
    }
    const f32 hour = m_gameTime.hourOfDay();
    const Vec4 sky(m_sky.zenith + m_environment.fogColor, m_environment.sunIntensity);
    if (m_probesDirty)
    {
        if (m_physicsDirty && m_paused)
        {
            syncPhysics(); // the editor pauses the simulation, which would sync the collision
        }
        if (m_physicsDirty)
        {
            return; // the influence boxes need the collision
        }
        m_probesDirty = false;
        // Only a world with window glass needs them (until welt's panes come, Leonberg has none).
        m_probeGlass = std::ranges::any_of(m_instances,
                                           [](const SceneInstance& instance)
                                           {
                                               const render::MaterialSet& set = instance.model->materials;
                                               for (usize i = 0; i < set.size(); ++i)
                                               {
                                                   if (set[i].glass.enabled)
                                                   {
                                                       return true;
                                                   }
                                               }
                                               return false;
                                           });
        if (!m_probeGlass)
        {
            m_probes = {};
            return;
        }
        if (!m_probes.ready())
        {
            auto created = render::ReflectionProbes::create(*m_device);
            if (!created)
            {
                G7_LOG_WARN("engine", "reflection probes: {} (window reflections low)",
                            created.error().message);
                m_windowReflections = render::WindowReflections::Low;
                return;
            }
            m_probes = std::move(created).value();
        }
        placeReflectionProbes();
        m_probesFresh = true;
        m_probeHour = hour;
        m_probeSky = sky;
    }
    if (!m_probeGlass || !m_probes.ready())
    {
        return;
    }
    // Anew every 10 game minutes and when the weather changes the sky (the old images meanwhile).
    f32 hours = std::abs(hour - m_probeHour);
    hours = std::min(hours, 24.0f - hours);
    if (hours >= kRedrawHours || glm::length(sky - m_probeSky) > kRedrawSky)
    {
        m_probes.schedule().invalidate();
        m_probeHour = hour;
        m_probeSky = sky;
    }
    const u32 budget = m_probesFresh ? kFreshProbes * render::kProbeFaces : 1u;
    m_probesFresh = false;
    m_probes.schedule().update(m_camera.transform.position, budget, m_probeFaces);
    if (!m_probeFaces.empty())
    {
        // The faces get sun (with the frame's shadows near the camera), sky and fog, no point lights; their
        // glass mirrors the sky alone (the atlas is the target).
        render::Environment environment = m_environment;
        environment.windowReflections = render::WindowReflections::Low;
        environment.probes = {};
        m_meshRenderer.setLighting(*m_device, environment, m_probeLights, shadows);
        for (const render::ProbeFace& face : m_probeFaces)
        {
            drawProbeFace(face);
            ++m_probeFacesDrawn;
        }
    }
    m_environment.probes = m_probes.schedule().shaderProbes();
    m_environment.probeParams = m_probes.shaderParams();
    m_meshRenderer.setReflectionProbes(&m_probes.atlas());
    // F3 (high): the previous frame's scene, if it has this frame's size.
    if (m_windowReflections == render::WindowReflections::High && m_screenHistory.valid() &&
        m_screenHistory.width() == m_sceneTarget.color().desc().width &&
        m_screenHistory.height() == m_sceneTarget.color().desc().height)
    {
        const render::Camera& previous = m_screenHistory.camera();
        m_environment.ssr = true;
        m_environment.ssrViewProjection = previous.viewProjection();
        m_environment.ssrCameraPosition = previous.transform.position;
        m_environment.ssrCameraForward = previous.transform.forward();
        m_meshRenderer.setScreenHistory(&m_screenHistory.color(), &m_screenHistory.depth());
    }
}

void Engine::captureScreenHistory(u32 width, u32 height)
{
    if (m_windowReflections != render::WindowReflections::High || !m_probeGlass || !m_probes.ready())
    {
        m_screenHistory.invalidate();
        return;
    }
    if (!m_screenHistory.ready())
    {
        auto created = render::ScreenHistory::create(*m_device, *m_shaders);
        if (!created)
        {
            G7_LOG_WARN("engine", "glass reflections: {} (window reflections medium)",
                        created.error().message);
            m_windowReflections = render::WindowReflections::Medium;
            return;
        }
        m_screenHistory = std::move(created).value();
    }
    if (auto captured =
            m_screenHistory.capture(*m_device, m_sceneTarget.color(), m_sceneTarget.depth(), m_camera);
        !captured)
    {
        G7_LOG_WARN("engine", "glass reflections: {}", captured.error().message);
        m_screenHistory.invalidate();
    }
    m_device->bindFramebuffer(&m_sceneTarget.framebuffer());
    m_device->setViewport(0, 0, width, height);
}

void Engine::drawProbeFace(const render::ProbeFace& face)
{
    const render::ReflectionProbe& probe = m_probes.schedule().probes()[face.probe];
    const f32 range = m_probes.schedule().settings().range;
    const render::Camera camera = render::probeFaceCamera(probe.position, face.face, range);
    m_probes.beginFace(*m_device, face);
    drawBackground(camera);
    if (m_hasTerrain)
    {
        m_meshRenderer.bindLighting(*m_device);
        m_terrain.setMaxDistance(range);
        m_terrain.draw(*m_device, camera, nullptr);
    }
    // The buildings and the ground at their coarsest level; no furniture of rooms, nothing small, no figures.
    const Frustum frustum = camera.frustum();
    m_cullCandidates.clear();
    m_cullGrid.query(frustum, probe.position, range, m_cullCandidates);
    m_drawItems.clear();
    for (const u32 index : m_cullCandidates)
    {
        const SceneInstance& instance = m_instances[index];
        const Vec3 size = instance.bounds.max - instance.bounds.min;
        if (!instance.solid || instance.roomCount > 0 || std::max({size.x, size.y, size.z}) < kSmallObject ||
            !frustum.intersects(instance.bounds))
        {
            continue;
        }
        const u32 lod = instance.model->mesh.maxLod();
        if (m_multiDraw)
        {
            m_drawItems.push_back({&instance.model->mesh, &instance.model->materials, instance.transform,
                                   instance.bounds, lod});
        }
        else
        {
            m_meshRenderer.draw(*m_device, instance.model->mesh, instance.model->materials,
                                instance.transform, camera, -1, lod);
        }
    }
    if (m_multiDraw)
    {
        m_meshRenderer.drawBatched(*m_device, m_drawItems, camera);
    }
    m_probes.endFace(*m_device, face);
}
} // namespace g7
