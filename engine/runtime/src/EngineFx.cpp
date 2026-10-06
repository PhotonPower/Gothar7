// Effects (M12 part A): particle emitters of data/fx/<name>.toml, simulated in the fixed step, drawn after
// the opaque scene, their lights added to the frame's. Scripts start them with fx(name, x, y, z).

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <format>

namespace g7
{
std::shared_ptr<const render::EmitterDef> Engine::effect(std::string_view name)
{
    if (const auto it = m_effects.find(std::string(name)); it != m_effects.end())
    {
        return it->second;
    }
    const std::string path = std::format("data/fx/{}.toml", name);
    auto bytes = m_vfs.read(path);
    if (!bytes)
    {
        G7_LOG_WARN("engine", "effect {}: {}", name, bytes.error().message);
        m_effects[std::string(name)] = nullptr; // asked once
        return nullptr;
    }
    auto def = render::EmitterDef::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()), path);
    if (!def)
    {
        G7_LOG_WARN("engine", "{}", def.error().message);
        m_effects[std::string(name)] = nullptr;
        return nullptr;
    }
    auto shared = std::make_shared<const render::EmitterDef>(std::move(def).value());
    m_effects[std::string(name)] = shared;
    return shared;
}

std::optional<u32> Engine::startEffect(std::string_view name, const Vec3& at, const Vec3& direction)
{
    auto def = effect(name);
    if (!def)
    {
        return std::nullopt;
    }
    return m_particles.spawn(std::move(def), at, direction);
}

void Engine::drawEffects()
{
    if (m_particles.emitterCount() == 0 || !m_particleRenderer)
    {
        return;
    }
    m_particleAdditive.clear();
    m_particleAlpha.clear();
    m_particles.collect(m_camera.transform.position, m_particleAdditive, m_particleAlpha);
    m_particleRenderer->draw(*m_device, m_camera, m_particleAlpha, false); // smoke behind the glow
    m_particleRenderer->draw(*m_device, m_camera, m_particleAdditive, true);
}

void Engine::bindFxFunctions()
{
    using script::Value;
    script::ScriptVm& vm = *m_scripts;
    vm.bind(
        {"fx",
         "fx(name: string, x: number, y: number, z: number, dx?: number, dy?: number, dz?: number) -> "
         "integer",
         "Startet einen Effekt aus data/fx/<name>.toml an einem Ort (Richtung: Vorgabe nach oben); gibt "
         "seine "
         "Nummer zurück, nil wenn es ihn nicht gibt.",
         "Effekte", [this](std::span<const Value> a) -> Result<Value>
         {
             if (a.size() < 4 || !a[0].isString() || !a[1].isNumber() || !a[2].isNumber() || !a[3].isNumber())
             {
                 return Error{"expects (name, x, y, z, dx?, dy?, dz?)"};
             }
             const Vec3 at(static_cast<f32>(a[1].asNumber()), static_cast<f32>(a[2].asNumber()),
                           static_cast<f32>(a[3].asNumber()));
             const Vec3 dir = a.size() >= 7
                                  ? Vec3(static_cast<f32>(a[4].asNumber()), static_cast<f32>(a[5].asNumber()),
                                         static_cast<f32>(a[6].asNumber()))
                                  : Vec3(0.0f, 1.0f, 0.0f);
             const auto id = startEffect(a[0].asString(), at, dir);
             return id ? Value(static_cast<i64>(*id)) : Value();
         }});
    vm.bind({"fx_move", "fx_move(id: integer, x: number, y: number, z: number)",
             "Setzt einen laufenden Effekt an einen neuen Ort (seine Teilchen bleiben, wo sie sind).",
             "Effekte", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.size() < 4 || !a[0].isNumber())
                 {
                     return Error{"expects (id, x, y, z)"};
                 }
                 m_particles.move(static_cast<u32>(a[0].asInteger()),
                                  Vec3(static_cast<f32>(a[1].asNumber()), static_cast<f32>(a[2].asNumber()),
                                       static_cast<f32>(a[3].asNumber())),
                                  Vec3(0.0f, 1.0f, 0.0f));
                 return Value();
             }});
    vm.bind({"fx_stop", "fx_stop(id: integer)", "Beendet einen Effekt; seine Teilchen verglühen noch.",
             "Effekte", [this](std::span<const Value> a) -> Result<Value>
             {
                 if (a.empty() || !a[0].isNumber())
                 {
                     return Error{"expects an effect id"};
                 }
                 m_particles.stop(static_cast<u32>(a[0].asInteger()));
                 return Value();
             }});
    vm.bind({"fx_alive", "fx_alive(id: integer) -> boolean", "Ob ein Effekt noch läuft oder Teilchen hat.",
             "Effekte", [this](std::span<const Value> a) -> Result<Value>
             {
                 return Value(!a.empty() && a[0].isNumber() &&
                              m_particles.alive(static_cast<u32>(a[0].asInteger())));
             }});
}
} // namespace g7
