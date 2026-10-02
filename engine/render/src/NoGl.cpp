// Stubs for builds without glad (nodeps preset): Device::create() fails, so none of these run;
// they only satisfy the linker for code that references them (engine, ShaderLibrary).
#ifdef G7_RENDER_NO_GL

#include <g7/render/Device.hpp>

namespace g7::render
{
namespace
{
Error noGl()
{
    return Error{"render module was built without OpenGL"};
}
} // namespace

Result<rhi::Buffer> Device::createBuffer(const rhi::BufferDesc&)
{
    return noGl();
}
Result<rhi::Texture> Device::createTexture(const rhi::TextureDesc&)
{
    return noGl();
}
Result<rhi::Sampler> Device::createSampler(const rhi::SamplerDesc&)
{
    return noGl();
}
Result<rhi::ShaderProgram> Device::createShaderProgram(const rhi::ShaderDesc&)
{
    return noGl();
}
Result<rhi::Pipeline> Device::createPipeline(const rhi::PipelineDesc&)
{
    return noGl();
}
Result<rhi::Framebuffer> Device::createFramebuffer(const rhi::FramebufferDesc&)
{
    return noGl();
}
void Device::bindFramebuffer(const rhi::Framebuffer*)
{
}
void Device::setViewport(i32, i32, u32, u32)
{
}
void Device::clear(std::optional<Vec4>, std::optional<f32>)
{
}
void Device::bindPipeline(const rhi::Pipeline&)
{
}
void Device::bindVertexBuffer(const rhi::Buffer&, usize)
{
}
void Device::bindIndexBuffer(const rhi::Buffer&, rhi::IndexType)
{
}
void Device::bindTexture(u32, const rhi::Texture&, const rhi::Sampler&)
{
}
void Device::bindUniformBuffer(u32, const rhi::Buffer&)
{
}
void Device::draw(u32, u32)
{
}
void Device::drawIndexed(u32, u32)
{
}
std::vector<u8> Device::readBuffer(const rhi::Buffer&, usize, usize) const
{
    return {};
}
std::vector<u8> Device::readTexture(const rhi::Texture&, u32) const
{
    return {};
}
} // namespace g7::render

namespace g7::render::rhi
{
Result<void> Buffer::update(usize, std::span<const u8>)
{
    return Error{"render module was built without OpenGL"};
}
Result<void> Texture::upload(u32, std::span<const u8>)
{
    return Error{"render module was built without OpenGL"};
}
void Texture::generateMipmaps()
{
}
void ShaderProgram::setUniform(std::string_view, i32)
{
}
void ShaderProgram::setUniform(std::string_view, f32)
{
}
void ShaderProgram::setUniform(std::string_view, const Vec2&)
{
}
void ShaderProgram::setUniform(std::string_view, const Vec3&)
{
}
void ShaderProgram::setUniform(std::string_view, const Vec4&)
{
}
void ShaderProgram::setUniform(std::string_view, const Mat4&)
{
}
void ShaderProgram::setUniform(std::string_view, std::span<const i32>)
{
}
} // namespace g7::render::rhi

#endif
