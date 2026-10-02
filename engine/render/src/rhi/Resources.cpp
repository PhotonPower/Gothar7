#ifndef G7_RENDER_NO_GL

#include "GlMapping.hpp"

#include <g7/render/rhi/Resources.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <string>

namespace g7::render::rhi
{
Result<void> Buffer::update(usize offset, std::span<const u8> data)
{
    if (m_usage != BufferUsage::Dynamic)
    {
        return Error{"Buffer::update on a static buffer"};
    }
    if (offset > m_size || data.size() > m_size - offset)
    {
        return Error{"Buffer::update out of range (" + std::to_string(offset) + " + " +
                     std::to_string(data.size()) + " > " + std::to_string(m_size) + ")"};
    }
    if (!data.empty())
    {
        glNamedBufferSubData(m_handle.id(), static_cast<GLintptr>(offset),
                             static_cast<GLsizeiptr>(data.size()), data.data());
    }
    return {};
}

Result<void> Texture::upload(u32 level, std::span<const u8> data, u32 layer)
{
    if (level >= m_desc.mipLevels)
    {
        return Error{"Texture::upload: mip level " + std::to_string(level) + " does not exist"};
    }
    if (layer >= m_desc.layers)
    {
        return Error{"Texture::upload: layer " + std::to_string(layer) + " does not exist"};
    }
    const u32 width = mipSize(m_desc.width, level);
    const u32 height = mipSize(m_desc.height, level);
    const usize expected = imageSize(m_desc.format, width, height);
    if (data.size() != expected)
    {
        return Error{"Texture::upload: expected " + std::to_string(expected) + " bytes for level " +
                     std::to_string(level) + ", got " + std::to_string(data.size())};
    }
    const auto format = gl::textureFormat(m_desc.format);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const auto w = static_cast<GLsizei>(width);
    const auto h = static_cast<GLsizei>(height);
    const auto lv = static_cast<GLint>(level);
    if (m_desc.isArray())
    {
        const auto z = static_cast<GLint>(layer);
        if (isCompressed(m_desc.format))
        {
            glCompressedTextureSubImage3D(m_handle.id(), lv, 0, 0, z, w, h, 1, format.internalFormat,
                                          static_cast<GLsizei>(data.size()), data.data());
        }
        else
        {
            glTextureSubImage3D(m_handle.id(), lv, 0, 0, z, w, h, 1, format.format, format.type, data.data());
        }
        return {};
    }
    if (isCompressed(m_desc.format))
    {
        glCompressedTextureSubImage2D(m_handle.id(), lv, 0, 0, w, h, format.internalFormat,
                                      static_cast<GLsizei>(data.size()), data.data());
        return {};
    }
    glTextureSubImage2D(m_handle.id(), lv, 0, 0, w, h, format.format, format.type, data.data());
    return {};
}

Result<void> Texture::generateMipmaps()
{
    if (isCompressed(m_desc.format))
    {
        return Error{"Texture::generateMipmaps: compressed textures bring their own mip levels"};
    }
    if (m_desc.mipLevels > 1)
    {
        glGenerateTextureMipmap(m_handle.id());
    }
    return {};
}

i32 ShaderProgram::location(std::string_view uniformName)
{
    const std::string key(uniformName);
    if (const auto it = m_locations.find(key); it != m_locations.end())
    {
        return it->second;
    }
    const i32 location = glGetUniformLocation(m_handle.id(), key.c_str());
    m_locations.emplace(key, location);
    return location;
}

void ShaderProgram::setUniform(std::string_view uniformName, i32 value)
{
    glProgramUniform1i(m_handle.id(), location(uniformName), value);
}

void ShaderProgram::setUniform(std::string_view uniformName, f32 value)
{
    glProgramUniform1f(m_handle.id(), location(uniformName), value);
}

void ShaderProgram::setUniform(std::string_view uniformName, const Vec2& value)
{
    glProgramUniform2fv(m_handle.id(), location(uniformName), 1, glm::value_ptr(value));
}

void ShaderProgram::setUniform(std::string_view uniformName, const Vec3& value)
{
    glProgramUniform3fv(m_handle.id(), location(uniformName), 1, glm::value_ptr(value));
}

void ShaderProgram::setUniform(std::string_view uniformName, const Vec4& value)
{
    glProgramUniform4fv(m_handle.id(), location(uniformName), 1, glm::value_ptr(value));
}

void ShaderProgram::setUniform(std::string_view uniformName, const Mat4& value)
{
    glProgramUniformMatrix4fv(m_handle.id(), location(uniformName), 1, GL_FALSE, glm::value_ptr(value));
}

void ShaderProgram::setUniform(std::string_view uniformName, std::span<const i32> values)
{
    glProgramUniform1iv(m_handle.id(), location(uniformName), static_cast<GLsizei>(values.size()),
                        values.data());
}

void ShaderProgram::setUniform(std::string_view uniformName, std::span<const f32> values)
{
    glProgramUniform1fv(m_handle.id(), location(uniformName), static_cast<GLsizei>(values.size()),
                        values.data());
}
} // namespace g7::render::rhi

#endif
