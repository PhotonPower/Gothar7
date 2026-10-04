#include <g7/core/Log.hpp>
#include <g7/platform/Input.hpp>
#include <g7/render/ShaderLibrary.hpp>
#include <g7/ui/DebugUi.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

namespace g7::ui
{
namespace
{
using platform::Key;

constexpr usize kFrameHistory = 240;

ImGuiKey toImGui(Key key) noexcept
{
    const auto k = static_cast<u16>(key);
    if (key >= Key::A && key <= Key::Z)
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + (k - static_cast<u16>(Key::A)));
    }
    if (key >= Key::Num0 && key <= Key::Num9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (k - static_cast<u16>(Key::Num0)));
    }
    if (key >= Key::F1 && key <= Key::F12)
    {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (k - static_cast<u16>(Key::F1)));
    }
    if (key >= Key::Keypad0 && key <= Key::Keypad9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (k - static_cast<u16>(Key::Keypad0)));
    }
    switch (key)
    {
    case Key::Escape:
        return ImGuiKey_Escape;
    case Key::Enter:
        return ImGuiKey_Enter;
    case Key::Space:
        return ImGuiKey_Space;
    case Key::Tab:
        return ImGuiKey_Tab;
    case Key::Backspace:
        return ImGuiKey_Backspace;
    case Key::Insert:
        return ImGuiKey_Insert;
    case Key::Delete:
        return ImGuiKey_Delete;
    case Key::Home:
        return ImGuiKey_Home;
    case Key::End:
        return ImGuiKey_End;
    case Key::PageUp:
        return ImGuiKey_PageUp;
    case Key::PageDown:
        return ImGuiKey_PageDown;
    case Key::Up:
        return ImGuiKey_UpArrow;
    case Key::Down:
        return ImGuiKey_DownArrow;
    case Key::Left:
        return ImGuiKey_LeftArrow;
    case Key::Right:
        return ImGuiKey_RightArrow;
    case Key::LeftShift:
        return ImGuiKey_LeftShift;
    case Key::RightShift:
        return ImGuiKey_RightShift;
    case Key::LeftCtrl:
        return ImGuiKey_LeftCtrl;
    case Key::RightCtrl:
        return ImGuiKey_RightCtrl;
    case Key::LeftAlt:
        return ImGuiKey_LeftAlt;
    case Key::RightAlt:
        return ImGuiKey_RightAlt;
    case Key::CapsLock:
        return ImGuiKey_CapsLock;
    case Key::Grave:
        return ImGuiKey_GraveAccent;
    case Key::Minus:
        return ImGuiKey_Minus;
    case Key::Equals:
        return ImGuiKey_Equal;
    case Key::LeftBracket:
        return ImGuiKey_LeftBracket;
    case Key::RightBracket:
        return ImGuiKey_RightBracket;
    case Key::Backslash:
        return ImGuiKey_Backslash;
    case Key::Semicolon:
        return ImGuiKey_Semicolon;
    case Key::Apostrophe:
        return ImGuiKey_Apostrophe;
    case Key::Comma:
        return ImGuiKey_Comma;
    case Key::Period:
        return ImGuiKey_Period;
    case Key::Slash:
        return ImGuiKey_Slash;
    case Key::KeypadPlus:
        return ImGuiKey_KeypadAdd;
    case Key::KeypadMinus:
        return ImGuiKey_KeypadSubtract;
    case Key::KeypadMultiply:
        return ImGuiKey_KeypadMultiply;
    case Key::KeypadDivide:
        return ImGuiKey_KeypadDivide;
    case Key::KeypadEnter:
        return ImGuiKey_KeypadEnter;
    case Key::KeypadPeriod:
        return ImGuiKey_KeypadDecimal;
    case Key::Pause:
        return ImGuiKey_Pause;
    case Key::PrintScreen:
        return ImGuiKey_PrintScreen;
    default:
        return ImGuiKey_None;
    }
}

struct ImGuiVertex
{
    Vec2 position;
    Vec2 uv;
    u32 color;
};
static_assert(sizeof(ImGuiVertex) == sizeof(ImDrawVert));
static_assert(sizeof(ImDrawIdx) == 2, "the renderer binds 16-bit indices");

/// Draws ImGui's output through the RHI and owns the textures ImGui asks for (font atlas).
class Renderer
{
public:
    Result<void> init(render::Device& device, render::ShaderLibrary& shaders)
    {
        auto program = shaders.load("imgui", {"imgui.vert", "imgui.frag", {}});
        if (!program)
        {
            return program.error();
        }
        m_program = program.value();
        render::rhi::PipelineDesc desc;
        desc.program = m_program;
        desc.attributes = {{0, render::rhi::VertexFormat::Float2, 0},
                           {1, render::rhi::VertexFormat::Float2, 8},
                           {2, render::rhi::VertexFormat::UNorm8x4, 16}};
        desc.vertexStride = sizeof(ImGuiVertex);
        desc.cull = render::rhi::CullMode::None;
        desc.depthTest = false;
        desc.depthWrite = false;
        desc.blend = render::rhi::BlendMode::Alpha;
        auto pipeline = device.createPipeline(desc);
        if (!pipeline)
        {
            return pipeline.error();
        }
        m_pipeline = std::move(pipeline).value();
        render::rhi::SamplerDesc samplerDesc;
        samplerDesc.mipFilter = render::rhi::Filter::Nearest;
        samplerDesc.wrapU = samplerDesc.wrapV = render::rhi::Wrap::Clamp;
        auto sampler = device.createSampler(samplerDesc);
        if (!sampler)
        {
            return sampler.error();
        }
        m_sampler = std::move(sampler).value();
        return {};
    }

    void render(render::Device& device, ImDrawData& data)
    {
        if (data.Textures != nullptr)
        {
            for (ImTextureData* texture : *data.Textures)
            {
                updateTexture(device, *texture);
            }
        }
        const Vec2 scale(data.FramebufferScale.x, data.FramebufferScale.y);
        const auto width = static_cast<u32>(data.DisplaySize.x * scale.x);
        const auto height = static_cast<u32>(data.DisplaySize.y * scale.y);
        if (width == 0 || height == 0 || data.TotalVtxCount == 0)
        {
            return;
        }

        // All lists into one vertex and one index buffer.
        m_vertices.clear();
        m_indices.clear();
        for (const ImDrawList* list : data.CmdLists)
        {
            const auto* v = reinterpret_cast<const u8*>(list->VtxBuffer.Data);
            m_vertices.insert(m_vertices.end(), v, v + list->VtxBuffer.Size * sizeof(ImDrawVert));
            const auto* i = reinterpret_cast<const u8*>(list->IdxBuffer.Data);
            m_indices.insert(m_indices.end(), i, i + list->IdxBuffer.Size * sizeof(ImDrawIdx));
        }
        if (!reserve(device, m_vertexBuffer, m_vertices.size()) ||
            !reserve(device, m_indexBuffer, m_indices.size()) || !m_vertexBuffer.update(0, m_vertices) ||
            !m_indexBuffer.update(0, m_indices))
        {
            return;
        }

        // Orthographic projection: ImGui's display rectangle (+Y down) to clip space.
        const f32 l = data.DisplayPos.x;
        const f32 r = data.DisplayPos.x + data.DisplaySize.x;
        const f32 t = data.DisplayPos.y;
        const f32 b = data.DisplayPos.y + data.DisplaySize.y;
        Mat4 projection(1.0f);
        projection[0][0] = 2.0f / (r - l);
        projection[1][1] = 2.0f / (t - b);
        projection[2][2] = -1.0f;
        projection[3] = Vec4((r + l) / (l - r), (t + b) / (b - t), 0.0f, 1.0f);

        device.setViewport(0, 0, width, height);
        m_program->setUniform("uProjection", projection);
        device.bindPipeline(m_pipeline);
        device.bindVertexBuffer(m_vertexBuffer);
        device.bindIndexBuffer(m_indexBuffer, render::rhi::IndexType::U16);

        const Vec2 origin(data.DisplayPos.x, data.DisplayPos.y);
        u32 firstIndex = 0;
        i32 firstVertex = 0;
        for (const ImDrawList* list : data.CmdLists)
        {
            for (const ImDrawCmd& cmd : list->CmdBuffer)
            {
                if (cmd.UserCallback != nullptr)
                {
                    if (cmd.UserCallback != ImDrawCallback_ResetRenderState)
                    {
                        cmd.UserCallback(list, &cmd);
                    }
                    continue;
                }
                const Vec4 clip((cmd.ClipRect.x - origin.x) * scale.x, (cmd.ClipRect.y - origin.y) * scale.y,
                                (cmd.ClipRect.z - origin.x) * scale.x, (cmd.ClipRect.w - origin.y) * scale.y);
                const auto scissor = scissorFromClip(clip, width, height);
                const auto* texture = reinterpret_cast<const render::rhi::Texture*>(cmd.GetTexID());
                if (!scissor || texture == nullptr)
                {
                    continue;
                }
                device.setScissor(scissor);
                device.bindTexture(0, *texture, m_sampler);
                device.drawIndexed(cmd.ElemCount, firstIndex + cmd.IdxOffset,
                                   firstVertex + static_cast<i32>(cmd.VtxOffset));
            }
            firstIndex += static_cast<u32>(list->IdxBuffer.Size);
            firstVertex += list->VtxBuffer.Size;
        }
        device.setScissor(std::nullopt);
    }

    /// Releases every texture ImGui knows about (shutdown).
    void destroyTextures()
    {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        {
            if (texture->RefCount == 1)
            {
                release(*texture);
            }
        }
        m_textures.clear();
    }

private:
    bool reserve(render::Device& device, render::rhi::Buffer& buffer, usize bytes)
    {
        if (buffer.size() >= bytes)
        {
            return true;
        }
        auto grown = device.createBuffer(
            {std::max<usize>(bytes + bytes / 2, 64 * 1024), render::rhi::BufferUsage::Dynamic, {}});
        if (!grown)
        {
            G7_LOG_ERROR("ui", "imgui buffer: {}", grown.error().message);
            return false;
        }
        buffer = std::move(grown).value();
        return true;
    }

    void updateTexture(render::Device& device, ImTextureData& texture)
    {
        switch (texture.Status)
        {
        case ImTextureStatus_WantCreate:
        case ImTextureStatus_WantUpdates:
        {
            // Small atlases with rare updates: always upload the whole texture.
            render::rhi::Texture* target = reinterpret_cast<render::rhi::Texture*>(texture.TexID);
            if (target == nullptr)
            {
                auto created =
                    device.createTexture({static_cast<u32>(texture.Width), static_cast<u32>(texture.Height),
                                          render::rhi::Format::RGBA8, 1});
                if (!created)
                {
                    G7_LOG_ERROR("ui", "imgui texture: {}", created.error().message);
                    return;
                }
                m_textures.push_back(std::make_unique<render::rhi::Texture>(std::move(created).value()));
                target = m_textures.back().get();
            }
            std::span<const u8> pixels(texture.Pixels, static_cast<usize>(texture.Width) * texture.Height *
                                                           texture.BytesPerPixel);
            std::vector<u8> expanded;
            if (texture.Format == ImTextureFormat_Alpha8)
            {
                expanded.resize(pixels.size() * 4, 255);
                for (usize i = 0; i < pixels.size(); ++i)
                {
                    expanded[i * 4 + 3] = pixels[i];
                }
                pixels = expanded;
            }
            if (auto uploaded = target->upload(0, pixels); !uploaded)
            {
                G7_LOG_ERROR("ui", "imgui texture upload: {}", uploaded.error().message);
            }
            texture.SetTexID(reinterpret_cast<ImTextureID>(target));
            texture.SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantDestroy:
            if (texture.UnusedFrames > 0)
            {
                release(texture);
            }
            break;
        default:
            break;
        }
    }

    void release(ImTextureData& texture)
    {
        const auto* target = reinterpret_cast<const render::rhi::Texture*>(texture.TexID);
        std::erase_if(m_textures, [&](const auto& owned) { return owned.get() == target; });
        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }

    render::rhi::ShaderProgram* m_program = nullptr; // owned by the ShaderLibrary
    render::rhi::Pipeline m_pipeline;
    render::rhi::Sampler m_sampler;
    render::rhi::Buffer m_vertexBuffer;
    render::rhi::Buffer m_indexBuffer;
    std::vector<std::unique_ptr<render::rhi::Texture>> m_textures; // stable addresses = ImTextureIDs
    std::vector<u8> m_vertices;
    std::vector<u8> m_indices;
};
} // namespace

std::optional<render::PixelRect> scissorFromClip(const Vec4& clip, u32 framebufferWidth,
                                                 u32 framebufferHeight) noexcept
{
    const f32 x0 = std::max(clip.x, 0.0f);
    const f32 y0 = std::max(clip.y, 0.0f);
    const f32 x1 = std::min(clip.z, static_cast<f32>(framebufferWidth));
    const f32 y1 = std::min(clip.w, static_cast<f32>(framebufferHeight));
    if (x1 <= x0 || y1 <= y0)
    {
        return std::nullopt;
    }
    // GL scissors count rows from the bottom.
    return render::PixelRect{static_cast<i32>(x0), static_cast<i32>(static_cast<f32>(framebufferHeight) - y1),
                             static_cast<u32>(x1 - x0), static_cast<u32>(y1 - y0)};
}

struct DebugUi::Impl
{
    ImGuiContext* context = nullptr;
    std::unique_ptr<Renderer> renderer; // null without a device
    std::array<f32, kFrameHistory> frameTimes{};
    usize frameIndex = 0;
    usize frameCount = 0; // valid entries in frameTimes
    bool showDemo = false;
    std::array<char, 512> consoleInput{};
    i32 consoleHistory = -1; // index into ConsolePanel::history while browsing, -1: a new line
    usize consoleShown = 0;  // lines seen: scroll down when more arrive

    ~Impl()
    {
        if (context != nullptr)
        {
            ImGui::SetCurrentContext(context);
            if (renderer)
            {
                renderer->destroyTextures();
            }
            ImGui::DestroyContext(context);
        }
    }
};

DebugUi::DebugUi() = default;
DebugUi::~DebugUi() = default;
DebugUi::DebugUi(DebugUi&&) noexcept = default;
DebugUi& DebugUi::operator=(DebugUi&&) noexcept = default;

Result<DebugUi> DebugUi::create(render::Device* device, render::ShaderLibrary* shaders, f32 scale)
{
    DebugUi ui;
    ui.m_impl = std::make_unique<Impl>();
    Impl& impl = *ui.m_impl;
    impl.context = ImGui::CreateContext();
    ImGui::SetCurrentContext(impl.context);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no imgui.ini next to the executable; panels place themselves
    io.LogFilename = nullptr;
    io.BackendPlatformName = "g7_platform";
    io.BackendRendererName = "g7_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    // No keyboard navigation: with it a focused panel would claim the keyboard and stop the camera.
    // ImGui then wants the keyboard only while a text field or similar is active.

    io.Fonts->AddFontDefaultVector();
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.FontSizeBase = 15.0f;
    scale = std::clamp(scale, 0.5f, 4.0f);
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    if (device != nullptr && shaders != nullptr)
    {
        impl.renderer = std::make_unique<Renderer>();
        if (auto result = impl.renderer->init(*device, *shaders); !result)
        {
            return Error{"debug UI: " + result.error().message};
        }
    }
    G7_LOG_INFO("ui", "Dear ImGui {} (scale {:.2f})", ImGui::GetVersion(), scale);
    return ui;
}

void DebugUi::beginFrame(const platform::Input& input, Vec2 size, Vec2 pixels, f32 deltaSeconds)
{
    ImGui::SetCurrentContext(m_impl->context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f));
    io.DisplayFramebufferScale = ImVec2(pixels.x / io.DisplaySize.x, pixels.y / io.DisplaySize.y);
    io.DeltaTime = std::max(deltaSeconds, 1e-4f);
    m_impl->frameTimes[m_impl->frameIndex] = deltaSeconds * 1000.0f;
    m_impl->frameIndex = (m_impl->frameIndex + 1) % kFrameHistory;
    m_impl->frameCount = std::min(m_impl->frameCount + 1, kFrameHistory);

    // Input is state-based; ImGui wants events. Edges become events in the order that leaves the
    // current state: a tap within one frame is down + up, a re-press of a held key up + down.
    const auto feed = [&](auto send, bool pressed, bool released, bool down)
    {
        if (pressed && released)
        {
            send(!down);
            send(down);
        }
        else if (pressed || released)
        {
            send(pressed);
        }
    };
    for (u16 k = 1; k < static_cast<u16>(Key::Count); ++k)
    {
        const auto key = static_cast<Key>(k);
        const ImGuiKey imguiKey = toImGui(key);
        if (imguiKey != ImGuiKey_None)
        {
            feed([&](bool d) { io.AddKeyEvent(imguiKey, d); }, input.pressed(key), input.released(key),
                 input.isDown(key));
        }
    }
    io.AddKeyEvent(ImGuiMod_Ctrl, input.isDown(Key::LeftCtrl) || input.isDown(Key::RightCtrl));
    io.AddKeyEvent(ImGuiMod_Shift, input.isDown(Key::LeftShift) || input.isDown(Key::RightShift));
    io.AddKeyEvent(ImGuiMod_Alt, input.isDown(Key::LeftAlt) || input.isDown(Key::RightAlt));

    io.AddMousePosEvent(input.mousePosition().x, input.mousePosition().y);
    constexpr std::array<std::pair<platform::MouseButton, int>, 3> kButtons = {
        {{platform::MouseButton::Left, ImGuiMouseButton_Left},
         {platform::MouseButton::Right, ImGuiMouseButton_Right},
         {platform::MouseButton::Middle, ImGuiMouseButton_Middle}}};
    for (const auto& [button, index] : kButtons)
    {
        feed([&](bool d) { io.AddMouseButtonEvent(index, d); }, input.pressed(button), input.released(button),
             input.isDown(button));
    }
    if (input.wheelDelta() != 0.0f)
    {
        io.AddMouseWheelEvent(0.0f, input.wheelDelta());
    }
    if (!input.text().empty())
    {
        const std::string text(input.text());
        io.AddInputCharactersUTF8(text.c_str());
    }
    ImGui::NewFrame();
}

void DebugUi::consolePanel(ConsolePanel& panel)
{
    ImGui::SetCurrentContext(m_impl->context);
    Impl& impl = *m_impl;
    const f32 scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(10.0f * scale, view.y * 0.55f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(view.x - 20.0f * scale, 900.0f * scale), view.y * 0.4f),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Console", &panel.open))
    {
        ImGui::End();
        return;
    }
    const f32 inputHeight = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("lines", ImVec2(0.0f, -inputHeight), ImGuiChildFlags_None))
    {
        for (const std::string& line : panel.lines)
        {
            const ImVec4 colour = line.starts_with("! ")   ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                  : line.starts_with("> ") ? ImVec4(0.6f, 0.8f, 1.0f, 1.0f)
                                                           : ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
            ImGui::TextColored(colour, "%s", line.c_str());
        }
        if (panel.lines.size() != impl.consoleShown)
        {
            ImGui::SetScrollHereY(1.0f);
            impl.consoleShown = panel.lines.size();
        }
    }
    ImGui::EndChild();
    // Up/Down browse the earlier inputs.
    const auto history = [](ImGuiInputTextCallbackData* data) -> int
    {
        auto* p = static_cast<std::pair<ConsolePanel*, i32*>*>(data->UserData);
        const auto& entries = p->first->history;
        i32& index = *p->second;
        if (entries.empty())
        {
            return 0;
        }
        if (data->EventKey == ImGuiKey_UpArrow)
        {
            index = index < 0 ? static_cast<i32>(entries.size()) - 1 : std::max(0, index - 1);
        }
        else if (data->EventKey == ImGuiKey_DownArrow)
        {
            index = index < 0 || index + 1 >= static_cast<i32>(entries.size()) ? -1 : index + 1;
        }
        data->DeleteChars(0, data->BufTextLen);
        if (index >= 0)
        {
            data->InsertChars(0, entries[static_cast<usize>(index)].c_str());
        }
        return 0;
    };
    std::pair<ConsolePanel*, i32*> user{&panel, &impl.consoleHistory};
    if (panel.focus)
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##input", impl.consoleInput.data(), impl.consoleInput.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, history,
                         &user))
    {
        panel.submitted = impl.consoleInput.data();
        impl.consoleInput.fill('\0');
        impl.consoleHistory = -1;
        ImGui::SetKeyboardFocusHere(-1); // keep typing
    }
    ImGui::End();
}

void DebugUi::inventoryPanel(InventoryPanel& panel)
{
    ImGui::SetCurrentContext(m_impl->context);
    const f32 scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(view.x - 470.0f * scale, 40.0f * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f * scale, std::min(view.y - 80.0f * scale, 560.0f * scale)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Inventory", &panel.open))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(panel.title.c_str());
    for (const std::string& line : panel.stats)
    {
        ImGui::TextDisabled("%s", line.c_str());
    }
    ImGui::Separator();
    if (panel.rows.empty())
    {
        ImGui::TextDisabled("(empty)");
    }
    if (panel.container)
    {
        ImGui::SeparatorText(panel.containerTitle.c_str());
        if (panel.containerRows.empty())
        {
            ImGui::TextDisabled("(leer)");
        }
        for (const InventoryPanel::Row& row : panel.containerRows)
        {
            ImGui::PushID(("c " + row.item).c_str());
            ImGui::Text("%u x %s", row.count, row.name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f * scale + ImGui::GetCursorPosX() -
                            ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::SmallButton("take"))
            {
                panel.action = "take";
                panel.actionItem = row.item;
            }
            ImGui::PopID();
        }
        ImGui::SeparatorText("Held");
    }
    std::string_view category;
    for (const InventoryPanel::Row& row : panel.rows)
    {
        if (row.category != category)
        {
            category = row.category;
            ImGui::SeparatorText(row.category.c_str());
        }
        ImGui::PushID(row.item.c_str());
        if (row.equipped.empty())
        {
            ImGui::Text("%u x %s", row.count, row.name.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%u x %s  [%s]", row.count, row.name.c_str(),
                               row.equipped.c_str());
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", row.item.c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150.0f * scale + ImGui::GetCursorPosX() -
                        ImGui::GetStyle().ItemSpacing.x);
        if (row.equippable)
        {
            const bool on = !row.equipped.empty();
            if (ImGui::SmallButton(on ? "unequip" : "equip"))
            {
                panel.action = on ? "unequip" : "equip";
                panel.actionItem = row.item;
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(panel.container ? "put" : "drop"))
        {
            panel.action = panel.container ? "put" : "drop";
            panel.actionItem = row.item;
        }
        ImGui::PopID();
    }
    if (!panel.message.empty())
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", panel.message.c_str());
    }
    ImGui::End();
}

void DebugUi::lockpickPanel(LockpickPanel& panel)
{
    ImGui::SetCurrentContext(m_impl->context);
    const f32 scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(view.x * 0.5f - 170.0f * scale, view.y * 0.62f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lockpick", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(panel.title.c_str());
    std::string steps;
    for (usize i = 0; i < panel.length; ++i)
    {
        steps += i < panel.progress ? "# " : "- ";
    }
    ImGui::Text("Schloss: %s", steps.c_str());
    ImGui::Text("Dietriche: %u", panel.picks);
    if (ImGui::Button("< links"))
    {
        panel.turn = 'L';
    }
    ImGui::SameLine();
    if (ImGui::Button("rechts >"))
    {
        panel.turn = 'R';
    }
    ImGui::SameLine();
    if (ImGui::Button("aufhören"))
    {
        panel.leave = true;
    }
    ImGui::TextDisabled("%s", panel.hint.c_str());
    if (!panel.result.empty())
    {
        ImGui::TextUnformatted(panel.result.c_str());
    }
    ImGui::End();
}

void DebugUi::choicePanel(ChoicePanel& panel)
{
    ImGui::SetCurrentContext(m_impl->context);
    const f32 scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(view.x * 0.5f - 160.0f * scale, view.y * 0.55f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Choice", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(panel.title.c_str());
    ImGui::Separator();
    for (usize i = 0; i < panel.options.size(); ++i)
    {
        if (ImGui::Button(panel.options[i].c_str(), ImVec2(-1.0f, 0.0f)))
        {
            panel.chosen = static_cast<i32>(i);
        }
    }
    if (ImGui::Button("zurück", ImVec2(-1.0f, 0.0f)))
    {
        panel.cancel = true;
    }
    if (!panel.message.empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted(panel.message.c_str());
    }
    ImGui::End();
}

void DebugUi::focusLabel(Vec2 screen, std::string_view text)
{
    ImGui::SetCurrentContext(m_impl->context);
    const std::string label(text);
    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
    const ImVec2 at(screen.x - 0.5f * size.x, screen.y - size.y);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 200), label.c_str()); // shadow
    draw->AddText(at, IM_COL32(255, 255, 255, 255), label.c_str());
}

void DebugUi::creaturesPanel(CreaturesPanel& panel)
{
    ImGui::SetCurrentContext(m_impl->context);
    const f32 scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(ImVec2(690.0f * scale, 10.0f * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Creatures"))
    {
        ImGui::End();
        return;
    }
    if (ImGui::BeginCombo("species", panel.speciesChoice.c_str()))
    {
        for (const std::string& s : panel.species)
        {
            if (ImGui::Selectable(s.c_str(), s == panel.speciesChoice))
            {
                panel.speciesChoice = s;
            }
        }
        ImGui::EndCombo();
    }
    panel.spawn = ImGui::Button("spawn in front");
    ImGui::SameLine();
    panel.removeAll = ImGui::Button("remove all");
    static constexpr std::array<std::pair<const char*, const char*>, 11> kActions = {{
        {"turn left", "turn_l"},
        {"turn right", "turn_r"},
        {"attack 1", "attack_1"},
        {"attack 2", "attack_2"},
        {"threaten", "threaten"},
        {"hit", "hit"},
        {"eat", "eat"},
        {"sleep", "sleep"},
        {"stop", "stop"},
        {"die", "die"},
        {"revive", "revive"},
    }};
    for (CreaturesPanel::Row& row : panel.rows)
    {
        ImGui::PushID(static_cast<int>(row.id));
        if (ImGui::CollapsingHeader(std::format("{}: {}", row.label, row.state).c_str()))
        {
            ImGui::Checkbox("showcase", &row.showcase);
            ImGui::SliderFloat("speed", &row.speed, 0.0f, row.maxSpeed, "%.2f m/s");
            for (usize i = 0; i < kActions.size(); ++i)
            {
                if (i % 4 != 0)
                {
                    ImGui::SameLine();
                }
                if (ImGui::Button(kActions[i].first))
                {
                    row.action = kActions[i].second;
                }
            }
            for (const std::string& event : row.events)
            {
                ImGui::TextDisabled("%s", event.c_str());
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void DebugUi::animationPanel(AnimationPanel& panel)
{
    ImGui::SetCurrentContext(m_impl->context);
    const f32 scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(ImVec2(360.0f * scale, 10.0f * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation"))
    {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted(panel.figure.c_str());
    ImGui::TextDisabled("%s", panel.graph.c_str());
    ImGui::Separator();
    ImGui::Text("state  %s", panel.state.c_str());
    if (panel.fade < 1.0f && !panel.previousState.empty())
    {
        ImGui::Text("from   %s  (fade %.0f %%)", panel.previousState.c_str(), panel.fade * 100.0f);
    }
    ImGui::Text("rate   %.2f", panel.rate);
    ImGui::ProgressBar(panel.progress - std::floor(panel.progress), ImVec2(-1.0f, 0.0f));
    if (ImGui::CollapsingHeader("Clips", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto& [clip, weight] : panel.clips)
        {
            ImGui::ProgressBar(weight, ImVec2(60.0f * scale, 0.0f));
            ImGui::SameLine();
            ImGui::TextUnformatted(clip.c_str());
        }
    }
    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto& [name, value] : panel.params)
        {
            ImGui::Text("%-8s %6.2f", name.c_str(), value);
        }
    }
    if (ImGui::CollapsingHeader("Events", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const std::string& event : panel.events)
        {
            ImGui::TextUnformatted(event.c_str());
        }
    }
    if (ImGui::CollapsingHeader("Try out"))
    {
        ImGui::Checkbox("show sockets", &panel.showSockets);
        const char* stick = panel.stickSocket.empty() ? "(none)" : panel.stickSocket.c_str();
        if (ImGui::BeginCombo("stick in", stick))
        {
            if (ImGui::Selectable("(none)", panel.stickSocket.empty()))
            {
                panel.stickSocket.clear();
            }
            for (const std::string& socket : panel.sockets)
            {
                if (ImGui::Selectable(socket.c_str(), socket == panel.stickSocket))
                {
                    panel.stickSocket = socket;
                }
            }
            ImGui::EndCombo();
        }
        static constexpr std::array<const char*, 6> kExpressions = {"",     "angry", "friendly",
                                                                    "fear", "pain",  "sleep"};
        const char* shown = panel.expression.empty() ? "(neutral)" : panel.expression.c_str();
        if (ImGui::BeginCombo("expression", shown))
        {
            for (const char* e : kExpressions)
            {
                if (ImGui::Selectable(*e == '\0' ? "(neutral)" : e, panel.expression == e))
                {
                    panel.expression = e;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("weight", &panel.expressionWeight, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("talking", &panel.talking);
        ImGui::Checkbox("look at the camera", &panel.lookAtCamera);
        ImGui::Text("head  yaw %.0f  pitch %.0f", panel.lookYaw, panel.lookPitch);
    }
    if (panel.outfit && ImGui::CollapsingHeader("Outfit"))
    {
        if (ImGui::BeginCombo("head", panel.head.c_str()))
        {
            for (const std::string& head : panel.heads)
            {
                if (ImGui::Selectable(head.c_str(), head == panel.head))
                {
                    panel.head = head;
                }
            }
            ImGui::EndCombo();
        }
        for (AnimationPanel::Garment& garment : panel.garments)
        {
            ImGui::Checkbox(garment.path.c_str(), &garment.worn);
        }
        if (!panel.outfitError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", panel.outfitError.c_str());
        }
    }
    ImGui::End();
}

void DebugUi::enginePanel(EnginePanel& panel)
{
    ImGui::SetCurrentContext(m_impl->context);
    const f32 scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(ImVec2(10.0f * scale, 10.0f * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Gothar"))
    {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Average over the recorded history for a calm number; the graph shows the spikes.
        f32 sum = 0.0f;
        f32 worst = 0.0f;
        for (usize i = 0; i < m_impl->frameCount; ++i)
        {
            const f32 ms = m_impl->frameTimes[(m_impl->frameIndex + kFrameHistory - 1 - i) % kFrameHistory];
            sum += ms;
            worst = std::max(worst, ms);
        }
        const f32 average = m_impl->frameCount > 0 ? sum / static_cast<f32>(m_impl->frameCount) : 0.0f;
        ImGui::Text("%.0f fps   %.2f ms   (worst %.1f ms)", average > 0.0f ? 1000.0f / average : 0.0f,
                    average, worst);
        ImGui::PlotLines("##frametimes", m_impl->frameTimes.data(), static_cast<int>(kFrameHistory),
                         static_cast<int>(m_impl->frameIndex), nullptr, 0.0f, std::max(worst, 20.0f),
                         ImVec2(-1.0f, 50.0f * scale));
        ImGui::Text("%u draws   %.1fk triangles", panel.frame.drawCalls, panel.frame.triangles / 1000.0f);
        ImGui::Text("%u pipeline changes   %u texture binds", panel.frame.pipelineChanges,
                    panel.frame.textureBinds);
        ImGui::Text("%u x %u px   %llu ticks", panel.width, panel.height,
                    static_cast<unsigned long long>(panel.simulationTicks));
    }
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("position %.1f  %.1f  %.1f", panel.cameraPosition.x, panel.cameraPosition.y,
                    panel.cameraPosition.z);
        ImGui::SliderFloat("FOV", &panel.fovDegrees, 30.0f, 110.0f, "%.0f deg");
        ImGui::SliderFloat("fly speed", &panel.flySpeed, 0.5f, 200.0f, "%.1f m/s",
                           ImGuiSliderFlags_Logarithmic);
    }
    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
    {
        constexpr std::array<const char*, 3> kTonemappers = {"ACES", "Reinhard", "none"};
        int tonemapper = static_cast<int>(panel.tonemapper);
        if (ImGui::Combo("tonemapper", &tonemapper, kTonemappers.data(),
                         static_cast<int>(kTonemappers.size())))
        {
            panel.tonemapper = static_cast<render::Tonemapper>(tonemapper);
        }
        ImGui::SliderFloat("exposure", &panel.exposure, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("fog start", &panel.fogStart, 0.0f, 300.0f, "%.0f m");
        ImGui::SliderFloat("fog density", &panel.fogDensity, 0.0f, 0.05f, "%.5f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("sun", &panel.sun);
        ImGui::SameLine();
        ImGui::Checkbox("cascade colours", &panel.shadowDebug);
        ImGui::Checkbox("debug draw (F2)", &panel.debugDraw);
    }
    if (ImGui::CollapsingHeader("Time"))
    {
        ImGui::Checkbox("paused", &panel.paused);
        ImGui::SliderFloat("time scale", &panel.timeScale, 0.0f, 10.0f, "%.2f");
        ImGui::SliderFloat("time of day", &panel.hour, 0.0f, 23.99f, "%.2f h");
        ImGui::SliderFloat("s per game minute", &panel.minuteSeconds, 0.01f, 60.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
    }
    ImGui::Checkbox("ImGui demo", &m_impl->showDemo);
    ImGui::End();
    if (m_impl->showDemo)
    {
        ImGui::ShowDemoWindow(&m_impl->showDemo);
    }
}

void DebugUi::endFrame(render::Device* device)
{
    ImGui::SetCurrentContext(m_impl->context);
    ImGui::Render();
    ImDrawData* data = ImGui::GetDrawData();
    if (data == nullptr)
    {
        return;
    }
    if (device != nullptr && m_impl->renderer)
    {
        m_impl->renderer->render(*device, *data);
    }
    else if (data->Textures != nullptr)
    {
        // Headless: acknowledge texture requests without creating anything.
        for (ImTextureData* texture : *data->Textures)
        {
            if (texture->Status == ImTextureStatus_WantCreate ||
                texture->Status == ImTextureStatus_WantUpdates)
            {
                texture->SetTexID(static_cast<ImTextureID>(1));
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantDestroy)
            {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }
}

bool DebugUi::wantsMouse() const noexcept
{
    return m_impl && ImGui::GetCurrentContext() == m_impl->context && ImGui::GetIO().WantCaptureMouse;
}

bool DebugUi::wantsKeyboard() const noexcept
{
    return m_impl && ImGui::GetCurrentContext() == m_impl->context && ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugUi::wantsText() const noexcept
{
    return m_impl && ImGui::GetCurrentContext() == m_impl->context && ImGui::GetIO().WantTextInput;
}
} // namespace g7::ui
