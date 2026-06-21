#pragma once
#include <functional>
#include <atomic>
#include <JuceHeader.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowFlatIcons.h"

namespace showcontrol::permissions
{
inline juce::Colour permissionCardSurface (const juce::LookAndFeel& laf) noexcept
{
    return laf.findColour (ShowControlLookAndFeel::panelBackgroundColourId).brighter (0.06f);
}

inline juce::Colour permissionPanelBackground (const juce::LookAndFeel& laf) noexcept
{
    return laf.findColour (juce::ResizableWindow::backgroundColourId);
}

enum class PermissionStatus
{
    enabled,
    disabled,
    warning
};

void paintEnabledBadge (juce::Graphics& g, juce::Rectangle<float> area);
void paintGrantPromptBadge (juce::Graphics& g, juce::Rectangle<float> area, const juce::LookAndFeel& laf);
void paintWarningBadge (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text);

//==============================================================================
class PermissionCardComponent final : public juce::Component
{
public:
    enum class IconKind { mic, globe, shield };

    PermissionCardComponent (IconKind kind,
                             juce::String titleIn,
                             juce::String descriptionIn);

    std::function<void()> onGrantRequested;

    void setAllowGrantButton (bool shouldAllow) noexcept;
    void setStatus (PermissionStatus newStatus, juce::String warningText = {});
    void refreshLocalizedText();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    IconKind iconKind;
    juce::String cardTitle, cardDescription, warningLabel;
    PermissionStatus status = PermissionStatus::disabled;
    bool allowGrantButton = false;
    juce::TextButton grantButton;
    juce::Rectangle<int> badgeAreaBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PermissionCardComponent)
};

//==============================================================================
/** Farrago flat — cột trái hero + cột phải thẻ quyền cuộn, cập nhật động 500ms. */
class SystemPermissionsPanel final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit SystemPermissionsPanel (juce::AudioDeviceManager* sharedDeviceManagerIn = nullptr);
    ~SystemPermissionsPanel() override;

    void haltActiveTimers() noexcept;

    static int getPreferredEmbeddedHeight() noexcept
    {
        constexpr int cardH = 116;
        constexpr int gap   = 12;
        constexpr int padV  = 28;
        const int cardCount =
       #if JUCE_WINDOWS
            4;
       #else
            2;
       #endif
        return padV + cardCount * cardH + (cardCount - 1) * gap;
    }

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void lookAndFeelChanged() override;
    void refreshLocalizedText();

private:
    bool checkAudioPermission();
    bool checkNetworkPermission();
    bool checkWindowsAdminConflict();
    bool checkWindowsFirewallPermission();

    void updatePermissionUi();
    void rebuildCardLayout();
    void performResetPermissions();

    void timerCallback() override;

    juce::TextButton resetPermissionsButton;
    juce::Viewport cardsViewport;
    juce::Component cardsContainer;

    PermissionCardComponent audioCard;
    PermissionCardComponent networkCard;
   #if JUCE_WINDOWS
    PermissionCardComponent firewallCard;
    PermissionCardComponent dragDropCard;
   #endif

    juce::Rectangle<int> leftHeroArea;
    juce::AudioDeviceManager* sharedDeviceManager = nullptr;
    std::atomic<bool> permissionProbeInFlight { false };
    std::atomic<bool> cachedFirewallRulesPresent { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SystemPermissionsPanel)
};

} // namespace showcontrol::permissions
