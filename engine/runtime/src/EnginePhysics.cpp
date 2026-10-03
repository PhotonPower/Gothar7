// The engine's collision world (M5): built from the terrain and the render instances of the loaded world.

#include <g7/core/Clock.hpp>
#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

#include <unordered_map>

namespace g7
{
namespace
{
/// Collision parts of a model: its COL_ nodes, or its render triangles if it has none (asset.md).
std::vector<physics::ShapePart> collisionParts(const asset::MeshData& data)
{
    std::vector<physics::ShapePart> parts;
    if (!data.collision.empty())
    {
        for (const asset::CollisionPart& part : data.collision)
        {
            parts.push_back({part.kind == asset::CollisionPart::Kind::Hull ? physics::ShapePart::Kind::Hull
                                                                           : physics::ShapePart::Kind::Mesh,
                             part.points, part.indices});
        }
        return parts;
    }
    physics::ShapePart mesh;
    mesh.points.reserve(data.vertices.size());
    for (const asset::Vertex& vertex : data.vertices)
    {
        mesh.points.push_back(vertex.position);
    }
    mesh.indices = data.indices;
    parts.push_back(std::move(mesh));
    return parts;
}

/// A slab below the top of `bounds` (the ground plate is a single quad: a hull needs volume).
physics::ShapePart slab(const AABB& bounds)
{
    physics::ShapePart part;
    part.kind = physics::ShapePart::Kind::Hull;
    const Vec3 lo(bounds.min.x, bounds.max.y - 1.0f, bounds.min.z);
    const Vec3 hi = bounds.max;
    for (int i = 0; i < 8; ++i)
    {
        part.points.push_back(Vec3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z));
    }
    return part;
}

/// Position, rotation and scale of an affine matrix (mirroring goes into scale x).
void decompose(const Mat4& m, Vec3& position, Quat& rotation, Vec3& scale)
{
    position = Vec3(m[3]);
    Mat3 basis(m);
    scale = Vec3(glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2]));
    if (glm::determinant(basis) < 0.0f)
    {
        scale.x = -scale.x;
    }
    for (int i = 0; i < 3; ++i)
    {
        basis[i] /= scale[i];
    }
    rotation = glm::normalize(glm::quat_cast(basis));
}
} // namespace

void Engine::syncPhysics()
{
    if (!m_physicsDirty || !m_physics.valid())
    {
        return;
    }
    m_physicsDirty = false;
    const Stopwatch timer;
    m_physics.clear();
    if (m_hasTerrain)
    {
        const world::TerrainRef& ref = m_heightfield.ref();
        std::vector<f32> heights(static_cast<usize>(ref.width) * ref.height);
        for (u32 r = 0; r < ref.height; ++r)
        {
            for (u32 c = 0; c < ref.width; ++c)
            {
                heights[static_cast<usize>(r) * ref.width + c] =
                    m_heightfield.sampleHeight(static_cast<i32>(c), static_cast<i32>(r));
            }
        }
        const physics::HeightfieldDesc desc{ref.width,       ref.height, ref.cellSize,
                                            ref.firstSample, heights,    m_heightfield.holes()};
        if (auto body = m_physics.addHeightfield(desc); !body)
        {
            G7_LOG_WARN("engine", "terrain without collision: {}", body.error().message);
        }
    }
    std::unordered_map<const LoadedModel*, physics::ShapeId> shapes; // each model once, shared
    u32 without = 0;
    for (const SceneInstance& instance : m_instances)
    {
        if (!instance.solid)
        {
            continue; // the water surface placeholder: swimming is handled by WaterBodies
        }
        auto [it, inserted] = shapes.try_emplace(instance.model);
        if (inserted)
        {
            const asset::MeshData* data = instance.model->source.get();
            std::vector<physics::ShapePart> parts;
            if (instance.model == m_groundModel.get())
            {
                parts.push_back(slab(instance.model->bounds));
            }
            else if (data != nullptr)
            {
                parts = collisionParts(*data);
            }
            auto shape = parts.empty() ? Result<physics::ShapeId>(Error{"no mesh data"})
                                       : m_physics.createShape(parts);
            if (shape)
            {
                it->second = shape.value();
            }
            else
            {
                G7_LOG_WARN("engine", "{}: no collision ({})", instance.model->name, shape.error().message);
            }
        }
        if (!it->second.valid())
        {
            ++without;
            continue;
        }
        Vec3 position;
        Quat rotation;
        Vec3 scale;
        decompose(instance.transform, position, rotation, scale);
        if (auto body = m_physics.addStatic(it->second, position, rotation, scale, physics::Layer::World,
                                            instance.vob.value);
            !body)
        {
            G7_LOG_WARN("engine", "collision of {}: {}", instance.model->name, body.error().message);
        }
    }
    m_physics.optimize();
    const physics::PhysicsStats stats = m_physics.stats();
    G7_LOG_INFO("engine", "collision: {} bodies, {} shapes, {} mesh triangles{} ({:.0f} ms)", stats.bodies,
                stats.shapes, stats.triangles, m_hasTerrain ? " + terrain" : "",
                timer.elapsedSeconds() * 1000.0);
    if (without > 0)
    {
        G7_LOG_WARN("engine", "{} instances without collision", without);
    }
}
} // namespace g7
