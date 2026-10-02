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

Result<void> Texture::upload(u32 level, std::span<const u8> data)
{
    if (level >= m_desc.mipLevels)
    {
        return Error{"Texture::upload: mip level " + std::to_string(level) + " does not exist"};
    }
    const u32 width = mipSize(m_desc.width, level);
    const u32 height = mipSize(m_desc.height, level);
    const usize expected = static_cast<usize>(width) * height * bytesPerPixel(m_desc.format);
    if (data.size() != expected)
    {
        return Error{"Texture::upload: expected " + std::to_string(expected) + " bytes for level " +
                     std::to_string(level) + ", got " + std::to_string(data.size())};
    }
    const auto format = gl::textureFormat(m_desc.format);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_handle.id(), static_cast<GLint>(level), 0, 0, static_cast<GLsizei>(width),
                        static_cast<GLsizei>(height), format.format, format.type, data.data());
    return {};
}

void Texture::generateMipmaps()
{
    if (m_desc.mipLevels > 1)
    {
        glGenerateTextureMipmap(m_handle.id());
    }
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
} // namespace g7::render::rhi

#endif
