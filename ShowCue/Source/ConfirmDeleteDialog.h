#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowControlMacWindow.h"
#include "ShowFlatIcons.h"

namespace showcontrol::ui
{
/** Chỉ trong phiên chạy app — không lưu project/settings; thoát app là hỏi xác nhận lại. */
inline bool& deleteConfirmSuppressedFlag()
{
    static bool suppressed = false;
    return suppressed;
}

inline juce::Colour destructiveActionColour (const juce::LookAndFeel& laf) noexcept
{
    if (auto* scLaf = dynamic_cast<const ShowControlLookAndFeel*> (&laf))
        return scLaf->isDarkMode() ? ShowTheme::rgb (0xC23B4E) : ShowTheme::rgb (0xD63B52);

    return juce::Desktop::getInstance().isDarkModeActive()
               ? ShowTheme::rgb (0xC23B4E)
               : ShowTheme::rgb (0xD63B52);
}

/** Checkbox phẳng Farrago — không kế thừa drawToggleButton dạng TextButton của app LAF. */
class CleanConfirmDialogLook final : public ShowControlLookAndFeel
{
public:
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        constexpr float tickSize = 16.0f;
        const float tickY = ((float) button.getHeight() - tickSize) * 0.5f;
        const auto tickBounds = juce::Rectangle<float> (0.0f, tickY, tickSize, tickSize);

        const auto textCol    = button.findColour (juce::ToggleButton::textColourId);
        const auto tickCol    = button.findColour (juce::ToggleButton::tickColourId);
        const auto outlineCol = button.findColour (juce::ToggleButton::tickDisabledColourId);

        showcontrol::icons::paintFlatCheckbox (g, tickBounds, button.getToggleState(),
                                               outlineCol, tickCol);

        g.setColour (textCol.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.setFont (ShowTheme::font (13.5f));
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds().withTrimmedLeft (juce::roundToInt (tickSize + 10))
                                                 .withTrimmedRight (2),
                          juce::Justification::centredLeft,
                          1);
    }
};

//==============================================================================
/** Hộp thoại xác nhận xóa — Farrago flat, tràn viền macOS, theme động từ LAF. */
class CleanConfirmationDialog final : public juce::Component
{
public:
    CleanConfirmationDialog (juce::String titleText,
                             juce::String subtextText,
                             juce::String confirmTextIn)
        : dontAskAgainBox { juce::String::fromUTF8 (u8"Đừng hỏi lại lần này") }
    {
        titleLabel.setJustificationType (juce::Justification::topLeft);
        titleLabel.setInterceptsMouseClicks (false, false);

        subtextLabel.setJustificationType (juce::Justification::topLeft);
        subtextLabel.setInterceptsMouseClicks (false, false);

        titleLabel.setText (std::move (titleText), juce::dontSendNotification);
        subtextLabel.setText (std::move (subtextText), juce::dontSendNotification);

        confirmButton.setButtonText (confirmTextIn.isNotEmpty() ? confirmTextIn
                                                                : juce::String::fromUTF8 (u8"Xóa"));
        cancelButton.setButtonText (juce::String::fromUTF8 (u8"Hủy"));

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (subtextLabel);
        addAndMakeVisible (confirmButton);
        addAndMakeVisible (cancelButton);
        addAndMakeVisible (dontAskAgainBox);

        confirmButton.onClick = [this] { closeWithResult (1); };
        cancelButton.onClick  = [this] { closeWithResult (0); };

        setLookAndFeel (&dialogLook);
        lookAndFeelChanged();
        setWantsKeyboardFocus (true);
        setSize (480, 252);
    }

    ~CleanConfirmationDialog() override { setLookAndFeel (nullptr); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            handleCancelOrCloseAction();
            return true;
        }

        return false;
    }

    void handleCancelOrCloseAction()
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    }

    void lookAndFeelChanged() override
    {
        if (auto* parentLaf = dynamic_cast<const ShowControlLookAndFeel*> (&getLookAndFeel()))
            dialogLook.setDarkMode (parentLaf->isDarkMode());
        else if (auto* parent = getParentComponent())
            if (auto* scLaf = dynamic_cast<const ShowControlLookAndFeel*> (&parent->getLookAndFeel()))
                dialogLook.setDarkMode (scLaf->isDarkMode());

        juce::Component::lookAndFeelChanged();
        applyTheme();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (showcontrol::mac::tryDragTopLevelWindowFromMouseDown (*this, e, windowDragger, windowDragActive))
            return;
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
        applyTheme();

        auto bounds = getLocalBounds();

        if (kMacTopDragInset > 0)
            bounds.removeFromTop (kMacTopDragInset);

        auto area = bounds.reduced (kEdgePad);

        // TẦNG ĐÁY — Hủy | Xóa (góc phải)
        {
            auto buttonRow = area.removeFromBottom (32);
            constexpr int buttonW = 90;
            constexpr int buttonGap = 12;

            confirmButton.setBounds (buttonRow.removeFromRight (buttonW));
            buttonRow.removeFromRight (buttonGap);
            cancelButton.setBounds (buttonRow.removeFromRight (buttonW));
        }

        // TẦNG ĐỆM — 12px giữa nút và checkbox
        area.removeFromBottom (12);

        // TẦNG TRÊN NÚT — ô Checkbox tích chọn (không phải TextButton)
        dontAskAgainBox.setBounds (area.removeFromBottom (24).withWidth (250));

        // VÙNG CÒN LẠI — tiêu đề + subtext
        titleLabel.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        subtextLabel.setBounds (area);
    }

    bool shouldSuppressFutureConfirm() const noexcept { return dontAskAgainBox.getToggleState(); }

    void initialiseThemeFrom (const juce::LookAndFeel& parentLaf)
    {
        if (auto* scLaf = dynamic_cast<const ShowControlLookAndFeel*> (&parentLaf))
            dialogLook.setDarkMode (scLaf->isDarkMode());

        applyTheme();
        repaint();
    }

private:
   #if JUCE_MAC
    static constexpr int kMacTopDragInset = 14;
   #else
    static constexpr int kMacTopDragInset = 0;
   #endif
    static constexpr int kEdgePad = 20;

    juce::Label titleLabel, subtextLabel;
    juce::TextButton confirmButton, cancelButton;
    juce::ToggleButton dontAskAgainBox;
    CleanConfirmDialogLook dialogLook;
    juce::ComponentDragger windowDragger;
    bool windowDragActive = false;

    void applyTheme()
    {
        const auto& laf = getLookAndFeel();
        const auto textCol  = laf.findColour (juce::Label::textColourId);
        const auto mutedCol = laf.findColour (ShowControlLookAndFeel::textSecondaryColourId);
        const auto btnSec   = laf.findColour (ShowControlLookAndFeel::panelBackgroundColourId).brighter (0.08f);
        const auto destructive = destructiveActionColour (laf);

        titleLabel.setColour (juce::Label::textColourId, textCol);
        titleLabel.setFont (ShowTheme::fontBold (18.0f));

        subtextLabel.setColour (juce::Label::textColourId, mutedCol);
        subtextLabel.setFont (ShowTheme::font (13.5f));

        cancelButton.setColour (juce::TextButton::buttonColourId, btnSec);
        cancelButton.setColour (juce::TextButton::textColourOffId, textCol);

        confirmButton.setColour (juce::TextButton::buttonColourId, destructive);
        confirmButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

        dontAskAgainBox.setColour (juce::ToggleButton::textColourId, textCol);
        dontAskAgainBox.setColour (juce::ToggleButton::tickColourId,
                                   laf.findColour (ShowControlLookAndFeel::accentColourId));
        dontAskAgainBox.setColour (juce::ToggleButton::tickDisabledColourId,
                                   mutedCol.withMultipliedAlpha (0.85f));
    }

    void closeWithResult (int result)
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (result);
    }
};

inline void showConfirmDeleteDialog (juce::Component* parent,
                                     const juce::String& title,
                                     const juce::String& subtext,
                                     const juce::String& confirmText,
                                     std::function<void (bool)> onDecision,
                                     juce::KeyListener* panicKeyListener = nullptr)
{
    if (deleteConfirmSuppressedFlag())
    {
        if (onDecision)
            onDecision (true);

        return;
    }

    juce::Component* centreTarget = parent;

    if (parent != nullptr)
        if (auto* top = parent->getTopLevelComponent())
            centreTarget = top;

    const auto& laf = parent != nullptr ? parent->getLookAndFeel()
                                        : juce::Desktop::getInstance().getDefaultLookAndFeel();

    juce::DialogWindow::LaunchOptions opts;
    auto* content = new CleanConfirmationDialog (title, subtext, confirmText);

    content->initialiseThemeFrom (laf);

    opts.content.setOwned (content);
    opts.componentToCentreAround = centreTarget;
    opts.dialogTitle             = {};
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar       = true;
    opts.resizable               = false;
    opts.dialogBackgroundColour  = laf.findColour (juce::ResizableWindow::backgroundColourId);

    if (auto* dw = opts.launchAsync())
    {
        dw->setResizable (false, false);
        dw->setUsingNativeTitleBar (true);
        dw->setName ({});
        dw->setColour (juce::ResizableWindow::backgroundColourId,
                       laf.findColour (juce::ResizableWindow::backgroundColourId));
        dw->setSize (content->getWidth(), content->getHeight());

        if (centreTarget != nullptr)
            dw->centreAroundComponent (centreTarget, dw->getWidth(), dw->getHeight());

        content->grabKeyboardFocus();
        dw->grabKeyboardFocus();

        if (panicKeyListener != nullptr)
            dw->addKeyListener (panicKeyListener);

       #if JUCE_MAC
        showcontrol::mac::deferFarragoFullSizeContentView (*dw);
       #endif

        if (auto* mm = juce::ModalComponentManager::getInstance())
        {
            juce::Component::SafePointer<CleanConfirmationDialog> safeContent (content);
            mm->attachCallback (dw,
                juce::ModalCallbackFunction::create ([cb = std::move (onDecision), safeContent] (int result)
                {
                    if (result == 1 && safeContent != nullptr && safeContent->shouldSuppressFutureConfirm())
                        deleteConfirmSuppressedFlag() = true;

                    if (cb)
                        cb (result == 1);
                }));
        }
    }
}
} // namespace showcontrol::ui
