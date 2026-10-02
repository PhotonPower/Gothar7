#ifndef G7_RENDER_NO_GL

#include "GlMapping.hpp"

#include <g7/core/Log.hpp>
#include <g7/render/Device.hpp>

#include <algorithm>
#include <format>
#include <string>

namespace g7::render
{
using namespace rhi;

namespace
{
void deleteBuffer(u32 id)
{
    glDeleteBuffers(1, &id);
}
void deleteTexture(u32 id)
{
    glDeleteTextures(1, &id);
}
void deleteSampler(u32 id)
{
    glDeleteSamplers(1, &id);
}
void deleteProgram(u32 id)
{
    glDeleteProgram(id);
}
void deleteVertexArray(u32 id)
{
    glDeleteVertexArrays(1, &id);
}
void deleteFramebuffer(u32 id)
{
    glDeleteFramebuffers(1, &id);
}

std::string shaderLog(GLuint shader)
{
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<usize>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    log.resize(std::char_traits<char>::length(log.c_str()));
    return log;
}

std::string programLog(GLuint program)
{
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<usize>(std::max(length, 1)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    log.resize(std::char_traits<char>::length(log.c_str()));
    return log;
}

Result<GLuint> compileStage(GLenum stage, std::string_view source, std::string_view debugName)
{
    const GLuint shader = glCreateShader(stage);
    const GLchar* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        std::string log = shaderLog(shader);
        glDeleteShader(shader);
        return Error{"shader '" + std::string(debugName) + "' (" +
                     (stage == GL_VERTEX_SHADER ? "vertex" : "fragment") + ") failed to compile:\n" + log};
    }
    return shader;
}
} // namespace

Result<Buffer> Device::createBuffer(const BufferDesc& desc)
{
    if (desc.size == 0)
    {
        return Error{"buffer size must not be 0"};
    }
    if (!desc.initialData.empty() && desc.initialData.size() != desc.size)
    {
        return Error{"buffer initial data (" + std::to_string(desc.initialData.size()) +
                     " bytes) does not match its size (" + std::to_string(desc.size) + ")"};
    }
    GLuint id = 0;
    glCreateBuffers(1, &id);
    glNamedBufferStorage(id, static_cast<GLsizeiptr>(desc.size),
                         desc.initialData.empty() ? nullptr : desc.initialData.data(),
                         desc.usage == BufferUsage::Dynamic ? GL_DYNAMIC_STORAGE_BIT : 0);
    Buffer buffer;
    buffer.m_handle = Handle(id, deleteBuffer);
    buffer.m_size = desc.size;
    buffer.m_usage = desc.usage;
    return buffer;
}

Result<Texture> Device::createTexture(const TextureDesc& desc)
{
    if (desc.width == 0 || desc.height == 0)
    {
        return Error{"texture size must not be 0"};
    }
    TextureDesc resolved = desc;
    const u32 fullChain = mipLevelCount(desc.width, desc.height);
    resolved.mipLevels = desc.mipLevels == 0 ? fullChain : std::min(desc.mipLevels, fullChain);

    GLuint id = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    glTextureStorage2D(id, static_cast<GLsizei>(resolved.mipLevels),
                       gl::textureFormat(desc.format).internalFormat, static_cast<GLsizei>(desc.width),
                       static_cast<GLsizei>(desc.height));
    Texture texture;
    texture.m_handle = Handle(id, deleteTexture);
    texture.m_desc = resolved;
    return texture;
}

Result<Sampler> Device::createSampler(const SamplerDesc& desc)
{
    GLuint id = 0;
    glCreateSamplers(1, &id);
    glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER,
                        static_cast<GLint>(gl::minFilter(desc.minFilter, desc.mipFilter, true)));
    glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, desc.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(id, GL_TEXTURE_WRAP_S, static_cast<GLint>(gl::wrap(desc.wrapU)));
    glSamplerParameteri(id, GL_TEXTURE_WRAP_T, static_cast<GLint>(gl::wrap(desc.wrapV)));
    if (desc.maxAnisotropy > 1.0f && m_info.maxAnisotropy > 1.0f)
    {
        glSamplerParameterf(id, gl::kTextureMaxAnisotropy,
                            std::min(desc.maxAnisotropy, m_info.maxAnisotropy));
    }
    if (desc.compare)
    {
        glSamplerParameteri(id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(id, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(gl::compareOp(*desc.compare)));
    }
    Sampler sampler;
    sampler.m_handle = Handle(id, deleteSampler);
    return sampler;
}

Result<ShaderProgram> Device::createShaderProgram(const ShaderDesc& desc)
{
    auto vertex = compileStage(GL_VERTEX_SHADER, desc.vertexSource, desc.debugName);
    if (!vertex)
    {
        return vertex.error();
    }
    auto fragment = compileStage(GL_FRAGMENT_SHADER, desc.fragmentSource, desc.debugName);
    if (!fragment)
    {
        glDeleteShader(vertex.value());
        return fragment.error();
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex.value());
    glAttachShader(program, fragment.value());
    glLinkProgram(program);
    glDetachShader(program, vertex.value());
    glDetachShader(program, fragment.value());
    glDeleteShader(vertex.value());
    glDeleteShader(fragment.value());

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        std::string log = programLog(program);
        glDeleteProgram(program);
        return Error{"shader '" + std::string(desc.debugName) + "' failed to link:\n" + log};
    }
    glObjectLabel(GL_PROGRAM, program, static_cast<GLsizei>(desc.debugName.size()),
                  desc.debugName.data()); // RenderDoc
    ShaderProgram result;
    result.m_handle = Handle(program, deleteProgram);
    result.m_name = std::string(desc.debugName);
    return result;
}

Result<Pipeline> Device::createPipeline(const PipelineDesc& desc)
{
    if (!desc.program || !desc.program->m_handle)
    {
        return Error{"pipeline needs a shader program"};
    }
    if (!desc.attributes.empty() && desc.vertexStride == 0)
    {
        return Error{"pipeline with vertex attributes needs a vertex stride"};
    }
    GLuint vao = 0;
    glCreateVertexArrays(1, &vao);
    for (const VertexAttribute& attribute : desc.attributes)
    {
        const auto format = gl::vertexFormat(attribute.format);
        glEnableVertexArrayAttrib(vao, attribute.location);
        glVertexArrayAttribFormat(vao, attribute.location, format.components, format.type, format.normalized,
                                  attribute.offset);
        glVertexArrayAttribBinding(vao, attribute.location, 0); // single interleaved vertex buffer
    }

    Pipeline pipeline;
    pipeline.m_vertexArray = Handle(vao, deleteVertexArray);
    pipeline.m_program = desc.program->m_handle.id();
    pipeline.m_programUid = desc.program->m_handle.uid();
    pipeline.m_vertexStride = desc.vertexStride;
    pipeline.m_topology = desc.topology;
    pipeline.m_cull = desc.cull;
    pipeline.m_depthTest = desc.depthTest;
    pipeline.m_depthWrite = desc.depthWrite;
    pipeline.m_depthCompare = desc.depthCompare;
    pipeline.m_blend = desc.blend;
    return pipeline;
}

Result<Framebuffer> Device::createFramebuffer(const FramebufferDesc& desc)
{
    u32 width = 0;
    u32 height = 0;
    const auto checkSize = [&](const Texture& texture)
    {
        if (width == 0)
        {
            width = texture.desc().width;
            height = texture.desc().height;
        }
        return texture.desc().width == width && texture.desc().height == height;
    };
    for (const Texture* color : desc.colors)
    {
        if (!color || isDepthFormat(color->desc().format))
        {
            return Error{"framebuffer colour attachment must be a colour texture"};
        }
        if (!checkSize(*color))
        {
            return Error{"framebuffer attachments differ in size"};
        }
    }
    if (desc.depth && (!isDepthFormat(desc.depth->desc().format) || !checkSize(*desc.depth)))
    {
        return Error{"framebuffer depth attachment must be a depth texture of the same size"};
    }

    GLuint id = 0;
    glCreateFramebuffers(1, &id);
    Framebuffer framebuffer;
    framebuffer.m_handle = Handle(id, deleteFramebuffer);
    std::vector<GLenum> drawBuffers;
    for (usize i = 0; i < desc.colors.size(); ++i)
    {
        const auto attachment = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
        glNamedFramebufferTexture(id, attachment, desc.colors[i]->m_handle.id(), 0);
        drawBuffers.push_back(attachment);
    }
    if (desc.depth)
    {
        glNamedFramebufferTexture(
            id, hasStencil(desc.depth->desc().format) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
            desc.depth->m_handle.id(), 0);
    }
    if (drawBuffers.empty())
    {
        glNamedFramebufferDrawBuffer(id, GL_NONE); // depth-only (shadow maps)
        glNamedFramebufferReadBuffer(id, GL_NONE);
    }
    else
    {
        glNamedFramebufferDrawBuffers(id, static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    }

    const GLenum status = glCheckNamedFramebufferStatus(id, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        return Error{"framebuffer incomplete (status 0x" + std::format("{:04X}", status) + ")"};
    }
    framebuffer.m_width = width;
    framebuffer.m_height = height;
    return framebuffer;
}
} // namespace g7::render

#endif
