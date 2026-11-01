// Multi-draw batches of MeshRenderer (M4 visibility B2): submeshes of arena meshes grouped by
// pipeline, material values and geometry block, one glMultiDrawElementsIndirect per group. Each draw
// finds its model matrix and point lights in a storage buffer through its baseInstance (see
// shaders/common/draws.glsl).

#include <g7/core/Log.hpp>
#include <g7/render/Camera.hpp>
#include <g7/render/Device.hpp>
#include <g7/render/Lighting.hpp>
#include <g7/render/Material.hpp>
#include <g7/render/Mesh.hpp>
#include <g7/render/Shadows.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <numeric>

namespace g7::render
{
namespace
{
/// std430 layout of DrawData in common/draws.glsl.
struct GpuDrawData
{
    Mat4 model{1.0f};
    Mat4 normalMatrix{1.0f}; // transpose(inverse(model)), once per draw instead of per vertex
    std::array<i32, LightList::kMaxPerObject> lights{};
    std::array<i32, 4> counts{};
};
static_assert(sizeof(GpuDrawData) == 176);
static_assert(LightList::kMaxPerObject == 8, "draws.glsl holds 8 light indices per draw");

u32 bitsOf(f32 value) noexcept
{
    return std::bit_cast<u32>(value);
}

template <typename T>
std::span<const u8> bytesOf(const std::vector<T>& v)
{
    return {reinterpret_cast<const u8*>(v.data()), v.size() * sizeof(T)};
}

/// The items of a pass with equal models and levels of detail next to each other (their order otherwise
/// kept), so their draw records follow each other and equal submeshes merge into one instanced command.
void orderByModel(std::span<const MeshDrawItem> items, std::vector<const MeshDrawItem*>& out)
{
    out.clear();
    for (const MeshDrawItem& item : items)
    {
        out.push_back(&item);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const MeshDrawItem* a, const MeshDrawItem* b)
                     {
                         if (a->mesh != b->mesh)
                         {
                             return std::less<const Mesh*>{}(a->mesh, b->mesh);
                         }
                         return a->lod < b->lod;
                     });
}
} // namespace

void MeshRenderer::bindMaterial(rhi::ShaderProgram& program, Device& device, const Material& material)
{
    program.setUniform("uBaseColor", material.baseColorFactor);
    program.setUniform("uEmissive", material.emissiveFactor);
    program.setUniform("uNormalScale", material.normalScale);
    program.setUniform("uNormalTwoChannel", material.normalTwoChannel ? 1 : 0);
    program.setUniform("uAlphaCutoff", material.alphaCutoff);
    device.bindTexture(0, *material.baseColor, m_sampler);
    device.bindTexture(1, *material.normal, m_sampler);
    device.bindTexture(2, *material.emissive, m_sampler);
}

void MeshRenderer::drawBatched(Device& device, std::span<const MeshDrawItem> items, const Camera& camera)
{
    m_entries.clear();
    m_drawData.clear();
    m_singles.clear();
    m_batch = {};
    std::vector<std::pair<const MeshDrawItem*, usize>> translucent;
    orderByModel(items, m_order); // equal models and LODs together: their draws become instances
    for (const MeshDrawItem* each : m_order)
    {
        const MeshDrawItem& item = *each;
        const auto submeshes = item.mesh->submeshes();
        if (item.mesh->arena() == nullptr)
        {
            for (usize i = 0; i < submeshes.size(); ++i)
            {
                if (!item.mesh->inLod(i, item.lod))
                {
                    continue;
                }
                ((*item.materials)[submeshes[i].material].alphaMode == asset::AlphaMode::Blend ? translucent
                                                                                               : m_singles)
                    .emplace_back(&item, i);
            }
            continue;
        }
        GpuDrawData data;
        data.model = item.model;
        data.normalMatrix = Mat4(glm::transpose(glm::inverse(Mat3(item.model))));
        m_selected.clear();
        if (m_lights != nullptr)
        {
            m_lights->selectFor(item.bounds, m_selected);
        }
        for (usize i = 0; i < m_selected.size(); ++i)
        {
            data.lights[i] = static_cast<i32>(m_selected[i]);
        }
        data.counts[0] = static_cast<i32>(m_selected.size());
        const auto drawIndex = static_cast<u32>(m_drawData.size() / sizeof(GpuDrawData));
        m_drawData.insert(m_drawData.end(), reinterpret_cast<const u8*>(&data),
                          reinterpret_cast<const u8*>(&data) + sizeof(data));
        for (usize i = 0; i < submeshes.size(); ++i)
        {
            if (!item.mesh->inLod(i, item.lod))
            {
                continue; // another level of detail
            }
            const Material& material = (*item.materials)[submeshes[i].material];
            if (material.alphaMode == asset::AlphaMode::Blend)
            {
                translucent.emplace_back(&item, i); // blending needs what lies behind: drawn last
                continue;
            }
            BatchEntry entry;
            const u64 variant = material.alphaMode == asset::AlphaMode::Mask ? AlphaTest : Opaque;
            entry.key.blockAndFlags = (u64(item.mesh->arenaBlock()) << 32) | variant |
                                      (material.doubleSided ? 1u << 8 : 0u) |
                                      (material.normalTwoChannel ? 1u << 9 : 0u);
            entry.key.textures = {reinterpret_cast<u64>(material.baseColor),
                                  reinterpret_cast<u64>(material.normal),
                                  reinterpret_cast<u64>(material.emissive)};
            const Vec4& c = material.baseColorFactor;
            const Vec3& e = material.emissiveFactor;
            entry.key.values = {bitsOf(c.r),
                                bitsOf(c.g),
                                bitsOf(c.b),
                                bitsOf(c.a),
                                bitsOf(e.r),
                                bitsOf(e.g),
                                bitsOf(e.b),
                                bitsOf(material.normalScale),
                                bitsOf(material.alphaCutoff)};
            entry.command = item.mesh->drawRecord(i);
            entry.command.baseInstance = drawIndex;
            entry.mesh = item.mesh;
            entry.material = &material;
            m_entries.push_back(entry);
        }
    }
    uploadBatch(device);
    drawGroups(device, false, camera.viewProjection(), camera.transform.position);

    // One by one: meshes outside an arena, then translucent submeshes.
    m_singles.insert(m_singles.end(), translucent.begin(), translucent.end());
    for (const auto& [item, submesh] : m_singles)
    {
        draw(device, *item->mesh, *item->materials, item->model, camera, static_cast<i32>(submesh),
             item->lod);
        ++m_batch.singleDraws;
    }
}

void MeshRenderer::drawShadowBatched(Device& device, std::span<const MeshDrawItem> items,
                                     const Cascade& cascade)
{
    m_entries.clear();
    m_drawData.clear();
    m_batch = {};
    orderByModel(items, m_order); // equal models and LODs together: their draws become instances
    for (const MeshDrawItem* each : m_order)
    {
        const MeshDrawItem& item = *each;
        if (item.mesh->arena() == nullptr)
        {
            drawShadow(device, *item.mesh, *item.materials, item.model, cascade, item.lod);
            ++m_batch.singleDraws;
            continue;
        }
        GpuDrawData data;
        data.model = item.model;
        const auto drawIndex = static_cast<u32>(m_drawData.size() / sizeof(GpuDrawData));
        m_drawData.insert(m_drawData.end(), reinterpret_cast<const u8*>(&data),
                          reinterpret_cast<const u8*>(&data) + sizeof(data));
        const auto submeshes = item.mesh->submeshes();
        for (usize i = 0; i < submeshes.size(); ++i)
        {
            const Material& material = (*item.materials)[submeshes[i].material];
            if (material.alphaMode == asset::AlphaMode::Blend || !item.mesh->inLod(i, item.lod))
            {
                continue; // translucent surfaces cast no shadow; other levels of detail
            }
            // Depth only: opaque submeshes of a block share one group; alpha-tested ones need their
            // base colour (texture, alpha, cutoff).
            BatchEntry entry;
            const bool alphaTest = material.alphaMode == asset::AlphaMode::Mask;
            entry.key.blockAndFlags = (u64(item.mesh->arenaBlock()) << 32) | (alphaTest ? AlphaTest : Opaque);
            if (alphaTest)
            {
                entry.key.textures[0] = reinterpret_cast<u64>(material.baseColor);
                entry.key.values[3] = bitsOf(material.baseColorFactor.a);
                entry.key.values[8] = bitsOf(material.alphaCutoff);
            }
            entry.command = item.mesh->drawRecord(i);
            entry.command.baseInstance = drawIndex;
            entry.mesh = item.mesh;
            entry.material = &material;
            m_entries.push_back(entry);
        }
    }
    uploadBatch(device);
    drawGroups(device, true, cascade.viewProjection, Vec3(0.0f));
}

void MeshRenderer::uploadBatch(Device& device)
{
    // Groups are runs of equal keys; within a group the submission order stays.
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [](const BatchEntry& a, const BatchEntry& b) { return a.key < b.key; });
    // This pass goes behind the earlier passes of the frame: data records and commands each from their base.
    const usize dataBase = m_frameRecords;
    const usize dataCount = m_drawData.size() / sizeof(GpuDrawData);
    // Per group (a run of equal keys) its commands; within a group the same submesh of consecutive draw
    // records (the same model and LOD, ordered together by orderByModel) becomes one command with more
    // instances.
    m_commands.clear();
    m_groups.clear();
    for (usize i = 0; i < m_entries.size(); ++i)
    {
        const BatchEntry& entry = m_entries[i];
        if (m_groups.empty() || !(entry.key == m_entries[m_groups.back().firstEntry].key))
        {
            m_groups.push_back({i, m_commands.size(), 0, 0, 0});
        }
        BatchGroup& group = m_groups.back();
        DrawIndexedIndirect command = entry.command;
        command.baseInstance += static_cast<u32>(dataBase);
        if (group.commands > 0)
        {
            DrawIndexedIndirect& last = m_commands.back();
            if (last.indexCount == command.indexCount && last.firstIndex == command.firstIndex &&
                last.baseVertex == command.baseVertex &&
                last.baseInstance + last.instanceCount == command.baseInstance)
            {
                last.instanceCount += command.instanceCount;
                ++group.entries;
                group.triangles += command.indexCount / 3;
                continue;
            }
        }
        m_commands.push_back(command);
        ++group.commands;
        ++group.entries;
        group.triangles += command.indexCount / 3;
    }
    m_batchBase = dataBase + dataCount; // commands follow the data records in record numbering
    const usize needed = m_batchBase + m_commands.size();
    BatchBuffers& buffers = m_batchBuffers[m_batchFrame];
    if (needed > buffers.capacity)
    {
        // Grow to the next power of two. Passes already issued keep the old buffers alive in the driver.
        const usize capacity = std::bit_ceil(std::max<usize>(needed, 4096));
        auto draws = device.createBuffer({capacity * sizeof(GpuDrawData), rhi::BufferUsage::Dynamic, {}});
        auto commands =
            device.createBuffer({capacity * sizeof(DrawIndexedIndirect), rhi::BufferUsage::Dynamic, {}});
        std::vector<u32> values(capacity);
        std::iota(values.begin(), values.end(), 0u);
        auto indices =
            device.createBuffer({capacity * sizeof(u32), rhi::BufferUsage::Static, bytesOf(values)});
        if (!draws || !commands || !indices)
        {
            G7_LOG_ERROR("render", "cannot grow the multi-draw buffers to {} draws", capacity);
            m_entries.clear();
            return;
        }
        buffers = {std::move(draws).value(), std::move(commands).value(), std::move(indices).value(),
                   capacity};
        // The earlier passes' data is in the old buffers; this pass starts afresh in the new ones.
        m_frameRecords = 0;
        return uploadBatch(device);
    }
    if (!m_drawData.empty())
    {
        (void)buffers.draws.update(dataBase * sizeof(GpuDrawData), m_drawData);
    }
    if (!m_commands.empty())
    {
        (void)buffers.commands.update(m_batchBase * sizeof(DrawIndexedIndirect), bytesOf(m_commands));
    }
    m_frameRecords = needed;
}

void MeshRenderer::beginFrame() noexcept
{
    m_batchFrame = (m_batchFrame + 1) % static_cast<u32>(m_batchBuffers.size());
    m_frameRecords = 0;
}

void MeshRenderer::drawGroups(Device& device, bool shadow, const Mat4& viewProjection,
                              const Vec3& cameraPosition)
{
    if (!shadow)
    {
        device.bindUniformBuffer(0, m_lightingBuffer);
        if (m_shadowMap)
        {
            device.bindTexture(3, m_shadowMap->texture(), m_shadowMap->sampler());
        }
    }
    for (const BatchGroup& group : m_groups)
    {
        const BatchEntry& head = m_entries[group.firstEntry];
        const bool alphaTest = (head.key.blockAndFlags & 0xFF) == AlphaTest;
        rhi::ShaderProgram* program = shadow
                                          ? (alphaTest ? m_multiShadowAlphaTestProgram : m_multiShadowProgram)
                                      : alphaTest ? m_multiAlphaTestProgram
                                                  : m_multiProgram;
        const rhi::Pipeline& pipeline =
            shadow ? m_multiShadowPipelines[alphaTest ? 1 : 0]
                   : m_multiPipelines[static_cast<usize>(alphaTest ? AlphaTest : Opaque) * 2 +
                                      (head.material->doubleSided ? 1 : 0)];
        device.bindPipeline(pipeline);
        head.mesh->bind(device);
        device.bindInstanceBuffer(m_batchBuffers[m_batchFrame].indices);
        device.bindStorageBuffer(0, m_batchBuffers[m_batchFrame].draws);
        program->setUniform("uViewProjection", viewProjection);
        if (shadow)
        {
            if (alphaTest)
            {
                program->setUniform("uBaseColor", head.material->baseColorFactor);
                program->setUniform("uAlphaCutoff", head.material->alphaCutoff);
                device.bindTexture(0, *head.material->baseColor, m_sampler);
            }
        }
        else
        {
            program->setUniform("uCameraPosition", cameraPosition);
            bindMaterial(*program, device, *head.material);
        }
        device.multiDrawIndexedIndirect(m_batchBuffers[m_batchFrame].commands,
                                        (m_batchBase + group.firstCommand) * sizeof(DrawIndexedIndirect),
                                        group.commands, group.triangles);
        ++m_batch.groups;
        m_batch.batchedDraws += group.entries;
        m_batch.commands += group.commands;
    }
}
} // namespace g7::render
