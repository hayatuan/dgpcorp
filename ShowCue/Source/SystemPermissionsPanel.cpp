#include "SystemPermissionsPanel.h"
#include "ShowLocalization.h"
#if JUCE_MAC
#include "ShowBackupMacNetwork.h"
#endif

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <shlobj.h>
#endif
namespace showcontrol::permissions
{
namespace
{
class FlatOutlineButtonLook final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& /*backgroundColour*/,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto& laf = button.getLookAndFeel();
        const auto textPrimary = laf.findColour (juce::Label::textColourId);
        const auto outline = textPrimary.withAlpha (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown ? 0.52f : 0.34f);

        if (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
        {
            g.setColour (textPrimary.withAlpha (0.07f));
            g.fillRoundedRectangle (bounds, 6.0f);
        }

        g.setColour (outline);
        g.drawRoundedRectangle (bounds, 6.0f, 1.1f);
    }

    void drawButtonText (juce::Graphics& g,
                         juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsDown);
        const auto textCol = button.findColour (juce::Label::textColourId)
                                 .withAlpha (shouldDrawButtonAsHighlighted ? 0.95f : 0.72f);

        g.setColour (textCol);
        g.setFont (ShowTheme::font (14.5f));
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds().reduced (8, 2),
                          juce::Justification::centred, 1);
    }
};

juce::String resolveMacBundleIdentifier()
{
   #if JUCE_MAC
    const auto appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);

    if (appFile.exists())
    {
        juce::ChildProcess proc;
        const juce::String appPath = appFile.getFullPathName().quoted();

        if (proc.start (juce::StringArray { "/bin/sh", "-c",
                                            "/usr/bin/defaults read " + appPath + "/Contents/Info CFBundleIdentifier 2>/dev/null" }))
        {
            const auto id = proc.readAllProcessOutput().trim();
            if (id.isNotEmpty())
                return id;
        }
    }
   #endif

    return "com.dgpco.showcue";
}

FlatOutlineButtonLook& getFlatOutlineButtonLook()
{
    static FlatOutlineButtonLook look;
    return look;
}
} // namespace

void paintEnabledBadge (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto green = juce::Colour (0xff1faa59);
    auto pill = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 96.0f), 22.0f);

    g.setColour (green);
    g.fillRoundedRectangle (pill, 11.0f);

    auto iconArea = pill.removeFromLeft (22.0f).reduced (5.0f);
    showcontrol::icons::paintCheckmark (g, iconArea, juce::Colours::white);

    g.setColour (juce::Colours::white);
    g.setFont (ShowTheme::fontBold (10.5f));
    g.drawText (showcontrol::localization::tr (u8"Enabled"), pill.toNearestInt(), juce::Justification::centredLeft);
}

void paintGrantPromptBadge (juce::Graphics& g, juce::Rectangle<float> area, const juce::LookAndFeel& laf)
{
    const auto mutedBg = laf.findColour (ShowControlLookAndFeel::panelBackgroundColourId).brighter (0.12f);
    const auto mutedTx = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);
    const float w = juce::jmin (area.getWidth(), 118.0f);
    const auto pill = area.withSizeKeepingCentre (w, 24.0f);

    g.setColour (mutedBg);
    g.fillRoundedRectangle (pill, 12.0f);

    g.setColour (mutedTx.withAlpha (0.35f));
    g.drawRoundedRectangle (pill, 12.0f, 1.0f);

    g.setColour (mutedTx);
    g.setFont (ShowTheme::font (10.0f));
    g.drawFittedText (showcontrol::localization::tr (u8"Cấp quyền ngay"),
                      pill.reduced (6.0f, 0.0f).toNearestInt(),
                      juce::Justification::centred, 1);
}

void paintWarningBadge (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
{
    const auto amber = juce::Colour (0xffe6a817);
    const float w = juce::jmin (area.getWidth(), 220.0f);
    const auto pill = area.withSizeKeepingCentre (w, 22.0f);

    g.setColour (amber);
    g.fillRoundedRectangle (pill, 11.0f);

    g.setColour (juce::Colours::white);
    g.setFont (ShowTheme::fontBold (10.0f));
    g.drawFittedText (text, pill.reduced (8, 0).toNearestInt(), juce::Justification::centred, 1);
}

//==============================================================================
PermissionCardComponent::PermissionCardComponent (IconKind kind,
                                                  juce::String titleIn,
                                                  juce::String descriptionIn)
    : iconKind (kind),
      cardTitle (std::move (titleIn)),
      cardDescription (std::move (descriptionIn))
{
    grantButton.setButtonText (showcontrol::localization::tr (u8"Cấp quyền ngay"));
    grantButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    grantButton.onClick = [this]
    {
        if (onGrantRequested != nullptr)
            onGrantRequested();
    };
    addChildComponent (grantButton);
    setSize (10, 116);
}

void PermissionCardComponent::setAllowGrantButton (bool shouldAllow) noexcept
{
    allowGrantButton = shouldAllow;
}

void PermissionCardComponent::setStatus (PermissionStatus newStatus, juce::String warningText)
{
    status = newStatus;
    warningLabel = std::move (warningText);
    grantButton.setVisible (allowGrantButton && status == PermissionStatus::disabled);

    if (grantButton.isVisible())
    {
        const auto& laf = getLookAndFeel();
        const auto mutedTx = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);
        grantButton.setColour (juce::TextButton::textColourOffId, mutedTx);
        grantButton.setColour (juce::TextButton::textColourOnId, mutedTx);
    }

    repaint();
}

void PermissionCardComponent::paint (juce::Graphics& g)
{
    const auto& laf = getLookAndFeel();
    const auto cardBg = permissionCardSurface (laf);
    const auto textPrimary = laf.findColour (juce::Label::textColourId);
    const auto textMuted   = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);

    auto bounds = getLocalBounds().toFloat().reduced (2.0f, 3.0f);

    juce::DropShadow shadow (juce::Colours::black.withAlpha (0.14f), 10, { 0, 3 });
    shadow.drawForRectangle (g, bounds.toNearestInt());

    g.setColour (cardBg);
    g.fillRoundedRectangle (bounds, 10.0f);

    auto row = bounds.reduced (14.0f, 12.0f);
    auto iconArea = row.removeFromLeft (36.0f).withSizeKeepingCentre (28.0f, 28.0f);
    const auto iconCol = textPrimary.withAlpha (0.82f);

    switch (iconKind)
    {
        case IconKind::mic:     showcontrol::icons::paintMicIcon (g, iconArea, iconCol); break;
        case IconKind::globe:   showcontrol::icons::paintGlobeIcon (g, iconArea, iconCol); break;
        case IconKind::shield:  showcontrol::icons::paintShieldCheckIcon (g, iconArea, iconCol); break;
    }

    row.removeFromLeft (10.0f);
    auto textCol = row;
    auto badgePaintArea = textCol.removeFromRight (122.0f).toFloat();
    badgeAreaBounds = badgePaintArea.toNearestInt();

    g.setColour (textPrimary);
    g.setFont (ShowTheme::fontBold (15.5f));
    g.drawFittedText (juce::LocalisedStrings::translateWithCurrentMappings (cardTitle),
                      textCol.removeFromTop (22).toNearestInt(),
                      juce::Justification::topLeft, 1);

    g.setColour (textMuted);
    g.setFont (ShowTheme::font (13.0f));
    const int descLines = (status == PermissionStatus::warning && warningLabel.isNotEmpty()) ? 2 : 4;
    g.drawFittedText (juce::LocalisedStrings::translateWithCurrentMappings (cardDescription),
                      textCol.removeFromTop (descLines > 2 ? 34 : 44).toNearestInt(),
                      juce::Justification::topLeft, descLines);

    if (status == PermissionStatus::warning && warningLabel.isNotEmpty())
    {
        g.setColour (juce::Colour (0xffe6a817));
        g.setFont (ShowTheme::font (10.5f));
        g.drawFittedText (warningLabel, textCol.toNearestInt(), juce::Justification::topLeft, 3);
    }

    if (status == PermissionStatus::enabled)
    {
        paintEnabledBadge (g, badgePaintArea.withSizeKeepingCentre (96.0f, 22.0f));
    }
    else if (status == PermissionStatus::warning)
    {
        paintWarningBadge (g, badgePaintArea, showcontrol::localization::tr (u8"Cảnh báo"));
    }
    else if (! (allowGrantButton && grantButton.isVisible()))
    {
        paintGrantPromptBadge (g, badgePaintArea.withSizeKeepingCentre (118.0f, 24.0f), laf);
    }
}

void PermissionCardComponent::resized()
{
    grantButton.setBounds (badgeAreaBounds.withSizeKeepingCentre (118, 24));
}

void PermissionCardComponent::refreshLocalizedText()
{
    grantButton.setButtonText (showcontrol::localization::tr (u8"Cấp quyền ngay"));
    repaint();
}

//==============================================================================
SystemPermissionsPanel::SystemPermissionsPanel()
    : audioCard (PermissionCardComponent::IconKind::mic,
                 "Audio Hardware Access",
                 juce::String::fromUTF8 (
                     u8"Truy cập soundcard / mixer sân khấu để phát CUE & BGM không độ trễ trên FOH.")),
      networkCard (PermissionCardComponent::IconKind::globe,
                   "Network Connectivity",
                   juce::String::fromUTF8 (
                       u8"Trạng thái kết nối Internet/mạng nội bộ để kiểm tra cập nhật từ GitHub và đồng bộ show."))
   #if JUCE_WINDOWS
      , dragDropCard (PermissionCardComponent::IconKind::shield,
                      "Drag & Drop File Access",
                      juce::String::fromUTF8 (
                          u8"Chạy không nâng quyền Admin để kéo thả file nhạc vào pad an toàn trên Windows."))
   #endif
{
    addAndMakeVisible (cardsViewport);
    cardsViewport.setViewedComponent (&cardsContainer, false);
    cardsViewport.setScrollBarsShown (true, false);
    cardsViewport.setScrollBarThickness (8);

    cardsContainer.addAndMakeVisible (audioCard);
    cardsContainer.addAndMakeVisible (networkCard);

    audioCard.setAllowGrantButton (true);
    audioCard.onGrantRequested = [this]
    {
        juce::Component::SafePointer<SystemPermissionsPanel> safe (this);
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                           [safe] (bool /*granted*/)
        {
            if (safe != nullptr)
                safe->updatePermissionUi();
        });
    };

   #if JUCE_MAC
    networkCard.setAllowGrantButton (true);
    networkCard.onGrantRequested = [this]
    {
        showcontrol::backup::mac::requestLocalNetworkPermissionPrompt();
        showcontrol::backup::mac::openLocalNetworkPrivacySettings();
        updatePermissionUi();
    };
   #endif

   #if JUCE_WINDOWS
    cardsContainer.addAndMakeVisible (dragDropCard);
   #endif

    resetPermissionsButton.setButtonText (showcontrol::localization::tr (u8"Reset Quyền"));
    resetPermissionsButton.setLookAndFeel (&getFlatOutlineButtonLook());
    resetPermissionsButton.onClick = [this] { performResetPermissions(); };
    addAndMakeVisible (resetPermissionsButton);

    rebuildCardLayout();
    updatePermissionUi();

    if (isShowing())
        startTimer (500);

    setSize (640, 420);
}

SystemPermissionsPanel::~SystemPermissionsPanel()
{
    stopTimer();
    resetPermissionsButton.setLookAndFeel (nullptr);
}

void SystemPermissionsPanel::haltActiveTimers() noexcept
{
    stopTimer();
}

bool SystemPermissionsPanel::checkAudioPermission()
{
    return juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio);
}

bool SystemPermissionsPanel::checkNetworkPermission()
{
    juce::Array<juce::IPAddress> activeAddresses;
    juce::IPAddress::findAllAddresses (activeAddresses);

    for (const auto& ip : activeAddresses)
    {
        if (ip.isNull())
            continue;

        const auto text = ip.toString();
        if (text.startsWith ("127.")
            || text == "::1"
            || text == "0.0.0.0"
            || text == "::")
            continue;

        return true;
    }

    return false;
}

bool SystemPermissionsPanel::checkWindowsAdminConflict()
{
   #if JUCE_WINDOWS
    return IsUserAnAdmin() != FALSE;
   #else
    return false;
   #endif
}

void SystemPermissionsPanel::performResetPermissions()
{
   #if JUCE_MAC
    const juce::String bundleID = resolveMacBundleIdentifier();

    juce::ChildProcess tccProc;
    tccProc.start (juce::StringArray { "/usr/bin/tccutil", "reset", "Microphone", bundleID });

    juce::URL ("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone")
        .launchInDefaultBrowser();

    showcontrol::backup::mac::openLocalNetworkPrivacySettings();
   #elif JUCE_WINDOWS
    juce::URL ("ms-settings:privacy-microphone").launchInDefaultBrowser();

    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::InfoIcon,
        juce::String::fromUTF8 (u8"Reset Quyền"),
        juce::String::fromUTF8 (
            u8"Hệ thống đã mở cài đặt Windows Privacy. Vui lòng đảm bảo tùy chọn "
            u8"'Allow apps to access your microphone' đã được bật, sau đó khởi động lại ShowCue."),
        juce::String::fromUTF8 (u8"Đóng"));
   #endif

    updatePermissionUi();
    repaint();
}

void SystemPermissionsPanel::updatePermissionUi()
{
    audioCard.setStatus (checkAudioPermission() ? PermissionStatus::enabled
                                                : PermissionStatus::disabled);

    networkCard.setStatus (checkNetworkPermission() ? PermissionStatus::enabled
                                                   : PermissionStatus::disabled);

   #if JUCE_WINDOWS
    if (checkWindowsAdminConflict())
    {
        dragDropCard.setStatus (PermissionStatus::warning,
            juce::String::fromUTF8 (
                u8"Hệ thống đang chạy quyền Admin - Chức năng Kéo thả File nhạc sẽ bị Windows khóa!"));
    }
    else
    {
        dragDropCard.setStatus (PermissionStatus::enabled);
    }
   #endif
}

void SystemPermissionsPanel::paint (juce::Graphics& g)
{
    g.fillAll (permissionPanelBackground (getLookAndFeel()));

    const auto& laf = getLookAndFeel();
    const auto textPrimary = laf.findColour (juce::Label::textColourId);
    const auto textMuted   = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);

    auto hero = leftHeroArea.toFloat();

    auto iconGlow = hero.removeFromTop (88.0f).withSizeKeepingCentre (72.0f, 72.0f);
    g.setColour (textPrimary.withAlpha (0.06f));
    g.fillEllipse (iconGlow.expanded (6.0f, 6.0f));
    showcontrol::icons::paintHeadphonesIcon (g, iconGlow.reduced (10.0f),
                                             textPrimary.withAlpha (0.75f));

    hero.removeFromTop (10.0f);
    g.setColour (textPrimary);
    g.setFont (showcontrol::preferences::sectionLabelFont());
    g.drawFittedText (showcontrol::localization::tr (u8"QUYỀN HỆ THỐNG"),
                      hero.removeFromTop (30.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);

    hero.removeFromTop (8.0f);
    g.setColour (textMuted);
    g.setFont (showcontrol::preferences::hintFont());
    g.drawFittedText (showcontrol::localization::tr (
        u8"Để phần mềm vận hành tối ưu nhất, chúng tôi khuyến nghị kích hoạt đầy đủ các quyền này."),
                      hero.toNearestInt(),
                      juce::Justification::topLeft, 5);
}

void SystemPermissionsPanel::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();
    refreshLocalizedText();
}

void SystemPermissionsPanel::refreshLocalizedText()
{
    resetPermissionsButton.setButtonText (showcontrol::localization::tr (u8"Reset Quyền"));
    audioCard.refreshLocalizedText();
    networkCard.refreshLocalizedText();
   #if JUCE_WINDOWS
    dragDropCard.refreshLocalizedText();
   #endif
    updatePermissionUi();
    repaint();
}

void SystemPermissionsPanel::resized()
{
    auto bounds = getLocalBounds().reduced (18, 14);
    auto leftCol = bounds.removeFromLeft (juce::jmin (210, bounds.getWidth() / 3));

    auto resetRow = leftCol.removeFromBottom (36);
    resetPermissionsButton.setBounds (resetRow.withSizeKeepingCentre (132, 30));
    resetPermissionsButton.setColour (juce::Label::textColourId,
                                      getLookAndFeel().findColour (juce::Label::textColourId));

    leftHeroArea = leftCol;
    bounds.removeFromLeft (12);
    cardsViewport.setBounds (bounds);

    rebuildCardLayout();
}

void SystemPermissionsPanel::visibilityChanged()
{
    juce::Component::visibilityChanged();

    if (isVisible() && isShowing())
    {
        updatePermissionUi();
        startTimer (500);
    }
    else
    {
        stopTimer();
    }
}

void SystemPermissionsPanel::rebuildCardLayout()
{
    const int cardH = 116;
    const int gap   = 12;
    int y = 4;
    const int w = juce::jmax (280, cardsViewport.getWidth() - 6);

    audioCard.setBounds (0, y, w, cardH);
    y += cardH + gap;

    networkCard.setBounds (0, y, w, cardH);
    y += cardH + gap;

   #if JUCE_WINDOWS
    dragDropCard.setBounds (0, y, w, cardH);
    y += cardH + gap;
   #endif

    cardsContainer.setSize (w, y + 4);
}

void SystemPermissionsPanel::timerCallback()
{
    if (isVisible())
        updatePermissionUi();
}

} // namespace showcontrol::permissions
