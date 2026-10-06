#include <g7/core/Config.hpp>
#include <g7/core/Log.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Particles.hpp>
#include <g7/render/ShaderLibrary.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>

namespace g7::render
{
namespace
{
constexpr u32 kSpriteSize = 64;

f32 srgbToLinear(f32 c) noexcept
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// [a, b] of the file (both >= 0 unless `signedValues`).
Result<std::array<f32, 2>> range(const Config& c, std::string_view key, std::array<f32, 2> fallback,
                                 std::string_view source)
{
    if (!c.contains(key))
    {
        return fallback;
    }
    const auto v = c.find<std::vector<f64>>(key);
    if (!v || v->size() != 2 || (*v)[0] < 0.0 || (*v)[1] < (*v)[0])
    {
        return Error{std::format("{}: {}: needs [min, max] with 0 <= min <= max", source, key)};
    }
    return std::array<f32, 2>{static_cast<f32>((*v)[0]), static_cast<f32>((*v)[1])};
}

Result<Vec4> color(const Config& c, std::string_view key, Vec4 fallback, std::string_view source)
{
    if (!c.contains(key))
    {
        return fallback;
    }
    const auto v = c.find<std::vector<f64>>(key);
    if (!v || v->size() != 4 || std::any_of(v->begin(), v->end(), [](f64 x) { return x < 0.0; }))
    {
        return Error{std::format("{}: {}: needs [r, g, b, a] (sRGB 0..1, alpha 0..1)", source, key)};
    }
    // Colours in sRGB as picked, linear here (above 1: brighter than white, for glow).
    return Vec4(srgbToLinear(static_cast<f32>((*v)[0])), srgbToLinear(static_cast<f32>((*v)[1])),
                srgbToLinear(static_cast<f32>((*v)[2])), static_cast<f32>((*v)[3]));
}

f32 uniform(std::mt19937& rng, f32 a, f32 b)
{
    return a + (b - a) * std::uniform_real_distribution<f32>(0.0f, 1.0f)(rng);
}

/// A unit direction within `spreadDegrees` of `axis`.
Vec3 coneDirection(std::mt19937& rng, const Vec3& axis, f32 spreadDegrees)
{
    const Vec3 a = glm::length(axis) > 1e-5f ? glm::normalize(axis) : Vec3(0.0f, 1.0f, 0.0f);
    const f32 cosMax = std::cos(glm::radians(std::clamp(spreadDegrees, 0.0f, 180.0f)));
    const f32 z = uniform(rng, cosMax, 1.0f);
    const f32 phi = uniform(rng, 0.0f, 2.0f * glm::pi<f32>());
    const f32 r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const Vec3 helper = std::abs(a.y) < 0.99f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 x = glm::normalize(glm::cross(helper, a));
    const Vec3 y = glm::cross(a, x);
    return glm::normalize(x * (r * std::cos(phi)) + y * (r * std::sin(phi)) + a * z);
}

/// The shortest rotation taking the unit vector `from` to the unit vector `to`.
Quat rotationBetween(const Vec3& from, const Vec3& to)
{
    const f32 d = glm::dot(from, to);
    if (d < -0.9999f)
    {
        const Vec3 axis = std::abs(from.x) < 0.9f ? glm::normalize(glm::cross(from, Vec3(1, 0, 0)))
                                                  : glm::normalize(glm::cross(from, Vec3(0, 0, 1)));
        return glm::angleAxis(glm::pi<f32>(), axis);
    }
    const Vec3 c = glm::cross(from, to);
    return glm::normalize(Quat(1.0f + d, c.x, c.y, c.z));
}

/// Cheap value noise for the smoke sprite.
f32 hashNoise(i32 x, i32 y)
{
    u32 h = static_cast<u32>(x) * 374761393u + static_cast<u32>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<f32>((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

f32 smoothNoise(f32 x, f32 y)
{
    const i32 xi = static_cast<i32>(std::floor(x));
    const i32 yi = static_cast<i32>(std::floor(y));
    const f32 fx = x - static_cast<f32>(xi);
    const f32 fy = y - static_cast<f32>(yi);
    const f32 sx = fx * fx * (3.0f - 2.0f * fx);
    const f32 sy = fy * fy * (3.0f - 2.0f * fy);
    const f32 a = glm::mix(hashNoise(xi, yi), hashNoise(xi + 1, yi), sx);
    const f32 b = glm::mix(hashNoise(xi, yi + 1), hashNoise(xi + 1, yi + 1), sx);
    return glm::mix(a, b, sy);
}
} // namespace

Result<EmitterDef> EmitterDef::parse(std::string_view toml, std::string_view source)
{
    auto parsed = Config::parse(toml, source);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    if (c.get<i64>("version", 1) != 1)
    {
        return Error{std::format("{}: version: must be 1", source)};
    }
    EmitterDef d;
    const std::string sprite = c.get<std::string>("sprite", "soft");
    if (sprite == "soft")
    {
        d.sprite = ParticleSprite::Soft;
    }
    else if (sprite == "smoke")
    {
        d.sprite = ParticleSprite::Smoke;
    }
    else if (sprite == "spark")
    {
        d.sprite = ParticleSprite::Spark;
    }
    else
    {
        return Error{std::format("{}: sprite: soft, smoke or spark, not \"{}\"", source, sprite)};
    }
    const std::string blend = c.get<std::string>("blend", "additive");
    if (blend != "additive" && blend != "alpha")
    {
        return Error{std::format("{}: blend: additive or alpha, not \"{}\"", source, blend)};
    }
    d.additive = blend == "additive";
    const auto number = [&](std::string_view key, f32& value, f64 min, f64 max) -> Result<void>
    {
        if (!c.contains(key))
        {
            return {};
        }
        const auto v = c.find<f64>(key);
        if (!v || *v < min || *v > max)
        {
            return Error{std::format("{}: {}: must be a number {}..{}", source, key, min, max)};
        }
        value = static_cast<f32>(*v);
        return {};
    };
    f32 burst = 0.0f;
    for (auto r :
         {number("rate", d.rate, 0.0, 10000.0), number("burst", burst, 0.0, ParticleSystem::kMaxPerEmitter),
          number("duration", d.duration, 0.0, 3600.0), number("spread", d.spreadDegrees, 0.0, 180.0),
          number("radius", d.radius, 0.0, 100.0), number("gravity", d.gravity, -100.0, 100.0),
          number("drag", d.drag, 0.0, 100.0)})
    {
        if (!r)
        {
            return r.error();
        }
    }
    d.burst = static_cast<u32>(burst);
    if (d.rate == 0.0f && d.burst == 0)
    {
        return Error{std::format("{}: needs rate or burst", source)};
    }
    for (auto [key, target] :
         {std::pair{"lifetime", &d.lifetime}, std::pair{"speed", &d.speed}, std::pair{"size", &d.size}})
    {
        const bool size = std::string_view(key) == "size";
        if (size && c.contains(key))
        {
            // Size may shrink or grow: [birth, death].
            const auto v = c.find<std::vector<f64>>(key);
            if (!v || v->size() != 2 || (*v)[0] < 0.0 || (*v)[1] < 0.0)
            {
                return Error{std::format("{}: size: needs [at birth, at death] (m, >= 0)", source)};
            }
            *target = {static_cast<f32>((*v)[0]), static_cast<f32>((*v)[1])};
            continue;
        }
        auto r = range(c, key, *target, source);
        if (!r)
        {
            return r.error();
        }
        *target = r.value();
    }
    if (d.lifetime[1] <= 0.0f)
    {
        return Error{std::format("{}: lifetime: must be above 0", source)};
    }
    if (const auto dir = c.find<std::vector<f64>>("direction"))
    {
        if (dir->size() != 3)
        {
            return Error{std::format("{}: direction: needs [x, y, z]", source)};
        }
        d.direction = Vec3((*dir)[0], (*dir)[1], (*dir)[2]);
    }
    auto start = color(c, "color_start", d.colorStart, source);
    auto end = color(c, "color_end", d.colorEnd, source);
    if (!start || !end)
    {
        return !start ? start.error() : end.error();
    }
    d.colorStart = start.value();
    d.colorEnd = end.value();
    if (c.contains("light.range"))
    {
        PointLight light;
        f32 range = 0.0f;
        f32 intensity = 3.0f;
        for (auto r :
             {number("light.range", range, 0.1, 100.0), number("light.intensity", intensity, 0.0, 1000.0),
              number("light.flicker", d.lightFlicker, 0.0, 1.0)})
        {
            if (!r)
            {
                return r.error();
            }
        }
        auto lightColor = color(c, "light.color", Vec4(1.0f), source);
        if (!lightColor && c.contains("light.color"))
        {
            // light.color is [r, g, b]
            const auto v = c.find<std::vector<f64>>("light.color");
            if (!v || v->size() != 3)
            {
                return Error{std::format("{}: light.color: needs [r, g, b] (sRGB 0..1)", source)};
            }
            lightColor =
                Vec4(srgbToLinear(static_cast<f32>((*v)[0])), srgbToLinear(static_cast<f32>((*v)[1])),
                     srgbToLinear(static_cast<f32>((*v)[2])), 1.0f);
        }
        light.radius = range;
        light.color = Vec3(lightColor.value());
        light.intensity = intensity;
        d.light = light;
    }
    return d;
}

u32 ParticleSystem::spawn(std::shared_ptr<const EmitterDef> def, const Vec3& position, const Vec3& direction)
{
    Emitter e;
    e.id = m_next++;
    e.def = std::move(def);
    e.position = position;
    e.direction = direction;
    e.particles.reserve(std::min<u32>(kMaxPerEmitter, 64));
    emit(e, e.def->burst);
    m_emitters.push_back(std::move(e));
    return m_emitters.back().id;
}

void ParticleSystem::move(u32 id, const Vec3& position, const Vec3& direction)
{
    for (Emitter& e : m_emitters)
    {
        if (e.id == id)
        {
            e.position = position;
            e.direction = direction;
        }
    }
}

void ParticleSystem::stop(u32 id)
{
    for (Emitter& e : m_emitters)
    {
        if (e.id == id)
        {
            e.emitting = false;
        }
    }
}

bool ParticleSystem::alive(u32 id) const noexcept
{
    return std::any_of(m_emitters.begin(), m_emitters.end(), [&](const Emitter& e) { return e.id == id; });
}

usize ParticleSystem::particleCount() const noexcept
{
    usize n = 0;
    for (const Emitter& e : m_emitters)
    {
        n += e.particles.size();
    }
    return n;
}

void ParticleSystem::emit(Emitter& e, u32 count)
{
    const EmitterDef& d = *e.def;
    // The definition's direction turned like the emitter's (+Y of the definition = the emitter's direction).
    const Vec3 up(0.0f, 1.0f, 0.0f);
    const Vec3 to = glm::length(e.direction) > 1e-5f ? glm::normalize(e.direction) : up;
    const Quat turn = rotationBetween(up, to);
    const Vec3 axis = turn * d.direction;
    for (u32 i = 0; i < count && e.particles.size() < kMaxPerEmitter; ++i)
    {
        Particle p;
        Vec3 offset(0.0f);
        if (d.radius > 0.0f)
        {
            offset = coneDirection(m_rng, up, 180.0f) * (d.radius * std::cbrt(uniform(m_rng, 0.0f, 1.0f)));
        }
        p.position = e.position + offset;
        p.velocity = coneDirection(m_rng, axis, d.spreadDegrees) * uniform(m_rng, d.speed[0], d.speed[1]);
        p.life = uniform(m_rng, d.lifetime[0], d.lifetime[1]);
        e.particles.push_back(p);
    }
}

void ParticleSystem::update(f32 seconds)
{
    for (Emitter& e : m_emitters)
    {
        const EmitterDef& d = *e.def;
        e.age += seconds;
        if (d.duration > 0.0f && e.age >= d.duration)
        {
            e.emitting = false;
        }
        if (e.emitting && d.rate > 0.0f)
        {
            e.owed += d.rate * seconds;
            const u32 due = static_cast<u32>(e.owed);
            e.owed -= static_cast<f32>(due);
            emit(e, due);
        }
        for (Particle& p : e.particles)
        {
            p.age += seconds;
            p.velocity.y -= d.gravity * seconds;
            p.velocity *= std::max(0.0f, 1.0f - d.drag * seconds);
            p.position += p.velocity * seconds;
        }
        std::erase_if(e.particles, [](const Particle& p) { return p.age >= p.life; });
        if (d.light && d.lightFlicker > 0.0f)
        {
            e.flicker = 1.0f - d.lightFlicker * uniform(m_rng, 0.0f, 1.0f);
        }
    }
    std::erase_if(m_emitters, [](const Emitter& e) { return !e.emitting && e.particles.empty(); });
}

void ParticleSystem::collect(const Vec3& eye, std::vector<ParticleInstance>& additive,
                             std::vector<ParticleInstance>& alpha) const
{
    for (const Emitter& e : m_emitters)
    {
        const EmitterDef& d = *e.def;
        for (const Particle& p : e.particles)
        {
            const f32 t = std::clamp(p.age / p.life, 0.0f, 1.0f);
            ParticleInstance i;
            i.position = p.position;
            i.size = glm::mix(d.size[0], d.size[1], t);
            i.color = glm::mix(d.colorStart, d.colorEnd, t);
            i.velocity = p.velocity;
            i.sprite = static_cast<f32>(d.sprite);
            (d.additive ? additive : alpha).push_back(i);
        }
    }
    std::sort(alpha.begin(), alpha.end(), [&](const ParticleInstance& a, const ParticleInstance& b)
              { return glm::length(a.position - eye) > glm::length(b.position - eye); });
}

void ParticleSystem::lights(std::vector<PointLight>& out) const
{
    for (const Emitter& e : m_emitters)
    {
        if (e.def->light && e.emitting)
        {
            PointLight l = *e.def->light;
            l.position = e.position;
            l.intensity *= e.flicker;
            out.push_back(l);
        }
    }
}

std::vector<u8> ParticleRenderer::spriteImage(ParticleSprite sprite, u32 size)
{
    std::vector<u8> pixels(static_cast<usize>(size) * size * 4, 255);
    for (u32 y = 0; y < size; ++y)
    {
        for (u32 x = 0; x < size; ++x)
        {
            const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size) * 2.0f - 1.0f;
            const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size) * 2.0f - 1.0f;
            const f32 r = std::sqrt(u * u + v * v);
            f32 a = 0.0f;
            switch (sprite)
            {
            case ParticleSprite::Soft:
                a = std::pow(std::clamp(1.0f - r, 0.0f, 1.0f), 2.0f);
                break;
            case ParticleSprite::Smoke:
            {
                const f32 n = 0.6f * smoothNoise(u * 3.0f + 7.0f, v * 3.0f + 3.0f) +
                              0.4f * smoothNoise(u * 7.0f + 1.0f, v * 7.0f + 9.0f);
                a = std::clamp(1.0f - r, 0.0f, 1.0f) * std::clamp(n * 1.6f - 0.2f, 0.0f, 1.0f);
                break;
            }
            case ParticleSprite::Spark:
                // Thin along u (the shader stretches it along the motion), bright core.
                a = std::pow(std::clamp(1.0f - std::abs(u) * 4.0f, 0.0f, 1.0f), 1.5f) *
                    std::clamp(1.0f - std::abs(v), 0.0f, 1.0f);
                break;
            }
            pixels[(static_cast<usize>(y) * size + x) * 4 + 3] = static_cast<u8>(std::lround(a * 255.0f));
        }
    }
    return pixels;
}

Result<ParticleRenderer> ParticleRenderer::create(Device& device, ShaderLibrary& shaders)
{
    ParticleRenderer r;
    auto program = shaders.load("particle", {"particle.vert", "particle.frag", {}});
    if (!program)
    {
        return program.error();
    }
    r.m_program = program.value();
    rhi::PipelineDesc desc;
    desc.program = r.m_program;
    desc.attributes = {{0, rhi::VertexFormat::Float4, 0, 1},
                       {1, rhi::VertexFormat::Float4, 16, 1},
                       {2, rhi::VertexFormat::Float4, 32, 1}};
    desc.instanceStride = sizeof(ParticleInstance);
    desc.cull = rhi::CullMode::None;
    desc.depthWrite = false;
    desc.blend = rhi::BlendMode::Additive;
    auto additive = device.createPipeline(desc);
    desc.blend = rhi::BlendMode::Alpha;
    auto alpha = device.createPipeline(desc);
    if (!additive || !alpha)
    {
        return !additive ? additive.error() : alpha.error();
    }
    r.m_additive = std::move(additive).value();
    r.m_alpha = std::move(alpha).value();

    auto sprites = device.createTexture({kSpriteSize, kSpriteSize, rhi::Format::RGBA8, 0, 3, true});
    if (!sprites)
    {
        return sprites.error();
    }
    r.m_sprites = std::move(sprites).value();
    for (u32 layer = 0; layer < 3; ++layer)
    {
        if (auto up =
                r.m_sprites.upload(0, spriteImage(static_cast<ParticleSprite>(layer), kSpriteSize), layer);
            !up)
        {
            return up.error();
        }
    }
    if (auto mips = r.m_sprites.generateMipmaps(); !mips)
    {
        return mips.error();
    }
    rhi::SamplerDesc linear;
    linear.wrapU = linear.wrapV = rhi::Wrap::Clamp;
    auto sampler = device.createSampler(linear);
    if (!sampler)
    {
        return sampler.error();
    }
    r.m_sampler = std::move(sampler).value();
    return r;
}

void ParticleRenderer::draw(Device& device, const Camera& camera, std::span<const ParticleInstance> particles,
                            bool additive)
{
    if (particles.empty() || m_program == nullptr)
    {
        return;
    }
    const usize bytes = particles.size_bytes();
    if (m_instances.size() < bytes)
    {
        auto grown = device.createBuffer(
            {std::bit_ceil(std::max<usize>(bytes, 64 * 1024)), rhi::BufferUsage::Dynamic, {}});
        if (!grown)
        {
            G7_LOG_ERROR("render", "particles: {}", grown.error().message);
            return;
        }
        m_instances = std::move(grown).value();
    }
    if (!m_instances.update(0, std::span(reinterpret_cast<const u8*>(particles.data()), bytes)))
    {
        return;
    }
    const Mat4 view = camera.view();
    m_program->setUniform("uViewProjection", camera.viewProjection());
    m_program->setUniform("uCameraRight", Vec3(view[0][0], view[1][0], view[2][0]));
    m_program->setUniform("uCameraUp", Vec3(view[0][1], view[1][1], view[2][1]));
    m_program->setUniform("uCameraPosition", camera.transform.position);
    device.bindPipeline(additive ? m_additive : m_alpha);
    device.bindInstanceBuffer(m_instances);
    device.bindTexture(0, m_sprites, m_sampler);
    device.drawInstanced(6, static_cast<u32>(particles.size()));
}
} // namespace g7::render
