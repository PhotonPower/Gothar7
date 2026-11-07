// Save slots in the engine (M15 part D, docs/modules/save.md): saving and loading through the save file
// (part A), quick save and quick load (F5/F9), the autosave, the screens Save and Load of the menus, the
// thumbnail; owner proposals S1-S8.

#include <g7/asset/ImageData.hpp>
#include <g7/asset/TextureData.hpp>
#include <g7/core/Crc32.hpp>
#include <g7/core/Log.hpp>
#include <g7/core/StringUtil.hpp>
#include <g7/core/Version.hpp>
#include <g7/render/TextureUpload.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/SaveScreens.hpp>

#include <chrono>
#include <format>

namespace g7
{
namespace
{
constexpr u32 kThumbnailWidth = 320;
constexpr u32 kThumbnailHeight = 180;
/// S5: loading from the menu asks first when this much was played since the last save or load.
constexpr f64 kUnsavedSeconds = 120.0;
/// How long an autosave waits for a moment it is allowed in (S1), then it is dropped.
constexpr f64 kAutosavePatience = 30.0;
} // namespace

fs::Path Engine::saveDirectory() const
{
    return m_config.saveDirectory.empty() ? fs::userPath("saves") : m_config.saveDirectory;
}

std::string Engine::saveBlocker() const
{
    // S1: anywhere, also in a fight - not in a dialogue, at a mob, under water, falling, in a menu world or
    // while loading.
    if (!m_hero || !m_player.valid() || m_worldPath.empty() || m_menuMode || m_loadJob)
    {
        return "UI_SAVE_NOT_HERE";
    }
    if (m_dialog || m_trade || m_mobUse || m_pickup)
    {
        return "UI_SAVE_NOT_NOW";
    }
    if (m_swimmer.mode() == gameplay::WaterMode::Dive || m_player.state() == physics::MoveState::Air)
    {
        return "UI_SAVE_NOT_NOW";
    }
    return {};
}

save::SaveMeta Engine::saveMeta(std::string_view slot) const
{
    save::SaveMeta meta;
    meta.world = m_worldPath;
    meta.place = contentText(m_sceneName);
    meta.language = language();
    meta.gameVersion =
        std::format("{}.{}.{}", kEngineVersion.major, kEngineVersion.minor, kEngineVersion.patch);
    meta.savedAtUtc =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    meta.playSeconds = m_playSeconds;
    meta.day = m_gameTime.day() + 1;
    meta.minute = static_cast<u32>(m_gameTime.minuteOfDay());
    meta.chapter = m_scripts ? static_cast<u32>(m_scripts->story()["chapter"].asInteger(1)) : 1;
    meta.heroLevel = m_hero ? m_hero->level() : 0;
    meta.contentStamp = contentStamp();
    // S4: "Ort - Kapitel n - Tag d, hh:mm"; the quick and auto saves say so.
    const std::string when =
        uiText("UI_SAVE_NAME", {{"place", meta.place},
                                {"chapter", std::to_string(meta.chapter)},
                                {"day", std::to_string(meta.day)},
                                {"time", std::format("{:02}:{:02}", meta.minute / 60, meta.minute % 60)}});
    meta.name = slot == "quick"  ? uiText("UI_SAVE_QUICK", {{"name", when}})
                : slot == "auto" ? uiText("UI_SAVE_AUTO", {{"name", when}})
                                 : when;
    return meta;
}

u64 Engine::contentStamp() const
{
    // The visited world files (S8: a save from other content warns when it is loaded).
    std::vector<std::string> paths{toLower(m_worldPath)};
    for (const auto& [path, left] : m_leftWorlds)
    {
        paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    u32 crc = 0;
    for (const std::string& path : paths)
    {
        if (auto bytes = m_vfs.read(path))
        {
            crc = crc32(bytes.value(), crc);
        }
    }
    return crc;
}

Result<void> Engine::saveGame(std::string_view slot)
{
    if (const std::string blocked = saveBlocker(); !blocked.empty())
    {
        notice(uiText(blocked));
        return Error{std::string(blocked)};
    }
    auto state = captureGame();
    if (!state)
    {
        G7_LOG_ERROR("save", "{}: {}", slot, state.error().message);
        notice(uiText("UI_SAVE_FAILED"));
        return state.error();
    }
    save::SaveFile file;
    file.meta = saveMeta(slot);
    file.thumbnailPng = m_thumbnailPng;
    file.state = std::move(state).value();
    const fs::Path path = save::slotPath(saveDirectory(), slot);
    if (auto written = save::writeSaveFile(path, file); !written)
    {
        G7_LOG_ERROR("save", "{}: {}", fs::toUtf8(path), written.error().message);
        notice(uiText("UI_SAVE_FAILED"));
        return written;
    }
    m_lastSavedPlaySeconds = m_playSeconds;
    m_slotsDirty = true;
    G7_LOG_INFO("save", "saved {} ({})", fs::toUtf8(path), file.meta.name);
    notice(uiText(slot == "quick" ? "UI_SAVED_QUICK" : slot == "auto" ? "UI_SAVED_AUTO" : "UI_SAVED"));
    if (m_scripts)
    {
        const script::Value args[] = {std::string(slot)};
        m_scripts->emit("game_saved", args);
    }
    return {};
}

void Engine::requestSave(std::string slot)
{
    // The thumbnail is taken from the next rendered frame (scene without the UI); without a renderer at once.
    m_pendingSave = std::move(slot);
    m_thumbnailRequested = m_device != nullptr;
}

void Engine::finishPendingSave()
{
    if (m_pendingSave && !m_thumbnailRequested)
    {
        const std::string slot = std::exchange(m_pendingSave, std::nullopt).value();
        (void)saveGame(slot);
    }
}

void Engine::captureThumbnail(u32 width, u32 height)
{
    if (!m_thumbnailRequested || width == 0 || height == 0)
    {
        return;
    }
    m_thumbnailRequested = false;
    // The scene before the game UI: RGBA, bottom row first; boxed down to the thumbnail (cropped to 16:9).
    const std::vector<u8> pixels =
        m_device->readPixels(0, 0, static_cast<i32>(width), static_cast<i32>(height));
    f32 srcW = static_cast<f32>(width);
    f32 srcH = static_cast<f32>(height);
    f32 x0 = 0.0f;
    f32 y0 = 0.0f;
    if (srcW / srcH > 16.0f / 9.0f)
    {
        x0 = (srcW - srcH * 16.0f / 9.0f) * 0.5f;
        srcW = srcH * 16.0f / 9.0f;
    }
    else
    {
        y0 = (srcH - srcW * 9.0f / 16.0f) * 0.5f;
        srcH = srcW * 9.0f / 16.0f;
    }
    asset::ImageData image{kThumbnailWidth, kThumbnailHeight, {}};
    image.rgba8.resize(static_cast<usize>(kThumbnailWidth) * kThumbnailHeight * 4);
    const f32 sx = srcW / static_cast<f32>(kThumbnailWidth);
    const f32 sy = srcH / static_cast<f32>(kThumbnailHeight);
    for (u32 y = 0; y < kThumbnailHeight; ++y)
    {
        for (u32 x = 0; x < kThumbnailWidth; ++x)
        {
            // The average of the source pixels under this one (a box filter).
            u32 sum[3] = {0, 0, 0};
            u32 count = 0;
            const auto fromX = static_cast<u32>(x0 + static_cast<f32>(x) * sx);
            const auto toX = std::max(fromX + 1, static_cast<u32>(x0 + static_cast<f32>(x + 1) * sx));
            const auto fromY = static_cast<u32>(y0 + static_cast<f32>(y) * sy);
            const auto toY = std::max(fromY + 1, static_cast<u32>(y0 + static_cast<f32>(y + 1) * sy));
            for (u32 v = fromY; v < std::min(toY, height); ++v)
            {
                const u32 row = height - 1 - v; // bottom row first in the read pixels
                for (u32 u = fromX; u < std::min(toX, width); ++u)
                {
                    const usize at = (static_cast<usize>(row) * width + u) * 4;
                    sum[0] += pixels[at];
                    sum[1] += pixels[at + 1];
                    sum[2] += pixels[at + 2];
                    ++count;
                }
            }
            const usize out = (static_cast<usize>(y) * kThumbnailWidth + x) * 4;
            for (int c = 0; c < 3; ++c)
            {
                image.rgba8[out + static_cast<usize>(c)] = static_cast<u8>(count > 0 ? sum[c] / count : 0);
            }
            image.rgba8[out + 3] = 255;
        }
    }
    auto png = asset::encodePng(image);
    if (png)
    {
        m_thumbnailPng = std::move(png).value();
    }
    else
    {
        G7_LOG_WARN("save", "thumbnail: {}", png.error().message);
        m_thumbnailPng.clear();
    }
}

Result<void> Engine::loadGame(std::string_view slot)
{
    const fs::Path path = save::slotPath(saveDirectory(), slot);
    auto file = save::readSaveFile(path);
    if (!file)
    {
        G7_LOG_WARN("save", "{}: {}", fs::toUtf8(path), file.error().message);
        notice(uiText(fs::exists(path) ? "UI_LOAD_FAILED" : "UI_LOAD_NONE"));
        return file.error();
    }
    G7_LOG_INFO("save", "loading {} ({})", fs::toUtf8(path), file.value().meta.name);
    m_dialog.reset();
    m_trade.reset();
    m_pendingSave.reset();
    beginLoading(file.value().state.currentWorld, {}, file.value().meta.place, false);
    m_loadJob->save = std::make_shared<save::SaveFile>(std::move(file).value());
    return {};
}

Result<void> Engine::applyLoadedSave(const save::SaveFile& file)
{
    m_config.player = true;
    if (auto applied = applyGame(file.state); !applied)
    {
        return applied;
    }
    m_playSeconds = file.meta.playSeconds;
    m_lastSavedPlaySeconds = m_playSeconds;
    if (file.meta.contentStamp != 0 && file.meta.contentStamp != contentStamp())
    {
        // S8: loaded anyway; the world files changed since (a newer build of the game).
        G7_LOG_WARN("save", "{}: saved with other world files", file.meta.name);
        notice(uiText("UI_LOAD_OTHER_CONTENT"));
    }
    return {};
}

bool Engine::anySave() const
{
    for (const save::SlotInfo& slot : saveSlots())
    {
        if (slot.meta)
        {
            return true;
        }
    }
    return false;
}

const std::vector<save::SlotInfo>& Engine::saveSlots() const
{
    if (m_slotsDirty)
    {
        m_slots = save::listSlots(saveDirectory());
        m_slotsDirty = false;
    }
    return m_slots;
}

void Engine::openSaveScreen(bool saving)
{
    m_slotsDirty = true;
    m_slotThumbnailOf.clear();
    ui::SaveScreen::Actions actions;
    actions.view = [this, saving]
    {
        ui::SaveScreenView view;
        for (const save::SlotInfo& slot : saveSlots())
        {
            if (saving && slot.slot == "auto")
            {
                continue; // the autosave is written by the game only
            }
            ui::SaveSlotRow row;
            row.slot = slot.slot;
            if (slot.meta)
            {
                row.title = slot.meta->name;
                const auto saved = std::chrono::sys_seconds(std::chrono::seconds(slot.meta->savedAtUtc));
                row.detail =
                    std::format("{:%d.%m.%Y %H:%M}", std::chrono::floor<std::chrono::minutes>(saved));
            }
            else
            {
                row.title = slot.error.empty() ? uiText("UI_SAVE_EMPTY") : uiText("UI_SAVE_BROKEN");
                row.usable = saving; // an empty or broken slot can be saved to, not loaded
            }
            view.rows.push_back(std::move(row));
        }
        view.thumbnail = m_slotThumbnail ? &*m_slotThumbnail : nullptr;
        view.message = m_saveScreenMessage;
        return view;
    };
    actions.select = [this, saving](usize row) { showSlotThumbnail(row, saving); };
    actions.question = [this, saving](usize row) -> std::string
    {
        const save::SlotInfo* slot = slotOfRow(row, saving);
        if (slot == nullptr)
        {
            return {};
        }
        if (saving && slot->meta)
        {
            return uiText("UI_SAVE_OVERWRITE");
        }
        if (!saving && !m_menuMode && m_playSeconds - m_lastSavedPlaySeconds > kUnsavedSeconds)
        {
            return uiText("UI_LOAD_UNSAVED"); // S5
        }
        return {};
    };
    actions.choose = [this, saving](usize row)
    {
        const save::SlotInfo* slot = slotOfRow(row, saving);
        if (slot != nullptr)
        {
            m_menuCommand = std::format("{}:{}", saving ? "save_to" : "load_from", slot->slot);
        }
    };
    actions.close = [this] { m_saveScreenMessage.clear(); };
    m_screens.push(std::make_unique<ui::SaveScreen>(
        saving ? ui::SaveScreen::Mode::Save : ui::SaveScreen::Mode::Load, std::move(actions)));
}

const save::SlotInfo* Engine::slotOfRow(usize row, bool saving) const
{
    usize at = 0;
    for (const save::SlotInfo& slot : saveSlots())
    {
        if (saving && slot.slot == "auto")
        {
            continue;
        }
        if (at++ == row)
        {
            return &slot;
        }
    }
    return nullptr;
}

void Engine::showSlotThumbnail(usize row, bool saving)
{
    const save::SlotInfo* slot = slotOfRow(row, saving);
    const std::string key = slot != nullptr ? slot->slot : std::string();
    if (key == m_slotThumbnailOf)
    {
        return;
    }
    m_slotThumbnailOf = key;
    m_slotThumbnail.reset();
    if (slot == nullptr || slot->thumbnailPng.empty() || !m_device)
    {
        return;
    }
    auto image = asset::decodeImage(slot->thumbnailPng, slot->slot);
    if (!image)
    {
        return;
    }
    auto texture =
        render::createTexture(*m_device, asset::textureFromImage(std::move(image).value(), true), true);
    if (texture)
    {
        m_slotThumbnail = std::move(texture).value();
    }
}

void Engine::runSaveCommand(const std::string& command)
{
    const auto colon = command.find(':');
    const std::string what = command.substr(0, colon);
    const std::string slot = colon == std::string::npos ? std::string() : command.substr(colon + 1);
    if (what == "save_to")
    {
        // The thumbnail was taken when the menu opened; the menus close once it is written.
        if (saveGame(slot))
        {
            m_screens.clear();
        }
        else
        {
            m_saveScreenMessage = m_notice;
        }
    }
    else if (what == "load_from")
    {
        if (!loadGame(slot))
        {
            m_saveScreenMessage = m_notice;
        }
    }
}

void Engine::autosave()
{
    if (m_config.settings.get<bool>("game.autosave", true))
    {
        m_autosaveWait = kAutosavePatience; // S3: as soon as saving is possible (on the ground, no dialogue)
    }
}

void Engine::updatePlayTime(f64 realSeconds)
{
    if (m_autosaveWait > 0.0)
    {
        m_autosaveWait -= realSeconds;
        if (saveBlocker().empty() && !m_pendingSave)
        {
            m_autosaveWait = 0.0;
            requestSave("auto");
        }
    }
    if (!m_menuMode && !m_loadJob && m_player.valid() && !m_paused && !m_screens.pausesGame())
    {
        m_playSeconds += realSeconds;
    }
}

void Engine::saveKeys()
{
    if (m_actions.pressed(m_input, platform::Action::QuickSave))
    {
        if (const std::string blocked = saveBlocker(); !blocked.empty())
        {
            notice(uiText(blocked));
        }
        else
        {
            requestSave("quick");
        }
    }
    if (m_actions.pressed(m_input, platform::Action::QuickLoad) && !m_menuMode && !m_loadJob)
    {
        (void)loadGame("quick"); // S5: F9 loads at once
    }
}

void Engine::bindSaveSlotFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"save_game", "save_game(slot?: string) -> boolean",
             "Speichert das Spiel in einen Platz (`\"slot_01\"` … `\"slot_15\"`, `\"quick\"`, Vorgabe "
             "`\"quick\"`); "
             "`false`, wenn es gerade nicht geht (Dialog, unter Wasser …). Konsole, Tests.",
             "Spielstand", [this](std::span<const script::Value> a) -> Result<script::Value>
             {
                 const std::string slot = a.empty() ? std::string("quick") : std::string(a[0].asString());
                 return script::Value(saveGame(slot).ok());
             }});
    vm.bind({"load_game", "load_game(slot?: string) -> boolean",
             "Lädt einen Spielstand (Vorgabe `\"quick\"`) über den Ladebildschirm; `false`, wenn es ihn "
             "nicht gibt "
             "oder er beschädigt ist.",
             "Spielstand", [this](std::span<const script::Value> a) -> Result<script::Value>
             {
                 const std::string slot = a.empty() ? std::string("quick") : std::string(a[0].asString());
                 return script::Value(loadGame(slot).ok());
             }});
    vm.bind(
        {"autosave", "autosave()",
         "Speichert in den Autosave-Platz, wenn es eingeschaltet ist und gerade geht (S3; Kapitelwechsel).",
         "Spielstand", [this](std::span<const script::Value>) -> Result<script::Value>
         {
             autosave();
             return script::Value();
         }});
    vm.bind({"game_saved",
             "on(\"game_saved\", fn(slot: string))",
             "Ein Spielstand ist geschrieben (M15).",
             "Ereignisse",
             {}});
}
} // namespace g7
