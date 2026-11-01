#pragma once

// The HUD of the game UI (M14 part C): bars as in Gothic 1 (owner decision F3: health bottom left, mana
// bottom right only with magic, enemy health top centre, air while diving), the focus name over the target,
// screen messages top centre (F7, 3 s) and subtitles bottom centre with the speaker's name (F6). The engine
// fills a HudModel every frame; the HUD only shows it.

#include <g7/core/Math.hpp>
#include <g7/core/Types.hpp>
#include <g7/ui/Canvas.hpp>

#include <algorithm>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace g7::ui
{
class Layout;
class UiResources;

struct BarValue
{
    f32 value = 0.0f;
    f32 max = 0.0f;
    [[nodiscard]] f32 fraction() const noexcept
    {
        return max > 0.0f ? std::clamp(value / max, 0.0f, 1.0f) : 0.0f;
    }
};

struct HudModel
{
    bool visible = true;
    BarValue health;
    std::optional<BarValue> mana; ///< only when the hero has magic (F3)
    struct Enemy
    {
        std::string name;
        BarValue health;
    };
    std::optional<Enemy> enemy; ///< the locked enemy in a fight
    std::optional<f32> air;     ///< 0..1, only under water
    struct Focus
    {
        std::string name;
        Vec2 point{0.0f}; ///< framebuffer pixels: the bottom centre of the label
    };
    std::optional<Focus> focus;
};

/// Messages to the player (F7): newest at the bottom, each for its time, the last half second fading.
class MessageQueue
{
public:
    static constexpr usize kMaxShown = 4;
    struct Message
    {
        std::string text;
        f32 remaining = 3.0f;
    };
    void push(std::string text, f32 seconds = 3.0f);
    void update(f32 deltaSeconds);
    void clear() noexcept { m_messages.clear(); }
    [[nodiscard]] const std::deque<Message>& messages() const noexcept { return m_messages; }

private:
    std::deque<Message> m_messages;
};

/// Subtitles (F6): spoken lines and shouts with the speaker's name, each for its time.
class SubtitleQueue
{
public:
    static constexpr usize kMaxShown = 2;
    struct Line
    {
        std::string speaker;
        std::string text;
        f32 remaining = 2.0f;
    };
    /// A line for `seconds`; the same speaker's previous line is replaced.
    void push(std::string speaker, std::string text, f32 seconds);
    void update(f32 deltaSeconds);
    void clear() noexcept { m_lines.clear(); }
    [[nodiscard]] const std::deque<Line>& lines() const noexcept { return m_lines; }

private:
    std::deque<Line> m_lines;
};

/// How long a line without a voice take stays: reading time from its length.
[[nodiscard]] f32 readingSeconds(std::string_view text) noexcept;

class Hud
{
public:
    /// Places everything for this frame (boxes from layout.toml "hud.*").
    void update(const HudModel& model, const MessageQueue& messages,
                std::span<const SubtitleQueue::Line> subtitles, const Rect& area, f32 scale,
                const Layout* layout);
    void draw(Canvas& canvas, const UiResources& ui) const;

    // Inspection (tests).
    [[nodiscard]] const HudModel& model() const noexcept { return m_model; }
    [[nodiscard]] const Rect& healthRect() const noexcept { return m_health; }
    [[nodiscard]] const Rect& manaRect() const noexcept { return m_mana; }
    [[nodiscard]] const Rect& enemyRect() const noexcept { return m_enemy; }
    [[nodiscard]] const Rect& airRect() const noexcept { return m_air; }
    [[nodiscard]] const Rect& messageRect() const noexcept { return m_message; }
    [[nodiscard]] const Rect& subtitleRect() const noexcept { return m_subtitle; }

private:
    HudModel m_model;
    std::vector<MessageQueue::Message> m_messages;
    std::vector<SubtitleQueue::Line> m_subtitles;
    f32 m_scale = 1.0f;
    Rect m_health;
    Rect m_mana;
    Rect m_enemy;
    Rect m_air;
    Rect m_message;
    Rect m_subtitle;
};
} // namespace g7::ui
