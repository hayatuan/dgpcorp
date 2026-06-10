#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HotkeyManager.h"
#include "ShowTheme.h"

//==============================================================================
/** Dialog gán phím: chế độ Hệ thống (nhấn phím) hoặc Bố cục mặc định (combo). */
class HotkeyAssignDialogContent : public juce::Component
{
public:
    enum class AssignMode { systemKey = 0, defaultMatrix = 1 };

    HotkeyAssignDialogContent (bool darkMode,
                               HotkeyManager& manager,
                               int listIndex,
                               int padIndex,
                               int padSlotIndex,
                               const juce::String& padName,
                               bool globalHotkeyScope,
                               int activeListIndex)
        : isDark (darkMode),
          hotkeys (manager),
          listIdx (listIndex),
          padIdx (padIndex),
          slotIdx (padSlotIndex),
          padTitle (padName),
          globalScope (globalHotkeyScope),
          activeList (activeListIndex)
    {
        modeLabel.setText (juce::String::fromUTF8 (u8"Chế độ gán"), juce::dontSendNotification);
        modeLabel.setFont (ShowTheme::fontBold (11.0f));
        addAndMakeVisible (modeLabel);

        modeCombo.addItem (juce::String::fromUTF8 (u8"Phím hệ thống (nhấn để gán)"), 1);
        modeCombo.addItem (juce::String::fromUTF8 (u8"Bố cục mặc định (1, Q, F1…)"), 2);
        modeCombo.setSelectedId (1, juce::dontSendNotification);
        addAndMakeVisible (modeCombo);
        modeCombo.onChange = [this] { refreshModeUi(); };

        captureLabel.setText (juce::String::fromUTF8 (u8"Nhấn tổ hợp phím…"), juce::dontSendNotification);
        captureLabel.setFont (ShowTheme::font (12.0f));
        captureLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (captureLabel);

        matrixCombo.addItem (juce::String::fromUTF8 (u8"— Chọn phím —"), 1);
        const juce::String keyMatrix = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
        for (int i = 0; i < keyMatrix.length(); ++i)
            matrixCombo.addItem (juce::String::charToString (keyMatrix[i]), i + 2);
        for (int f = 0; f < 8; ++f)
            matrixCombo.addItem ("F" + juce::String (f + 1), (int) keyMatrix.length() + f + 2);
        addAndMakeVisible (matrixCombo);
        matrixCombo.onChange = [this]
        {
            if (getAssignMode() == AssignMode::defaultMatrix)
            {
                const int matrixIndex = juce::jmax (0, matrixCombo.getSelectedId() - 2);
                pendingKey = HotkeyManager::keyPressForMatrixIndex (matrixIndex);
            }
            updateConflictLabel();
        };

        statusLabel.setFont (ShowTheme::font (11.0f));
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (statusLabel);

        conflictLabel.setFont (ShowTheme::font (11.0f));
        conflictLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (conflictLabel);

        okBtn.setButtonText (juce::String::fromUTF8 (u8"Lưu"));
        cancelBtn.setButtonText (juce::String::fromUTF8 (u8"Hủy"));
        clearBtn.setButtonText (juce::String::fromUTF8 (u8"Xóa gán"));
        addAndMakeVisible (okBtn);
        addAndMakeVisible (cancelBtn);
        addAndMakeVisible (clearBtn);

        okBtn.onClick = [this] { tryApplyAndClose (true); };
        cancelBtn.onClick = [this] { tryApplyAndClose (false); };
        clearBtn.onClick = [this]
        {
            pendingKey = juce::KeyPress();
            hotkeys.clearKeyboardBindingForPad (listIdx, padIdx);
            statusLabel.setText (juce::String::fromUTF8 (u8"Đã xóa — pad dùng phím mặc định khi load list."), juce::dontSendNotification);
            conflictLabel.setText ({}, juce::dontSendNotification);
        };

        if (const auto* existing = hotkeys.findKeyboardBindingForPad (listIdx, padIdx))
        {
            pendingKey = existing->keyPress;
            statusLabel.setText (juce::String::fromUTF8 (u8"Hiện tại: ") + HotkeyManager::describeKeyPress (pendingKey),
                                 juce::dontSendNotification);
        }
        else
        {
            pendingKey = HotkeyManager::keyPressForMatrixIndex (slotIdx);
            statusLabel.setText (juce::String::fromUTF8 (u8"Mặc định: ") + HotkeyManager::describeKeyPress (pendingKey),
                                 juce::dontSendNotification);
        }

        setWantsKeyboardFocus (true);
        grabKeyboardFocus();
        refreshModeUi();
        updateConflictLabel();
    }

    std::function<void(bool applied)> onFinished;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ShowTheme::get (isDark).panelBg);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (16);
        modeLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (4);
        modeCombo.setBounds (b.removeFromTop (26));
        b.removeFromTop (8);
        captureLabel.setBounds (b.removeFromTop (32));
        matrixCombo.setBounds (b.removeFromTop (26));
        b.removeFromTop (8);
        statusLabel.setBounds (b.removeFromTop (18));
        conflictLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (12);
        auto btnRow = b.removeFromBottom (30);
        clearBtn.setBounds (btnRow.removeFromLeft (80));
        btnRow.removeFromLeft (8);
        cancelBtn.setBounds (btnRow.removeFromRight (72));
        btnRow.removeFromRight (8);
        okBtn.setBounds (btnRow.removeFromRight (72));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (getAssignMode() != AssignMode::systemKey)
            return false;

        if (key == juce::KeyPress::escapeKey)
            return false;

        if (key.getKeyCode() == 0)
            return true;

        pendingKey = key;
        captureLabel.setText (HotkeyManager::describeKeyPress (key), juce::dontSendNotification);
        updateConflictLabel();
        return true;
    }

private:
    bool isDark = true;
    HotkeyManager& hotkeys;
    int listIdx = 0, padIdx = 0, slotIdx = 0;
    juce::String padTitle;
    bool globalScope = false;
    int activeList = 0;
    juce::KeyPress pendingKey;

    juce::Label modeLabel, captureLabel, statusLabel, conflictLabel;
    juce::ComboBox modeCombo, matrixCombo;
    juce::TextButton okBtn, cancelBtn, clearBtn;

    AssignMode getAssignMode() const
    {
        return modeCombo.getSelectedId() == 2 ? AssignMode::defaultMatrix : AssignMode::systemKey;
    }

    void refreshModeUi()
    {
        const bool system = (getAssignMode() == AssignMode::systemKey);
        captureLabel.setVisible (system);
        matrixCombo.setVisible (! system);
        if (! system)
        {
            const int matrixIndex = juce::jmax (0, matrixCombo.getSelectedId() - 2);
            pendingKey = HotkeyManager::keyPressForMatrixIndex (matrixIndex);
        }
        updateConflictLabel();
    }

    void updateConflictLabel()
    {
        juce::String msg;
        if (const auto* c = hotkeys.findConflictingKey (pendingKey, globalScope, activeList, listIdx, padIdx))
            msg = juce::String::fromUTF8 (u8"⚠ Trùng: ") + c->description;
        conflictLabel.setText (msg, juce::dontSendNotification);
        conflictLabel.setColour (juce::Label::textColourId, msg.isNotEmpty()
            ? ShowTheme::get (isDark).danger
            : ShowTheme::get (isDark).success);
    }

    void tryApplyAndClose (bool apply)
    {
        if (apply && pendingKey.getKeyCode() != 0)
        {
            juce::String err;
            if (! hotkeys.assignKeyboardToPad (pendingKey, listIdx, padIdx, padTitle, globalScope, activeList, &err))
            {
                conflictLabel.setText (err, juce::dontSendNotification);
                return;
            }
        }

        if (onFinished)
            onFinished (apply);

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (apply ? 1 : 0);
    }
};

namespace showcontrol::ui
{
inline void showHotkeyAssignDialog (juce::Component* parent,
                                    bool darkMode,
                                    HotkeyManager& manager,
                                    int listIndex,
                                    int padIndex,
                                    int padSlotIndex,
                                    const juce::String& padName,
                                    bool globalHotkeyScope,
                                    int activeListIndex,
                                    std::function<void(bool applied)> onDone)
{
    auto* content = new HotkeyAssignDialogContent (darkMode, manager, listIndex, padIndex, padSlotIndex,
                                                    padName, globalHotkeyScope, activeListIndex);
    content->onFinished = std::move (onDone);

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = juce::String::fromUTF8 (u8"Gán phím tắt");
    opt.content.setOwned (content);
    opt.componentToCentreAround = parent;
    opt.dialogBackgroundColour = ShowTheme::get (darkMode).panelBg;
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}
} // namespace showcontrol::ui
