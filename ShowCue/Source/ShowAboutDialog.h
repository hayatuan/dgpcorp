#pragma once
#include <JuceHeader.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowControlMacWindow.h"
#include "ShowLocalization.h"

namespace showcontrol::about
{
/** Chuỗi phiên bản marketing — đồng bộ ProjectInfo::versionString. */
inline juce::String marketingVersionLine()
{
    juce::String line = juce::String::fromUTF8 (u8"v")
                     + juce::String (ProjectInfo::versionString);

   #if JUCE_MAC
    line += juce::String::fromUTF8 (u8" · macOS Universal");
   #elif JUCE_WINDOWS
    line += juce::String::fromUTF8 (u8" · Windows x64");
   #endif

    return line;
}

inline juce::File resolveBundledResourceFile (const juce::String& fileName)
{
    const auto exeFile = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

   #if JUCE_MAC
    const auto bundled = exeFile.getParentDirectory().getParentDirectory()
                             .getChildFile ("Resources")
                             .getChildFile (fileName);

    if (bundled.existsAsFile())
        return bundled;
   #elif JUCE_WINDOWS
    const auto resourcesDir = exeFile.getParentDirectory().getChildFile ("Resources").getChildFile (fileName);
    if (resourcesDir.existsAsFile())
        return resourcesDir;

    const auto besideExe = exeFile.getSiblingFile (fileName);
    if (besideExe.existsAsFile())
        return besideExe;
   #endif

    const auto devPath = juce::File::getCurrentWorkingDirectory()
                             .getChildFile ("Resources")
                             .getChildFile (fileName);

    if (devPath.existsAsFile())
        return devPath;

    return juce::File::getCurrentWorkingDirectory()
               .getChildFile ("ShowCue")
               .getChildFile ("Resources")
               .getChildFile (fileName);
}

inline void paintQrDonatePlaceholder (juce::Graphics& g,
                                      juce::Rectangle<int> area,
                                      const juce::LookAndFeel& laf)
{
    const auto outline = laf.findColour (juce::Label::textColourId).withAlpha (0.35f);
    const auto fill    = laf.findColour (juce::ResizableWindow::backgroundColourId).brighter (0.06f);
    const auto textCol = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);

    g.setColour (fill);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);

    g.setColour (outline);
    const float dash[] = { 5.0f, 4.0f };
    juce::Path border;
    border.addRoundedRectangle (area.toFloat().reduced (1.0f), 8.0f);
    juce::PathStrokeType stroke (1.2f);
    stroke.createDashedStroke (border, border, dash, 2);
    g.strokePath (border, stroke);

    g.setColour (textCol);
    g.setFont (ShowTheme::font (11.0f));
    g.drawFittedText (showcontrol::localization::tr (u8"QR ủng hộ"),
                      area.reduced (8),
                      juce::Justification::centred,
                      2);
}

/** About ShowCue — nền/màu chữ từ LAF, không TextEditor (tránh crash LnF). */
class AboutPanel final : public juce::Component
{
public:
    AboutPanel()
    {
        okButton.setButtonText (showcontrol::localization::tr (u8"Đóng"));
        okButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible (okButton);

        feedbackButton.setButtonText (showcontrol::localization::tr (u8"Gửi phản hồi Beta"));
        feedbackButton.onClick = [this]
        {
            juce::URL ("https://forms.gle/zmaaB7i9Ltj6oo1M6").launchInDefaultBrowser();
        };
        addAndMakeVisible (feedbackButton);

        donateQrComponent.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (donateQrComponent);

        if (const auto iconFile = resolveBundledResourceFile ("AppIconAbout.png"); iconFile.existsAsFile())
            appIcon = juce::ImageFileFormat::loadFrom (iconFile);
        else if (const auto fallbackIcon = resolveBundledResourceFile ("AppIcon.png"); fallbackIcon.existsAsFile())
            appIcon = juce::ImageFileFormat::loadFrom (fallbackIcon);

        if (const auto qrFile = resolveBundledResourceFile ("DonateQR.png"); qrFile.existsAsFile())
        {
            donateQrImage = juce::ImageFileFormat::loadFrom (qrFile);

            if (donateQrImage.isValid())
            {
                donateQrComponent.setImage (donateQrImage);
                donateQrComponent.setImagePlacement (juce::RectanglePlacement::centred);
                hasDonateQrImage = true;
            }
        }

        donateQrComponent.setVisible (hasDonateQrImage);
        setSize (624, 600);
    }

    std::function<void()> onCloseRequested;

    void paint (juce::Graphics& g) override
    {
        const auto& laf = getLookAndFeel();
        g.fillAll (laf.findColour (juce::ResizableWindow::backgroundColourId));

        const auto textPrimary   = laf.findColour (juce::Label::textColourId);
        const auto textSecondary = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);
        const auto accent        = laf.findColour (ShowControlLookAndFeel::accentColourId);

        if (appIcon.isValid())
        {
            g.drawImage (appIcon,
                         iconArea.toFloat(),
                         juce::RectanglePlacement::centred);
        }

        auto text = headerTextArea;

        g.setColour (textPrimary);
        g.setFont (ShowTheme::fontBold (26.0f));
        g.drawFittedText (juce::String::fromUTF8 (u8"ShowCue"),
                          text.removeFromTop (32),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (4);
        g.setFont (ShowTheme::font (15.5f));
        g.setColour (textSecondary.withAlpha (0.95f));
        g.drawFittedText (showcontrol::localization::tr (u8"Phần mềm phát nhạc sự kiện"),
                          text.removeFromTop (22),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (8);
        g.setFont (ShowTheme::fontBold (14.0f));
        g.setColour (accent.withAlpha (0.95f));
        g.drawFittedText (marketingVersionLine(),
                          text.removeFromTop (20),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (10);
        g.setFont (ShowTheme::font (13.0f));
        g.setColour (textPrimary.withAlpha (0.92f));
        g.drawFittedText (showcontrol::localization::tr (u8"Thiết kế & lập trình: Hayatuan"),
                          text.removeFromTop (20),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (2);
        g.setColour (textSecondary.withAlpha (0.88f));
        g.drawFittedText (showcontrol::localization::tr (u8"© 2026 Hayatuan. All rights reserved."),
                          text.removeFromTop (18),
                          juce::Justification::centredLeft,
                          1);

        auto features = featuresArea;
        features.removeFromTop (6);
        g.setFont (ShowTheme::fontBold (13.0f));
        g.setColour (textPrimary);
        g.drawFittedText (showcontrol::localization::tr (u8"Chức năng chính"),
                          features.removeFromTop (20),
                          juce::Justification::centredLeft,
                          1);

        features.removeFromTop (4);
        g.setFont (ShowTheme::font (12.5f));
        g.setColour (textPrimary.withAlpha (0.90f));
        g.drawFittedText (showcontrol::localization::tr (
            u8"• Phát CUE & BGM độ trễ thấp, đa bus FOH\n"
            u8"• Lưới cue, danh sách BGM, trim, EQ, đồng bộ âm lượng\n"
            u8"• Đồng bộ show Primary ↔ Backup qua LAN\n"
            u8"• Phím tắt, tìm kiếm, kéo thả sắp xếp nhanh\n"
            u8"• Nhập / xuất cấu hình .showcue giữa các máy"),
                          features,
                          juce::Justification::topLeft,
                          6);

        if (! hasDonateQrImage)
            paintQrDonatePlaceholder (g, qrArea, laf);

        g.setColour (textSecondary.withAlpha (0.9f));
        g.setFont (ShowTheme::fontBold (11.0f));
        g.drawFittedText (showcontrol::localization::tr (u8"Ủng hộ phát triển"),
                          qrCaptionArea,
                          juce::Justification::centred,
                          1);
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        okButton.setButtonText (showcontrol::localization::tr (u8"Đóng"));
        feedbackButton.setButtonText (showcontrol::localization::tr (u8"Gửi phản hồi Beta"));
        repaint();
    }

    void resized() override
    {
        constexpr int kButtonWidth  = 160;
        constexpr int kButtonHeight = 32;
        constexpr int kButtonGap    = 10;
        constexpr int kBottomMargin = 18;
        constexpr int kIconSize     = 64;

        auto bounds = getLocalBounds().reduced (24, 18);

        const int closeY = bounds.getBottom() - kBottomMargin - kButtonHeight;
        okButton.setBounds (bounds.getCentreX() - kButtonWidth / 2,
                            closeY,
                            kButtonWidth,
                            kButtonHeight);
        feedbackButton.setBounds (bounds.getCentreX() - kButtonWidth / 2,
                                  closeY - kButtonGap - kButtonHeight,
                                  kButtonWidth,
                                  kButtonHeight);

        bounds.removeFromBottom (kBottomMargin + kButtonHeight * 2 + kButtonGap);

        auto headerRow = bounds.removeFromTop (juce::jmax (kIconSize, 132));
        auto rightColumn = headerRow.removeFromRight (148);
        qrArea = rightColumn.removeFromTop (128).reduced (4, 0);
        qrCaptionArea = rightColumn.removeFromTop (20);

        if (appIcon.isValid())
        {
            iconArea = headerRow.removeFromLeft (kIconSize).withHeight (kIconSize).reduced (0, 2);
            headerRow.removeFromLeft (12);
        }
        else
        {
            iconArea = {};
        }

        headerTextArea = headerRow.reduced (0, 2);

        featuresArea = bounds.reduced (0, 4);

        if (appIcon.isValid())
            featuresArea = featuresArea.withTrimmedLeft (kIconSize + 12);

        if (hasDonateQrImage)
            donateQrComponent.setBounds (qrArea.reduced (4));
        else
            donateQrComponent.setBounds ({});
    }

private:
    juce::TextButton okButton;
    juce::TextButton feedbackButton;
    juce::ImageComponent donateQrComponent;
    juce::Image appIcon;
    juce::Image donateQrImage;
    bool hasDonateQrImage = false;

    juce::Rectangle<int> iconArea;
    juce::Rectangle<int> headerTextArea;
    juce::Rectangle<int> featuresArea;
    juce::Rectangle<int> qrArea;
    juce::Rectangle<int> qrCaptionArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

inline void showAboutDialog (juce::Component* centreAround,
                             juce::LookAndFeel& laf)
{
    auto* panel = new AboutPanel();
    panel->setLookAndFeel (&laf);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (panel);
    opts.dialogTitle                  = showcontrol::localization::tr (u8"Giới thiệu");
    opts.dialogBackgroundColour       = laf.findColour (juce::ResizableWindow::backgroundColourId);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar            = true;
    opts.resizable                    = false;
    opts.componentToCentreAround      = centreAround;

    if (auto* dw = opts.launchAsync())
    {
        dw->centreWithSize (panel->getWidth(), panel->getHeight());

        panel->onCloseRequested = [safeWindow = juce::Component::SafePointer<juce::DialogWindow> (dw)]
        {
            if (safeWindow != nullptr)
                safeWindow->exitModalState (0);
        };

       #if JUCE_MAC
        showcontrol::mac::applyFarragoFullSizeContentView (*dw);

        juce::Component::SafePointer<juce::DialogWindow> safeDw (dw);
        juce::MessageManager::callAsync ([safeDw]
        {
            if (safeDw != nullptr)
                showcontrol::mac::applyFarragoFullSizeContentView (*safeDw);
        });
       #endif
    }
}
} // namespace showcontrol::about
