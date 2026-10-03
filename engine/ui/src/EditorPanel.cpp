#include <g7/core/StringUtil.hpp>
#include <g7/ui/DebugUi.hpp>
#include <g7/ui/EditorPanel.hpp>

#include <imgui.h>

#include <array>
#include <string>

namespace g7::ui
{
namespace
{
/// ImGui text field on a std::string (no ImGui std::string helpers in this build).
bool inputText(const char* label, std::string& text)
{
    std::array<char, 256> buffer{};
    text.copy(buffer.data(), buffer.size() - 1);
    if (ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        text = buffer.data();
        return true;
    }
    return false;
}

void field(EditorField& f)
{
    ImGui::PushID(f.label.c_str());
    switch (f.kind)
    {
    case EditorField::Kind::ReadOnly:
        ImGui::LabelText(f.label.c_str(), "%s", f.text.c_str());
        break;
    case EditorField::Kind::Text:
        f.changed = inputText(f.label.c_str(), f.text);
        break;
    case EditorField::Kind::Number:
        f.changed = ImGui::DragFloat(f.label.c_str(), &f.number, 0.05f);
        break;
    case EditorField::Kind::Vector:
        f.changed = ImGui::DragFloat3(f.label.c_str(), &f.vector.x, 0.05f);
        break;
    case EditorField::Kind::Color:
        f.changed = ImGui::ColorEdit3(f.label.c_str(), &f.vector.x, ImGuiColorEditFlags_Float);
        break;
    case EditorField::Kind::Flag:
        f.changed = ImGui::Checkbox(f.label.c_str(), &f.flag);
        break;
    case EditorField::Kind::Choice:
        if (ImGui::BeginCombo(f.label.c_str(), f.choice >= 0 && f.choice < static_cast<i32>(f.choices.size())
                                                   ? f.choices[static_cast<usize>(f.choice)].c_str()
                                                   : ""))
        {
            for (usize i = 0; i < f.choices.size(); ++i)
            {
                if (ImGui::Selectable(f.choices[i].c_str(), static_cast<i32>(i) == f.choice))
                {
                    f.choice = static_cast<i32>(i);
                    f.changed = true;
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    ImGui::PopID();
}
} // namespace

void DebugUi::editorPanel(EditorPanel& panel)
{
    panel.clickedVob = 0;
    panel.placeAsset = -1;
    panel.save = panel.remove = panel.duplicate = false;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 370.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Editor");
    ImGui::TextUnformatted(panel.world.empty() ? "(no world)" : panel.world.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(panel.dirty ? "* unsaved" : "");
    ImGui::BeginDisabled(!panel.canSave);
    panel.save = ImGui::Button("Save (Ctrl+S)");
    ImGui::EndDisabled();
    if (!panel.status.empty())
    {
        ImGui::TextWrapped("%s", panel.status.c_str());
    }

    if (ImGui::CollapsingHeader("Gizmo", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::RadioButton("move (1)", &panel.gizmoMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("turn (2)", &panel.gizmoMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("scale (3)", &panel.gizmoMode, 2);
        ImGui::Checkbox("local axes (L)", &panel.localAxes);
        ImGui::SameLine();
        ImGui::Checkbox("snap (X)", &panel.snap);
        ImGui::DragFloat("grid", &panel.snapMove, 0.05f, 0.05f, 10.0f, "%.2f m");
        ImGui::DragFloat("angle", &panel.snapAngle, 1.0f, 1.0f, 90.0f, "%.0f deg");
    }

    if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (!panel.warning.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
            ImGui::TextWrapped("%s", panel.warning.c_str());
            ImGui::PopStyleColor();
        }
        if (panel.fields.empty())
        {
            ImGui::TextDisabled("nothing selected (left click)");
        }
        for (EditorField& f : panel.fields)
        {
            field(f);
        }
        if (!panel.fields.empty())
        {
            panel.duplicate = ImGui::Button("Duplicate (Ctrl+D)");
            ImGui::SameLine();
            panel.remove = ImGui::Button("Delete (Del)");
        }
    }

    if (ImGui::CollapsingHeader("Vobs"))
    {
        ImGui::BeginChild("vobs", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders);
        for (const EditorVobRow& row : panel.vobs)
        {
            ImGui::PushID(static_cast<int>(row.id));
            ImGui::Indent(static_cast<f32>(row.depth) * 12.0f + 0.001f);
            if (ImGui::Selectable(row.label.c_str(), row.selected))
            {
                panel.clickedVob = row.id;
            }
            ImGui::Unindent(static_cast<f32>(row.depth) * 12.0f + 0.001f);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("Models"))
    {
        inputText("filter", panel.assetFilter);
        ImGui::BeginChild("assets", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders);
        for (usize i = 0; i < panel.assets.size(); ++i)
        {
            if (!panel.assetFilter.empty() &&
                toLower(panel.assets[i]).find(toLower(panel.assetFilter)) == std::string::npos)
            {
                continue;
            }
            if (ImGui::Selectable(panel.assets[i].c_str()))
            {
                panel.placeAsset = static_cast<i32>(i); // placed in front of the camera
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}
} // namespace g7::ui
