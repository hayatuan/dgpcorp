#include "ShowControlLookAndFeel.h"

#if __has_include ("BinaryData.h")
 #include "BinaryData.h"
 #define SHOWCONTROL_HAS_BINARY_FONTS 1
#endif

namespace showcontrol::typography
{
    static bool isBoldRequest (const juce::Font& font) noexcept
    {
        return font.isBold()
            || font.getTypefaceStyle().containsIgnoreCase ("bold");
    }

    static juce::Typeface::Ptr loadFromMemory (const void* data, size_t size)
    {
        if (data == nullptr || size == 0)
            return nullptr;

        return juce::Typeface::createSystemTypefaceFor (data, size);
    }

    static juce::Typeface::Ptr loadSystemFamily (const juce::StringArray& families,
                                                 const juce::String& style)
    {
        const auto available = juce::Font::findAllTypefaceNames();

        for (const auto& family : families)
        {
            if (! available.contains (family, true))
                continue;

            const juce::Font probe (juce::FontOptions().withName (family).withStyle (style).withHeight (16.0f));
            auto face = juce::Typeface::createSystemTypefaceFor (probe);

            if (face != nullptr)
                return face;
        }

        return nullptr;
    }

    static juce::Typeface::Ptr& uiRegularCache()     { static juce::Typeface::Ptr p; return p; }
    static juce::Typeface::Ptr& uiBoldCache()        { static juce::Typeface::Ptr p; return p; }
    static juce::Typeface::Ptr& timerRegularCache()  { static juce::Typeface::Ptr p; return p; }
    static juce::Typeface::Ptr& timerBoldCache()     { static juce::Typeface::Ptr p; return p; }

    static void clearCaches()
    {
        uiRegularCache() = nullptr;
        uiBoldCache() = nullptr;
        timerRegularCache() = nullptr;
        timerBoldCache() = nullptr;
        juce::Typeface::clearTypefaceCache();
    }

    static juce::Typeface::Ptr resolveUiTypeface (bool bold)
    {
        auto& cache = bold ? uiBoldCache() : uiRegularCache();

        if (cache != nullptr)
            return cache;

        const juce::String style = bold ? "Bold" : "Regular";

    #if defined (SHOWCONTROL_HAS_BINARY_FONTS)
        #if defined (BinaryData_InterBold_ttf)
        if (bold)
            cache = loadFromMemory (BinaryData::InterBold_ttf, (size_t) BinaryData::InterBold_ttfSize);
        #endif
        #if defined (BinaryData_InterRegular_ttf)
        if (! bold || cache == nullptr)
            cache = loadFromMemory (BinaryData::InterRegular_ttf, (size_t) BinaryData::InterRegular_ttfSize);
        #endif
    #endif

        if (cache == nullptr)
        {
            cache = loadSystemFamily ({
                "Inter",
                "Inter Regular",
                "Roboto",
                "Roboto Regular",
                juce::Font::getSystemUIFontName(),
                "SF Pro Text",
                "Segoe UI",
                "Helvetica Neue",
                "Arial"
            }, style);
        }

        if (cache == nullptr)
        {
            const juce::Font fallback (juce::FontOptions()
                                           .withName (juce::Font::getDefaultSansSerifFontName())
                                           .withStyle (style)
                                           .withHeight (16.0f));
            cache = juce::Typeface::createSystemTypefaceFor (fallback);
        }

        return cache;
    }

    static juce::Typeface::Ptr resolveTimerTypeface (bool bold)
    {
        auto& cache = bold ? timerBoldCache() : timerRegularCache();

        if (cache != nullptr)
            return cache;

        const juce::String style = bold ? "Bold" : "Regular";

    #if defined (SHOWCONTROL_HAS_BINARY_FONTS)
        #if defined (BinaryData_JetBrainsMonoBold_ttf)
        if (bold)
            cache = loadFromMemory (BinaryData::JetBrainsMonoBold_ttf,
                                    (size_t) BinaryData::JetBrainsMonoBold_ttfSize);
        #endif
        #if defined (BinaryData_JetBrainsMonoRegular_ttf)
        if (! bold || cache == nullptr)
            cache = loadFromMemory (BinaryData::JetBrainsMonoRegular_ttf,
                                    (size_t) BinaryData::JetBrainsMonoRegular_ttfSize);
        #endif
    #endif

        if (cache == nullptr)
        {
            cache = loadSystemFamily ({
                "JetBrains Mono",
                "Roboto Mono",
                "SF Mono",
                "Menlo",
                "Consolas",
                "Courier New",
                juce::Font::getDefaultMonospacedFontName()
            }, style);
        }

        if (cache == nullptr)
        {
            const juce::Font fallback (juce::FontOptions()
                                           .withName (juce::Font::getDefaultMonospacedFontName())
                                           .withStyle (style)
                                           .withHeight (16.0f));
            cache = juce::Typeface::createSystemTypefaceFor (fallback);
        }

        return cache;
    }

    static void ensureInitialised()
    {
        resolveUiTypeface (false);
        resolveUiTypeface (true);
        resolveTimerTypeface (false);
        resolveTimerTypeface (true);
    }
}

//==============================================================================
ShowControlLookAndFeel::ShowControlLookAndFeel()
{
    showcontrol::typography::ensureInitialised();
    setDefaultSansSerifTypeface (showcontrol::typography::resolveUiTypeface (false));
    applyPalette (true);
}

ShowControlLookAndFeel::~ShowControlLookAndFeel() = default;

void ShowControlLookAndFeel::setDarkMode (bool dark)
{
    applyPalette (dark);
}

void ShowControlLookAndFeel::refreshTypography()
{
    showcontrol::typography::clearCaches();
    showcontrol::typography::ensureInitialised();
    setDefaultSansSerifTypeface (showcontrol::typography::resolveUiTypeface (false));
}

juce::Typeface::Ptr ShowControlLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (ShowTheme::isTimerFontRequest (font))
        return showcontrol::typography::resolveTimerTypeface (showcontrol::typography::isBoldRequest (font));

    if (font.getTypefaceName() == ShowTheme::uiTypefaceName()
        || font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return showcontrol::typography::resolveUiTypeface (showcontrol::typography::isBoldRequest (font));

    return LookAndFeel_V4::getTypefaceForFont (font);
}

void ShowControlLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& /*backgroundColour*/,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    const auto cols = showcontrol::ui::textButtonColours (currentIsDark,
                                                          shouldDrawButtonAsHighlighted,
                                                          shouldDrawButtonAsDown);
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto pal = ShowTheme::get (currentIsDark);

    g.setColour (cols.background);
    g.fillRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius);
    g.setColour (pal.inputOutline);
    g.drawRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius, 1.0f);
}

void ShowControlLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    const auto cols = showcontrol::ui::textButtonColours (currentIsDark,
                                                          shouldDrawButtonAsHighlighted,
                                                          shouldDrawButtonAsDown);
    g.setColour (cols.text.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (ShowTheme::fontBold (11.0f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void ShowControlLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                               bool shouldDrawButtonAsHighlighted,
                                               bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsDown);
    const auto bounds = button.getLocalBounds().toFloat();
    const auto cols = showcontrol::ui::toggleButtonColours (currentIsDark,
                                                            button.getToggleState(),
                                                            shouldDrawButtonAsHighlighted);
    const auto pal = ShowTheme::get (currentIsDark);

    g.setColour (cols.background);
    g.fillRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius);
    g.setColour (pal.inputOutline.withAlpha (0.85f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.0f);

    g.setColour (cols.text.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (ShowTheme::fontBold (11.0f));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred);
}

void ShowControlLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPos, float minSliderPos, float maxSliderPos,
                                               juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused (minSliderPos, maxSliderPos);

    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                          minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const bool isHoriz = (style == juce::Slider::LinearHorizontal);
    constexpr float trackH = 3.0f;
    constexpr float thumbD = 10.0f;
    const float thumbR = thumbD * 0.5f;

    const auto trackCol = slider.findColour (juce::Slider::trackColourId);
    const auto thumbCol = slider.findColour (juce::Slider::thumbColourId);

    // Scrub ẩn (transparentWhite + withAlpha) — không vẽ track ngang đè waveform.
    if (isHoriz && trackCol.getAlpha() == 0 && thumbCol.getAlpha() == 0)
        return;

    const auto fillCol  = thumbCol.isTransparent() ? juce::Colours::transparentBlack
                                                   : thumbCol.withAlpha (0.55f);

    juce::Rectangle<float> track;
    if (isHoriz)
    {
        track = { (float) x + thumbR, (float) y + (float) height * 0.5f - trackH * 0.5f,
                  (float) width - thumbD, trackH };
    }
    else
    {
        track = { (float) x + (float) width * 0.5f - trackH * 0.5f, (float) y + thumbR,
                  trackH, (float) height - thumbD };
    }

    g.setColour (trackCol);
    g.fillRoundedRectangle (track, trackH * 0.5f);

    if (isHoriz)
    {
        const float fillW = juce::jlimit (0.0f, track.getWidth(), sliderPos - track.getX());
        if (fillW > 0.5f)
        {
            g.setColour (fillCol);
            g.fillRoundedRectangle (track.withWidth (fillW), trackH * 0.5f);
        }

        const float thumbX = juce::jlimit (track.getX(), track.getRight() - thumbD, sliderPos - thumbR);
        g.setColour (thumbCol);
        g.fillEllipse (thumbX, track.getCentreY() - thumbR, thumbD, thumbD);
    }
    else
    {
        const float fillH = juce::jlimit (0.0f, track.getHeight(), track.getBottom() - sliderPos);
        if (fillH > 0.5f)
        {
            g.setColour (fillCol);
            g.fillRoundedRectangle (track.withTop (sliderPos).withHeight (fillH), trackH * 0.5f);
        }

        const float thumbY = juce::jlimit (track.getY(), track.getBottom() - thumbD, sliderPos - thumbR);
        g.setColour (thumbCol);
        g.fillEllipse (track.getCentreX() - thumbR, thumbY, thumbD, thumbD);
    }
}

void ShowControlLookAndFeel::applyPalette (bool dark)
{
    currentIsDark = dark;
    const auto pal   = ShowTheme::get (dark);
    const auto onAcc = dark ? juce::Colours::white : pal.accentOnDark;

    setColour (appBackgroundColourId,      pal.windowBg);
    setColour (panelBackgroundColourId,    pal.panelBg);
    setColour (panelBorderColourId,        pal.border);
    setColour (textPrimaryColourId,        pal.textPrimary);
    setColour (textSecondaryColourId,      pal.textSecondary);
    setColour (accentColourId,             pal.accent);
    setColour (successColourId,            pal.success);
    setColour (padBorderColourId,          pal.padBorder);
    setColour (padPlayingBorderColourId,   pal.padPlayingBorder);
    setColour (sliderTrackColourId,        pal.sliderTrack);
    setColour (sliderThumbColourId,        pal.sliderThumb);

    setColour (juce::PopupMenu::backgroundColourId,            pal.panelElevated);
    setColour (juce::PopupMenu::textColourId,                  pal.textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, dark ? pal.rowSelected : pal.accent.withAlpha (0.14f));
    setColour (juce::PopupMenu::highlightedTextColourId,       pal.textPrimary);
    setColour (juce::PopupMenu::headerTextColourId,            pal.textSecondary);

    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId,      pal.textSecondary);
    setColour (juce::ScrollBar::trackColourId,      juce::Colours::transparentBlack);

    setColour (juce::AlertWindow::backgroundColourId, pal.panelElevated);
    setColour (juce::AlertWindow::textColourId,       pal.textPrimary);
    setColour (juce::AlertWindow::outlineColourId,    pal.border);

    setColour (juce::TextEditor::backgroundColourId,      pal.inputBg);
    setColour (juce::TextEditor::textColourId,            pal.textPrimary);
    setColour (juce::TextEditor::outlineColourId,         pal.inputOutline);
    setColour (juce::TextEditor::focusedOutlineColourId,  pal.accent);
    setColour (juce::TextEditor::highlightColourId,       pal.accentSoft.withAlpha (0.28f));
    setColour (juce::TextEditor::highlightedTextColourId, pal.textPrimary);

    setColour (juce::CaretComponent::caretColourId, pal.accent);

    setColour (juce::TextButton::buttonColourId,    pal.buttonSecondary);
    setColour (juce::TextButton::textColourOffId,   pal.textPrimary);
    setColour (juce::TextButton::buttonOnColourId,  pal.accentSoft);
    setColour (juce::TextButton::textColourOnId,    onAcc);

    setColour (juce::ToggleButton::textColourId,        pal.textPrimary);
    setColour (juce::ToggleButton::tickColourId,        pal.accent);
    setColour (juce::ToggleButton::tickDisabledColourId, pal.textMuted);

    setColour (juce::Label::textColourId,                  pal.textPrimary);
    setColour (juce::Label::backgroundColourId,            juce::Colours::transparentBlack);
    setColour (juce::Label::textWhenEditingColourId,       pal.textPrimary);
    setColour (juce::Label::backgroundWhenEditingColourId, pal.panelElevated);
    setColour (juce::Label::outlineWhenEditingColourId,    pal.accent);

    setColour (juce::ComboBox::backgroundColourId,    pal.panelElevated);
    setColour (juce::ComboBox::textColourId,          pal.textPrimary);
    setColour (juce::ComboBox::outlineColourId,       pal.border);
    setColour (juce::ComboBox::arrowColourId,         pal.textSecondary);
    setColour (juce::ComboBox::focusedOutlineColourId, pal.accent);
    setColour (juce::ComboBox::buttonColourId,        pal.panelElevated);

    setColour (juce::Slider::backgroundColourId,         pal.panelElevated);
    setColour (juce::Slider::thumbColourId,              pal.sliderThumb);
    setColour (juce::Slider::trackColourId,              pal.sliderTrack);
    setColour (juce::Slider::rotarySliderFillColourId,   pal.accentSoft);
    setColour (juce::Slider::rotarySliderOutlineColourId, pal.border);
    setColour (juce::Slider::textBoxTextColourId,        pal.textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId,  pal.inputBg);
    setColour (juce::Slider::textBoxHighlightColourId,   pal.rowSelected);
    setColour (juce::Slider::textBoxOutlineColourId,     pal.inputOutline);

    setColour (juce::ResizableWindow::backgroundColourId, pal.windowBg);
    setColour (juce::DocumentWindow::backgroundColourId,  pal.windowBg);

    setColour (juce::ListBox::backgroundColourId, pal.listRowBg);
    setColour (juce::ListBox::textColourId,       pal.textPrimary);
    setColour (juce::ListBox::outlineColourId,    pal.border);

    if (dark)
    {
        setColour (juce::TooltipWindow::backgroundColourId, ShowTheme::rgb (0x2C2C2E));
        setColour (juce::TooltipWindow::textColourId,       ShowTheme::rgb (0xFFFFFF));
        setColour (juce::TooltipWindow::outlineColourId,    ShowTheme::rgb (0x3A3A3C));
    }
    else
    {
        setColour (juce::TooltipWindow::backgroundColourId, ShowTheme::rgb (0xFFFFFF));
        setColour (juce::TooltipWindow::textColourId,       ShowTheme::rgb (0x1C1C1E));
        setColour (juce::TooltipWindow::outlineColourId,    ShowTheme::rgb (0xC7C7CC));
    }

    MasterDeckComponent::applyColoursTo (*this, dark);
}
