#pragma once

#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"

namespace showcontrol::colours
{

inline juce::Colour defaultTagColour() noexcept
{
    return juce::Colour (0xFF2F3542);
}

inline const std::array<juce::Colour, 9>& tagPalette() noexcept
{
    static const std::array<juce::Colour, 9> palette {
        juce::Colour (0xFF5E35B1), // 1 Tím Matte
        juce::Colour (0xFFD81B60), // 2 Hồng Rose
        juce::Colour (0xFFE53935), // 3 Đỏ Ruby
        juce::Colour (0xFFFB8C00), // 4 Cam Đất
        juce::Colour (0xFFFFB300), // 5 Vàng Hổ Phách
        juce::Colour (0xFF43A047), // 6 Xanh Lâm Nghiệp
        juce::Colour (0xFF00ACC1), // 7 Xanh Ngọc Mờ
        juce::Colour (0xFF1E88E5), // 8 Xanh Studio
        defaultTagColour()         // 9 Mặc định Xám Tối
    };
    return palette;
}

inline constexpr juce::uint32 kPadMatteBaseArgb = 0xFF1E222B;
inline constexpr juce::uint32 kPadLightSurfaceArgb = 0xFFFFFFFF;
inline constexpr juce::uint32 kPadDarkSurfaceArgb  = 0xFF21252E;

inline juce::Colour padSurfaceColour (bool isDark) noexcept
{
    return juce::Colour (isDark ? kPadDarkSurfaceArgb : kPadLightSurfaceArgb);
}

inline constexpr float kPadWaveformInkAlpha          = 0.45f;
inline constexpr float kInspectorWaveformInkAlpha    = 0.45f;
inline constexpr float kInspectorWaveformPlayedAlpha = 0.62f;

inline constexpr int kTagPaletteSize = 9;

inline juce::String colourToHexString (juce::Colour c)
{
    return juce::String::toHexString ((juce::uint32) c.getARGB()).toUpperCase();
}

inline juce::Colour colourFromHexString (const juce::String& hex)
{
    const auto trimmed = hex.trim();
    if (trimmed.isEmpty())
        return defaultTagColour();

    return juce::Colour ((juce::uint32) trimmed.getHexValue32());
}

inline bool coloursMatch (juce::Colour a, juce::Colour b) noexcept
{
    return a.getARGB() == b.getARGB();
}

inline bool isDefaultTagColour (juce::Colour c) noexcept
{
    const auto argb = c.getARGB();
    return argb == defaultTagColour().getARGB()
        || argb == (juce::uint32) 0xFF333333
        || argb == ShowTheme::darkPalette().textMuted.getARGB();
}

inline int indexOfTagColour (juce::Colour c) noexcept
{
    const auto& palette = tagPalette();
    const auto argb = c.getARGB();

    static const std::array<juce::uint32, kTagPaletteSize> legacyNeonArgb {{
        0xFF7F39FB, 0xFFFF00FF, 0xFFFF0000, 0xFFFF6600, 0xFFFF9900,
        0xFF00CC00, 0xFF00CCCC, 0xFF0066FF, 0xFF333333
    }};

    for (int i = 0; i < kTagPaletteSize; ++i)
    {
        if (coloursMatch (palette[(size_t) i], c) || argb == legacyNeonArgb[(size_t) i])
            return i;
    }

    return kTagPaletteSize - 1;
}

inline juce::Colour tagColourAt (int index) noexcept
{
    const auto& palette = tagPalette();
    return palette[(size_t) juce::jlimit (0, kTagPaletteSize - 1, index)];
}

inline juce::Colour snapToPalette (juce::Colour c) noexcept
{
    return tagColourAt (indexOfTagColour (c));
}

//==============================================================================
/** Cột 9 ô màu dọc — popup Inspector, không chữ (image_f9bb28). */
class ColorColumnMenuComponent final : public juce::PopupMenu::CustomComponent
{
public:
    static constexpr int kColumnWidth = 56;
    static constexpr int kRowHeight   = 32;
    static constexpr int kSwatchSize  = 22;

    ColorColumnMenuComponent (juce::Colour& colourRef, std::function<void (juce::Colour)> onSelect)
        : juce::PopupMenu::CustomComponent (false),
          currentColour (colourRef),
          callback (std::move (onSelect))
    {
        setSize (kColumnWidth, kTagPaletteSize * kRowHeight);
    }

    void getIdealSize (int& idealWidth, int& idealHeight) override
    {
        idealWidth  = kColumnWidth;
        idealHeight = kTagPaletteSize * kRowHeight;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::PopupMenu::backgroundColourId));

        const auto active = snapToPalette (currentColour);

        for (int i = 0; i < kTagPaletteSize; ++i)
        {
            const auto rowBounds = juce::Rectangle<float> (0.0f, (float) (i * kRowHeight),
                                                           (float) kColumnWidth, (float) kRowHeight);
            const auto colour = tagColourAt (i);
            const auto blockBounds = juce::Rectangle<float> (rowBounds.getX() + 24.0f,
                                                             rowBounds.getY() + 5.0f,
                                                             (float) kSwatchSize, (float) kSwatchSize);

            g.setColour (colour);
            g.fillRoundedRectangle (blockBounds, 4.0f);

            if (coloursMatch (active, colour))
            {
                g.setColour (getLookAndFeel().findColour (juce::PopupMenu::textColourId));
                g.setFont (ShowTheme::fontBold (14.0f));
                g.drawText (juce::String (juce::CharPointer_UTF8 ("✓")),
                            0, i * kRowHeight, 24, kRowHeight,
                            juce::Justification::centred);
            }
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;

        const int index = e.y / kRowHeight;

        if (index >= 0 && index < kTagPaletteSize)
        {
            const auto picked = tagColourAt (index);

            if (callback)
                callback (picked);

            currentColour = picked;

            juce::PopupMenu::dismissAllActiveMenus();
        }
    }

private:
    juce::Colour& currentColour;
    std::function<void (juce::Colour)> callback;
};

//==============================================================================
/** Dải 9 chấm tròn ngang — menu chuột phải PAD / CUE / BGM (image_fab764). */
class ColorRowMenuComponent final : public juce::PopupMenu::CustomComponent
{
public:
    ColorRowMenuComponent (juce::Colour& colourRef, std::function<void (juce::Colour)> onSelect)
        : juce::PopupMenu::CustomComponent (false),
          currentColour (colourRef),
          callback (std::move (onSelect))
    {
        setSize (240, 36);
    }

    void getIdealSize (int& idealWidth, int& idealHeight) override
    {
        idealWidth  = 240;
        idealHeight = 36;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::PopupMenu::backgroundColourId));

        const auto bounds = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        const float dotD  = juce::jmin (20.0f, bounds.getHeight());
        const float gap   = (bounds.getWidth() - dotD * (float) kTagPaletteSize)
                          / (float) juce::jmax (1, kTagPaletteSize - 1);
        const auto active = snapToPalette (currentColour);

        for (int i = 0; i < kTagPaletteSize; ++i)
        {
            const auto colour = tagColourAt (i);
            const float cx = bounds.getX() + (float) i * (dotD + gap) + dotD * 0.5f;
            const float cy = bounds.getCentreY();
            const bool selected = coloursMatch (active, colour);

            if (selected)
            {
                g.setColour (colour.brighter (0.4f));
                g.drawEllipse (cx - dotD * 0.62f, cy - dotD * 0.62f, dotD * 1.24f, dotD * 1.24f, 2.2f);
            }

            g.setColour (colour);
            g.fillEllipse (cx - dotD * 0.44f, cy - dotD * 0.44f, dotD * 0.88f, dotD * 0.88f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;

        const auto bounds = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        const float dotD  = juce::jmin (20.0f, bounds.getHeight());
        const float gap   = (bounds.getWidth() - dotD * (float) kTagPaletteSize)
                          / (float) juce::jmax (1, kTagPaletteSize - 1);

        for (int i = 0; i < kTagPaletteSize; ++i)
        {
            const float cx = bounds.getX() + (float) i * (dotD + gap) + dotD * 0.5f;
            const float cy = bounds.getCentreY();
            const juce::Rectangle<float> hit (cx - dotD * 0.58f, cy - dotD * 0.58f, dotD * 1.16f, dotD * 1.16f);

            if (hit.contains (e.position))
            {
                const auto picked = tagColourAt (i);
                currentColour = picked;

                if (callback)
                    callback (picked);

                juce::PopupMenu::dismissAllActiveMenus();
                return;
            }
        }
    }

private:
    juce::Colour& currentColour;
    std::function<void (juce::Colour)> callback;
};

inline std::unique_ptr<ColorRowMenuComponent> makeTagColourMenuRow (
    juce::Colour& colourRef,
    std::function<void (juce::Colour)> onColourPicked)
{
    return std::make_unique<ColorRowMenuComponent> (colourRef, std::move (onColourPicked));
}

//==============================================================================
/** Nút Color Inspector — swatch vuông + ▼, không chữ (image_f9ca0a). */
class InspectorColorButton final : public juce::TextButton
{
public:
    InspectorColorButton()
        : juce::TextButton (juce::String())
    {
        setButtonText ({});
        setClickingTogglesState (false);
        setConnectedEdges (0);
        onClick = [this] { showColourPopup(); };
    }

    std::function<void (juce::Colour)> onColourSelected;

    void setCurrentColour (juce::Colour c)
    {
        swatchColour = snapToPalette (c);
        repaint();
    }

    juce::Colour getCurrentColour() const noexcept { return swatchColour; }

    void setDarkMode (bool dark) noexcept
    {
        isDark = dark;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const auto& pal   = ShowTheme::get (isDark);

        g.setColour (pal.buttonSecondary);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (pal.border.withAlpha (0.65f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        const float chevronW = 10.0f;
        const float swatchSize = juce::jmin (bounds.getHeight() - 4.0f,
                                             bounds.getWidth() - chevronW - 6.0f);
        const auto swatch = juce::Rectangle<float> (bounds.getX() + 3.0f,
                                                    bounds.getCentreY() - swatchSize * 0.5f,
                                                    swatchSize, swatchSize);

        g.setColour (swatchColour);
        g.fillRoundedRectangle (swatch, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (swatch, 2.5f, 0.5f);

        g.setColour (pal.textSecondary);
        const float cx = bounds.getRight() - chevronW * 0.55f;
        const float cy = bounds.getCentreY();
        juce::Path arrow;
        arrow.startNewSubPath (cx - 3.0f, cy - 1.5f);
        arrow.lineTo (cx, cy + 2.5f);
        arrow.lineTo (cx + 3.0f, cy - 1.5f);
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
    }

private:
    juce::Colour swatchColour = defaultTagColour();
    bool isDark = true;

    void showColourPopup()
    {
        juce::PopupMenu menu;

        menu.addCustomItem (1,
                            std::make_unique<ColorColumnMenuComponent> (
                                swatchColour,
                                [this] (juce::Colour picked)
                                {
                                    swatchColour = picked;
                                    repaint();

                                    if (onColourSelected)
                                        onColourSelected (picked);
                                }),
                            nullptr,
                            juce::String::fromUTF8 (u8" "));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }
};

} // namespace showcontrol::colours
