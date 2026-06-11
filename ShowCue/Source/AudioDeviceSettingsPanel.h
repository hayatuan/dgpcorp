#pragma once
#include <array>
#include <functional>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowOutputRouting.h"
#include "ShowLocalization.h"
#include "ShowGraphicsSafe.h"

//==============================================================================
/** TextEditor Bus — Enter lưu & đóng, Escape hủy đóng. */
class BusNameTextEditor : public juce::TextEditor
{
public:
    std::function<void()> onEnterPressed;
    std::function<void()> onEscapePressed;

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            if (onEnterPressed != nullptr)
                onEnterPressed();

            return true;
        }

        if (key == juce::KeyPress::escapeKey)
        {
            if (onEscapePressed != nullptr)
                onEscapePressed();

            return true;
        }

        return juce::TextEditor::keyPressed (key);
    }
};

//==============================================================================
/** Lưới 2 cột đặt tên Bus 0–5 — message thread only, không cuộn. */
class AudioBusNamingList : public juce::Component,
                           public juce::SettableTooltipClient
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

    void refreshLocalizedPlaceholders()
    {
        const auto placeholderCol = resolveLookAndFeel()
                                        .findColour (juce::Label::textColourId)
                                        .withAlpha (0.40f);

        for (int i = 0; i < kNumBuses; ++i)
            nameEdits[i].setTextToShowWhenEmpty (showcontrol::routing::getBusDisplayName (i),
                                                 placeholderCol);
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
            nameEdits[i].setIndents (8, 5);
            nameEdits[i].setBorder (juce::BorderSize<int> (1));
            nameEdits[i].setReturnKeyStartsNewLine (false);
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
        const auto focusedOutline  = lf.findColour (juce::TextEditor::focusedOutlineColourId);
        const auto caretColour     = lf.findColour (juce::CaretComponent::caretColourId);
        const auto placeholderCol  = textColour.withAlpha (0.40f);

        const auto chromeRowA = lf.findColour (juce::ListBox::backgroundColourId);
        const auto chromeRowB = lf.findColour (ShowControlLookAndFeel::panelBackgroundColourId).brighter (0.04f);

        BusNameTextEditor* editors[kNumBuses] = {
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

    void setBusNames (const juce::StringArray& busNames)
    {
        for (int i = 0; i < kNumBuses; ++i)
        {
            const juce::String saved = (i < busNames.size()) ? busNames[i].trim() : juce::String();
            nameEdits[i].setText (saved, juce::dontSendNotification);
        }

        applyRowTheme();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& laf = resolveLookAndFeel();
        syncDarkModeFromLookAndFeel();
        const auto pal = ShowTheme::get (isDark);
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

    BusNameTextEditor* getEditor (int index) noexcept
    {
        if (index < 0 || index >= kNumBuses)
            return nullptr;

        return &nameEdits[index];
    }

    void resized() override
    {
        constexpr int rowH    = 36;
        constexpr int rowGap  = 8;
        constexpr int colGap  = 16;
        constexpr int sidePad = 4;
        constexpr int labelW  = 56;

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
    std::array<BusNameTextEditor, kNumBuses> nameEdits;

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
/** Thẻ popup đặt tên Bus — bo góc, viền Brand Blue, nút [X]. */
class OutputBusNamingCard : public juce::Component
{
public:
    std::function<void (bool saveChanges)> onRequestDismiss;
    AudioBusNamingList busList;

    OutputBusNamingCard()
    {
        setOpaque (false);

        titleLabel.setText (showcontrol::localization::tr (u8"ĐẶT TÊN OUTPUT BUS (OUTPUT ROUTING)"),
                            juce::dontSendNotification);
        titleLabel.setFont (ShowTheme::fontBold (11.5f));
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel);

        closeButton.setButtonText ("X");
        closeButton.setTooltip (showcontrol::localization::tr (u8"Đóng"));
        closeButton.onClick = [this] { requestDismiss (true); };
        addAndMakeVisible (closeButton);

        addAndMakeVisible (busList);
    }

    void setDarkMode (bool dark) noexcept
    {
        isDark = dark;
        busList.setDarkMode (dark);
        repaint();
    }

    void applyTheme (const juce::LookAndFeel& lf)
    {
        if (auto* showLaf = dynamic_cast<const ShowControlLookAndFeel*> (&lf))
            isDark = showLaf->isDarkMode();

        const auto pal = ShowTheme::get (isDark);
        titleLabel.setColour (juce::Label::textColourId, pal.textSecondary);
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        closeButton.setColour (juce::TextButton::textColourOffId, pal.textSecondary);
        busList.applyThemeColoursDirectly (lf);
        repaint();
    }

    void wireEditorDismiss (std::function<void (bool)> dismissHandler)
    {
        onRequestDismiss = std::move (dismissHandler);

        for (int i = 0; i < AudioBusNamingList::kNumBuses; ++i)
        {
            if (auto* editor = busList.getEditor (i))
            {
                editor->onEnterPressed = [this] { requestDismiss (true); };
                editor->onEscapePressed = [this] { requestDismiss (false); };
            }
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            requestDismiss (false);
            return true;
        }

        return false;
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDark);
        const auto bounds = getLocalBounds().toFloat();
        const auto fill = isDark ? pal.panelElevated : juce::Colours::white;
        const auto border = ShowTheme::rgb (0x007FFF);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (border.withAlpha (0.85f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.25f);
    }

    void resized() override
    {
        constexpr int margin = 16;
        constexpr int titleH = 28;
        constexpr int closeSize = 24;

        auto area = getLocalBounds().reduced (margin);
        auto titleRow = area.removeFromTop (titleH);
        closeButton.setBounds (titleRow.removeFromRight (closeSize).withSizeKeepingCentre (closeSize, closeSize));
        titleRow.removeFromRight (8);
        titleLabel.setBounds (titleRow);

        area.removeFromTop (10);
        busList.setBounds (area);
        busList.resized();
    }

private:
    bool isDark = true;
    juce::Label titleLabel;
    juce::TextButton closeButton;

    void requestDismiss (bool saveChanges)
    {
        if (onRequestDismiss != nullptr)
            onRequestDismiss (saveChanges);
    }
};

//==============================================================================
/** Lớp phủ mờ + auto-dismiss khi click ngoài thẻ Bus. */
class OutputBusNamingOverlay : public juce::Component
{
public:
    struct Config
    {
        juce::StringArray busNames;
        bool darkMode = true;
        std::function<void (int busIndex, const juce::String& text)> onLiveChanged;
        std::function<void (const juce::StringArray& busNames)> onApplied;
    };

    /** Đóng overlay đang mở — gọi trước khi thoát app để tránh leak Desktop. */
    static void dismissActive (bool saveChanges = false) noexcept
    {
        if (activeOverlay == nullptr)
            return;

        auto& overlay = *activeOverlay;

        if (saveChanges && overlay.cfg.onApplied != nullptr)
            overlay.cfg.onApplied (overlay.card.busList.getBusNames());

        overlay.setVisible (false);

        if (auto* parent = overlay.getParentComponent())
            parent->removeChildComponent (&overlay);

        activeOverlay.reset();
    }

    static void present (juce::Component* parent, Config config)
    {
        if (parent == nullptr)
            return;

        dismissActive (true);

        activeOverlay.reset (new OutputBusNamingOverlay (std::move (config), *parent));
        parent->addAndMakeVisible (*activeOverlay);
        activeOverlay->setBounds (parent->getLocalBounds());
        activeOverlay->toFront (true);
        activeOverlay->grabKeyboardFocus();
    }

    ~OutputBusNamingOverlay() override = default;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.42f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getEventRelativeTo (this).getPosition();

        if (! card.getBounds().contains (pos))
            dismiss (true);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            dismiss (false);
            return true;
        }

        return false;
    }

    void inputAttemptWhenModal() override
    {
        dismiss (true);
    }

private:
    inline static std::unique_ptr<OutputBusNamingOverlay> activeOverlay;

    Config cfg;
    OutputBusNamingCard card;
    bool isDismissing = false;

    OutputBusNamingOverlay (Config configIn, juce::Component& themeSource)
        : cfg (std::move (configIn))
    {
        addAndMakeVisible (card);
        card.setDarkMode (cfg.darkMode);
        card.busList.setPreferencesChrome (true);
        card.busList.initialiseRows (cfg.busNames);
        card.busList.onBusNameLiveChanged = cfg.onLiveChanged;
        card.applyTheme (themeSource.getLookAndFeel());
        card.wireEditorDismiss ([this] (bool save) { dismiss (save); });

        setWantsKeyboardFocus (true);
        setInterceptsMouseClicks (true, true);
    }

    void resized() override
    {
        const int cardW = juce::jmin (520, juce::jmax (360, getWidth() - 48));
        const int cardH = juce::jmin (420, juce::jmax (300, getHeight() - 64));
        card.setBounds (getLocalBounds().withSizeKeepingCentre (cardW, cardH));
    }

    void dismiss (bool saveChanges)
    {
        if (isDismissing || activeOverlay.get() != this)
            return;

        isDismissing = true;

        if (saveChanges && cfg.onApplied != nullptr)
            cfg.onApplied (card.busList.getBusNames());

        setVisible (false);

        if (auto* parent = getParentComponent())
            parent->removeChildComponent (this);

        activeOverlay.reset();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputBusNamingOverlay)
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
        audioGroup.setText (showcontrol::localization::tr (u8"Cấu hình Audio"));
        audioGroup.setTextLabelPosition (juce::Justification::centredLeft);
        addAndMakeVisible (audioGroup);

        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager, 0, 0, 2, 16, false, false, false, false);
        addAndMakeVisible (*deviceSelector);

        busNamingBtn.setButtonText (showcontrol::localization::tr (u8"Đặt tên Output Bus"));
        busNamingBtn.setTooltip (showcontrol::localization::tr (u8"Cấu hình đường Bus"));
        busNamingBtn.onClick = [this] { presentBusNamingPopup(); };
        addAndMakeVisible (busNamingBtn);

        busList.setDarkMode (isDark);
        busList.setPreferencesChrome (embeddedInPreferences);
        busList.initialiseRows (busNames);

        okBtn.setButtonText (showcontrol::localization::tr (u8"Đóng"));
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
            closeParentDialogWindow (0);
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

        audioGroup.setColour (juce::GroupComponent::textColourId, groupText);
        audioGroup.setColour (juce::GroupComponent::outlineColourId, groupOutline);
        audioGroup.repaint();

        busList.setPreferencesChrome (embeddedInPreferences);
        busList.applyThemeColoursDirectly (lf);
        repaint();
    }

    void refreshLocalizedText()
    {
        audioGroup.setText (showcontrol::localization::tr (u8"Cấu hình Audio"));
        busNamingBtn.setButtonText (showcontrol::localization::tr (u8"Đặt tên Output Bus"));
        busNamingBtn.setTooltip (showcontrol::localization::tr (u8"Cấu hình đường Bus"));
        okBtn.setButtonText (showcontrol::localization::tr (u8"Đóng"));
        busList.refreshLocalizedPlaceholders();
        repaint();
    }

    void lookAndFeelChanged() override
    {
        if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
            isDark = showLaf->isDarkMode();

        applyThemeColoursDirectly();
        juce::Component::lookAndFeelChanged();
        refreshLocalizedText();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
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

        constexpr int busBtnH = 30;
        audioGroup.setBounds (bounds.removeFromTop (bounds.getHeight() - busBtnH - sectionGap));
        bounds.removeFromTop (sectionGap);
        busNamingBtn.setBounds (bounds.removeFromTop (busBtnH));

        auto audioInner = audioGroup.getBounds().reduced (10, 22);
        deviceSelector->setBounds (audioInner);
    }

private:
    bool isDark = true;
    bool embeddedInPreferences = false;
    bool hasAppliedOnClose = false;

    juce::GroupComponent audioGroup;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::TextButton okBtn;
    juce::TextButton busNamingBtn;
    AudioBusNamingList busList;
    std::function<void (int busIndex, const juce::String& text)> onBusNameLiveChanged;

    void presentBusNamingPopup()
    {
        auto* parent = getTopLevelComponent();
        if (parent == nullptr)
            return;

        OutputBusNamingOverlay::Config cfg;
        cfg.busNames = getBusNames();
        cfg.darkMode = isDark;
        cfg.onLiveChanged = [this] (int busIndex, const juce::String& text)
        {
            if (auto* editor = busList.getEditor (busIndex))
                editor->setText (text, juce::dontSendNotification);

            if (onBusNameLiveChanged != nullptr)
                onBusNameLiveChanged (busIndex, text);
        };
        cfg.onApplied = [this] (const juce::StringArray& names)
        {
            busList.setBusNames (names);

            if (onApplied)
            {
                ApplyResult r;
                r.busNames = names;
                onApplied (r);
            }
        };

        OutputBusNamingOverlay::present (parent, std::move (cfg));
    }

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

        audioGroup.setColour (juce::GroupComponent::textColourId, groupText);
        audioGroup.setColour (juce::GroupComponent::outlineColourId, groupOutline);

        okBtn.setColour (juce::TextButton::buttonColourId, pal.buttonSecondary);
        okBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
        busNamingBtn.setColour (juce::TextButton::buttonColourId, pal.buttonSecondary);
        busNamingBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);

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
            closeParentDialogWindow (1);
    }

    static void closeParentDialogWindow (juce::Component& source, int result) noexcept
    {
        if (auto* dw = source.findParentComponentOfClass<juce::DialogWindow>())
        {
            dw->exitModalState (result);
            dw->setVisible (false);
        }
    }

    void closeParentDialogWindow (int result) noexcept
    {
        closeParentDialogWindow (*this, result);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceSettingsPanel)
};
