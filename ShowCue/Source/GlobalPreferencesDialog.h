#pragma once
#include <functional>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioDeviceSettingsPanel.h"
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowControlMacWindow.h"
#include "ShowFlatIcons.h"

namespace showcontrol::preferences
{
    inline juce::Colour cardSelectionBorder() noexcept { return juce::Colour (0xff9b51e0); }

    inline juce::Colour dynamicDialogBackground (const juce::LookAndFeel& laf) noexcept
    {
        return laf.findColour (juce::ResizableWindow::backgroundColourId);
    }

    inline juce::Colour dynamicPrimaryText (const juce::LookAndFeel& laf) noexcept
    {
        return laf.findColour (juce::Label::textColourId);
    }

    inline juce::Colour dynamicMutedText (const juce::LookAndFeel& laf) noexcept
    {
        return laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);
    }

    inline juce::Colour dynamicSeparator (const juce::LookAndFeel& laf) noexcept
    {
        return dynamicPrimaryText (laf).withAlpha (0.12f);
    }

    inline juce::Colour dynamicTabActiveFill (const juce::LookAndFeel& laf) noexcept
    {
        return dynamicPrimaryText (laf).withAlpha (0.10f);
    }

    inline juce::Colour dynamicCardSurface (const juce::LookAndFeel& laf) noexcept
    {
        return laf.findColour (ShowControlLookAndFeel::panelBackgroundColourId).brighter (0.06f);
    }

    inline void drawMacTrafficLights (juce::Graphics& g, float x, float y, float dotR) noexcept
    {
        const float step = dotR * 2.35f;
        g.setColour (juce::Colour (0xffff5f57)); g.fillEllipse (x,              y, dotR * 2.0f, dotR * 2.0f);
        g.setColour (juce::Colour (0xfffebc2e)); g.fillEllipse (x + step,       y, dotR * 2.0f, dotR * 2.0f);
        g.setColour (juce::Colour (0xff28c840)); g.fillEllipse (x + step * 2.0f, y, dotR * 2.0f, dotR * 2.0f);
    }

    inline void drawFlatGear (juce::Graphics& g,
                              juce::Rectangle<float> area,
                              juce::Colour colour,
                              juce::Colour innerHole) noexcept
    {
        const float r  = juce::jmin (area.getWidth(), area.getHeight()) * 0.34f;
        const float cx = area.getCentreX();
        const float cy = area.getCentreY();

        g.setColour (colour);

        for (int i = 0; i < 8; ++i)
        {
            const float angle = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
            juce::Rectangle<float> tooth (cx - r * 0.16f, cy - r * 1.05f, r * 0.32f, r * 0.34f);
            g.saveState();
            g.addTransform (juce::AffineTransform::rotation (angle, cx, cy));
            g.fillRoundedRectangle (tooth, 1.0f);
            g.restoreState();
        }

        g.fillEllipse (cx - r * 0.42f, cy - r * 0.42f, r * 0.84f, r * 0.84f);
        g.setColour (innerHole);
        g.fillEllipse (cx - r * 0.18f, cy - r * 0.18f, r * 0.36f, r * 0.36f);
    }

    inline void drawMiniWindow (juce::Graphics& g,
                                juce::Rectangle<float> bounds,
                                juce::Colour frame,
                                juce::Colour titleBar,
                                juce::Colour content) noexcept
    {
        g.setColour (frame);
        g.fillRoundedRectangle (bounds, 5.0f);

        auto title = bounds.removeFromTop (bounds.getHeight() * 0.28f);
        g.setColour (titleBar);
        g.fillRoundedRectangle (title.getX(), title.getY(), title.getWidth(), title.getHeight() + 2.0f, 5.0f);

        drawMacTrafficLights (g, title.getX() + 5.0f, title.getCentreY() - 2.5f, 2.2f);

        g.setColour (content);
        g.fillRoundedRectangle (bounds.reduced (3.0f, 2.0f), 3.0f);
    }

    enum class TabIcon { audio, appearance };

    inline void drawTabIcon (juce::Graphics& g, juce::Rectangle<float> area, TabIcon icon, juce::Colour colour) noexcept
    {
        const auto iconBounds = showcontrol::icons::centredIconIn (area, showcontrol::icons::kTabIconSize);

        if (icon == TabIcon::audio)
            showcontrol::icons::paintHeadphonesIcon (g, iconBounds, colour);
        else
            showcontrol::icons::paintLayoutIcon (g, iconBounds, colour);
    }
} // namespace showcontrol::preferences

//==============================================================================
/** Nút Tab phẳng — icon vector phía trên, nhãn phía dưới (Farrago style). */
class PreferencesTabButton : public juce::Component
{
public:
    std::function<void()> onSelected;

    PreferencesTabButton (showcontrol::preferences::TabIcon iconType, juce::String labelText)
        : tabIcon (iconType), label (std::move (labelText))
    {
        setInterceptsMouseClicks (true, false);
    }

    void setTabActive (bool active) noexcept
    {
        if (tabActive == active)
            return;

        tabActive = active;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& laf = getLookAndFeel();
        const auto textCol = showcontrol::preferences::dynamicPrimaryText (laf);
        auto bounds = getLocalBounds().toFloat().reduced (4.0f, 3.0f);

        if (tabActive)
        {
            g.setColour (showcontrol::preferences::dynamicTabActiveFill (laf));
            g.fillRoundedRectangle (bounds, 8.0f);
        }

        const auto iconCol = tabActive ? textCol : textCol.withAlpha (0.55f);

        auto iconArea = bounds.removeFromTop (bounds.getHeight() * 0.62f).reduced (bounds.getWidth() * 0.22f, 2.0f);
        showcontrol::preferences::drawTabIcon (g, iconArea, tabIcon, iconCol);

        g.setColour (iconCol);
        g.setFont (ShowTheme::font (11.0f));
        g.drawText (label, bounds.toNearestInt(), juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onSelected != nullptr)
            onSelected();
    }

private:
    showcontrol::preferences::TabIcon tabIcon;
    juce::String label;
    bool tabActive = false;
};

//==============================================================================
/** Card chọn theme — mini preview + viền tím khi active. */
class ThemeAppearanceCard : public juce::Button
{
public:
    enum class Kind { matchSystem, light, dark };

    ThemeAppearanceCard (Kind cardKind, juce::String cardLabel)
        : juce::Button (cardLabel),
          kind (cardKind)
    {
        setClickingTogglesState (true);
        setRadioGroupId (2001);
    }

    void paintButton (juce::Graphics& g, bool /*highlighted*/, bool /*down*/) override
    {
        const auto& laf = getLookAndFeel();
        const auto cardBg = showcontrol::preferences::dynamicCardSurface (laf);
        const auto labelCol = showcontrol::preferences::dynamicPrimaryText (laf);
        const auto mutedCol = showcontrol::preferences::dynamicMutedText (laf);

        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        const bool selected = getToggleState();

        g.setColour (cardBg);
        g.fillRoundedRectangle (bounds, 8.0f);

        auto preview = bounds.reduced (10.0f, 12.0f);
        preview.removeFromBottom (preview.getHeight() * 0.18f);

        switch (kind)
        {
            case Kind::matchSystem:
                showcontrol::preferences::drawFlatGear (g, preview,
                    labelCol.withAlpha (0.78f), cardBg);
                break;

            case Kind::light:
                showcontrol::preferences::drawMiniWindow (g, preview,
                    juce::Colour (0xffd8d8dc), juce::Colour (0xffececf0), juce::Colours::white);
                break;

            case Kind::dark:
                showcontrol::preferences::drawMiniWindow (g, preview,
                    juce::Colour (0xff3a3a40), juce::Colour (0xff2c2c32), juce::Colour (0xff1a1a1e));
                break;
        }

        // Farrago-style selection ring — tím đậm 2.5px khi Card được chọn
        if (selected)
        {
            g.setColour (showcontrol::preferences::cardSelectionBorder());
            g.drawRoundedRectangle (bounds.reduced (1.25f), 8.0f, 2.5f);
        }

        g.setColour (selected ? labelCol : mutedCol);
        g.setFont (ShowTheme::font (10.5f));
        g.drawText (getButtonText(), bounds.removeFromBottom (16.0f).toNearestInt(),
                    juce::Justification::centred);
    }

private:
    Kind kind;
};

//==============================================================================
/** Tab Giao diện — 3 Card Appearance nằm ngang. */
class ThemePreferencesPanel : public juce::Component
{
public:
    std::function<void (int themeId)> onThemeChanged;

    explicit ThemePreferencesPanel (int themeId)
    {
        sectionLabel.setText (juce::String::fromUTF8 (u8"Chế độ hiển thị"), juce::dontSendNotification);
        sectionLabel.setFont (ShowTheme::fontBold (13.0f));
        sectionLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (sectionLabel);

        hintLabel.setText (juce::String::fromUTF8 (
            u8"Chọn giao diện cho toàn bộ ứng dụng. Thay đổi có hiệu lực ngay lập tức."),
            juce::dontSendNotification);
        hintLabel.setFont (ShowTheme::font (11.0f));
        hintLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (hintLabel);

        addAndMakeVisible (systemCard);
        addAndMakeVisible (lightCard);
        addAndMakeVisible (darkCard);

        systemCard.setButtonText ("Match System");
        lightCard.setButtonText ("Light");
        darkCard.setButtonText ("Dark");

        auto pickTheme = [this] (int id, ThemeAppearanceCard& card)
        {
            if (! card.getToggleState())
                return;

            applyThemeChoice (id);
        };

        systemCard.onClick = [this, pickTheme] { pickTheme (3, systemCard); };
        lightCard.onClick  = [this, pickTheme] { pickTheme (2, lightCard); };
        darkCard.onClick   = [this, pickTheme] { pickTheme (1, darkCard); };

        syncLabelColours();
        setThemeSelection (themeId);
    }

    void refreshSectionLabelColours()
    {
        syncLabelColours();
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        syncLabelColours();
        repaint();
        systemCard.repaint();
        lightCard.repaint();
        darkCard.repaint();
    }

    void setThemeSelection (int themeId)
    {
        currentThemeId = juce::jlimit (1, 3, themeId);
        systemCard.setToggleState (currentThemeId == 3, juce::dontSendNotification);
        lightCard.setToggleState  (currentThemeId == 2, juce::dontSendNotification);
        darkCard.setToggleState   (currentThemeId == 1, juce::dontSendNotification);
        systemCard.repaint();
        lightCard.repaint();
        darkCard.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (showcontrol::preferences::dynamicDialogBackground (getLookAndFeel()));
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (22, 18);
        sectionLabel.setBounds (bounds.removeFromTop (22));
        bounds.removeFromTop (6);
        hintLabel.setBounds (bounds.removeFromTop (34));
        bounds.removeFromTop (18);

        const int cardW = 88;
        const int cardH = 78;
        const int gap   = 16;
        const int totalW = cardW * 3 + gap * 2;
        auto row = bounds.removeFromTop (cardH);
        row = row.withSizeKeepingCentre (totalW, cardH);

        systemCard.setBounds (row.removeFromLeft (cardW));
        row.removeFromLeft (gap);
        lightCard.setBounds (row.removeFromLeft (cardW));
        row.removeFromLeft (gap);
        darkCard.setBounds (row.removeFromLeft (cardW));
    }

private:
    int currentThemeId = 1;
    juce::Label sectionLabel, hintLabel;
    ThemeAppearanceCard systemCard { ThemeAppearanceCard::Kind::matchSystem, {} };
    ThemeAppearanceCard lightCard  { ThemeAppearanceCard::Kind::light, {} };
    ThemeAppearanceCard darkCard   { ThemeAppearanceCard::Kind::dark, {} };

    void syncLabelColours()
    {
        const auto& laf = getLookAndFeel();
        sectionLabel.setColour (juce::Label::textColourId,
                                showcontrol::preferences::dynamicPrimaryText (laf));
        hintLabel.setColour (juce::Label::textColourId,
                             showcontrol::preferences::dynamicMutedText (laf));
    }

    void applyThemeChoice (int themeId)
    {
        if (currentThemeId == themeId)
            return;

        currentThemeId = themeId;

        if (onThemeChanged != nullptr)
            onThemeChanged (themeId);
    }
};

//==============================================================================
/** Hộp thoại Cài đặt — Tab bar icon Farrago + nội dung phẳng. */
class GlobalPreferencesDialog : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void (int busIndex, const juce::String& text)> onBusNameLiveChanged;
        std::function<void (const AudioDeviceSettingsPanel::ApplyResult&)> onAudioSettingsApplied;
        std::function<void (int themeId)> onThemeChanged;
    };

    GlobalPreferencesDialog (juce::AudioDeviceManager& deviceManager,
                             bool darkMode,
                             const juce::StringArray& busNames,
                             int themeId,
                             Callbacks callbacks)
        : cb (std::move (callbacks))
    {
        audioPanel = std::make_unique<AudioDeviceSettingsPanel> (deviceManager, darkMode, busNames, true);
        themePanel = std::make_unique<ThemePreferencesPanel> (themeId);

        if (cb.onBusNameLiveChanged != nullptr)
            audioPanel->setOnBusNameLiveChanged (cb.onBusNameLiveChanged);

        audioPanel->onApplied = [this] (const AudioDeviceSettingsPanel::ApplyResult& result)
        {
            if (cb.onAudioSettingsApplied != nullptr)
                cb.onAudioSettingsApplied (result);
        };

        themePanel->onThemeChanged = [this] (int newThemeId)
        {
            if (cb.onThemeChanged != nullptr)
                cb.onThemeChanged (newThemeId);

            // ÉP MÀU TRỰC TIẾP xuống ô Bus 0–5 ngay khi buông chuột chọn Card Theme
            pushThemeColoursToEmbeddedPanels();
        };

        addAndMakeVisible (audioPanel.get());
        addAndMakeVisible (themePanel.get());

        audioTabBtn = std::make_unique<PreferencesTabButton> (
            showcontrol::preferences::TabIcon::audio,
            juce::String::fromUTF8 (u8"Âm thanh"));

        appearanceTabBtn = std::make_unique<PreferencesTabButton> (
            showcontrol::preferences::TabIcon::appearance,
            juce::String::fromUTF8 (u8"Giao diện"));

        addAndMakeVisible (*audioTabBtn);
        addAndMakeVisible (*appearanceTabBtn);

        audioTabBtn->onSelected = [this] { selectTab (0); };
        appearanceTabBtn->onSelected = [this] { selectTab (1); };

        selectTab (0);
        setWantsKeyboardFocus (true);
        setSize (680, 560);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);

            return true;
        }

        return false;
    }

    void setInitialTabIndex (int tabIndex) noexcept
    {
        selectTab (juce::jlimit (0, 1, tabIndex));
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        pushThemeColoursToEmbeddedPanels();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bgColor = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
        g.fillAll (bgColor);

        const int sepY = kMacTitleDragInset + kTabBarHeight;
        g.setColour (showcontrol::preferences::dynamicSeparator (getLookAndFeel()));
        g.fillRect (0, sepY, getWidth(), 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        constexpr int kTabBarDragHeight = 65;

        if (e.y < kTabBarDragHeight && e.mods.isLeftButtonDown())
        {
            if (! isClickOnTabButtonArea (e))
            {
                if (auto* topLevel = getTopLevelComponent())
                {
                    if (auto* peer = topLevel->getPeer())
                    {
                       #if JUCE_MAC
                        if (showcontrol::mac::startDraggingWindow (*peer, e))
                            return;
                       #endif

                        windowDragger.startDraggingComponent (topLevel, e.getEventRelativeTo (topLevel));
                        windowDragActive = true;
                        return;
                    }
                }
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! windowDragActive)
            return;

        if (auto* topLevel = getTopLevelComponent())
            windowDragger.dragComponent (topLevel, e.getEventRelativeTo (topLevel), nullptr);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);
        windowDragActive = false;
    }

    void resized() override
    {
        auto bounds = getLocalBounds();

        if (kMacTitleDragInset > 0)
            bounds.removeFromTop (kMacTitleDragInset);

        auto tabBar = bounds.removeFromTop (kTabBarHeight).reduced (10, 8);

        const int tabW = juce::jmax (96, tabBar.getWidth() / 2);
        const int total = tabW * 2;
        tabBar = tabBar.withSizeKeepingCentre (total, tabBar.getHeight());

        audioTabBtn->setBounds (tabBar.removeFromLeft (tabW));
        appearanceTabBtn->setBounds (tabBar.removeFromLeft (tabW));

        bounds.removeFromTop (1);
        audioPanel->setBounds (bounds);
        themePanel->setBounds (bounds);
    }

    void parentHierarchyChanged() override
    {
        juce::Component::parentHierarchyChanged();

        if (getParentComponent() == nullptr && audioPanel != nullptr)
            audioPanel->parentHierarchyChanged();
    }

private:
   #if JUCE_MAC
    static constexpr int kMacTitleDragInset = 14;
   #else
    static constexpr int kMacTitleDragInset = 0;
   #endif
    static constexpr int kTabBarHeight = 76;

    juce::ComponentDragger windowDragger;
    bool windowDragActive = false;

    bool isClickOnTabButtonArea (const juce::MouseEvent& e) const
    {
        if (auto* hit = e.eventComponent)
        {
            if (hit == audioTabBtn.get() || hit == appearanceTabBtn.get())
                return true;

            for (auto* p = hit->getParentComponent(); p != nullptr; p = p->getParentComponent())
            {
                if (p == audioTabBtn.get() || p == appearanceTabBtn.get())
                    return true;
            }
        }

        return false;
    }

    Callbacks cb;
    std::unique_ptr<PreferencesTabButton> audioTabBtn, appearanceTabBtn;
    std::unique_ptr<AudioDeviceSettingsPanel> audioPanel;
    std::unique_ptr<ThemePreferencesPanel> themePanel;
    int activeTab = 0;

    void syncDialogWindowBackground()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        {
            dw->setBackgroundColour (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
            dw->repaint();
        }
    }

    void pushThemeColoursToEmbeddedPanels()
    {
        if (audioPanel != nullptr)
            audioPanel->applyThemeColoursDirectly();

        if (themePanel != nullptr)
            themePanel->refreshSectionLabelColours();

        syncDialogWindowBackground();
        repaint();
        audioTabBtn->repaint();
        appearanceTabBtn->repaint();

        if (themePanel != nullptr)
            themePanel->repaint();
    }

    void selectTab (int index) noexcept
    {
        activeTab = index;
        audioTabBtn->setTabActive (index == 0);
        appearanceTabBtn->setTabActive (index == 1);
        audioPanel->setVisible (index == 0);
        themePanel->setVisible (index == 1);
    }
};
