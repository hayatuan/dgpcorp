#pragma once
#include <array>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowOutputRouting.h"

//==============================================================================
/** Lưới 2 cột đặt tên Bus 0–5 — message thread only, không cuộn. */
class AudioBusNamingList : public juce::Component
{
public:
    static constexpr int kNumBuses      = 6;
    static constexpr int kRowsPerColumn = 3;

    std::function<void (int busIndex, const juce::String& text)> onBusNameLiveChanged;

    AudioBusNamingList() = default;

    void setDarkMode (bool dark) noexcept
    {
        if (isDark == dark)
            return;

        isDark = dark;
        applyRowTheme();
        repaint();
    }

    void setPreferencesChrome (bool enabled) noexcept
    {
        if (preferencesChrome == enabled)
            return;

        preferencesChrome = enabled;
        applyRowTheme();
        repaint();
    }

    void initialiseRows (const juce::StringArray& busNames)
    {
        for (int i = 0; i < kNumBuses; ++i)
        {
            indexLabels[i].setText ("Bus " + juce::String (i) + ":", juce::dontSendNotification);
            indexLabels[i].setFont (ShowTheme::timerFont (11.5f, true));
            indexLabels[i].setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (indexLabels[i]);

            const auto defaultName = showcontrol::routing::getBusDisplayName (i);
            const juce::String saved = (i < busNames.size()) ? busNames[i].trim() : juce::String();

            if (saved.isNotEmpty())
                nameEdits[i].setText (saved, juce::dontSendNotification);
            else
                nameEdits[i].setText ({}, juce::dontSendNotification);

            nameEdits[i].setFont (ShowTheme::font (12.5f));
            nameEdits[i].setJustification (juce::Justification::centredLeft);
            nameEdits[i].setIndents (6, 3);
            nameEdits[i].setBorder (juce::BorderSize<int> (1));
            nameEdits[i].setTextToShowWhenEmpty (defaultName, juce::Colours::grey);
            nameEdits[i].onTextChange = [this, i]
            {
                if (onBusNameLiveChanged != nullptr)
                    onBusNameLiveChanged (i, nameEdits[i].getText());
            };
            addAndMakeVisible (nameEdits[i]);
        }

        applyRowTheme();
    }

    /** Ép màu thủ công — không phụ thuộc luồng lookAndFeelChanged tự động. */
    void applyThemeColoursDirectly (const juce::LookAndFeel& lf)
    {
        if (auto* showLaf = dynamic_cast<const ShowControlLookAndFeel*> (&lf))
            isDark = showLaf->isDarkMode();

        const auto labelCol = preferencesChrome
            ? lf.findColour (ShowControlLookAndFeel::textSecondaryColourId)
            : ShowTheme::get (isDark).textSecondary;

        for (int i = 0; i < kNumBuses; ++i)
            indexLabels[i].setColour (juce::Label::textColourId, labelCol);

        pushBusEditorColours (lf);
        repaint();
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        applyThemeColoursDirectly (resolveLookAndFeel());
    }

    void applyRowTheme()
    {
        const auto pal = ShowTheme::get (isDark);
        const auto& laf = resolveLookAndFeel();

        const auto labelCol = preferencesChrome
            ? laf.findColour (ShowControlLookAndFeel::textSecondaryColourId)
            : pal.textSecondary;

        for (int i = 0; i < kNumBuses; ++i)
            indexLabels[i].setColour (juce::Label::textColourId, labelCol);

        pushBusEditorColours (resolveLookAndFeel());
    }

    void pushBusEditorColours (const juce::LookAndFeel& lf)
    {
        const auto pal = ShowTheme::get (isDark);

        const auto textColour      = lf.findColour (juce::TextEditor::textColourId);
        const auto lafBgColour     = lf.findColour (juce::TextEditor::backgroundColourId);
        const auto highlightColour = lf.findColour (juce::TextEditor::highlightColourId);
        const auto outlineColour   = lf.findColour (juce::TextEditor::outlineColourId);
        const auto focusedOutline  = preferencesChrome ? juce::Colour (0xff9b51e0)
                                                       : lf.findColour (juce::TextEditor::focusedOutlineColourId);
        const auto caretColour     = lf.findColour (juce::CaretComponent::caretColourId);
        const auto placeholderCol  = textColour.withAlpha (0.40f);

        const auto chromeRowA = lf.findColour (juce::ListBox::backgroundColourId);
        const auto chromeRowB = lf.findColour (ShowControlLookAndFeel::panelBackgroundColourId).brighter (0.04f);

        juce::TextEditor* editors[kNumBuses] = {
            &nameEdits[0], &nameEdits[1], &nameEdits[2],
            &nameEdits[3], &nameEdits[4], &nameEdits[5]
        };

        for (int i = 0; i < kNumBuses; ++i)
        {
            auto* editor = editors[i];
            if (editor == nullptr)
                continue;

            editor->removeColour (juce::TextEditor::textColourId);
            editor->removeColour (juce::TextEditor::backgroundColourId);
            editor->removeColour (juce::TextEditor::outlineColourId);
            editor->removeColour (juce::TextEditor::focusedOutlineColourId);
            editor->removeColour (juce::TextEditor::highlightColourId);
            editor->removeColour (juce::TextEditor::highlightedTextColourId);
            editor->removeColour (juce::CaretComponent::caretColourId);

            const auto rowBg = preferencesChrome
                ? (((i & 1) == 0) ? chromeRowA : chromeRowB)
                : (((i & 1) == 0) ? pal.listRowBg : pal.panelElevated);
            const auto bgColour = preferencesChrome ? rowBg : lafBgColour;

            editor->setColour (juce::TextEditor::textColourId, textColour);
            editor->setColour (juce::TextEditor::backgroundColourId, bgColour);
            editor->setColour (juce::TextEditor::highlightColourId, highlightColour);
            editor->setColour (juce::TextEditor::highlightedTextColourId, textColour);
            editor->setColour (juce::TextEditor::outlineColourId, outlineColour);
            editor->setColour (juce::TextEditor::focusedOutlineColourId, focusedOutline);
            editor->setColour (juce::CaretComponent::caretColourId, caretColour);

            editor->setTextToShowWhenEmpty (showcontrol::routing::getBusDisplayName (i), placeholderCol);
            editor->repaint();
        }
    }

    juce::StringArray getBusNames() const
    {
        juce::StringArray names;

        for (int i = 0; i < kNumBuses; ++i)
        {
            auto t = nameEdits[i].getText().trim();
            names.add (t.isNotEmpty() ? t : showcontrol::routing::getBusDisplayName (i));
        }

        return names;
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDark);
        const auto& laf = (getParentComponent() != nullptr)
                              ? getParentComponent()->getLookAndFeel()
                              : juce::LookAndFeel::getDefaultLookAndFeel();
        const auto rowA = preferencesChrome ? laf.findColour (juce::ListBox::backgroundColourId) : pal.listRowBg;
        const auto rowB = preferencesChrome
            ? laf.findColour (ShowControlLookAndFeel::panelBackgroundColourId).brighter (0.04f)
            : pal.panelElevated;

        for (int i = 0; i < kNumBuses; ++i)
        {
            if (rowBounds[i].isEmpty())
                continue;

            g.setColour ((i & 1) == 0 ? rowA : rowB);
            g.fillRect (rowBounds[i]);
        }
    }

    void resized() override
    {
        constexpr int rowH    = 34;
        constexpr int rowGap  = 5;
        constexpr int colGap  = 14;
        constexpr int sidePad = 6;
        constexpr int labelW  = 52;

        const int w = getWidth();
        const int colW = (w - sidePad * 2 - colGap) / 2;
        const int leftX  = sidePad;
        const int rightX = leftX + colW + colGap;

        rowBounds.fill ({});

        // Lưới 2 cột: trái Bus 0–2, phải Bus 3–5
        for (int row = 0; row < kRowsPerColumn; ++row)
        {
            const int y = sidePad + row * (rowH + rowGap);

            for (int col = 0; col < 2; ++col)
            {
                const int busIndex = row + col * kRowsPerColumn;
                const int x = (col == 0) ? leftX : rightX;

                juce::Rectangle<int> rowRect (x, y, colW, rowH);
                rowBounds[busIndex] = rowRect;

                auto labelArea = rowRect.removeFromLeft (labelW).reduced (0, 4);
                rowRect.removeFromLeft (6);
                indexLabels[busIndex].setBounds (labelArea);
                nameEdits[busIndex].setBounds (rowRect.reduced (0, 4));
            }
        }

        const int totalH = sidePad + kRowsPerColumn * rowH + (kRowsPerColumn - 1) * rowGap + sidePad;
        setSize (w, totalH);
    }

    std::array<juce::Label, kNumBuses> indexLabels;
    std::array<juce::TextEditor, kNumBuses> nameEdits;

private:
    bool isDark = true;
    bool preferencesChrome = false;
    std::array<juce::Rectangle<int>, kNumBuses> rowBounds;

    const juce::LookAndFeel& resolveLookAndFeel() const noexcept
    {
        if (getParentComponent() != nullptr)
            return getParentComponent()->getLookAndFeel();

        return juce::LookAndFeel::getDefaultLookAndFeel();
    }

    void syncDarkModeFromLookAndFeel() noexcept
    {
        const auto& laf = resolveLookAndFeel();

        if (auto* showLaf = dynamic_cast<const ShowControlLookAndFeel*> (&laf))
            isDark = showLaf->isDarkMode();
    }
};

//==============================================================================
/** ⚙ Audio: thiết bị + đặt tên output bus (message thread only). */
class AudioDeviceSettingsPanel : public juce::Component
{
public:
    static constexpr int kNumBuses = AudioBusNamingList::kNumBuses;

    AudioDeviceSettingsPanel (juce::AudioDeviceManager& deviceManager,
                              bool darkMode,
                              const juce::StringArray& busNames,
                              bool embeddedInPreferences = false)
        : isDark (darkMode),
          embeddedInPreferences (embeddedInPreferences)
    {
        audioGroup.setText (juce::String::fromUTF8 (u8"Cấu hình Audio"));
        audioGroup.setTextLabelPosition (juce::Justification::centredLeft);
        addAndMakeVisible (audioGroup);

        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager, 0, 0, 2, 16, false, false, false, false);
        addAndMakeVisible (*deviceSelector);

        busGroup.setText (juce::String::fromUTF8 (u8"Đặt tên Output Bus"));
        busGroup.setTextLabelPosition (juce::Justification::centredLeft);
        addAndMakeVisible (busGroup);

        busList.setDarkMode (isDark);
        busList.setPreferencesChrome (embeddedInPreferences);
        busList.initialiseRows (busNames);
        addAndMakeVisible (busList);

        okBtn.setButtonText (juce::String::fromUTF8 (u8"Đóng"));
        addAndMakeVisible (okBtn);
        okBtn.onClick = [this] { closeAndApply(); };
        okBtn.setVisible (! embeddedInPreferences);

        applyTheme();
        setWantsKeyboardFocus (true);
        setSize (540, embeddedInPreferences ? 460 : 500);
    }

    void setDarkMode (bool dark) noexcept
    {
        if (isDark == dark)
            return;

        isDark = dark;
        applyTheme();
        repaint();
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

    struct ApplyResult
    {
        juce::StringArray busNames;
    };

    std::function<void (const ApplyResult&)> onApplied;

    /** Gán callback live — chỉ message thread, không chạm audio callback. */
    void setOnBusNameLiveChanged (std::function<void (int busIndex, const juce::String& text)> handler)
    {
        onBusNameLiveChanged = std::move (handler);
        busList.onBusNameLiveChanged = [this] (int busIndex, const juce::String& text)
        {
            if (onBusNameLiveChanged != nullptr)
                onBusNameLiveChanged (busIndex, text);
        };
    }

    void parentHierarchyChanged() override
    {
        juce::Component::parentHierarchyChanged();

        if (getParentComponent() == nullptr && ! hasAppliedOnClose)
            closeAndApply();
    }

    juce::StringArray getBusNames() const
    {
        return busList.getBusNames();
    }

    /** Manual Push — ép màu Bus 0–5 và nhãn phân khu ngay lập tức. */
    void applyThemeColoursDirectly()
    {
        auto& lf = getLookAndFeel();

        if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&lf))
            isDark = showLaf->isDarkMode();

        const auto groupText    = lf.findColour (juce::Label::textColourId);
        const auto groupOutline = lf.findColour (ShowControlLookAndFeel::panelBorderColourId);

        for (auto* group : { &audioGroup, &busGroup })
        {
            group->setColour (juce::GroupComponent::textColourId, groupText);
            group->setColour (juce::GroupComponent::outlineColourId, groupOutline);
            group->repaint();
        }

        busList.setPreferencesChrome (embeddedInPreferences);
        busList.applyThemeColoursDirectly (lf);
        repaint();
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        applyThemeColoursDirectly();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (embeddedInPreferences
                       ? getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId)
                       : ShowTheme::get (isDark).windowBg);
    }

    void resized() override
    {
        constexpr int outerPad   = 12;
        constexpr int sectionGap = 10;
        const int footerH = embeddedInPreferences ? 0 : 44;

        auto bounds = getLocalBounds().reduced (outerPad);

        if (! embeddedInPreferences)
        {
            auto footer = bounds.removeFromBottom (footerH);
            okBtn.setBounds (footer.removeFromRight (108).withHeight (32).reduced (0, 4));
        }

        const int audioH = juce::jmax (210, (int) (bounds.getHeight() * 0.52f));
        audioGroup.setBounds (bounds.removeFromTop (audioH));
        bounds.removeFromTop (sectionGap);

        busGroup.setBounds (bounds);

        auto audioInner = audioGroup.getBounds().reduced (10, 22);
        deviceSelector->setBounds (audioInner);

        auto busInner = busGroup.getBounds().reduced (10, 22);
        busList.setBounds (busInner);
        busList.setSize (busInner.getWidth(), 1);
        busList.resized();
    }

private:
    bool isDark = true;
    bool embeddedInPreferences = false;
    bool hasAppliedOnClose = false;

    juce::GroupComponent audioGroup, busGroup;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::TextButton okBtn;
    AudioBusNamingList busList;
    std::function<void (int busIndex, const juce::String& text)> onBusNameLiveChanged;

    void applyTheme()
    {
        if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
            isDark = showLaf->isDarkMode();

        const auto pal = ShowTheme::get (isDark);
        const auto& laf = getLookAndFeel();
        const auto groupText = embeddedInPreferences
            ? laf.findColour (juce::Label::textColourId)
            : pal.textPrimary;
        const auto groupOutline = embeddedInPreferences
            ? laf.findColour (ShowControlLookAndFeel::panelBorderColourId)
            : pal.border;

        for (auto* group : { &audioGroup, &busGroup })
        {
            group->setColour (juce::GroupComponent::textColourId, groupText);
            group->setColour (juce::GroupComponent::outlineColourId, groupOutline);
        }

        okBtn.setColour (juce::TextButton::buttonColourId, pal.buttonSecondary);
        okBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);

        busList.setDarkMode (isDark);
        busList.setPreferencesChrome (embeddedInPreferences);
        busList.applyRowTheme();
    }

    void closeAndApply()
    {
        if (hasAppliedOnClose)
            return;

        hasAppliedOnClose = true;

        if (onApplied)
        {
            ApplyResult r;
            r.busNames = getBusNames();
            onApplied (r);
        }

        if (! embeddedInPreferences)
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (1);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceSettingsPanel)
};
