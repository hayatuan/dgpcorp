#include "ShowControlLookAndFeel.h"
#include <BinaryData.h>

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

    static juce::Typeface::Ptr& timerRegularCache()  { static juce::Typeface::Ptr p; return p; }
    static juce::Typeface::Ptr& timerBoldCache()     { static juce::Typeface::Ptr p; return p; }

    static void clearTimerCaches()
    {
        timerRegularCache() = nullptr;
        timerBoldCache() = nullptr;
    }

    static juce::Typeface::Ptr resolveTimerTypeface (bool bold)
    {
        auto& cache = bold ? timerBoldCache() : timerRegularCache();

        if (cache != nullptr)
            return cache;

        const juce::String style = bold ? "Bold" : "Regular";

        cache = loadSystemFamily ({
            "JetBrains Mono",
            "Roboto Mono",
            "SF Mono",
            "Menlo",
            "Consolas",
            "Courier New",
            juce::Font::getDefaultMonospacedFontName()
        }, style);

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

    static void ensureTimerTypefaces()
    {
        resolveTimerTypeface (false);
        resolveTimerTypeface (true);
    }
}

//==============================================================================
void ShowControlLookAndFeel::loadRobotoTypefaces()
{
    robotoRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::RobotoRegular_ttf,
                                                             BinaryData::RobotoRegular_ttfSize);
    robotoBold    = juce::Typeface::createSystemTypefaceFor (BinaryData::RobotoBold_ttf,
                                                             BinaryData::RobotoBold_ttfSize);
}

ShowControlLookAndFeel::ShowControlLookAndFeel()
{
    loadRobotoTypefaces();
    showcontrol::typography::ensureTimerTypefaces();
    setDefaultSansSerifTypefaceName ("Roboto");
    applyPalette (true);
}

ShowControlLookAndFeel::~ShowControlLookAndFeel()
{
    robotoRegular = nullptr;
    robotoBold = nullptr;
}

void ShowControlLookAndFeel::setDarkMode (bool dark)
{
    applyPalette (dark);
}

void ShowControlLookAndFeel::refreshTypography()
{
    robotoRegular = nullptr;
    robotoBold = nullptr;
    showcontrol::typography::clearTimerCaches();
    juce::Typeface::clearTypefaceCache();
    loadRobotoTypefaces();
    showcontrol::typography::ensureTimerTypefaces();
    setDefaultSansSerifTypefaceName ("Roboto");
}

void ShowControlLookAndFeel::shutdownTypographyCaches() noexcept
{
    showcontrol::typography::clearTimerCaches();
    juce::Typeface::clearTypefaceCache();
}

juce::Typeface::Ptr ShowControlLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (ShowTheme::isTimerFontRequest (font))
        return showcontrol::typography::resolveTimerTypeface (showcontrol::typography::isBoldRequest (font));

    if (showcontrol::typography::isBoldRequest (font))
    {
        if (robotoBold != nullptr)
            return robotoBold;
    }

    if (robotoRegular != nullptr)
        return robotoRegular;

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

juce::Font ShowControlLookAndFeel::getTabButtonFont (juce::TabBarButton&, float height)
{
    juce::ignoreUnused (height);
    return showcontrol::preferences::tabLabelFont();
}

juce::Font ShowControlLookAndFeel::getPopupMenuFont()
{
    return showcontrol::ui::popupMenuFont();
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

namespace showcontrol::inlineEditor
{
    constexpr float kCornerRadius = 4.5f;
    const juce::Colour kBrandFocusBlue { 0xFF007FFF };

    bool isInlineListNameEditor (const juce::TextEditor& te) noexcept
    {
        return te.getComponentID() == "showcue-inline-list-name-editor";
    }
}

void ShowControlLookAndFeel::applyTrackNameLabelStyle (juce::Label& label, bool isDark,
                                                      bool isOnSelectedRow) noexcept
{
    const auto pal = ShowTheme::get (isDark);
    const auto bg = isDark ? (isOnSelectedRow ? pal.rowSelected : pal.panelBg) : pal.panelElevated;

    label.setFont (showcontrol::bgmList::playlistTrackNameFont());
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, pal.textPrimary);
    label.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setColour (juce::Label::backgroundWhenEditingColourId, bg);
    label.setColour (juce::Label::outlineWhenEditingColourId, showcontrol::inlineEditor::kBrandFocusBlue);
    label.setColour (juce::Label::textWhenEditingColourId, pal.textPrimary);
}

void ShowControlLookAndFeel::applyInlineListNameEditorStyle (juce::TextEditor& editor, bool isDark,
                                                             bool isOnSelectedRow) noexcept
{
    const auto pal = ShowTheme::get (isDark);
    const auto bg = isDark ? (isOnSelectedRow ? pal.rowSelected : pal.panelBg) : pal.panelElevated;

    editor.setColour (juce::TextEditor::backgroundColourId,      bg);
    editor.setColour (juce::TextEditor::focusedOutlineColourId,  showcontrol::inlineEditor::kBrandFocusBlue);
    editor.setColour (juce::TextEditor::outlineColourId,         juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::textColourId,             pal.textPrimary);
    editor.setColour (juce::TextEditor::highlightColourId,       showcontrol::inlineEditor::kBrandFocusBlue.withAlpha (0.4f));
    editor.setColour (juce::TextEditor::highlightedTextColourId, pal.textPrimary);
    editor.setColour (juce::CaretComponent::caretColourId,       showcontrol::inlineEditor::kBrandFocusBlue);

    editor.setFont (showcontrol::bgmList::playlistTrackNameFont());
    editor.setJustification (juce::Justification::centredLeft);
    editor.setIndents (6, 0);
    editor.setBorder (juce::BorderSize<int> (5, 6, 5, 6));
}

void ShowControlLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                       juce::TextEditor& textEditor)
{
    if (showcontrol::inlineEditor::isInlineListNameEditor (textEditor))
    {
        g.setColour (textEditor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height,
                                showcontrol::inlineEditor::kCornerRadius);
        return;
    }

    LookAndFeel_V4::fillTextEditorBackground (g, width, height, textEditor);
}

void ShowControlLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                                    juce::TextEditor& textEditor)
{
    if (showcontrol::inlineEditor::isInlineListNameEditor (textEditor))
    {
        if (! textEditor.isEnabled())
            return;

        if (textEditor.hasKeyboardFocus (true) && ! textEditor.isReadOnly())
        {
            g.setColour (textEditor.findColour (juce::TextEditor::focusedOutlineColourId));
            g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f,
                                    showcontrol::inlineEditor::kCornerRadius, 1.5f);
        }

        return;
    }

    LookAndFeel_V4::drawTextEditorOutline (g, width, height, textEditor);
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
    setColour (juce::TextEditor::outlineColourId,         juce::Colours::transparentBlack);
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

    setColour (juce::TabbedComponent::backgroundColourId, pal.windowBg);
    setColour (juce::TabbedComponent::outlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::tabTextColourId, pal.textSecondary);
    setColour (juce::TabbedButtonBar::frontTextColourId, pal.textPrimary);

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
