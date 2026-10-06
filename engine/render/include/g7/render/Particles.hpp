// Particles (M12 part A, render.md "Partikel"): emitters as data (data/fx/<name>.toml), simulated on the CPU,
// drawn as camera-facing quads with GPU instancing. The sprites are procedural (a soft dot, smoke, a spark) -
// no texture assets. An emitter may carry a flickering point light.
#pragma once

#include <g7/core/Math.hpp>
#include <g7/core/Result.hpp>
#include <g7/core/Types.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/rhi/Resources.hpp>

#include <array>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace g7::render
{
class Device;
class ShaderLibrary;
struct Camera;

enum class ParticleSprite : u8
{
    Soft,  ///< a soft round dot (fire, magic, glow)
    Smoke, ///< a cloudy blot
    Spark, ///< a thin streak along the motion
};

/// One emitter type: data/fx/<name>.toml, version 1.
struct EmitterDef
{
    ParticleSprite sprite = ParticleSprite::Soft;
    bool additive = true; ///< additive (fire, magic) or alpha (smoke)
    f32 rate = 20.0f;     ///< particles per second while emitting (0: only the burst)
    u32 burst = 0;        ///< particles at once when it starts
    f32 duration = 0.0f;  ///< seconds of emitting; 0: until stopped
    std::array<f32, 2> lifetime{0.5f, 1.0f};
    std::array<f32, 2> speed{0.5f, 1.0f};
    Vec3 direction{0.0f, 1.0f, 0.0f};     ///< main direction (turned with the emitter)
    f32 spreadDegrees = 20.0f;            ///< cone around it
    f32 radius = 0.0f;                    ///< start positions within this sphere
    f32 gravity = 0.0f;                   ///< m/s² along -Y (negative: rises)
    f32 drag = 0.0f;                      ///< 1/s
    std::array<f32, 2> size{0.2f, 0.05f}; ///< m at birth and at death
    Vec4 colorStart{1.0f};                ///< linear rgb, alpha
    Vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
    std::optional<PointLight> light; ///< follows the emitter
    f32 lightFlicker = 0.0f;         ///< 0..1

    /// Errors name `source` and the key ("fx/firebolt.toml: lifetime: needs [min, max]").
    [[nodiscard]] static Result<EmitterDef> parse(std::string_view toml, std::string_view source);
};

/// What one particle sends to the GPU (per instance).
struct ParticleInstance
{
    Vec3 position{0.0f};
    f32 size = 0.1f;
    Vec4 color{1.0f};
    Vec3 velocity{0.0f}; ///< sparks stretch along it
    f32 sprite = 0.0f;   ///< ParticleSprite as a layer index
};
static_assert(sizeof(ParticleInstance) == 48);

/// The emitters of a world and their particles.
class ParticleSystem
{
public:
    static constexpr u32 kMaxPerEmitter = 2048;

    explicit ParticleSystem(u32 seed = 0x9e3779b9u) : m_rng(seed) {}

    /// Starts an emitter; returns its id (never 0).
    u32 spawn(std::shared_ptr<const EmitterDef> def, const Vec3& position,
              const Vec3& direction = Vec3(0.0f, 1.0f, 0.0f));
    /// Moves an emitter (attached to a hand, a projectile); its particles stay where they are.
    void move(u32 id, const Vec3& position, const Vec3& direction);
    /// Stops emitting; it goes away when its last particle dies.
    void stop(u32 id);
    /// Emitting or still with particles.
    [[nodiscard]] bool alive(u32 id) const noexcept;
    [[nodiscard]] usize particleCount() const noexcept;
    [[nodiscard]] usize emitterCount() const noexcept { return m_emitters.size(); }

    void update(f32 seconds);
    /// The particles to draw: additive ones, and alpha ones sorted back to front from `eye`.
    void collect(const Vec3& eye, std::vector<ParticleInstance>& additive,
                 std::vector<ParticleInstance>& alpha) const;
    /// The lights of the emitting emitters (flickering).
    void lights(std::vector<PointLight>& out) const;

private:
    struct Particle
    {
        Vec3 position{0.0f};
        Vec3 velocity{0.0f};
        f32 age = 0.0f;
        f32 life = 1.0f;
    };
    struct Emitter
    {
        u32 id = 0;
        std::shared_ptr<const EmitterDef> def;
        Vec3 position{0.0f};
        Vec3 direction{0.0f, 1.0f, 0.0f};
        f32 age = 0.0f;
        f32 owed = 0.0f; ///< particles due but not yet born (fractions)
        bool emitting = true;
        f32 flicker = 1.0f;
        std::vector<Particle> particles;
    };
    void emit(Emitter& e, u32 count);

    std::vector<Emitter> m_emitters;
    u32 m_next = 1;
    std::mt19937 m_rng;
};

/// Draws collected particles into the bound (HDR) target: depth-tested, not written.
class ParticleRenderer
{
public:
    [[nodiscard]] static Result<ParticleRenderer> create(Device& device, ShaderLibrary& shaders);
    void draw(Device& device, const Camera& camera, std::span<const ParticleInstance> particles,
              bool additive);

    /// The procedural sprites (tests): RGBA8, `size`² per layer, layers in ParticleSprite order.
    [[nodiscard]] static std::vector<u8> spriteImage(ParticleSprite sprite, u32 size);

private:
    rhi::ShaderProgram* m_program = nullptr;
    rhi::Pipeline m_additive;
    rhi::Pipeline m_alpha;
    rhi::Texture m_sprites;
    rhi::Sampler m_sampler;
    rhi::Buffer m_instances;
};
} // namespace g7::render
