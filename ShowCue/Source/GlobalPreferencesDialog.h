#pragma once
#include <functional>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioDeviceSettingsPanel.h"
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowControlMacWindow.h"
#include "ShowFlatIcons.h"
#include "ShowLocalization.h"
#include "SystemPermissionsPanel.h"
#include "BackupSyncPreferencesPanel.h"
#include "ShowBackupLanDiscovery.h"

namespace showcontrol::preferences
{
    /** Viền chọn card — đồng bộ accent xanh FOH từ LAF, không dùng tím mặc định JUCE. */
    inline juce::Colour cardSelectionBorder (const juce::LookAndFeel& laf) noexcept
    {
        return laf.findColour (ShowControlLookAndFeel::accentColourId);
    }

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

    enum class TabIcon { audio, appearance, permissions, network };

    inline void drawTabIcon (juce::Graphics& g, juce::Rectangle<float> area, TabIcon icon, juce::Colour colour) noexcept
    {
        const auto iconBounds = showcontrol::icons::centredIconIn (area, showcontrol::icons::kTabIconSize);

        if (icon == TabIcon::audio)
            showcontrol::icons::paintHeadphonesIcon (g, iconBounds, colour);
        else if (icon == TabIcon::appearance)
            showcontrol::icons::paintLayoutIcon (g, iconBounds, colour);
        else if (icon == TabIcon::network)
            showcontrol::icons::paintGlobeIcon (g, iconBounds, colour);
        else
            showcontrol::icons::paintShieldCheckIcon (g, iconBounds, colour);
    }

    /** Lá cờ Việt Nam phẳng — nền đỏ + sao vàng vector, tâm sao khớp tâm hình học nền đỏ. */
    inline void drawFlatVietnamFlag (juce::Graphics& g, juce::Rectangle<float> area) noexcept
    {
        auto flagBounds = area.reduced (area.getWidth() * 0.08f, area.getHeight() * 0.12f);
        g.setColour (juce::Colour (0xffda251d));
        g.fillRoundedRectangle (flagBounds, 3.5f);

        const float centerX = flagBounds.getCentreX();
        const float centerY = flagBounds.getCentreY();
        const float outerRadius = flagBounds.getHeight() * 0.24f;
        const float innerRadius = outerRadius * 0.382f;

        juce::Path star;
        star.addStar ({ centerX, centerY },
                      5,
                      innerRadius,
                      outerRadius,
                      -juce::MathConstants<float>::halfPi);
        g.setColour (juce::Colour (0xfff9d71c));
        g.fillPath (star);
    }

    /** Lá cờ Mỹ phẳng — sọc đỏ/trắng + canton xanh. */
    inline void drawFlatUsFlag (juce::Graphics& g, juce::Rectangle<float> area) noexcept
    {
        auto flag = area.reduced (area.getWidth() * 0.08f, area.getHeight() * 0.12f);
        constexpr int stripes = 7;
        const float stripeH = flag.getHeight() / (float) stripes;

        for (int i = 0; i < stripes; ++i)
        {
            g.setColour (i % 2 == 0 ? juce::Colour (0xffb22234) : juce::Colours::white);
            g.fillRect (flag.getX(), flag.getY() + stripeH * (float) i,
                        flag.getWidth(), stripeH + 0.5f);
        }

        const float cantonW = flag.getWidth() * 0.42f;
        const float cantonH = stripeH * 4.0f;
        g.setColour (juce::Colour (0xff3c3b6e));
        g.fillRect (flag.getX(), flag.getY(), cantonW, cantonH);
    }
} // namespace showcontrol::preferences

//==============================================================================
/** Nút Tab phẳng — icon vector phía trên, nhãn phía dưới (Farrago style). */
class PreferencesTabButton : public juce::Component
{
public:
    std::function<void()> onSelected;

    PreferencesTabButton (showcontrol::preferences::TabIcon iconType, const char* labelUtf8)
        : tabIcon (iconType), labelKey (labelUtf8)
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
        g.setFont (showcontrol::preferences::tabLabelFont());
        g.drawText (showcontrol::localization::tr (labelKey), bounds.toNearestInt(), juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && onSelected != nullptr)
            onSelected();
    }

private:
    showcontrol::preferences::TabIcon tabIcon;
    const char* labelKey = "";
    bool tabActive = false;
};

//==============================================================================
/** Card chọn theme — mini preview + viền accent xanh khi active. */
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

        if (selected)
        {
            g.setColour (showcontrol::preferences::cardSelectionBorder (laf));
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
/** Card chọn ngôn ngữ — globe / cờ VN / cờ US + viền accent xanh khi active. */
class LanguageAppearanceCard : public juce::Button
{
public:
    enum class Kind { matchSystem, vietnamese, english };

    LanguageAppearanceCard (Kind cardKind, const char* labelUtf8)
        : juce::Button ({}),
          kind (cardKind),
          labelKey (labelUtf8)
    {
        setClickingTogglesState (true);
        setRadioGroupId (2002);
    }

    void paintButton (juce::Graphics& g, bool /*highlighted*/, bool /*down*/) override
    {
        const auto& laf = getLookAndFeel();
        const auto cardBg = showcontrol::preferences::dynamicCardSurface (laf);
        const auto labelCol = showcontrol::preferences::dynamicPrimaryText (laf);
        const auto mutedCol = showcontrol::preferences::dynamicMutedText (laf);
        const bool selected = getToggleState();

        auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (cardBg);
        g.fillRoundedRectangle (bounds, 8.0f);

        auto preview = bounds.reduced (10.0f, 12.0f);
        preview.removeFromBottom (preview.getHeight() * 0.18f);

        switch (kind)
        {
            case Kind::matchSystem:
                showcontrol::icons::paintGlobeIcon (g, preview, labelCol.withAlpha (0.78f));
                break;

            case Kind::vietnamese:
                showcontrol::preferences::drawFlatVietnamFlag (g, preview);
                break;

            case Kind::english:
                showcontrol::preferences::drawFlatUsFlag (g, preview);
                break;
        }

        if (selected)
        {
            g.setColour (showcontrol::preferences::cardSelectionBorder (laf));
            g.drawRoundedRectangle (bounds.reduced (1.25f), 8.0f, 2.5f);
        }

        g.setColour (selected ? labelCol : mutedCol);
        g.setFont (showcontrol::preferences::tabLabelFont().withHeight (12.5f));
        g.drawText (showcontrol::localization::tr (labelKey),
                    bounds.removeFromBottom (16.0f).toNearestInt(),
                    juce::Justification::centred);
    }

private:
    Kind kind;
    const char* labelKey = "";
};

//==============================================================================
/** Tab Giao diện — 3 Card Appearance nằm ngang. */
class ThemePreferencesPanel : public juce::Component
{
public:
    std::function<void (int themeId)> onThemeChanged;
    std::function<void (int languageIndex)> onLanguageChanged;
    std::function<void()> onCheckForUpdatesRequested;

    explicit ThemePreferencesPanel (int themeId, int languageIndex)
    {
        sectionLabel.setFont (showcontrol::preferences::sectionLabelFont());
        sectionLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (sectionLabel);

        hintLabel.setFont (showcontrol::preferences::hintFont());
        hintLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (hintLabel);

        languageLabel.setFont (showcontrol::preferences::sectionLabelFont());
        languageLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (languageLabel);

        addAndMakeVisible (systemLangCard);
        addAndMakeVisible (vietnameseLangCard);
        addAndMakeVisible (englishLangCard);

        auto pickLanguage = [this] (int id, LanguageAppearanceCard& card)
        {
            if (! card.getToggleState())
                return;

            applyLanguageChoice (id);
        };

        systemLangCard.onClick    = [this, pickLanguage] { pickLanguage (0, systemLangCard); };
        vietnameseLangCard.onClick = [this, pickLanguage] { pickLanguage (1, vietnameseLangCard); };
        englishLangCard.onClick   = [this, pickLanguage] { pickLanguage (2, englishLangCard); };

        addAndMakeVisible (systemCard);
        addAndMakeVisible (lightCard);
        addAndMakeVisible (darkCard);

        systemCard.setButtonText (showcontrol::localization::tr (u8"Theo hệ thống"));
        lightCard.setButtonText (showcontrol::localization::tr (u8"Sáng"));
        darkCard.setButtonText (showcontrol::localization::tr (u8"Tối"));

        auto pickTheme = [this] (int id, ThemeAppearanceCard& card)
        {
            if (! card.getToggleState())
                return;

            applyThemeChoice (id);
        };

        systemCard.onClick = [this, pickTheme] { pickTheme (3, systemCard); };
        lightCard.onClick  = [this, pickTheme] { pickTheme (2, lightCard); };
        darkCard.onClick   = [this, pickTheme] { pickTheme (1, darkCard); };

        addAndMakeVisible (updateSectionLabel);
        addAndMakeVisible (updateHintLabel);
        addAndMakeVisible (checkUpdateButton);
        checkUpdateButton.onClick = [this]
        {
            if (onCheckForUpdatesRequested != nullptr)
                onCheckForUpdatesRequested();
        };

        syncLabelColours();
        refreshLocalizedText();
        setThemeSelection (themeId);
        setLanguageSelection (languageIndex);
    }

    void refreshSectionLabelColours()
    {
        syncLabelColours();
    }

    void refreshLocalizedText()
    {
        sectionLabel.setText (showcontrol::localization::tr (u8"Chế độ hiển thị"), juce::dontSendNotification);
        hintLabel.setText (showcontrol::localization::tr (
            u8"Chọn giao diện cho toàn bộ ứng dụng. Thay đổi có hiệu lực ngay lập tức."),
            juce::dontSendNotification);
        languageLabel.setText (showcontrol::localization::tr (u8"Ngôn ngữ"), juce::dontSendNotification);
        systemCard.setButtonText (showcontrol::localization::tr (u8"Theo hệ thống"));
        lightCard.setButtonText (showcontrol::localization::tr (u8"Sáng"));
        darkCard.setButtonText (showcontrol::localization::tr (u8"Tối"));
        systemCard.repaint();
        lightCard.repaint();
        darkCard.repaint();

        updateSectionLabel.setText (showcontrol::localization::tr (u8"Cập nhật ứng dụng"),
                                    juce::dontSendNotification);
        updateHintLabel.setText (showcontrol::localization::tr (
            u8"Kiểm tra phiên bản mới từ GitHub và mở trang tải về."),
            juce::dontSendNotification);
        checkUpdateButton.setButtonText (showcontrol::localization::tr (u8"Kiểm tra cập nhật..."));
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        syncLabelColours();
        refreshLocalizedText();
        repaint();
        systemCard.repaint();
        lightCard.repaint();
        darkCard.repaint();
        systemLangCard.repaint();
        vietnameseLangCard.repaint();
        englishLangCard.repaint();
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

    void setLanguageSelection (int languageIndex)
    {
        currentLanguageIndex = juce::jlimit (0, 2, languageIndex);
        systemLangCard.setToggleState    (currentLanguageIndex == 0, juce::dontSendNotification);
        vietnameseLangCard.setToggleState (currentLanguageIndex == 1, juce::dontSendNotification);
        englishLangCard.setToggleState   (currentLanguageIndex == 2, juce::dontSendNotification);
        systemLangCard.repaint();
        vietnameseLangCard.repaint();
        englishLangCard.repaint();
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

        bounds.removeFromTop (20);
        languageLabel.setBounds (bounds.removeFromTop (22));
        bounds.removeFromTop (14);

        auto langRow = bounds.removeFromTop (cardH);
        langRow = langRow.withSizeKeepingCentre (totalW, cardH);

        systemLangCard.setBounds (langRow.removeFromLeft (cardW));
        langRow.removeFromLeft (gap);
        vietnameseLangCard.setBounds (langRow.removeFromLeft (cardW));
        langRow.removeFromLeft (gap);
        englishLangCard.setBounds (langRow.removeFromLeft (cardW));

        bounds.removeFromTop (24);
        updateSectionLabel.setBounds (bounds.removeFromTop (22));
        bounds.removeFromTop (4);
        updateHintLabel.setBounds (bounds.removeFromTop (34));
        bounds.removeFromTop (8);
        auto btnRow = bounds.removeFromTop (32);
        const int btnW = 180;
        checkUpdateButton.setBounds (btnRow.withSizeKeepingCentre (btnW, 28));
    }

private:
    int currentThemeId = 1;
    int currentLanguageIndex = 0;
    juce::Label sectionLabel, hintLabel, languageLabel;
    juce::Label updateSectionLabel, updateHintLabel;
    ThemeAppearanceCard systemCard { ThemeAppearanceCard::Kind::matchSystem, {} };
    ThemeAppearanceCard lightCard  { ThemeAppearanceCard::Kind::light, {} };
    ThemeAppearanceCard darkCard   { ThemeAppearanceCard::Kind::dark, {} };
    LanguageAppearanceCard systemLangCard    { LanguageAppearanceCard::Kind::matchSystem, u8"Mặc định hệ thống" };
    LanguageAppearanceCard vietnameseLangCard { LanguageAppearanceCard::Kind::vietnamese, u8"Tiếng Việt" };
    LanguageAppearanceCard englishLangCard   { LanguageAppearanceCard::Kind::english, u8"English" };
    juce::TextButton checkUpdateButton;

    void syncLabelColours()
    {
        const auto& laf = getLookAndFeel();
        sectionLabel.setColour (juce::Label::textColourId,
                                showcontrol::preferences::dynamicPrimaryText (laf));
        hintLabel.setColour (juce::Label::textColourId,
                             showcontrol::preferences::dynamicMutedText (laf));
        languageLabel.setColour (juce::Label::textColourId,
                                 showcontrol::preferences::dynamicPrimaryText (laf));
        updateSectionLabel.setColour (juce::Label::textColourId,
                                      showcontrol::preferences::dynamicPrimaryText (laf));
        updateHintLabel.setColour (juce::Label::textColourId,
                                   showcontrol::preferences::dynamicMutedText (laf));
    }

    void applyLanguageChoice (int languageIndex)
    {
        if (currentLanguageIndex == languageIndex)
            return;

        currentLanguageIndex = languageIndex;

        if (onLanguageChanged != nullptr)
            onLanguageChanged (languageIndex);
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
    static constexpr int kDefaultDialogWidth = 680;

    static int getPreferredPanelHeight() noexcept
    {
        const int cardCount =
       #if JUCE_WINDOWS
            4;
       #else
            2;
       #endif

        constexpr int cardH = 116;
        constexpr int gap   = 12;
        constexpr int pad   = 28;
        const int cardsH = pad + cardCount * cardH + (cardCount - 1) * gap;
        const int heroMinH = 260;
        return juce::jmax (cardsH, heroMinH);
    }

    static int getPermissionsPanelHeight() noexcept
    {
        return getPreferredPanelHeight();
    }

    static int getPreferredPanelHeightForTab (int tabIndex) noexcept
    {
        switch (tabIndex)
        {
            case 0: return AudioDeviceSettingsPanel::getPreferredEmbeddedHeight();
            case 1: return 460;
            case 2: return getPreferredPanelHeight();
            case 3: return 640;
            default: return 520;
        }
    }

    /** Chiều cao nội dung tab Quyền (đo từ UI mẫu ~645px cửa sổ trên Windows). */
    static int getDefaultContentHeight() noexcept
    {
        return kMacTitleDragInset + kTabBarHeight + 1 + getPreferredPanelHeightForTab (2);
    }

    static juce::Point<int> getDefaultContentSize() noexcept
    {
        return { kDefaultDialogWidth, getDefaultContentHeight() };
    }

    static int getNativeTitleBarAllowance() noexcept
    {
       #if JUCE_WINDOWS
        return 32;
       #elif JUCE_MAC
        return 28;
       #else
        return 28;
       #endif
    }

    static juce::Point<int> getDefaultWindowSize() noexcept
    {
        const auto content = getDefaultContentSize();
        return { content.x, content.y + getNativeTitleBarAllowance() };
    }

    static void configureDialogWindow (juce::DialogWindow& window)
    {
        const auto windowSize = getDefaultWindowSize();
        window.setResizeLimits (kDefaultDialogWidth - 120,
                                windowSize.y - 140,
                                1200,
                                1000);
        window.centreWithSize (windowSize.x, windowSize.y);
    }

    struct Callbacks
    {
        std::function<void (int busIndex, const juce::String& text)> onBusNameLiveChanged;
        std::function<void (const AudioDeviceSettingsPanel::ApplyResult&)> onAudioSettingsApplied;
        std::function<void (int themeId)> onThemeChanged;
        std::function<void (int languageIndex)> onLanguageChanged;
        std::function<void()> onCheckForUpdatesRequested;
        std::function<void()> onBackupSettingsChanged;
        std::function<void (bool)> onBackupTakeoverChanged;
        std::function<bool()> getBackupTakeoverActive;
        std::function<void (int wantRole,
                            std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone)> onScanLanPeers;
        std::function<juce::Array<showcontrol::backup::PeerRuntimeStatus>()> queryBackupPeerRuntimeStatus;
        std::function<void()> onReconnectBackupSync;
        std::function<void()> onSyncProjectConfigToBackups;
    };

    GlobalPreferencesDialog (juce::AudioDeviceManager& deviceManager,
                             bool darkMode,
                             const juce::StringArray& busNames,
                             int themeId,
                             int languageIndex,
                             Callbacks callbacks)
        : cb (std::move (callbacks))
    {
        audioPanel = std::make_unique<AudioDeviceSettingsPanel> (deviceManager, darkMode, busNames, true);
        themePanel = std::make_unique<ThemePreferencesPanel> (themeId, languageIndex);
        permissionsPanel = std::make_unique<showcontrol::permissions::SystemPermissionsPanel> (&deviceManager);
        backupPanel      = std::make_unique<BackupSyncPreferencesPanel>();

        if (cb.getBackupTakeoverActive != nullptr)
            backupPanel->setTakeoverActive (cb.getBackupTakeoverActive());

        backupPanel->onSettingsChanged = [this]
        {
            backupPanel->saveToPreferences();

            if (cb.onBackupSettingsChanged != nullptr)
                cb.onBackupSettingsChanged();
        };

        backupPanel->onTakeoverToggled = [this]
        {
            if (cb.onBackupTakeoverChanged != nullptr)
                cb.onBackupTakeoverChanged (backupPanel->isTakeoverActive());
        };

        backupPanel->onScanLanPeers = [this] (int wantRole,
                                              std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone)
        {
            if (cb.onScanLanPeers != nullptr)
                cb.onScanLanPeers (wantRole, std::move (onDone));
        };

        backupPanel->queryPeerRuntimeStatus = [this]
        {
            if (cb.queryBackupPeerRuntimeStatus != nullptr)
                return cb.queryBackupPeerRuntimeStatus();

            return juce::Array<showcontrol::backup::PeerRuntimeStatus>();
        };

        backupPanel->onReconnectRequested = [this]
        {
            if (cb.onReconnectBackupSync != nullptr)
                cb.onReconnectBackupSync();
        };

        backupPanel->onSyncConfigRequested = [this]
        {
            if (cb.onSyncProjectConfigToBackups != nullptr)
                cb.onSyncProjectConfigToBackups();
        };

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

        themePanel->onLanguageChanged = [this] (int newLanguageIndex)
        {
            if (cb.onLanguageChanged != nullptr)
                cb.onLanguageChanged (newLanguageIndex);

            refreshLocalizedText();
        };

        themePanel->onCheckForUpdatesRequested = [this]
        {
            if (cb.onCheckForUpdatesRequested != nullptr)
                cb.onCheckForUpdatesRequested();
        };

        addAndMakeVisible (audioPanel.get());
        addAndMakeVisible (themePanel.get());
        addAndMakeVisible (permissionsPanel.get());
        addAndMakeVisible (backupPanel.get());

        audioTabBtn = std::make_unique<PreferencesTabButton> (
            showcontrol::preferences::TabIcon::audio,
            u8"Âm thanh");

        appearanceTabBtn = std::make_unique<PreferencesTabButton> (
            showcontrol::preferences::TabIcon::appearance,
            u8"Giao diện");

        permissionsTabBtn = std::make_unique<PreferencesTabButton> (
            showcontrol::preferences::TabIcon::permissions,
            u8"Quyền");

        networkTabBtn = std::make_unique<PreferencesTabButton> (
            showcontrol::preferences::TabIcon::network,
            u8"Mạng");

        addAndMakeVisible (*audioTabBtn);
        addAndMakeVisible (*appearanceTabBtn);
        addAndMakeVisible (*permissionsTabBtn);
        addAndMakeVisible (*networkTabBtn);

        audioTabBtn->onSelected = [this] { selectTab (0); };
        appearanceTabBtn->onSelected = [this] { selectTab (1); };
        permissionsTabBtn->onSelected = [this] { selectTab (2); };
        networkTabBtn->onSelected = [this] { selectTab (3); };

        selectTab (0);
        setWantsKeyboardFocus (true);

        const auto contentSize = getDefaultContentSize();
        setSize (contentSize.x, contentSize.y);
    }

    ~GlobalPreferencesDialog() override
    {
        if (backupPanel != nullptr)
            backupPanel->haltActiveTimers();

        if (permissionsPanel != nullptr)
            permissionsPanel->haltActiveTimers();
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
        selectTab (juce::jlimit (0, 3, tabIndex));
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        pushThemeColoursToEmbeddedPanels();
        refreshLocalizedText();
    }

    void refreshLocalizedText()
    {
        if (themePanel != nullptr)
            themePanel->refreshLocalizedText();

        if (permissionsPanel != nullptr)
            permissionsPanel->refreshLocalizedText();

        if (backupPanel != nullptr)
            backupPanel->refreshLocalizedText();

        if (audioPanel != nullptr)
            audioPanel->refreshLocalizedText();

        if (audioTabBtn != nullptr)
            audioTabBtn->repaint();

        if (appearanceTabBtn != nullptr)
            appearanceTabBtn->repaint();

        if (permissionsTabBtn != nullptr)
            permissionsTabBtn->repaint();

        if (networkTabBtn != nullptr)
            networkTabBtn->repaint();
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

        const int tabW = juce::jmax (72, tabBar.getWidth() / 4);
        const int total = tabW * 4;
        tabBar = tabBar.withSizeKeepingCentre (total, tabBar.getHeight());

        audioTabBtn->setBounds (tabBar.removeFromLeft (tabW));
        appearanceTabBtn->setBounds (tabBar.removeFromLeft (tabW));
        permissionsTabBtn->setBounds (tabBar.removeFromLeft (tabW));
        networkTabBtn->setBounds (tabBar.removeFromLeft (tabW));

        bounds.removeFromTop (1);
        audioPanel->setBounds (bounds);
        themePanel->setBounds (bounds);
        permissionsPanel->setBounds (bounds);
        backupPanel->setBounds (bounds);
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
            if (hit == audioTabBtn.get() || hit == appearanceTabBtn.get()
                || hit == permissionsTabBtn.get() || hit == networkTabBtn.get())
                return true;

            for (auto* p = hit->getParentComponent(); p != nullptr; p = p->getParentComponent())
            {
                if (p == audioTabBtn.get() || p == appearanceTabBtn.get()
                    || p == permissionsTabBtn.get() || p == networkTabBtn.get())
                    return true;
            }
        }

        return false;
    }

    Callbacks cb;
    std::unique_ptr<PreferencesTabButton> audioTabBtn, appearanceTabBtn, permissionsTabBtn, networkTabBtn;
    std::unique_ptr<AudioDeviceSettingsPanel> audioPanel;
    std::unique_ptr<ThemePreferencesPanel> themePanel;
    std::unique_ptr<showcontrol::permissions::SystemPermissionsPanel> permissionsPanel;
    std::unique_ptr<BackupSyncPreferencesPanel> backupPanel;
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
        permissionsTabBtn->repaint();

        if (themePanel != nullptr)
            themePanel->repaint();

        if (permissionsPanel != nullptr)
            permissionsPanel->repaint();
    }

    void selectTab (int index) noexcept
    {
        activeTab = index;
        audioTabBtn->setTabActive (index == 0);
        appearanceTabBtn->setTabActive (index == 1);
        permissionsTabBtn->setTabActive (index == 2);
        networkTabBtn->setTabActive (index == 3);
        audioPanel->setVisible (index == 0);
        themePanel->setVisible (index == 1);
        permissionsPanel->setVisible (index == 2);
        backupPanel->setVisible (index == 3);

        if (index == 2 && permissionsPanel != nullptr)
            permissionsPanel->resized();

        if (index == 3 && backupPanel != nullptr)
        {
            backupPanel->refreshLocalizedText();
            backupPanel->refreshNetworkInfo();
            backupPanel->resized();
        }

        resizeToFitActiveTab();
    }

    void resizeToFitActiveTab()
    {
        const int panelH = getPreferredPanelHeightForTab (activeTab);
        const int contentH = kMacTitleDragInset + kTabBarHeight + 1 + panelH;
        setSize (kDefaultDialogWidth, contentH);
        resized();

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        {
            const int winH = contentH + getNativeTitleBarAllowance();
            dw->setSize (kDefaultDialogWidth, winH);
            dw->centreWithSize (kDefaultDialogWidth, winH);
        }
    }
};
