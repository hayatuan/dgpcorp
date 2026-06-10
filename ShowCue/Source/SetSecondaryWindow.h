#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "CueListPanel.h"
#include "ShowTheme.h"

/** Cửa sổ phụ hiển thị Set — chỉ UI/message thread, không đụng audio callback. */
class SetSecondaryWindow final : public juce::DocumentWindow
{
public:
    SetSecondaryWindow (int listIdx,
                        const juce::String& title,
                        const juce::Array<CueItem>& cues,
                        bool isDark,
                        std::function<void (int cueIndex)> onCueTriggeredCallback,
                        std::function<void (SetSecondaryWindow*)> onWindowClosed)
        : DocumentWindow (title,
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                      .findColour (backgroundColourId),
                          DocumentWindow::closeButton),
          listIndex (listIdx),
          closedCallback (std::move (onWindowClosed))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);

        auto* panel = new CueListPanel();
        panel->updateTheme (isDark);
        panel->setCues (cues);
        panel->onCueTriggered = std::move (onCueTriggeredCallback);
        panel->onCueSelected = [panel] (int idx) { panel->setSelectedIndex (idx); };

        setContentOwned (panel, true);
        setSize (420, 520);
        centreAroundComponent (nullptr, getWidth(), getHeight());
    }

    void closeButtonPressed() override
    {
        if (closedCallback)
            closedCallback (this);

        setVisible (false);
    }

    int getListIndex() const noexcept { return listIndex; }

private:
    int listIndex = -1;
    std::function<void (SetSecondaryWindow*)> closedCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetSecondaryWindow)
};
