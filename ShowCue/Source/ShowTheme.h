#pragma once
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Premium Playout palette — ma trận màu động Sáng / Tối (Apple HIG-inspired).
 * Toàn bộ màu opaque; component chỉ đọc qua get() hoặc findColour từ LookAndFeel.
 */
namespace ShowTheme
{
    constexpr float kPanelCornerRadius = 6.0f;

    inline juce::Colour rgb (uint32_t hex) noexcept
    {
        return juce::Colour (static_cast<juce::uint32> (0xFF000000u | (hex & 0x00FFFFFFu)));
    }

    struct Palette
    {
        juce::Colour windowBg;
        juce::Colour panelBg;
        juce::Colour panelElevated;
        juce::Colour centerBg;
        juce::Colour border;
        juce::Colour borderSubtle;
        juce::Colour textPrimary;
        juce::Colour textSecondary;
        juce::Colour textMuted;
        juce::Colour accent;
        juce::Colour accentSoft;
        juce::Colour accentOnDark;
        juce::Colour success;
        juce::Colour padPlayingBorder;
        juce::Colour warning;
        juce::Colour danger;
        juce::Colour rowHover;
        juce::Colour rowSelected;
        juce::Colour listRowBg;
        juce::Colour padGradientTop;
        juce::Colour padGradientBottom;
        juce::Colour padBorder;
        juce::Colour waveformFill;
        juce::Colour waveformPlayhead;
        juce::Colour shortcutBadgeBg;
        juce::Colour shortcutBadgeText;
        juce::Colour dragTargetGlow;
        juce::Colour dragGhostFill;
        juce::Colour dragGhostText;
        juce::Colour trimMarkerStart;
        juce::Colour trimMarkerEnd;
        juce::Colour buttonSecondary;
        juce::Colour sliderTrack;
        juce::Colour sliderThumb;
        /** TextEditor + Slider text-box — tương phản rõ trên nền panel trắng (Light). */
        juce::Colour inputBg;
        juce::Colour inputOutline;
        /** Prev/Next transport — nền xám nhạt Light, charcoal Dark. */
        juce::Colour navButtonBg;
    };

    /** themeId: 1 = Dark, 2 = Light, 3 = Match System (macOS). */
    inline bool resolveIsDarkFromThemeId (int themeId) noexcept
    {
        if (themeId == 2)
            return false;

        if (themeId == 3)
            return juce::Desktop::getInstance().isDarkModeActive();

        return true;
    }

    /** Dark — Charcoal FOH / chữ bạc dịu mắt. */
    inline Palette darkPalette() noexcept
    {
        const auto accent = rgb (0x0A84FF);
        const auto success = rgb (0x30D158);

        return {
            rgb (0x1E1E22),   // windowBg
            rgb (0x1C1C1E),   // panelBg
            rgb (0x25252C),   // panelElevated
            rgb (0x1E1E22),   // centerBg
            rgb (0x2C2C2E),   // border
            rgb (0x252527),   // borderSubtle
            rgb (0xE0E0E0),   // textPrimary
            rgb (0x8E8E93),   // textSecondary
            rgb (0x8E8E93),   // textMuted
            accent,           // accent
            accent,           // accentSoft
            juce::Colours::white, // accentOnDark
            success,          // success
            success,          // padPlayingBorder — Emerald #30D158
            rgb (0xFF9F0A),   // warning
            rgb (0xFF453A),   // danger
            rgb (0x242426),   // rowHover
            rgb (0x2C2C2E),   // rowSelected
            rgb (0x1C1C1E),   // listRowBg
            rgb (0x1C1C1E),   // padGradientTop
            rgb (0x242426),   // padGradientBottom
            rgb (0x2C2C2E),   // padBorder — standby
            rgb (0x636366),   // waveformFill
            accent,           // waveformPlayhead
            rgb (0x2C2C2E),   // shortcutBadgeBg
            rgb (0xFFFFFF),   // shortcutBadgeText
            accent.withAlpha (0.45f), // dragTargetGlow
            rgb (0x2C2C2E),   // dragGhostFill
            rgb (0xFFFFFF),   // dragGhostText
            rgb (0xFF9F0A),   // trimMarkerStart
            rgb (0xFF6B35),   // trimMarkerEnd
            rgb (0x2C2C2E),   // buttonSecondary
            accent.withAlpha (0.28f), // sliderTrack
            accent,           // sliderThumb
            rgb (0x1C1C1E),   // inputBg
            rgb (0x2C2C2E),   // inputOutline
            rgb (0x2C2C2E)    // navButtonBg
        };
    }

    /** Light — nền trắng xám sạch, chữ/graphite đậm. */
    inline Palette lightPalette() noexcept
    {
        const auto accent = rgb (0x007AFF);
        const auto success = rgb (0x34C759);

        return {
            rgb (0xF0F2F5),   // windowBg
            rgb (0xFFFFFF),   // panelBg
            rgb (0xFFFFFF),   // panelElevated
            rgb (0xF0F2F5),   // centerBg
            rgb (0xE5E5EA),   // border
            rgb (0xEDEDF0),   // borderSubtle
            rgb (0x1C1C1E),   // textPrimary
            rgb (0x8E8E93),   // textSecondary
            rgb (0x8E8E93),   // textMuted
            accent,           // accent
            accent,           // accentSoft
            juce::Colours::white, // accentOnDark
            success,          // success
            success,          // padPlayingBorder — #34C759
            rgb (0xFF9500),   // warning
            rgb (0xFF3B30),   // danger
            rgb (0xF2F2F7),   // rowHover
            rgb (0xE5E5EA),   // rowSelected
            rgb (0xFFFFFF),   // listRowBg
            rgb (0xFFFFFF),   // padGradientTop
            rgb (0xF2F2F7),   // padGradientBottom
            rgb (0xE5E5EA),   // padBorder
            rgb (0xAEAEB2),   // waveformFill
            accent,           // waveformPlayhead
            rgb (0xE5E5EA),   // shortcutBadgeBg
            rgb (0x1C1C1E),   // shortcutBadgeText
            accent.withAlpha (0.35f), // dragTargetGlow
            rgb (0xF2F2F7),   // dragGhostFill
            rgb (0x1C1C1E),   // dragGhostText
            rgb (0xFF9500),   // trimMarkerStart
            rgb (0xFF6B35),   // trimMarkerEnd
            rgb (0xE5E5EA),   // buttonSecondary
            accent.withAlpha (0.22f), // sliderTrack
            accent,           // sliderThumb
            rgb (0xFFFFFF),   // inputBg
            rgb (0xC7C7CC),   // inputOutline
            rgb (0xE5E5EA)    // navButtonBg
        };
    }

    inline Palette get (bool isDark) noexcept
    {
        return isDark ? darkPalette() : lightPalette();
    }

    inline const juce::String& uiTypefaceName()
    {
        static const juce::String kName { "UIFont" };
        return kName;
    }

    inline const juce::String& timerTypefaceName()
    {
        static const juce::String kName { "TimerFont" };
        return kName;
    }

    inline bool isTimerFontRequest (const juce::Font& font) noexcept
    {
        const auto name = font.getTypefaceName();

        if (name == timerTypefaceName())
            return true;

        if (name == juce::Font::getDefaultMonospacedFontName())
            return true;

        return false;
    }

    inline juce::Font font (float heightPx, const juce::String& style = {})
    {
        auto opts = juce::FontOptions().withName (uiTypefaceName()).withHeight (heightPx);

        if (style.equalsIgnoreCase ("Bold") || style == "Plain")
            opts = opts.withStyle (style);

        return juce::Font (opts);
    }

    inline juce::Font fontBold (float heightPx)
    {
        return font (heightPx, "Bold");
    }

    /** Monospace tabular — đồng hồ MainDesk, Inspector, cột thời gian CUE/PAD. */
    inline juce::Font timerFont (float heightPx, bool bold = false)
    {
        auto opts = juce::FontOptions()
                        .withName (timerTypefaceName())
                        .withHeight (heightPx)
                        .withStyle (bold ? "Bold" : "Regular");

        return juce::Font (opts);
    }

    inline void applyTo (juce::Label& label, float heightPx, juce::Colour colour,
                         const juce::String& style = {})
    {
        label.setFont (style == "Bold" ? fontBold (heightPx) : font (heightPx, style));
        label.setColour (juce::Label::textColourId, colour);
    }
}

/** Cột BGM list — header (MainComponent) và dòng (SoundPad) dùng chung bounds. */
namespace showcontrol::bgmList
{
    inline constexpr int kTimeRemainingWidth      = 140;
    inline constexpr int kTotalDurationWidth      = 130;
    inline constexpr int kTimeRemainingRightOffset = 285;
    inline constexpr int kTotalDurationRightOffset  = 145;
    inline constexpr int kTitleLeftPad            = 20;
    inline constexpr int kTitleRightReserve       = 315;

    /** Cột số thứ tự + icon trạng thái + tên bài (BGM / CUE list). */
    inline constexpr int kIndexX                  = 10;
    inline constexpr int kIndexWidth              = 25;
    inline constexpr float kStatusIconX           = 38.0f;
    inline constexpr float kListIconSize          = 14.0f;
    inline constexpr int kNameStartWithStatusIcon = 60;
    inline constexpr int kNameStartDefault        = 38;
    inline constexpr float kLoopIconGapBeforeTime = 6.0f;

    inline juce::Rectangle<float> statusIconBounds (int rowHeight) noexcept
    {
        const float y = ((float) rowHeight - kListIconSize) * 0.5f;
        return { kStatusIconX, y, kListIconSize, kListIconSize };
    }

    inline juce::Rectangle<float> loopIconBounds (int panelWidth, int rowHeight) noexcept
    {
        const float x = (float) panelWidth - (float) kTimeRemainingRightOffset
                        - kListIconSize - kLoopIconGapBeforeTime;
        const float y = ((float) rowHeight - kListIconSize) * 0.5f;
        return { x, y, kListIconSize, kListIconSize };
    }

    inline juce::Rectangle<int> timeRemainingBounds (int panelWidth, int height) noexcept
    {
        return { panelWidth - kTimeRemainingRightOffset, 0, kTimeRemainingWidth, height };
    }

    inline juce::Rectangle<int> totalDurationBounds (int panelWidth, int height) noexcept
    {
        return { panelWidth - kTotalDurationRightOffset, 0, kTotalDurationWidth, height };
    }

    inline juce::Rectangle<int> titleBounds (int panelWidth, int height) noexcept
    {
        return { kTitleLeftPad, 0, juce::jmax (0, panelWidth - kTitleRightReserve), height };
    }

    inline int nameColumnMaxWidth (int panelWidth, int nameStartX) noexcept
    {
        return juce::jmax (0, panelWidth - kTimeRemainingRightOffset - nameStartX);
    }
}

/** Màu nền/chữ nút — đảm bảo tương phản khi hover (isHighlighted). */
namespace showcontrol::ui
{
    struct ButtonSurfaceColours
    {
        juce::Colour background;
        juce::Colour text;
    };

    inline ButtonSurfaceColours toggleButtonColours (bool isDark, bool toggled, bool highlighted) noexcept
    {
        const auto pal = ShowTheme::get (isDark);

        if (toggled)
        {
            const auto bg = highlighted ? pal.accent.brighter (isDark ? 0.12f : 0.08f) : pal.accent;
            return { bg, pal.accentOnDark };
        }

        if (highlighted)
        {
            const auto hoverBg = isDark ? ShowTheme::rgb (0x3A3A3C) : ShowTheme::rgb (0xD1D1D6);
            return { hoverBg, pal.textPrimary };
        }

        return { pal.buttonSecondary, pal.textPrimary };
    }

    inline ButtonSurfaceColours textButtonColours (bool isDark, bool highlighted, bool isDown = false) noexcept
    {
        const auto pal = ShowTheme::get (isDark);
        auto bg = pal.buttonSecondary;

        if (highlighted)
            bg = isDark ? ShowTheme::rgb (0x3A3A3C) : ShowTheme::rgb (0xD1D1D6);

        if (isDown)
            bg = bg.darker (0.14f);

        return { bg, pal.textPrimary };
    }

    inline juce::Colour comboBoxBackground (bool isDark, bool highlighted) noexcept
    {
        const auto pal = ShowTheme::get (isDark);
        if (! highlighted)
            return pal.inputBg;

        return isDark ? ShowTheme::rgb (0x3A3A3C) : ShowTheme::rgb (0xE5E5EA);
    }
}
