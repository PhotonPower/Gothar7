// The editor's prefabs (M16 part F, decision 5): the selected vob with its children and the waynet entries
// near it as a template (assets/source/prefabs/<name>.g7prefab, world format), placed as loose copies in one
// undo step. Vobs from Blender (ADR 0023) are placed with the model in Editor::place.

#include <g7/core/FileSystem.hpp>
#include <g7/editor/Editor.hpp>
#include <g7/editor/Operations.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/world/Scene.hpp>
#include <g7/world/Terrain.hpp>

#include <algorithm>
#include <filesystem>
#include <format>

namespace g7::editor
{
namespace
{
constexpr std::string_view kPrefabSuffix = ".g7prefab";

bool validPrefabName(std::string_view name)
{
    return !name.empty() &&
           std::ranges::all_of(name, [](char c)
                               { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}
} // namespace

std::vector<std::string> Editor::prefabNames() const
{
    std::vector<std::string> names;
    std::error_code ec;
    if (m_prefabDirectory.empty() || !std::filesystem::is_directory(m_prefabDirectory, ec))
    {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(m_prefabDirectory, ec))
    {
        const std::string file = fs::toUtf8(entry.path().filename());
        if (entry.is_regular_file(ec) && file.ends_with(kPrefabSuffix))
        {
            names.push_back(file.substr(0, file.size() - kPrefabSuffix.size()));
        }
    }
    std::ranges::sort(names);
    return names;
}

Result<fs::Path> Editor::savePrefab(std::string_view name)
{
    if (!validPrefabName(name))
    {
        return Error{std::format("'{}': a prefab name is lower-case letters, digits and '_'", name)};
    }
    if (!m_selection.valid())
    {
        return Error{"select the vob to keep as a prefab (its children come along)"};
    }
    if (m_prefabDirectory.empty())
    {
        return Error{"no prefab folder (assets/source/prefabs) - the assets come from an archive"};
    }
    auto prefab = makePrefab(m_engine.scene(), std::vector{m_selection}, m_waynetData, std::string(name));
    if (!prefab)
    {
        m_status = prefab.error().message;
        return prefab.error();
    }
    std::error_code ec;
    std::filesystem::create_directories(m_prefabDirectory, ec);
    const fs::Path file = m_prefabDirectory / fs::fromUtf8(std::string(name) + std::string(kPrefabSuffix));
    if (auto written = fs::writeText(file, writePrefab(prefab.value())); !written)
    {
        m_status = written.error().message;
        return written.error();
    }
    m_status = std::format("prefab {}: {} vob(s), {} waynet entries", name, prefab.value().vobs.size(),
                           prefab.value().waynet ? prefab.value().waynet->points.size() +
                                                       prefab.value().waynet->freepoints.size()
                                                 : 0u);
    return file;
}

Result<PlacedPrefab> Editor::placePrefab(std::string_view name, std::optional<Vec3> at)
{
    const fs::Path file = m_prefabDirectory / fs::fromUtf8(std::string(name) + std::string(kPrefabSuffix));
    auto text = fs::readText(file);
    if (!text)
    {
        return text.error();
    }
    auto prefab = parsePrefab(text.value(), fs::toUtf8(file));
    if (!prefab)
    {
        m_status = prefab.error().message;
        return prefab.error();
    }
    if (!at)
    {
        const render::Camera& camera = m_engine.camera();
        const Vec3 f = camera.transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
        Vec3 p =
            camera.transform.position + glm::normalize(Vec3(f.x, 0.0f, f.z) + Vec3(1e-4f, 0.0f, 0.0f)) * 8.0f;
        p.y = m_engine.terrain() != nullptr ? m_engine.terrain()->heightAt(p.x, p.z) : 0.0f;
        at = p;
    }
    // One step: the new vobs (not there before) and the new waynet entries.
    world::Scene& scene = m_engine.scene();
    const auto names = prefabWaynetNames(m_waynetData, prefab.value());
    std::vector<std::string> newNames;
    for (const auto& [old, renamed] : names)
    {
        newNames.push_back(renamed);
    }
    Command command{std::format("place prefab {}", name), {}, {}, false};
    command.waynetBefore = captureWaynet(m_waynetData, newNames);
    world::WaynetData edited = m_waynetData;
    auto placed = editor::placePrefab(scene, edited, prefab.value(), *at);
    if (!placed)
    {
        m_status = placed.error().message;
        sceneChanged();
        return placed.error();
    }
    if (auto handed = m_engine.setWaynet(edited); !handed)
    {
        return handed.error();
    }
    m_waynetData = std::move(edited);
    for (const world::VobId id : placed.value().vobs)
    {
        command.before.push_back({id, std::nullopt});
    }
    command.after = captureVobs(scene, placed.value().vobs);
    command.waynetAfter = captureWaynet(m_waynetData, newNames, placed.value().waynetEdges);
    m_history.record(std::move(command));
    sceneChanged();
    if (!placed.value().vobs.empty())
    {
        select(placed.value().vobs.front());
    }
    m_status = std::format("placed prefab {}: {} vob(s), {} waynet entries", name, placed.value().vobs.size(),
                           placed.value().waynetNames.size());
    return placed;
}
} // namespace g7::editor
