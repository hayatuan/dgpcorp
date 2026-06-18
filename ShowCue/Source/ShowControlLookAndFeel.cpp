#include "ShowControlLookAndFeel.h"

//==============================================================================
ShowControlLookAndFeel::ShowControlLookAndFeel()
{
    showcontrol::typography::ensureLoaded();
    setDefaultSansSerifTypefaceName (ShowTheme::uiTypefaceName());
    applyPalette (true);
}

ShowControlLookAndFeel::~ShowControlLookAndFeel()
{
}

void ShowControlLookAndFeel::setDarkMode (bool dark)
{
    applyPalette (dark);
}

void ShowControlLookAndFeel::refreshTypography()
{
    showcontrol::typography::reload();
    setDefaultSansSerifTypefaceName (ShowTheme::uiTypefaceName());
}

void ShowControlLookAndFeel::shutdownTypographyCaches() noexcept
{
    showcontrol::typography::shutdown();
}

juce::Typeface::Ptr ShowControlLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (auto face = showcontrol::typography::resolveForLookAndFeel (font))
        return face;

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
    g.setFont (showcontrol::ui::globalButtonFont());
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

juce::Font ShowControlLookAndFeel::getLabelFont (juce::Label& label)
{
    const auto font = label.getFont();

    // Tôn trọng setFont() trên đồng hồ / readout lớn — trước đây luôn 14pt nên timer không to lên được.
    if (font.getHeight() > 15.5f || ShowTheme::isTimerFontRequest (font))
        return font;

    return ShowTheme::font (14.0f);
}

void ShowControlLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto bg = label.findColour (juce::Label::backgroundColourId);

    if (bg.isOpaque())
        g.fillAll (bg);

    if (! label.isBeingEdited())
    {
        const float alpha = label.isEnabled() ? 1.0f : 0.5f;
        const juce::Font font (getLabelFont (label));

        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);

        const auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        const int maxLines = font.getHeight() >= 18.0f
                                 ? 1
                                 : juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight()));

        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          maxLines, label.getMinimumHorizontalScale());
    }
}

juce::Font ShowControlLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return ShowTheme::font (14.0f);
}

juce::Font ShowControlLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    juce::ignoreUnused (buttonHeight);
    return showcontrol::ui::globalButtonFont();
}

juce::Font ShowControlLookAndFeel::getAlertWindowTitleFont()
{
    return ShowTheme::fontBold (18.0f);
}

juce::Font ShowControlLookAndFeel::getAlertWindowMessageFont()
{
    return ShowTheme::font (14.0f);
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
    g.setFont (showcontrol::ui::globalButtonFont());
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

void ShowControlLookAndFeel::drawMenuBarBackground (juce::Graphics& g, int width, int height,
                                                    bool, juce::MenuBarComponent&)
{
    const auto pal = ShowTheme::get (currentIsDark);
    g.fillAll (pal.windowBg);
    g.setColour (pal.borderSubtle);
    g.fillRect (0, height - 1, width, 1);
}

void ShowControlLookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height,
                                              int itemIndex, const juce::String& itemText,
                                              bool isMouseOverItem, bool isMenuOpen,
                                              bool, juce::MenuBarComponent& menuBar)
{
    const auto pal = ShowTheme::get (currentIsDark);

    if (! menuBar.isEnabled())
    {
        g.setColour (pal.textPrimary.withMultipliedAlpha (0.5f));
    }
    else if (isMenuOpen || isMouseOverItem)
    {
        g.setColour (currentIsDark ? pal.rowSelected : pal.accent.withAlpha (0.14f));
        g.fillRect (0, 0, width, height);
        g.setColour (pal.textPrimary);
    }
    else
    {
        g.setColour (pal.textPrimary);
    }

    g.setFont (getMenuBarFont (menuBar, itemIndex, itemText));
    g.drawFittedText (itemText, 0, 0, width, height, juce::Justification::centred, 1);
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
