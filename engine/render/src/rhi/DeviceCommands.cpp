#ifndef G7_RENDER_NO_GL

#include "GlMapping.hpp"

#include <g7/core/Assert.hpp>
#include <g7/render/Device.hpp>

namespace g7::render
{
using namespace rhi;

void Device::bindFramebuffer(const Framebuffer* framebuffer)
{
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer ? framebuffer->m_handle.id() : 0);
}

void Device::setViewport(i32 x, i32 y, u32 width, u32 height)
{
    glViewport(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
}

void Device::clear(std::optional<Vec4> color, std::optional<f32> depth)
{
    GLbitfield mask = 0;
    if (color)
    {
        glClearColor(color->r, color->g, color->b, color->a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (depth)
    {
        // Clears honour the depth/stencil write masks; enable them and record it in the cache.
        if (!m_cache.depthWrite)
        {
            glDepthMask(GL_TRUE);
            m_cache.depthWrite = true;
            m_cache.pipeline = 0; // the next bindPipeline restores the pipeline's mask
        }
        glStencilMask(0xFF);
        glClearDepth(static_cast<GLdouble>(*depth));
        glClearStencil(0);
        mask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    }
    if (mask != 0)
    {
        glClear(mask);
    }
}

void Device::bindPipeline(const Pipeline& pipeline)
{
    const u64 uid = pipeline.m_vertexArray.uid();
    const u64 programUid = pipeline.m_program->m_handle.uid();
    if (m_cache.valid && m_cache.pipeline == uid && m_cache.program == programUid)
    {
        return; // same pipeline and its program was not reloaded meanwhile
    }
    ++m_stats.pipelineChanges;
    const bool all = !m_cache.valid;

    if (all || m_cache.program != programUid)
    {
        glUseProgram(pipeline.m_program->m_handle.id());
        m_cache.program = programUid;
    }
    // Each pipeline owns its vertex array, so a pipeline change always rebinds it (comparing GL names
    // would be wrong once a deleted VAO name is reused).
    glBindVertexArray(pipeline.m_vertexArray.id());
    m_cache.vertexArray = pipeline.m_vertexArray.id();
    if (all || m_cache.cull != pipeline.m_cull)
    {
        if (pipeline.m_cull == CullMode::None)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(pipeline.m_cull == CullMode::Back ? GL_BACK : GL_FRONT);
        }
        glFrontFace(GL_CCW);
        m_cache.cull = pipeline.m_cull;
    }
    if (all || m_cache.depthTest != pipeline.m_depthTest)
    {
        pipeline.m_depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        m_cache.depthTest = pipeline.m_depthTest;
    }
    if (all || m_cache.depthWrite != pipeline.m_depthWrite)
    {
        glDepthMask(pipeline.m_depthWrite ? GL_TRUE : GL_FALSE);
        m_cache.depthWrite = pipeline.m_depthWrite;
    }
    if (all || m_cache.depthCompare != pipeline.m_depthCompare)
    {
        glDepthFunc(gl::compareOp(pipeline.m_depthCompare));
        m_cache.depthCompare = pipeline.m_depthCompare;
    }
    if (all || m_cache.blend != pipeline.m_blend)
    {
        switch (pipeline.m_blend)
        {
        case BlendMode::Opaque:
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        }
        m_cache.blend = pipeline.m_blend;
    }
    m_cache.topology = pipeline.m_topology;
    m_cache.pipeline = uid;
    m_cache.vertexStride = pipeline.m_vertexStride;
    m_cache.valid = true;
}

void Device::bindVertexBuffer(const Buffer& buffer, usize offset)
{
    G7_ASSERT(m_cache.valid && m_cache.vertexArray != 0, "bindVertexBuffer needs a bound pipeline");
    glVertexArrayVertexBuffer(m_cache.vertexArray, 0, buffer.m_handle.id(), static_cast<GLintptr>(offset),
                              static_cast<GLsizei>(m_cache.vertexStride));
}

void Device::bindIndexBuffer(const Buffer& buffer, IndexType type)
{
    G7_ASSERT(m_cache.valid && m_cache.vertexArray != 0, "bindIndexBuffer needs a bound pipeline");
    glVertexArrayElementBuffer(m_cache.vertexArray, buffer.m_handle.id());
    m_cache.indexType = type;
}

void Device::bindTexture(u32 unit, const Texture& texture, const Sampler& sampler)
{
    G7_ASSERT(unit < kMaxTextureUnits, "texture unit out of range");
    BoundTexture& bound = m_cache.textures[unit];
    if (bound.texture != texture.m_handle.uid())
    {
        glBindTextureUnit(unit, texture.m_handle.id());
        bound.texture = texture.m_handle.uid();
        ++m_stats.textureBinds;
    }
    if (bound.sampler != sampler.m_handle.uid())
    {
        glBindSampler(unit, sampler.m_handle.id());
        bound.sampler = sampler.m_handle.uid();
    }
}

void Device::bindUniformBuffer(u32 slot, const Buffer& buffer)
{
    glBindBufferBase(GL_UNIFORM_BUFFER, slot, buffer.m_handle.id());
}

void Device::draw(u32 vertexCount, u32 firstVertex)
{
    G7_ASSERT(m_cache.valid, "draw needs a bound pipeline");
    glDrawArrays(gl::topology(m_cache.topology), static_cast<GLint>(firstVertex),
                 static_cast<GLsizei>(vertexCount));
    ++m_stats.drawCalls;
    if (m_cache.topology == Topology::Triangles)
    {
        m_stats.triangles += vertexCount / 3;
    }
}

void Device::drawIndexed(u32 indexCount, u32 firstIndex)
{
    G7_ASSERT(m_cache.valid, "drawIndexed needs a bound pipeline");
    const auto offset = static_cast<usize>(firstIndex) * indexSize(m_cache.indexType);
    glDrawElements(gl::topology(m_cache.topology), static_cast<GLsizei>(indexCount),
                   gl::indexType(m_cache.indexType), reinterpret_cast<const void*>(offset));
    ++m_stats.drawCalls;
    if (m_cache.topology == Topology::Triangles)
    {
        m_stats.triangles += indexCount / 3;
    }
}

std::vector<u8> Device::readTexture(const Texture& texture, u32 level) const
{
    const TextureDesc& desc = texture.desc();
    if (level >= desc.mipLevels || (desc.format != Format::RGBA8 && desc.format != Format::RGBA8_SRGB))
    {
        return {};
    }
    std::vector<u8> pixels(static_cast<usize>(mipSize(desc.width, level)) * mipSize(desc.height, level) * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(texture.m_handle.id(), static_cast<GLint>(level), GL_RGBA, GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(pixels.size()), pixels.data());
    return pixels;
}

std::vector<u8> Device::readBuffer(const Buffer& buffer, usize offset, usize size) const
{
    if (offset > buffer.size() || size > buffer.size() - offset)
    {
        return {};
    }
    std::vector<u8> data(size);
    glGetNamedBufferSubData(buffer.m_handle.id(), static_cast<GLintptr>(offset),
                            static_cast<GLsizeiptr>(size), data.data());
    return data;
}
} // namespace g7::render

#endif
