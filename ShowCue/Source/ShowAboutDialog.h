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
    line += juce::String::fromUTF8 (u8" (Universal Binary)");
   #endif

    return line;
}

inline juce::File resolveBundledResourceFile (const juce::String& fileName)
{
   #if JUCE_MAC
    const auto bundled = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                             .getParentDirectory().getParentDirectory()
                             .getChildFile ("Resources")
                             .getChildFile (fileName);

    if (bundled.existsAsFile())
        return bundled;
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
    const auto outline = laf.findColour (juce::Label::textColourId).withAlpha (0.28f);
    const auto fill    = laf.findColour (juce::ResizableWindow::backgroundColourId).brighter (0.04f);
    const auto textCol = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);

    g.setColour (fill);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);

    g.setColour (outline);
    const float dash[] = { 5.0f, 4.0f };
    juce::Path border;
    border.addRoundedRectangle (area.toFloat().reduced (1.0f), 6.0f);
    juce::PathStrokeType stroke (1.2f);
    stroke.createDashedStroke (border, border, dash, 2);
    g.strokePath (border, stroke);

    g.setColour (textCol);
    g.setFont (ShowTheme::font (10.0f));
    g.drawFittedText ("QR CODE\nDONATE\nPLACEHOLDER",
                      area.reduced (6),
                      juce::Justification::centred,
                      3);
}

/** About phẳng HAYATUAN — nền/màu chữ từ LAF, không TextEditor (tránh crash LnF line 82). */
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

        if (const auto iconFile = resolveBundledResourceFile ("AppIcon.png"); iconFile.existsAsFile())
            appIcon = juce::ImageFileFormat::loadFrom (iconFile);

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
        setSize (560, 472);
    }

    std::function<void()> onCloseRequested;

    void paint (juce::Graphics& g) override
    {
        const auto& laf = getLookAndFeel();
        g.fillAll (laf.findColour (juce::ResizableWindow::backgroundColourId));

        const auto textPrimary   = laf.findColour (juce::Label::textColourId);
        const auto textSecondary = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);

        if (appIcon.isValid())
        {
            g.drawImage (appIcon,
                         iconArea.toFloat(),
                         juce::RectanglePlacement::centred);
        }

        auto text = leftTextArea;

        g.setColour (textPrimary);
        g.setFont (ShowTheme::fontBold (22.0f));
        g.drawFittedText (juce::String::fromUTF8 (u8"ShowCue"),
                          text.removeFromTop (28),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (2);
        g.setFont (ShowTheme::font (14.0f));
        g.setColour (textSecondary);
        g.drawFittedText (showcontrol::localization::tr (u8"Phần mềm phát nhạc sự kiện"),
                          text.removeFromTop (20),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (6);
        g.setFont (ShowTheme::font (12.5f));
        g.drawFittedText (marketingVersionLine(),
                          text.removeFromTop (18),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (6);
        g.drawFittedText (showcontrol::localization::tr (u8"Thiết kế & lập trình: Hayatuan"),
                          text.removeFromTop (20),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (2);
        g.drawFittedText (showcontrol::localization::tr (u8"© 2026 Hayatuan. All rights reserved."),
                          text.removeFromTop (18),
                          juce::Justification::centredLeft,
                          1);

        text.removeFromTop (10);
        g.setFont (ShowTheme::font (11.0f));
        g.setColour (textPrimary.withAlpha (0.88f));
        g.drawFittedText (juce::String::fromUTF8 (
            u8"Chức năng chính: Phát CUE & BGM độ trễ thấp; "
            u8"Kéo thả sắp xếp nhanh; Đồng bộ tên bài hát theo thời gian thực; "
            u8"Phím tắt điều khiển sân khấu."),
                          text.removeFromTop (52),
                          juce::Justification::topLeft,
                          5);

        text.removeFromTop (6);
        g.setFont (ShowTheme::font (10.5f).italicised());
        g.setColour (textSecondary);
        g.drawFittedText (juce::String::fromUTF8 (
            u8"Ý tưởng tham khảo từ các ứng dụng tiêu chuẩn công nghiệp: "
            u8"QLab (macOS), Sports Sound Pro, và Ableton Live."),
                          text,
                          juce::Justification::topLeft,
                          4);

        if (! hasDonateQrImage)
            paintQrDonatePlaceholder (g, qrArea, laf);

        g.setColour (textSecondary);
        g.setFont (ShowTheme::font (10.0f));
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
        constexpr int kButtonWidth  = 148;
        constexpr int kButtonHeight = 30;
        constexpr int kButtonGap    = 12;
        constexpr int kBottomMargin = 16;

        auto bounds = getLocalBounds().reduced (20, 16);

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

        iconArea = bounds.removeFromTop (72).withSizeKeepingCentre (64, 64);

        bounds.removeFromTop (4);
        auto content = bounds;

        auto rightColumn = content.removeFromRight (136);
        qrArea = rightColumn.removeFromTop (120).reduced (4, 0);
        qrCaptionArea = rightColumn.removeFromTop (18);

        leftTextArea = content.reduced (0, 2);

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
    juce::Rectangle<int> leftTextArea;
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
    opts.dialogTitle                  = juce::String::fromUTF8 (u8"Giới thiệu");
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
