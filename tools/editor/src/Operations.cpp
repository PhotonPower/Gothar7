#include <g7/core/StringUtil.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Triggers.hpp>

#include <algorithm>
#include <limits>
#include <string>

namespace g7::editor
{
Transform worldTransformOf(const world::Scene& scene, entt::entity vob)
{
    return Transform::fromMatrix(scene.worldMatrix(vob));
}

void setWorldTransform(world::Scene& scene, entt::entity vob, const Transform& world)
{
    // Keeps the parent: the local transform becomes parent^-1 * world.
    const entt::entity parent = scene.parent(vob);
    scene.setTransform(vob,
                       scene.valid(parent)
                           ? Transform::fromMatrix(glm::inverse(scene.worldMatrix(parent)) * world.toMatrix())
                           : world);
}

Result<entt::entity> placeMesh(world::Scene& scene, std::string_view meshPath, const Vec3& position)
{
    // Name after the file: "testscene/nature/rock_largeA.glb" -> "ROCK_LARGEA".
    const usize slash = meshPath.find_last_of('/');
    std::string_view file = slash == std::string_view::npos ? meshPath : meshPath.substr(slash + 1);
    file = file.substr(0, file.find('.'));
    Transform transform;
    transform.position = position;
    auto vob = scene.spawnVob({toUpper(file), transform, {}, {}});
    if (!vob)
    {
        return vob.error();
    }
    scene.set<world::MeshRef>(vob.value(), {std::string(meshPath), world::VobCategory::Deco});
    return vob;
}

Result<entt::entity> duplicateVob(world::Scene& scene, entt::entity vob, const Vec3& offset)
{
    const world::Vob* source = scene.get<world::Vob>(vob);
    Transform transform = *std::as_const(scene).get<Transform>(vob);
    transform.position += offset; // in the parent's space: fine for an offset next to the original
    auto copy = scene.spawnVob({source->nameText, transform, scene.idOf(scene.parent(vob)), {}});
    if (!copy)
    {
        return copy.error();
    }
    const auto take = [&]<typename C>(const C* component)
    {
        if (component != nullptr)
        {
            scene.set<C>(copy.value(), *component);
        }
    };
    take(scene.get<world::MeshRef>(vob));
    take(scene.get<world::LightSource>(vob));
    take(scene.get<world::StartPoint>(vob));
    take(scene.get<world::SoundEmitter>(vob));
    take(scene.get<world::TriggerVolume>(vob));
    take(scene.get<world::MobRef>(vob));
    return copy;
}

void removeVob(world::Scene& scene, entt::entity vob)
{
    scene.destroyVob(vob);
}

std::optional<f32> intersect(const Ray& ray, const AABB& box) noexcept
{
    f32 near = 0.0f;
    f32 far = std::numeric_limits<f32>::max();
    for (int a = 0; a < 3; ++a)
    {
        if (std::abs(ray.direction[a]) < 1e-8f)
        {
            if (ray.origin[a] < box.min[a] || ray.origin[a] > box.max[a])
            {
                return std::nullopt;
            }
            continue;
        }
        f32 t0 = (box.min[a] - ray.origin[a]) / ray.direction[a];
        f32 t1 = (box.max[a] - ray.origin[a]) / ray.direction[a];
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        near = std::max(near, t0);
        far = std::min(far, t1);
        if (near > far)
        {
            return std::nullopt;
        }
    }
    return near;
}

world::VobId pick(const world::Scene& scene, std::span<const SceneInstance> instances, const Ray& ray)
{
    world::VobId best;
    f32 bestDistance = std::numeric_limits<f32>::max();
    const auto consider = [&](world::VobId id, std::optional<f32> distance)
    {
        if (id.valid() && distance && *distance < bestDistance)
        {
            best = id;
            bestDistance = *distance;
        }
    };
    for (const SceneInstance& instance : instances)
    {
        consider(instance.vob, intersect(ray, instance.bounds));
    }
    // Vobs without a mesh: a small box around their marker (start points at eye height).
    const auto marker = [&](const world::Vob& vob, const Vec3& centre, f32 half)
    { consider(vob.id, intersect(ray, AABB{centre - Vec3(half), centre + Vec3(half)})); };
    scene.each<world::Vob, world::StartPoint, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::StartPoint&, const world::WorldTransform& t)
        { marker(vob, Vec3(t.matrix[3]) + Vec3(0.0f, world::kStartEyeHeight * 0.5f, 0.0f), 0.5f); });
    scene.each<world::Vob, world::SoundEmitter, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::SoundEmitter&, const world::WorldTransform& t)
        { marker(vob, Vec3(t.matrix[3]), 0.3f); });
    scene.each<world::Vob, world::LightSource, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::LightSource&, const world::WorldTransform& t)
        { marker(vob, Vec3(t.matrix[3]), 0.3f); });
    scene.each<world::Vob, world::TriggerVolume, world::WorldTransform>(
        [&](entt::entity, const world::Vob& vob, const world::TriggerVolume& volume,
            const world::WorldTransform& t)
        {
            // In the trigger's own space: the box (or the sphere's cube) turned and scaled with it.
            const Mat4 inverse = glm::inverse(t.matrix);
            const Vec3 origin = Vec3(inverse * Vec4(ray.origin, 1.0f));
            const Vec3 direction = Vec3(inverse * Vec4(ray.direction, 0.0f));
            const f32 scale = glm::length(direction);
            const Vec3 half =
                volume.shape == world::TriggerVolume::Shape::Box ? volume.halfExtents : Vec3(volume.radius);
            if (const auto local = intersect({origin, direction / scale}, AABB{-half, half}))
            {
                consider(vob.id, *local / scale);
            }
        });
    return best;
}
} // namespace g7::editor
