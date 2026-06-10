#pragma once
#include <JuceHeader.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowControlMacWindow.h"

namespace showcontrol::about
{
inline juce::File resolveBundledAppIconPng()
{
   #if JUCE_MAC
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto bundled = exe.getParentDirectory().getParentDirectory()
                             .getChildFile ("Resources")
                             .getChildFile ("AppIcon.png");

    if (bundled.existsAsFile())
        return bundled;
   #endif

    return juce::File::getCurrentWorkingDirectory()
               .getChildFile ("Resources")
               .getChildFile ("AppIcon.png");
}

/** About phẳng — nền/màu chữ từ LAF, không đụng TextEditor (tránh crash LnF line 82). */
class AboutPanel final : public juce::Component
{
public:
    AboutPanel()
    {
        okButton.setButtonText (juce::String::fromUTF8 (u8"Đóng"));
        okButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
        addAndMakeVisible (okButton);

        if (const auto iconFile = resolveBundledAppIconPng(); iconFile.existsAsFile())
            appIcon = juce::ImageFileFormat::loadFrom (iconFile);

        setSize (420, 380);
    }

    std::function<void()> onCloseRequested;

    void paint (juce::Graphics& g) override
    {
        const auto& laf = getLookAndFeel();
        g.fillAll (laf.findColour (juce::ResizableWindow::backgroundColourId));

        const auto textPrimary   = laf.findColour (juce::Label::textColourId);
        const auto textSecondary = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);

        auto bounds = getLocalBounds().reduced (28, 24);

        if (appIcon.isValid())
        {
            const int iconSize = 96;
            auto iconArea = bounds.removeFromTop (iconSize + 12);
            g.drawImage (appIcon,
                         iconArea.withSizeKeepingCentre (iconSize, iconSize).toFloat(),
                         juce::RectanglePlacement::centred);
        }

        bounds.removeFromTop (8);
        g.setColour (textPrimary);
        g.setFont (ShowTheme::font (18.0f).boldened());
        g.drawFittedText (juce::String::fromUTF8 (u8"ShowControl TRÌNH PHÁT SỰ KIỆN"),
                          bounds.removeFromTop (30),
                          juce::Justification::centred,
                          2);

        bounds.removeFromTop (6);
        g.setFont (ShowTheme::font (13.5f));
        g.setColour (textSecondary);
        g.drawFittedText (juce::String::fromUTF8 (u8"Phiên bản ")
                              + juce::String (ProjectInfo::versionString),
                          bounds.removeFromTop (22),
                          juce::Justification::centred,
                          1);

        bounds.removeFromTop (4);
        g.drawFittedText (juce::String::fromUTF8 (u8"© 2024 DGP Co. Bảo lưu mọi quyền."),
                          bounds.removeFromTop (22),
                          juce::Justification::centred,
                          2);
    }

    void resized() override
    {
        okButton.setBounds (getWidth() / 2 - 52, getHeight() - 52, 104, 30);
    }

private:
    juce::TextButton okButton;
    juce::Image appIcon;

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
