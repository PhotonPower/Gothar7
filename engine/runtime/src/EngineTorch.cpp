// The torch (owner decision, as Gothic 1): an item of category "torch" without an equipment slot. Using it
// lights it - figuren's none/t_torch_light: at "torch_take" it is in the left hand, at "torch_light" flame
// and light burn at its socket_flame - then the additive hold pose (none/a_torch_hold over clavicle_l). Using
// it again puts it out and away; so do water and drawing a two-handed weapon, a bow, a crossbow or magic
// (one-handed weapons and fists go with it). It burns for as long as it is held. Dropped (none/t_torch_drop)
// it lies burning.

#include "PlayerFigure.hpp"

#include <g7/core/Log.hpp>
#include <g7/runtime/Engine.hpp>

namespace g7
{
namespace
{
using script::Value;

constexpr std::string_view kTorchItem = "it_torch";
constexpr std::string_view kHand = "socket_hand_l";
constexpr std::string_view kFlameEffect = "torch";
const Vec4 kFlame(0.0f, 0.6f, 0.0f, 1.0f);  ///< socket_flame in the item (figuren #268): 0.6 m up the shaft
constexpr f32 kLightFallbackSeconds = 1.2f; ///< no clip: lit after this long
constexpr std::string_view kHoldClip = "none/a_torch_hold";
} // namespace

void Engine::holdTorch(f32 blend)
{
    // The carrying pose, held until the torch is put away (an "a_" clip of 1 s: looped).
    if (m_figure && m_figure->animator.hasClip(kHoldClip))
    {
        m_figure->animator.playOverlay(kHoldClip, "clavicle_l", blend, true, "none/a_neutral", true);
    }
}

bool Engine::torchLit() const noexcept
{
    return m_torch.has_value() && m_torch->lit;
}

void Engine::lightTorch(std::string_view item)
{
    if (m_torch || !m_figure || !m_hero || m_hero->itemCount(item) == 0)
    {
        return;
    }
    if (m_weaponMode >= 3) // a bow or crossbow, magic, an animal's shape: no hand free
    {
        notice("Dafür ist keine Hand frei.");
        return;
    }
    if (m_weaponMode == 1)
    {
        const script::Instance* weapon = m_scripts ? m_scripts->findInstance("Item", m_weaponDrawn) : nullptr;
        if (weapon != nullptr && weapon->fields["category"].asString() == "melee_2h")
        {
            notice("Dafür ist keine Hand frei.");
            return;
        }
    }
    Torch t;
    t.item = std::string(item);
    if (m_figure->animator.hasState("none_t_torch_light") && m_weaponMode == 0)
    {
        m_figure->animator.enter("none_t_torch_light", 0.15f);
        t.lighting = true;
    }
    m_torch = std::move(t);
    if (!m_torch->lighting)
    {
        torchEvent("torch_take");
        torchEvent("torch_light");
    }
}

void Engine::torchEvent(std::string_view event)
{
    if (!m_torch)
    {
        return;
    }
    if (event == "torch_take")
    {
        if (const LoadedModel* model = itemModel(m_torch->item))
        {
            if (auto attached = attachModel(kHand, model, nullptr); !attached)
            {
                G7_LOG_WARN("engine", "torch: {}", attached.error().message);
            }
        }
    }
    else if (event == "torch_light" && !m_torch->lit)
    {
        m_torch->lit = true;
        m_torch->lighting = false;
        const auto hand = playerSocketTransform(kHand);
        m_torch->flame =
            startEffect(kFlameEffect, hand ? Vec3(*hand * kFlame) : m_playerFeet + Vec3(0.0f, 1.5f, 0.0f));
        holdTorch(0.25f);
        if (m_scripts)
        {
            const Value args[] = {m_torch->item};
            m_scripts->emit("torch_lit", args);
        }
    }
    else if (event == "torch_drop")
    {
        dropTorch();
    }
}

void Engine::putTorchAway()
{
    if (!m_torch)
    {
        return;
    }
    if (m_torch->flame)
    {
        m_particles.stop(*m_torch->flame);
    }
    detachFromPlayer(kHand);
    if (m_figure && m_torch->lit)
    {
        m_figure->animator.stopOverlay(0.25f);
    }
    m_torch.reset();
    if (m_scripts)
    {
        const Value args[] = {std::string(kTorchItem)};
        m_scripts->emit("torch_out", args);
    }
}

void Engine::dropTorch()
{
    if (!m_torch || !m_hero)
    {
        return;
    }
    const std::string item = m_torch->item;
    const auto hand = playerSocketTransform(kHand);
    const Vec3 at = hand ? Vec3((*hand)[3]) : m_playerFeet + Vec3(0.0f, 1.0f, 0.0f);
    putTorchAway();
    if (!m_hero->removeItem(item, 1))
    {
        return;
    }
    // It lies burning where it fell (on the ground below the hand).
    Vec3 ground = Vec3(at.x, m_playerFeet.y, at.z);
    auto vob = spawnItem(item, 1, ground + Vec3(0.0f, 0.05f, 0.0f), m_movement.yaw());
    if (!vob)
    {
        G7_LOG_WARN("engine", "torch: {}", vob.error().message);
        return;
    }
    if (const auto flame = startEffect(kFlameEffect, ground + Vec3(0.0f, 0.15f, 0.0f)))
    {
        m_burningItems[vob.value().value] = *flame;
    }
}

void Engine::fixedUpdateTorch(f32 seconds)
{
    // Dropped torches stop burning once picked up (their vob is gone).
    for (auto it = m_burningItems.begin(); it != m_burningItems.end();)
    {
        if (m_scene.findById(world::VobId{it->first}) == entt::null)
        {
            m_particles.stop(it->second);
            it = m_burningItems.erase(it);
            continue;
        }
        ++it;
    }
    if (!m_torch)
    {
        return;
    }
    Torch& t = *m_torch;
    // Water puts it out; the torch gone from the bag (sold, stolen) too.
    if (m_swimmer.mode() != gameplay::WaterMode::Land || !m_hero || m_hero->itemCount(t.item) == 0)
    {
        putTorchAway();
        return;
    }
    t.seconds += seconds;
    if (t.lighting)
    {
        const bool inClip = m_figure && m_figure->animator.state() == "none_t_torch_light";
        if (!inClip || t.seconds > kLightFallbackSeconds * 2.0f)
        {
            torchEvent("torch_take"); // the clip broken off: lit all the same
            torchEvent("torch_light");
        }
        return;
    }
    // Back into the carrying pose once another overlay (a dialogue gesture) has taken its place.
    if (m_figure && m_figure->animator.overlayClip().empty() &&
        m_figure->animator.state() != "none_t_torch_drop")
    {
        holdTorch(0.25f);
    }
    // The flame follows the hand.
    if (t.flame)
    {
        if (const auto hand = playerSocketTransform(kHand))
        {
            m_particles.move(*t.flame, Vec3(*hand * kFlame), Vec3(0.0f, 1.0f, 0.0f));
        }
    }
}

void Engine::bindTorchFunctions()
{
    script::ScriptVm& vm = *m_scripts;
    vm.bind(
        {"hero_torch", "hero_torch() -> boolean",
         "Zündet die Fackel an bzw. steckt sie weg, wie Benutzen im Inventar (Entscheidung Projektinhaber); "
         "gibt zurück, ob sie danach brennt bzw. gerade angezündet wird.",
         "Gegenstände", [this](std::span<const Value>) -> Result<Value>
         {
             if (m_torch)
             {
                 putTorchAway();
             }
             else
             {
                 lightTorch(kTorchItem);
             }
             return Value(m_torch.has_value());
         }});
    vm.bind({"hero_torch_drop", "hero_torch_drop()",
             "Lässt die brennende Fackel fallen (none/t_torch_drop); sie brennt am Boden weiter.",
             "Gegenstände", [this](std::span<const Value>) -> Result<Value>
             {
                 if (torchLit())
                 {
                     if (m_figure && m_figure->animator.hasState("none_t_torch_drop"))
                     {
                         m_figure->animator.stopOverlay(0.1f); // the throw moves the arm itself
                         m_figure->animator.enter("none_t_torch_drop", 0.1f);
                     }
                     else
                     {
                         dropTorch();
                     }
                 }
                 return Value();
             }});
    vm.bind({"hero_torch_lit", "hero_torch_lit() -> boolean", "Ob der Held eine brennende Fackel hält.",
             "Gegenstände", [this](std::span<const Value>) -> Result<Value> { return Value(torchLit()); }});
    vm.bind({"torch_lit",
             "on(\"torch_lit\", fn(item: string))",
             "Der Held hat seine Fackel angezündet.",
             "Ereignisse",
             {}});
    vm.bind({"torch_out",
             "on(\"torch_out\", fn(item: string))",
             "Die Fackel des Helden ist aus (weggesteckt, fallen gelassen, im Wasser).",
             "Ereignisse",
             {}});
}
} // namespace g7
