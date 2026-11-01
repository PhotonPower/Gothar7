// The game UI in the engine (M14, docs/modules/ui.md "Plan M14"): resources from the VFS, the canvas, the
// screen stack with its input (ui_* actions, stick, mouse), UI sounds, hot reload of the UI files, and
// drawing after the tonemapped scene and before ImGui.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>
#include <g7/ui/GameScreens.hpp>
#include <g7/ui/SamplePage.hpp>

#include <algorithm>
#include <format>

namespace g7
{
namespace
{
/// UI units are pixels at this framebuffer height.
constexpr f32 kReferenceHeight = 1080.0f;
/// How often the UI files are checked for changes (hot reload).
constexpr f64 kReloadCheckSeconds = 0.5;
constexpr u64 kCreatureFocusBit = 1ull << 62; ///< as in Engine.cpp: the focus is a creature
constexpr u32 kRunePlaces = 6;
} // namespace

void Engine::initGameUi()
{
    auto canvas = ui::Canvas::create(m_device.get(), m_shaders.get());
    if (!canvas)
    {
        G7_LOG_ERROR("ui", "game UI canvas: {} (no game UI)", canvas.error().message);
        return;
    }
    m_canvas = std::move(canvas).value();
    m_navRepeat.delay = static_cast<f32>(m_config.settings.get<f64>("ui.nav_repeat_delay", 0.35));
    m_navRepeat.rate = static_cast<f32>(m_config.settings.get<f64>("ui.nav_repeat_rate", 0.08));
    const std::string language = m_config.settings.get<std::string>("ui.language", "de");
    auto resources = ui::UiResources::load([this](std::string_view path) { return m_vfs.read(path); },
                                           language, m_device.get());
    if (!resources)
    {
        G7_LOG_ERROR("ui", "game UI: {} (no game UI)", resources.error().message);
        return;
    }
    m_gameUi = std::move(resources).value();
    m_gameUiStamp.clear();
    m_gameUiReloadTimer = kReloadCheckSeconds;
    reloadGameUiIfChanged(0.0); // records the files' times
    if (m_config.uiSample)
    {
        m_screens.push(ui::makeSampleScreen());
    }
}

void Engine::reloadGameUiIfChanged(f64 realSeconds)
{
    if (!m_gameUi || !m_assets || !m_assets->hotReload())
    {
        return;
    }
    m_gameUiReloadTimer += realSeconds;
    if (m_gameUiReloadTimer < kReloadCheckSeconds)
    {
        return;
    }
    m_gameUiReloadTimer = 0.0;
    std::string stamp;
    for (const std::string& path : m_gameUi->files())
    {
        const auto disk = m_vfs.diskPath(path);
        if (!disk)
        {
            continue; // packed or missing: nothing to watch
        }
        const auto time = fs::lastWriteTime(*disk);
        stamp += std::format("{}={};", path, time ? time.value().time_since_epoch().count() : 0);
    }
    if (m_gameUiStamp.empty())
    {
        m_gameUiStamp = std::move(stamp);
        return;
    }
    if (stamp == m_gameUiStamp)
    {
        return;
    }
    m_gameUiStamp = std::move(stamp);
    reloadGameUi();
}

void Engine::reloadGameUi()
{
    if (!m_gameUi)
    {
        return;
    }
    const std::string language = m_config.settings.get<std::string>("ui.language", "de");
    auto resources = ui::UiResources::load([this](std::string_view path) { return m_vfs.read(path); },
                                           language, m_device.get());
    if (!resources)
    {
        G7_LOG_ERROR("ui", "game UI not reloaded: {} (the previous version stays)",
                     resources.error().message);
        return;
    }
    m_gameUi = std::move(resources).value();
    G7_LOG_INFO("ui", "game UI reloaded ({})", language);
}

void Engine::updateGameUi(f64 realSeconds, bool allowMouse, bool allowKeyboard)
{
    reloadGameUiIfChanged(realSeconds);
    if (!m_gameUi || !m_window)
    {
        return;
    }
    const auto size = m_window->size();
    const auto pixels = m_window->pixelSize();
    const Vec2 pointerScale(
        size.width > 0 ? static_cast<f32>(pixels.width) / static_cast<f32>(size.width) : 1.0f,
        size.height > 0 ? static_cast<f32>(pixels.height) / static_cast<f32>(size.height) : 1.0f);
    ui::UiInput input =
        ui::buildUiInput(m_input, m_actions, m_navRepeat, static_cast<f32>(realSeconds), pointerScale);
    if (!allowKeyboard)
    {
        // The debug UI has the keyboard (a focused text field): no navigation, choosing or going back.
        input.nav = ui::NavDir::None;
        input.accept = false;
        input.back = false;
        input.prevTab = false;
        input.nextTab = false;
        input.drop = false;
    }
    if (!allowMouse)
    {
        input.pointerMoved = false;
        input.click = false;
        input.wheel = 0.0f;
    }
    // Without a framebuffer (no renderer) the layout assumes 1920 x 1080.
    const f32 width = pixels.width > 0 ? static_cast<f32>(pixels.width) : 1920.0f;
    const f32 height = pixels.height > 0 ? static_cast<f32>(pixels.height) : 1080.0f;
    const f32 userScale =
        std::clamp(static_cast<f32>(m_config.settings.get<f64>("ui.scale", 1.0)), 0.5f, 2.0f);
    // The inventory key closes the inventory again (Gothic: Tab), though the screen holds the game keys.
    if (allowKeyboard && m_screens.top() != nullptr && m_screens.top()->name() == "inventory" &&
        m_actions.pressed(m_input, platform::Action::Inventory))
    {
        setInventoryOpen(false);
        m_lootTarget.reset();
    }
    // The pause action closes the pause menu again (Start on the gamepad; Esc is also ui_back).
    if (allowKeyboard && !m_menuMode && m_screens.top() != nullptr && m_screens.top()->name() == "menu" &&
        m_actions.pressed(m_input, platform::Action::Pause))
    {
        m_menuCommand = "resume";
    }
    if (allowKeyboard)
    {
        journalKeys();
    }
    advanceItemSpin(static_cast<f32>(realSeconds));
    syncGameScreens();
    const ui::Rect area{Vec2(0.0f), Vec2(width, height)};
    const f32 scale = height / kReferenceHeight * userScale;
    m_screens.update(input, static_cast<f32>(realSeconds), area, scale, &m_gameUi->screenLayout());
    syncGameScreens(); // what the input changed (a dialogue ended, a document closed)

    // A loading hitch (world change, first frame) must not eat messages and subtitles.
    const f32 uiSeconds = std::min(static_cast<f32>(realSeconds), 0.1f);
    m_messages.update(uiSeconds);
    m_subtitles.update(uiSeconds);
    std::vector<ui::SubtitleQueue::Line> subtitles;
    if (m_config.settings.get<bool>("ui.subtitles", true))
    {
        subtitles.assign(m_subtitles.lines().begin(), m_subtitles.lines().end());
        if (m_dialog && !m_dialog->lines.empty() && m_dialog->linePresented)
        {
            const DialogLine& line = m_dialog->lines.front();
            subtitles.push_back(
                {contentText(line.name), contentText(line.text), line.seconds - m_dialog->lineTime});
        }
    }
    m_hud.update(buildHudModel(Vec2(width, height)), m_messages, subtitles, area, scale,
                 &m_gameUi->screenLayout());
    for (const std::string& event : m_screens.takeEvents())
    {
        const std::string_view sound = m_gameUi->style().sound(event);
        if (!sound.empty())
        {
            (void)playSound(sound);
        }
    }
}

void Engine::drawGameUi(u32 width, u32 height)
{
    if (!m_gameUi || width == 0 || height == 0)
    {
        return;
    }
    const f32 userScale =
        std::clamp(static_cast<f32>(m_config.settings.get<f64>("ui.scale", 1.0)), 0.5f, 2.0f);
    const f32 scale = static_cast<f32>(height) / kReferenceHeight * userScale;
    m_canvas.begin(Vec2(static_cast<f32>(width), static_cast<f32>(height)), scale);
    m_hud.draw(m_canvas, *m_gameUi);
    m_screens.draw(m_canvas, *m_gameUi); // may render item pictures into their atlases (other framebuffers)
    m_device->bindFramebuffer(nullptr);
    m_canvas.end(m_device.get());
}
void Engine::showScreenMessage(std::string text, f32 seconds)
{
    m_messages.push(std::move(text), seconds);
}

bool Engine::heroHasMagic() const
{
    if (!m_hero)
    {
        return false;
    }
    if (m_hero->talent("magic_circle") > 0)
    {
        return true;
    }
    for (u32 slot = 0; slot < kRunePlaces; ++slot)
    {
        if (!runeInSlot(slot).empty())
        {
            return true;
        }
    }
    return false;
}

ui::HudModel Engine::buildHudModel(Vec2 pixels) const
{
    ui::HudModel model;
    model.visible = m_hero && m_player.valid() && !m_screens.hidesHud();
    if (!model.visible)
    {
        return model;
    }
    const auto value = [](const gameplay::Character& c, std::string_view current, std::string_view max)
    { return ui::BarValue{static_cast<f32>(c.attribute(current)), static_cast<f32>(c.attribute(max))}; };
    model.health = value(*m_hero, "hp", "hp_max");
    if (heroHasMagic())
    {
        model.mana = value(*m_hero, "mana", "mana_max");
    }
    if (m_combatTarget)
    {
        const Creature* enemy = creature(*m_combatTarget);
        if (enemy != nullptr && enemy->character)
        {
            model.enemy =
                ui::HudModel::Enemy{focusName(gameplay::FocusKind::Npc, enemy->id | kCreatureFocusBit),
                                    value(*enemy->character, "hp", "hp_max")};
        }
    }
    if (m_swimmer.mode() == gameplay::WaterMode::Dive && m_movementSettings.swim.airSeconds > 0.0f)
    {
        model.air = std::clamp(m_swimmer.airSeconds() / m_movementSettings.swim.airSeconds, 0.0f, 1.0f);
    }
    // The focus name above the target (not in the inventory, a dialogue or a blocking screen).
    if (m_focus && !m_focus->name.empty() && !m_inventoryOpen && !m_dialog && !m_screens.blocksGameInput())
    {
        const Vec4 clip = m_camera.viewProjection() * Vec4(m_focus->point + Vec3(0.0f, 0.35f, 0.0f), 1.0f);
        if (clip.w > 0.0f)
        {
            const Vec2 ndc = Vec2(clip) / clip.w;
            if (std::abs(ndc.x) <= 1.0f && std::abs(ndc.y) <= 1.0f)
            {
                model.focus = ui::HudModel::Focus{
                    m_focus->name, Vec2((ndc.x * 0.5f + 0.5f) * pixels.x, (0.5f - ndc.y * 0.5f) * pixels.y)};
            }
        }
    }
    return model;
}

void Engine::syncGameScreens()
{
    syncItemScreens();    // inventory, trading, containers (part D)
    syncJournalScreens(); // character sheet, diary, map (part E)
    // Dialogue (replaces the ImGui window; E3: like Gothic 1).
    ui::Screen* dialog = m_screens.find("dialog");
    if (m_dialog && dialog == nullptr)
    {
        ui::DialogScreen::Actions actions;
        actions.view = [this]
        {
            ui::DialogView view;
            if (!m_dialog)
            {
                return view;
            }
            view.menu = m_dialog->lines.empty();
            view.hidden = m_trade.has_value();
            for (const DialogOption& o : m_dialog->menu)
            {
                view.options.push_back(contentText(o.text));
            }
            view.selected = static_cast<usize>(std::max(0, m_dialog->selected));
            return view;
        };
        actions.skip = [this] { dialogSkip(); };
        actions.choose = [this](usize option)
        {
            if (m_dialog && option < m_dialog->menu.size())
            {
                dialogChoose(option);
            }
        };
        actions.select = [this](usize option)
        {
            if (m_dialog)
            {
                m_dialog->selected = static_cast<i32>(option);
            }
        };
        m_screens.push(std::make_unique<ui::DialogScreen>(std::move(actions)));
    }
    else if (!m_dialog && dialog != nullptr)
    {
        dialog->close();
    }

    // A document being read.
    auto* document = dynamic_cast<ui::DocumentScreen*>(m_screens.find("document"));
    if (document != nullptr && !document->closed() &&
        (!m_document || document->title() != contentText(m_document->title) ||
         document->text() != contentText(m_document->text)))
    {
        document->close();
    }
    if (m_document && (document == nullptr || document->closed()))
    {
        m_screens.push(std::make_unique<ui::DocumentScreen>(
            contentText(m_document->title), contentText(m_document->text), [this] { m_document.reset(); }));
    }

    // A choice at a mob (anvil, bed).
    const bool choosing = m_mobUse && m_mobUse->phase == MobUse::Phase::Loop && !m_mobUse->containerOpen &&
                          (m_mobUse->type == "anvil" || !mobChoices().empty());
    ui::Screen* choice = m_screens.find("choice");
    if (choosing && choice == nullptr)
    {
        ui::ChoiceScreen::Actions actions;
        actions.view = [this]
        {
            ui::ChoiceView view;
            if (!m_mobUse)
            {
                return view;
            }
            view.title = contentText(m_mobs.at(m_mobUse->vob.value).name);
            view.options = mobChoices();
            if (m_mobUse->strikesLeft > 0)
            {
                view.message = uiText("UI_ANVIL_STRIKES", {{"n", std::to_string(m_mobUse->strikesLeft)}});
            }
            else
            {
                view.message = m_mobUse->choiceMessage;
            }
            return view;
        };
        actions.choose = [this](usize option) { (void)chooseMobOption(option); };
        actions.leave = [this] { mobCommand(MobCommand::Leave); };
        m_screens.push(std::make_unique<ui::ChoiceScreen>(std::move(actions)));
    }
    else if (!choosing && choice != nullptr)
    {
        choice->close();
    }

    // Lockpicking.
    const bool picking = m_mobUse && m_mobUse->phase == MobUse::Phase::Picklock && m_mobUse->lockpick;
    ui::Screen* lockpick = m_screens.find("lockpick");
    if (picking && lockpick == nullptr)
    {
        ui::LockpickScreen::Actions actions;
        actions.view = [this]
        {
            ui::LockpickView view;
            if (!m_mobUse || !m_mobUse->lockpick)
            {
                return view;
            }
            view.title = contentText(m_mobs.at(m_mobUse->vob.value).name);
            view.progress = m_mobUse->lockpick->progress();
            view.length = m_mobUse->lockpick->length();
            view.picks = m_hero ? m_hero->itemCount(lockpickItem()) : 0;
            view.result = m_mobUse->lockpickResult;
            return view;
        };
        actions.turn = [this](bool left) { mobCommand(left ? MobCommand::TurnLeft : MobCommand::TurnRight); };
        actions.leave = [this] { mobCommand(MobCommand::Leave); };
        m_screens.push(std::make_unique<ui::LockpickScreen>(std::move(actions)));
    }
    else if (!picking && lockpick != nullptr)
    {
        lockpick->close();
    }
}

void Engine::bindUiFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind({"inventory_open", "inventory_open(open?: boolean)",
             "Öffnet (oder mit `false` schließt) das Inventar des Helden wie die Inventar-Taste (Konsole, "
             "Tests).",
             "Bildschirm", [this](std::span<const script::Value> a) -> Result<script::Value>
             {
                 if (!m_hero)
                 {
                     return Error{"no hero"};
                 }
                 setInventoryOpen(a.empty() || a[0].asBool(true));
                 return script::Value{};
             }});
    vm.bind({"menu_open", "menu_open(page?: string, tab?: number)",
             "Öffnet das Pausenmenü wie Esc, mit `\"options\"` gleich die Optionen darüber, auf Reiter `tab` "
             "(ab 1; "
             "Konsole, Prüfbilder).",
             "Bildschirm", [this](std::span<const script::Value> a) -> Result<script::Value>
             {
                 if (!m_gameUi)
                 {
                     return Error{"no game UI"};
                 }
                 if (!m_menuMode)
                 {
                     openPauseMenu();
                 }
                 if (!a.empty() && a[0].asString() == "options")
                 {
                     m_menuCommand = "options"; // opened after this frame's screen update
                     m_optionsTab =
                         a.size() > 1 ? static_cast<usize>(std::max(a[1].asNumber(1.0), 1.0)) - 1 : 0;
                 }
                 return script::Value{};
             }});
    vm.bind(
        {"ui_text", "ui_text(key: string, args?: table): string",
         "Ein UI-Text aus `text/ui.<sprache>.toml` in der eingestellten Sprache, Platzhalter `{name}` aus "
         "`args` (z. B. `ui_text(\"UI_QUEST_NEW\", { quest = tr(q.name) })`) – für Texte, die erst zur "
         "Laufzeit entstehen.",
         "Bildschirm", [this](std::span<const script::Value> a) -> Result<script::Value>
         {
             if (a.empty() || !a[0].isString())
             {
                 return Error{"argument 1 must be the key"};
             }
             std::string text(m_uiStrings.get(a[0].asString()));
             if (a.size() > 1)
             {
                 if (const script::Table* args = a[1].asTable())
                 {
                     for (const auto& [name, value] : args->fields)
                     {
                         const std::string arg = value.isString()    ? std::string(value.asString())
                                                 : value.isInteger() ? std::to_string(value.asInteger())
                                                                     : value.toString();
                         text = ui::substitute(text, {{name, arg}});
                     }
                 }
             }
             return script::Value{std::move(text)};
         }});
    vm.bind({"tr", "tr(text: string): string",
             "Ein deutscher Inhaltstext in der eingestellten Sprache (Übersetzung aus "
             "`text/script.<sprache>.json`, sonst der Text selbst; M14 Teil G).",
             "Bildschirm", [this](std::span<const script::Value> a) -> Result<script::Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"argument 1 must be the text"};
                 }
                 return script::Value{contentText(a[0].asString())};
             }});
    vm.bind({"screen_message", "screen_message(text: string, seconds?: number)",
             "Zeigt dem Spieler eine Meldung oben in der Mitte (F7; Vorgabe 3 s), z. B. `Erfahrung +50`.",
             "Bildschirm", [this](std::span<const script::Value> a) -> Result<script::Value>
             {
                 if (a.empty() || !a[0].isString())
                 {
                     return Error{"argument 1 must be the text"};
                 }
                 const f32 seconds =
                     a.size() > 1 && a[1].isNumber() ? static_cast<f32>(a[1].asNumber()) : 3.0f;
                 if (!(seconds > 0.0f))
                 {
                     return Error{"argument 2 (seconds) must be > 0"};
                 }
                 G7_LOG_INFO("engine", "message: {}", a[0].asString());
                 showScreenMessage(std::string(a[0].asString()), seconds);
                 return script::Value{};
             }});
}
} // namespace g7
