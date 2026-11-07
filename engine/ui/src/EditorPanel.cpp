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
    panel.hourChanged = panel.addLight = panel.checkMobs = false;
    panel.clickedMobIssue = -1;

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

    if (ImGui::CollapsingHeader("Lights & mobs", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Preview time (display only, not saved): judge lights by night.
        panel.hourChanged = ImGui::SliderFloat("time of day", &panel.hour, 0.0f, 24.0f, "%.2f h");
        ImGui::TextWrapped("%s", panel.lightInfo.c_str());
        std::vector<const char*> presets;
        for (const std::string& p : panel.lightPresets)
        {
            presets.push_back(p.c_str());
        }
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##preset", &panel.lightPreset, presets.data(), static_cast<int>(presets.size()));
        ImGui::SameLine();
        panel.addLight = ImGui::Button("add light");
        panel.checkMobs = ImGui::Button("check the mobs");
        ImGui::BeginChild("mobIssues", ImVec2(0.0f, 90.0f), ImGuiChildFlags_Borders);
        for (usize i = 0; i < panel.mobIssues.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(panel.mobIssues[i].c_str()))
            {
                panel.clickedMobIssue = static_cast<i32>(i); // selects the mob
            }
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

void DebugUi::zonePanel(ZonePanel& panel)
{
    panel.clickedRow = panel.clickedIssue = -1;
    panel.boxChanged = panel.valueChanged = panel.add = panel.remove = panel.checkTriggers = false;
    if (!panel.shown)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 740.0f, 540.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Zones & triggers");
    ImGui::Checkbox("zone mode (V)", &panel.active);
    ImGui::TextWrapped("Click: select a box; the gizmo moves, turns (about up only) and sizes it (1/2/3).");
    ImGui::TextWrapped("here: %s", panel.here.c_str());
    if (ImGui::CollapsingHeader("Zones", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginChild("zones", ImVec2(0.0f, 130.0f), ImGuiChildFlags_Borders);
        for (usize i = 0; i < panel.rows.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(panel.rows[i].c_str(), static_cast<i32>(i) == panel.selected))
            {
                panel.clickedRow = static_cast<i32>(i);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        std::vector<const char*> types;
        for (const std::string& t : panel.types)
        {
            types.push_back(t.c_str());
        }
        ImGui::SetNextItemWidth(90.0f);
        ImGui::Combo("##newType", &panel.newType, types.data(), static_cast<int>(types.size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::BeginCombo("##newValue", panel.newValue.c_str()))
        {
            for (const std::string& v : panel.newValues)
            {
                if (ImGui::Selectable(v.c_str(), v == panel.newValue))
                {
                    panel.newValue = v;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        panel.add = ImGui::Button("add zone");
        if (panel.selected >= 0)
        {
            ImGui::Separator();
            if (!panel.selectedEditable)
            {
                ImGui::TextDisabled("a zone of type '%s': kept as it is", panel.selectedType.c_str());
            }
            else
            {
                if (panel.selectedType == "indoor")
                {
                    panel.valueChanged = inputText("room", panel.value);
                }
                else if (ImGui::BeginCombo(panel.selectedType == "music" ? "theme" : "ambience",
                                           panel.value.c_str()))
                {
                    for (const std::string& v : panel.values)
                    {
                        if (ImGui::Selectable(v.c_str(), v == panel.value))
                        {
                            panel.value = v;
                            panel.valueChanged = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                panel.boxChanged = ImGui::DragFloat3("centre", &panel.center.x, 0.05f);
                panel.boxChanged =
                    ImGui::DragFloat3("half extents", &panel.halfExtents.x, 0.05f, 0.05f, 5000.0f) ||
                    panel.boxChanged;
                panel.boxChanged = ImGui::DragFloat("yaw (deg)", &panel.yawDegrees, 0.5f, -180.0f, 180.0f) ||
                                   panel.boxChanged;
            }
            panel.remove = ImGui::Button("delete zone (Del)");
        }
    }
    if (ImGui::CollapsingHeader("Triggers", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::TextWrapped(
            "Triggers are vobs: select them outside the zone mode; the inspector lists the script "
            "functions, worlds and start points.");
        panel.checkTriggers = ImGui::Button("check the triggers");
        ImGui::BeginChild("triggerIssues", ImVec2(0.0f, 110.0f), ImGuiChildFlags_Borders);
        for (usize i = 0; i < panel.triggerIssues.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(panel.triggerIssues[i].c_str()))
            {
                panel.clickedIssue = static_cast<i32>(i); // selects the trigger
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void DebugUi::routinePanel(RoutinePanel& panel)
{
    panel.clickedNpc = panel.clickedRoutine = panel.clickedEntry = -1;
    panel.addEntry = panel.changeEntry = panel.removeEntry = panel.newRoutine = panel.write = false;
    panel.play = panel.playAtStart = panel.stop = false;
    if (!panel.shown)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(10.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Routines");
    if (panel.playing)
    {
        ImGui::TextWrapped("Playing from here. F5 or Esc: stop - the world as before the start.");
        panel.stop = ImGui::Button("stop (F5)");
        ImGui::End();
        return;
    }
    panel.play = ImGui::Button("play here (F5)");
    ImGui::SameLine();
    panel.playAtStart = ImGui::Button("play at the start (Shift+F5)");
    ImGui::Checkbox("ghosts", &panel.ghosts);
    ImGui::SameLine();
    ImGui::SliderFloat("hour", &panel.hour, 0.0f, 24.0f, "%.2f");
    if (ImGui::CollapsingHeader("NPCs", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginChild("npcs", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
        for (usize i = 0; i < panel.npcs.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(panel.npcs[i].c_str()))
            {
                panel.clickedNpc = static_cast<i32>(i); // its routine
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    if (ImGui::CollapsingHeader("Routines", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginChild("routines", ImVec2(0.0f, 90.0f), ImGuiChildFlags_Borders);
        for (usize i = 0; i < panel.routines.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(panel.routines[i].c_str(), static_cast<i32>(i) == panel.selectedRoutine))
            {
                panel.clickedRoutine = static_cast<i32>(i);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        inputText("##newName", panel.newName);
        ImGui::SameLine();
        panel.newRoutine = ImGui::Button("new routine");
        if (panel.selectedRoutine >= 0)
        {
            ImGui::TextWrapped("%s%s", panel.file.c_str(),
                               panel.editable ? "" : " - written by hand: shown only");
            ImGui::BeginChild("entries", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
            for (usize i = 0; i < panel.entries.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(panel.entries[i].c_str(), static_cast<i32>(i) == panel.selectedEntry))
                {
                    panel.clickedEntry = static_cast<i32>(i);
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (panel.editable)
            {
                ImGui::SetNextItemWidth(60.0f);
                inputText("from", panel.from);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(60.0f);
                inputText("to", panel.to);
                std::vector<const char*> states;
                for (const std::string& s : panel.states)
                {
                    states.push_back(s.c_str());
                }
                ImGui::Combo("state", &panel.state, states.data(), static_cast<int>(states.size()));
                inputText("place", panel.at);
                ImGui::SameLine();
                ImGui::Checkbox("pick", &panel.pickPlace);
                panel.addEntry = ImGui::Button("add entry");
                ImGui::SameLine();
                ImGui::BeginDisabled(panel.selectedEntry < 0);
                panel.changeEntry = ImGui::Button("change entry");
                ImGui::SameLine();
                panel.removeEntry = ImGui::Button("remove entry");
                ImGui::EndDisabled();
            }
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s%s", panel.editorFile.c_str(), panel.dirty ? " * unsaved" : "");
        ImGui::BeginDisabled(!panel.dirty);
        panel.write = ImGui::Button("write the routines (also Ctrl+S)");
        ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader("Day plan check", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginChild("routineIssues", ImVec2(0.0f, 100.0f), ImGuiChildFlags_Borders);
        for (const std::string& issue : panel.issues)
        {
            ImGui::TextWrapped("%s", issue.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
}
} // namespace g7::ui
