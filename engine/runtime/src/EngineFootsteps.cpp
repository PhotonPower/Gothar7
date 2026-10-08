// Footsteps (M13 part E, owner decision 6; materials agreed with welt): at every footstep_l/r event of the
// hero and of human NPCs near the listener, the material under the foot picks footstep_<material> from
// data/sounds.toml - water where the foot is wet, else the model below (its components.surface.footstep, a
// path pattern of data/footsteps.toml, or the default for models and mobs), else the terrain's strongest
// splat layer. Slow steps (sneaking) are quieter, running louder.

#include "PlayerFigure.hpp"

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>
#include <g7/physics/Physics.hpp>
#include <g7/runtime/AssetMounts.hpp>
#include <g7/runtime/Engine.hpp>

#include <algorithm>
#include <format>

namespace g7
{
namespace
{
using script::Value;

constexpr f32 kRayAbove = 0.5f;    ///< m above the feet the ray down starts
constexpr f32 kRayLength = 1.0f;   ///< ... and this far it looks
constexpr f32 kNpcHearing = 25.0f; ///< m: NPC steps farther from the listener are not played
constexpr f32 kSlowSpeed = 1.2f;   ///< m/s: up to here the slow volume (sneaking)
constexpr f32 kRunSpeed = 3.0f;    ///< m/s: from here the running volume

/// `*` matches any characters (also '/').
bool matches(std::string_view pattern, std::string_view text)
{
    usize p = 0;
    usize t = 0;
    usize star = std::string_view::npos;
    usize resume = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            resume = t;
        }
        else if (p < pattern.size() && pattern[p] == text[t])
        {
            ++p;
            ++t;
        }
        else if (star != std::string_view::npos)
        {
            p = star + 1;
            t = ++resume;
        }
        else
        {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
    {
        ++p;
    }
    return p == pattern.size();
}
} // namespace

void Engine::loadFootsteps()
{
    const std::string path = m_config.settings.get<std::string>("game.footsteps", "data/footsteps.toml");
    auto bytes = m_vfs.read(path);
    if (!bytes)
    {
        G7_LOG_WARN("engine", "footsteps: {}", bytes.error().message);
        return;
    }
    auto parsed = Config::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
    if (!parsed)
    {
        G7_LOG_WARN("engine", "{}", parsed.error().message);
        return;
    }
    const Config& c = parsed.value();
    FootstepRules rules;
    rules.terrainDefault = c.get<std::string>("default", rules.terrainDefault);
    rules.model = c.get<std::string>("model", rules.model);
    rules.mob = c.get<std::string>("mob", rules.mob);
    rules.waterDepth = static_cast<f32>(c.get<f64>("water_depth", rules.waterDepth));
    rules.slowVolume = static_cast<f32>(c.get<f64>("volume.slow", rules.slowVolume));
    rules.walkVolume = static_cast<f32>(c.get<f64>("volume.walk", rules.walkVolume));
    rules.runVolume = static_cast<f32>(c.get<f64>("volume.run", rules.runVolume));
    for (const std::string& layer : c.keys("layers"))
    {
        rules.layers[layer] = c.get<std::string>("layers." + layer, rules.terrainDefault);
    }
    for (usize i = 0, n = c.arraySize("models"); i < n; ++i)
    {
        const std::string at = std::format("models[{}]", i);
        const std::string pattern = c.get<std::string>(at + ".pattern", "");
        const std::string material = c.get<std::string>(at + ".material", "");
        if (pattern.empty() || material.empty())
        {
            G7_LOG_WARN("engine", "{}: {} needs pattern and material", path, at);
            continue;
        }
        rules.models.emplace_back(pattern, material);
    }
    m_footsteps = std::move(rules);
    G7_LOG_INFO("engine", "footsteps: {} terrain layers, {} model patterns", m_footsteps->layers.size(),
                m_footsteps->models.size());
}

void Engine::loadFootstepTerrain()
{
    // The splat weights on the CPU (the renderer has them only on the GPU): PNG, or the cooked KTX2.
    m_splatImages.clear();
    if (!m_hasTerrain || !m_footsteps)
    {
        return;
    }
    for (const std::string& map : m_heightfield.ref().splatMaps)
    {
        const std::string path = preferCooked(m_vfs, map);
        auto bytes = m_vfs.read(path);
        auto image = !bytes                    ? Result<asset::ImageData>(bytes.error())
                     : path.ends_with(".ktx2") ? asset::decodeKtx2Rgba(bytes.value(), path)
                                               : asset::decodeImage(bytes.value(), path);
        if (!image)
        {
            G7_LOG_WARN("engine", "footsteps: {} - the terrain counts as '{}'", image.error().message,
                        m_footsteps->terrainDefault);
            m_splatImages.clear();
            return;
        }
        m_splatImages.push_back(std::move(image).value());
    }
}

std::string Engine::terrainLayerAt(f32 x, f32 z) const
{
    const world::TerrainRef& ref = m_heightfield.ref();
    if (m_splatImages.empty() || ref.width < 2 || ref.height < 2)
    {
        return {};
    }
    // Pixel centres lie on samples (world.md): the nearest pixel of each map.
    const f32 column = (x - ref.firstSample.x) / ref.cellSize / static_cast<f32>(ref.width - 1);
    const f32 row = (z - ref.firstSample.y) / ref.cellSize / static_cast<f32>(ref.height - 1);
    f32 best = -1.0f;
    usize bestLayer = 0;
    for (usize m = 0; m < m_splatImages.size(); ++m)
    {
        const asset::ImageData& image = m_splatImages[m];
        const auto px = static_cast<u32>(std::clamp(std::lround(column * static_cast<f32>(image.width - 1)),
                                                    0L, static_cast<long>(image.width) - 1));
        const auto py = static_cast<u32>(std::clamp(std::lround(row * static_cast<f32>(image.height - 1)), 0L,
                                                    static_cast<long>(image.height) - 1));
        const u8* texel = &image.rgba8[(usize(py) * image.width + px) * 4];
        for (usize k = 0; k < 4 && m * 4 + k < ref.layers.size(); ++k)
        {
            if (texel[k] > best)
            {
                best = texel[k];
                bestLayer = m * 4 + k;
            }
        }
    }
    return bestLayer < ref.layers.size() ? ref.layers[bestLayer].name : std::string();
}

std::string Engine::footstepMaterial(const Vec3& feet) const
{
    if (!m_footsteps)
    {
        return {};
    }
    const FootstepRules& rules = *m_footsteps;
    if (const auto surface = m_water.surfaceAt(feet); surface && *surface > feet.y + rules.waterDepth)
    {
        return "water";
    }
    if (m_physics.valid())
    {
        const auto hit = m_physics.raycast(feet + Vec3(0.0f, kRayAbove, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
                                           kRayLength, physics::layerBit(physics::Layer::World));
        if (hit && hit->userData != 0)
        {
            const entt::entity e = m_scene.findById(world::VobId{hit->userData});
            if (e != entt::null)
            {
                if (const world::SurfaceRef* surface = m_scene.get<world::SurfaceRef>(e))
                {
                    return surface->footstep;
                }
                const world::MeshRef* mesh = m_scene.get<world::MeshRef>(e);
                for (const auto& [pattern, material] : rules.models)
                {
                    if (mesh != nullptr && matches(pattern, mesh->path))
                    {
                        return material;
                    }
                }
                return m_scene.get<world::MobRef>(e) != nullptr ? rules.mob : rules.model;
            }
        }
    }
    const std::string layer = terrainLayerAt(feet.x, feet.z);
    const auto it = rules.layers.find(layer);
    return it != rules.layers.end() ? it->second : rules.terrainDefault;
}

void Engine::footstep(const Vec3& feet, f32 speed)
{
    if (!m_audio || !m_footsteps)
    {
        return;
    }
    const std::string material = footstepMaterial(feet);
    m_lastFootstep = material;
    const FootstepRules& rules = *m_footsteps;
    const f32 volume = speed <= kSlowSpeed  ? rules.slowVolume
                       : speed >= kRunSpeed ? rules.runVolume
                                            : rules.walkVolume;
    if (const auto id = playSound("footstep_" + material, feet + Vec3(0.0f, 0.1f, 0.0f));
        id && volume != 1.0f)
    {
        m_audio->setVolume(*id, volume);
    }
}

bool Engine::nearListener(const Vec3& point) const
{
    return glm::length(point - m_camera.transform.position) <= kNpcHearing;
}

void Engine::bindFootstepFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"footstep_material", "footstep_material(x: number, y: number, z: number) -> string",
             "Das Fußschritt-Material an einem Punkt (M13: water, Modell darunter, Gelände; "
             "data/footsteps.toml).",
             "Klang", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.size() < 3 || !a[0].isNumber() || !a[1].isNumber() || !a[2].isNumber())
                 {
                     return Error{"expects (x, y, z)"};
                 }
                 return Value(footstepMaterial(Vec3(static_cast<f32>(a[0].asNumber()),
                                                    static_cast<f32>(a[1].asNumber()),
                                                    static_cast<f32>(a[2].asNumber()))));
             }});
    vm.bind({"last_footstep", "last_footstep() -> string",
             "Das Material des zuletzt gespielten Schritts (Held oder NPC); leer vor dem ersten.", "Klang",
             [this](std::span<const Value>) -> Result<Value> { return Value(m_lastFootstep); }});
}
} // namespace g7
