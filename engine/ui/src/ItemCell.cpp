#include <g7/core/Config.hpp>
#include <g7/ui/ItemCell.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace g7::ui
{
namespace
{
constexpr std::array<std::string_view, 4> kFields = {"yaw", "pitch", "roll", "fill"};

Mat3 rotationOf(const InventoryView& view)
{
    const f32 y = toRadians(view.yaw);
    const f32 p = toRadians(view.pitch);
    const f32 r = toRadians(view.roll);
    // Row-major as written in the contract, transposed into glm's columns.
    const Mat3 ry = glm::transpose(
        Mat3(std::cos(y), 0.0f, std::sin(y), 0.0f, 1.0f, 0.0f, -std::sin(y), 0.0f, std::cos(y)));
    const Mat3 rx = glm::transpose(
        Mat3(1.0f, 0.0f, 0.0f, 0.0f, std::cos(p), -std::sin(p), 0.0f, std::sin(p), std::cos(p)));
    const Mat3 rz = glm::transpose(
        Mat3(std::cos(r), -std::sin(r), 0.0f, std::sin(r), std::cos(r), 0.0f, 0.0f, 0.0f, 1.0f));
    return rz * rx * ry;
}

Vec2 image(const Mat3& rotation, const Vec3& centre, f32 distance, f32 t, const Vec3& v)
{
    const Vec3 p = rotation * v - centre;
    return Vec2(p.x, p.y) / ((distance - p.z) * t);
}
} // namespace

Result<InventoryViews> InventoryViews::load(std::string_view toml, std::string_view sourceName)
{
    auto parsed = Config::parse(toml, sourceName);
    if (!parsed)
    {
        return parsed.error();
    }
    const Config& c = parsed.value();
    const auto fail = [&](const std::string& what) { return Error{std::string(sourceName) + ": " + what}; };
    if (c.get<i64>("version", 0) != 2)
    {
        return fail("version must be 2");
    }
    InventoryViews views;
    views.m_fov = static_cast<f32>(c.get<f64>("camera.fov", 30.0));
    if (!(views.m_fov > 1.0f && views.m_fov < 170.0f))
    {
        return fail("camera.fov must be 1..170 degrees");
    }
    for (const std::string_view field : kFields)
    {
        const auto value = c.find<f64>("default." + std::string(field));
        if (!value)
        {
            return fail("[default] needs yaw, pitch, roll and fill");
        }
        const auto v = static_cast<f32>(*value);
        if (field == "yaw")
        {
            views.m_default.yaw = v;
        }
        else if (field == "pitch")
        {
            views.m_default.pitch = v;
        }
        else if (field == "roll")
        {
            views.m_default.roll = v;
        }
        else
        {
            views.m_default.fill = v;
        }
    }
    const auto partial = [&](const Config& table, const std::string& where) -> Result<Partial>
    {
        Partial p;
        const auto number = [&](std::string_view field) -> std::optional<f32>
        {
            const auto value = table.find<f64>(field);
            return value ? std::optional<f32>(static_cast<f32>(*value)) : std::nullopt;
        };
        p.yaw = number("yaw");
        p.pitch = number("pitch");
        p.roll = number("roll");
        p.fill = number("fill");
        p.category = table.get<std::string>("category", "");
        if (p.fill && !(*p.fill >= 0.1f && *p.fill <= 1.0f))
        {
            return fail(where + ".fill must be 0.1..1");
        }
        return p;
    };
    if (!(views.m_default.fill >= 0.1f && views.m_default.fill <= 1.0f))
    {
        return fail("default.fill must be 0.1..1");
    }
    for (const std::string& name : c.keys("category"))
    {
        const auto table = c.child("category", name);
        if (!table)
        {
            return fail("category." + name + " must be a table");
        }
        auto p = partial(*table, "category." + name);
        if (!p)
        {
            return p.error();
        }
        views.m_categories.emplace(name, std::move(p).value());
    }
    // Model keys are paths with dots and slashes: each table is taken by its exact name.
    for (const std::string& name : c.keys("model"))
    {
        const auto table = c.child("model", name);
        if (!table)
        {
            return fail("model." + name + " must be a table");
        }
        auto p = partial(*table, "model." + name);
        if (!p)
        {
            return p.error();
        }
        views.m_models.emplace(name, std::move(p).value());
    }
    return views;
}

void InventoryViews::apply(InventoryView& view, const Partial& partial)
{
    view.yaw = partial.yaw.value_or(view.yaw);
    view.pitch = partial.pitch.value_or(view.pitch);
    view.roll = partial.roll.value_or(view.roll);
    view.fill = partial.fill.value_or(view.fill);
}

InventoryView InventoryViews::resolve(std::string_view mesh, std::string_view itemCategory) const
{
    InventoryView view = m_default;
    const auto model = m_models.find(mesh);
    const std::string_view category = model != m_models.end() && !model->second.category.empty()
                                          ? std::string_view(model->second.category)
                                          : itemCategory;
    if (const auto it = m_categories.find(category); it != m_categories.end())
    {
        apply(view, it->second);
    }
    if (model != m_models.end())
    {
        apply(view, model->second);
    }
    return view;
}

Mat4 CellFrame::modelMatrix(f32 spinRadians) const noexcept
{
    Mat4 m(rotation);
    m[3] = Vec4(-centre, 1.0f);
    if (spinRadians != 0.0f)
    {
        // About the view's Y axis through the centre: after centring.
        m = glm::rotate(Mat4(1.0f), spinRadians, Vec3(0.0f, 1.0f, 0.0f)) * m;
    }
    return m;
}

Result<CellFrame> computeCell(std::span<const Vec3> lod0Positions, const InventoryView& view, f32 fovDegrees)
{
    if (lod0Positions.empty())
    {
        return Error{"no vertices"};
    }
    CellFrame frame;
    frame.rotation = rotationOf(view);
    frame.fovDegrees = fovDegrees;
    const f32 t = std::tan(toRadians(fovDegrees) * 0.5f);
    Vec3 lo(std::numeric_limits<f32>::max());
    Vec3 hi(std::numeric_limits<f32>::lowest());
    for (const Vec3& v : lod0Positions)
    {
        const Vec3 turned = frame.rotation * v;
        lo = glm::min(lo, turned);
        hi = glm::max(hi, turned);
    }
    frame.centre = (lo + hi) * 0.5f;
    f32 distance = std::numeric_limits<f32>::lowest();
    f32 nearest = std::numeric_limits<f32>::lowest();
    for (const Vec3& v : lod0Positions)
    {
        const Vec3 p = frame.rotation * v - frame.centre;
        distance = std::max(distance, p.z + std::max(std::abs(p.x), std::abs(p.y)) / t);
        nearest = std::max(nearest, p.z);
    }
    if (!(distance > nearest))
    {
        return Error{"degenerate model: no extent across the view"};
    }
    frame.distance = distance;
    Vec2 m(std::numeric_limits<f32>::max());
    Vec2 M(std::numeric_limits<f32>::lowest());
    for (const Vec3& v : lod0Positions)
    {
        const Vec2 u = image(frame.rotation, frame.centre, distance, t, v);
        m = glm::min(m, u);
        M = glm::max(M, u);
    }
    frame.window = (m + M) * 0.5f;
    const Vec2 extent = M - m;
    frame.halfSize = std::max(extent.x, extent.y) / (2.0f * std::clamp(view.fill, 0.1f, 1.0f));
    if (!(frame.halfSize > 0.0f))
    {
        return Error{"degenerate model: no extent across the view"};
    }
    return frame;
}

Vec2 projectInCell(const CellFrame& frame, const Vec3& vertex) noexcept
{
    const f32 t = std::tan(toRadians(frame.fovDegrees) * 0.5f);
    return (image(frame.rotation, frame.centre, frame.distance, t, vertex) - frame.window) / frame.halfSize;
}
} // namespace g7::ui
