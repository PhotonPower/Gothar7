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
    panel.undo = panel.redo = panel.confirmYes = panel.confirmNo = false;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 370.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Editor");
    ImGui::TextUnformatted(panel.world.empty() ? "(no world)" : panel.world.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(panel.dirty ? "* unsaved" : "");
    ImGui::BeginDisabled(!panel.canSave);
    panel.save = ImGui::Button("Save (Ctrl+S)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(panel.undoLabel.empty());
    panel.undo = ImGui::Button("Undo (Ctrl+Z)");
    if (!panel.undoLabel.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", panel.undoLabel.c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(panel.redoLabel.empty());
    panel.redo = ImGui::Button("Redo (Ctrl+Y)");
    if (!panel.redoLabel.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", panel.redoLabel.c_str());
    }
    ImGui::EndDisabled();
    if (!panel.confirm.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
        ImGui::TextWrapped("%s", panel.confirm.c_str());
        ImGui::PopStyleColor();
        panel.confirmYes = ImGui::Button("Yes");
        ImGui::SameLine();
        panel.confirmNo = ImGui::Button("No");
    }
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

void DebugUi::waynetPanel(WaynetPanel& panel)
{
    panel.moved = panel.rename = panel.addPoint = panel.addFreepoint = panel.remove = false;
    panel.check = panel.probe = false;
    panel.clickedIssue = -1;
    if (!panel.shown)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 740.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Waynet");
    ImGui::Checkbox("waynet mode (G)", &panel.active);
    ImGui::TextWrapped(
        "Click: select a point. Ctrl+click on the ground: new point (joined to the selected one). "
        "Shift+click on a point: join/part it with the selected one. Del: delete.");
    panel.addPoint = ImGui::Button("add point here");
    ImGui::SameLine();
    std::vector<const char*> types;
    for (const std::string& t : panel.freepointTypes)
    {
        types.push_back(t.c_str());
    }
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##type", &panel.type, types.data(), static_cast<int>(types.size()));
    ImGui::SameLine();
    panel.addFreepoint = ImGui::Button("add freepoint");

    if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (panel.selected.empty())
        {
            ImGui::TextDisabled("nothing selected");
        }
        else
        {
            ImGui::TextUnformatted(panel.selected.c_str());
            if (panel.selectedGenerated)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
                ImGui::TextWrapped(
                    "generated (owner worldgen): moving or renaming makes it yours; deleting lasts "
                    "only until the next generator run");
                ImGui::PopStyleColor();
            }
            panel.moved = ImGui::DragFloat3("position", &panel.position.x, 0.05f);
            if (panel.selectedFreepoint)
            {
                panel.moved =
                    ImGui::DragFloat("facing (deg)", &panel.yawDegrees, 1.0f, -180.0f, 180.0f) || panel.moved;
            }
            inputText("name", panel.name);
            ImGui::SameLine();
            panel.rename = ImGui::Button("rename");
            panel.remove = ImGui::Button("delete (Del)");
        }
        if (!panel.annotation.empty())
        {
            ImGui::TextWrapped("For good: add to tools/worldgen/data/<site>/waynet.json \"remove\" (welt):");
            ImGui::InputText("##annotation", panel.annotation.data(), panel.annotation.size() + 1,
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button("copy"))
            {
                ImGui::SetClipboardText(panel.annotation.c_str());
            }
        }
    }
    if (ImGui::CollapsingHeader("Check", ImGuiTreeNodeFlags_DefaultOpen))
    {
        panel.check = ImGui::Button("check the waynet");
        ImGui::BeginChild("issues", ImVec2(0.0f, 160.0f), ImGuiChildFlags_Borders);
        for (usize i = 0; i < panel.issues.size(); ++i)
        {
            if (ImGui::Selectable(panel.issues[i].c_str()))
            {
                panel.clickedIssue = static_cast<i32>(i); // the camera goes there
            }
        }
        ImGui::EndChild();
    }
    if (ImGui::CollapsingHeader("Path probe"))
    {
        inputText("from", panel.pathFrom);
        inputText("to", panel.pathTo);
        panel.probe = ImGui::Button("find the path");
        ImGui::TextWrapped("%s", panel.pathResult.c_str());
    }
    ImGui::End();
}
} // namespace g7::ui
