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
#include "ShowAppPreferences.h"
#include "ShowLocalization.h"
#include "ShowGraphicsSafe.h"

namespace
{
void localiseAudioDeviceSelector (juce::Component* root)
{
    if (root == nullptr)
        return;

    struct LabelMap { const char* english; const char* keyUtf8; };
    static constexpr LabelMap maps[] = {
        { "Output:", u8"Đầu ra:" },
        { "Input:", u8"Đầu vào:" },
        { "Sample rate:", u8"Tần số lấy mẫu:" },
        { "Audio buffer size:", u8"Kích thước buffer:" },
        { "Active input channels:", u8"Kênh vào:" },
        { "Active output channels:", u8"Kênh ra:" },
        { "Test:", u8"Thử:" },
        { "Test", u8"Thử" },
    };

    std::function<void (juce::Component*)> walk;
    walk = [&] (juce::Component* c)
    {
        if (auto* lbl = dynamic_cast<juce::Label*> (c))
        {
            for (const auto& m : maps)
            {
                if (lbl->getText() == m.english)
                {
                    lbl->setText (showcontrol::localization::tr (m.keyUtf8), juce::dontSendNotification);
                    break;
                }
            }
        }

        if (auto* btn = dynamic_cast<juce::TextButton*> (c))
        {
            for (const auto& m : maps)
            {
                if (btn->getButtonText() == m.english)
                {
                    btn->setButtonText (showcontrol::localization::tr (m.keyUtf8));
                    break;
                }
            }
        }

        for (int i = 0; i < c->getNumChildComponents(); ++i)
            walk (c->getChildComponent (i));
    };

    walk (root);
}
} // namespace

//==============================================================================
/** LAF phạm vi Cài đặt Audio — Roboto 14.5–15pt cho ComboBox, Label, nút bấm. */
class AudioSettingsPanelLookAndFeel : public ShowControlLookAndFeel
{
public:
    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return showcontrol::audioSettings::comboFont();
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return showcontrol::audioSettings::labelFont();
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        auto col = button.findColour (juce::TextButton::textColourOffId);

        if (! button.isEnabled())
            col = col.withAlpha (0.45f);

        g.setColour (col);
        g.setFont (showcontrol::audioSettings::buttonFont());
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }

    void drawGroupComponentOutline (juce::Graphics& g, int width, int height,
                                    const juce::String& text,
                                    const juce::Justification& justification,
                                    juce::GroupComponent& group) override
    {
        const float halfH = 0.5f * (float) height;
        g.setColour (group.findColour (juce::GroupComponent::outlineColourId));
        g.drawRoundedRectangle (1.0f, halfH, (float) width - 2.0f, halfH, 4.0f, 1.0f);

        if (text.isNotEmpty())
        {
            g.setColour (group.findColour (juce::GroupComponent::textColourId));
            g.setFont (showcontrol::audioSettings::labelFont());
            g.drawText (text, 12, 0, width - 24, (int) halfH,
                        justification.getOnlyVerticalFlags() | juce::Justification::left, true);
        }
    }
};

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

    /** Cập nhật placeholder + tên mặc định khi đổi ngôn ngữ (giữ tên tùy chỉnh). */
    void refreshLocalizedDefaultBusTexts()
    {
        for (int i = 0; i < kNumBuses; ++i)
        {
            const auto current = nameEdits[i].getText().trim();

            if (showcontrol::routing::isDefaultBusName (i, current))
                nameEdits[i].setText ({}, juce::dontSendNotification);
        }

        refreshLocalizedPlaceholders();
        applyRowTheme();
    }

    void initialiseRows (const juce::StringArray& busNames)
    {
        for (int i = 0; i < kNumBuses; ++i)
        {
            indexLabels[i].setText ("Bus " + juce::String (i) + ":", juce::dontSendNotification);
            indexLabels[i].setFont (showcontrol::audioSettings::labelFont());
            indexLabels[i].setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (indexLabels[i]);

            const auto defaultName = showcontrol::routing::getBusDisplayName (i);
            const juce::String saved = (i < busNames.size()) ? busNames[i].trim() : juce::String();

            if (saved.isNotEmpty())
                nameEdits[i].setText (saved, juce::dontSendNotification);
            else
                nameEdits[i].setText ({}, juce::dontSendNotification);

            nameEdits[i].setFont (showcontrol::audioSettings::editorFont());
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
        const auto labelCol = pal.textSecondary;

        for (int i = 0; i < kNumBuses; ++i)
            indexLabels[i].setColour (juce::Label::textColourId, labelCol);

        pushBusEditorColours (resolveLookAndFeel());
    }

    void pushBusEditorColours (const juce::LookAndFeel& lf)
    {
        const auto pal = ShowTheme::get (isDark);

        const auto textColour      = lf.findColour (juce::TextEditor::textColourId);
        const auto highlightColour = lf.findColour (juce::TextEditor::highlightColourId);
        const auto outlineColour   = lf.findColour (juce::TextEditor::outlineColourId);
        const auto focusedOutline  = lf.findColour (juce::TextEditor::focusedOutlineColourId);
        const auto caretColour     = lf.findColour (juce::CaretComponent::caretColourId);
        const auto placeholderCol  = textColour.withAlpha (0.40f);

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

            const auto rowBg = ((i & 1) == 0) ? pal.listRowBg : pal.panelElevated;
            const auto bgColour = rowBg;

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
        syncDarkModeFromLookAndFeel();
        const auto pal = ShowTheme::get (isDark);
        const auto rowA = pal.listRowBg;
        const auto rowB = pal.panelElevated;

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
        titleLabel.setFont (showcontrol::audioSettings::buttonFont());
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel);

        closeButton.setButtonText ("X");
        closeButton.setTooltip (showcontrol::localization::tr (u8"Đóng"));
        closeButton.onClick = [this] { requestDismiss (true); };
        addAndMakeVisible (closeButton);

        addAndMakeVisible (busList);
    }

    ~OutputBusNamingCard() override
    {
        setLookAndFeel (nullptr);
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

    void refreshLocalizedText()
    {
        titleLabel.setText (showcontrol::localization::tr (u8"ĐẶT TÊN OUTPUT BUS (OUTPUT ROUTING)"),
                            juce::dontSendNotification);
        closeButton.setTooltip (showcontrol::localization::tr (u8"Đóng"));
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

    static void refreshLocalizedActive() noexcept
    {
        if (activeOverlay == nullptr)
            return;

        activeOverlay->card.refreshLocalizedText();
        activeOverlay->card.busList.refreshLocalizedDefaultBusTexts();
        activeOverlay->card.repaint();
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
        card.setLookAndFeel (&themeSource.getLookAndFeel());
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
/** Bảng định tuyến phẳng: Master + Bus phụ → cặp cổng hardware (quét động). */
class DirectRoutingSettingsList : public juce::Component
{
public:
    static constexpr int kNumRoutes = showcontrol::routing::kRouteCount;

    explicit DirectRoutingSettingsList (juce::AudioDeviceManager& deviceManagerIn)
        : deviceManager (deviceManagerIn)
    {
        for (int i = 0; i < kNumRoutes; ++i)
        {
            rowLabels[i].setFont (showcontrol::audioSettings::labelFont().withHeight (12.5f));
            rowLabels[i].setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (rowLabels[i]);

            if (i == showcontrol::routing::kMasterRouteId)
            {
                masterNameLabel.setFont (showcontrol::audioSettings::editorFont());
                masterNameLabel.setJustificationType (juce::Justification::centredLeft);
                addAndMakeVisible (masterNameLabel);
            }
            else
            {
                const int busIdx = i - 1;
                nameEdits[(size_t) busIdx].setFont (showcontrol::audioSettings::editorFont());
                nameEdits[(size_t) busIdx].setJustification (juce::Justification::centredLeft);
                nameEdits[(size_t) busIdx].setIndents (8, 5);
                nameEdits[(size_t) busIdx].setReturnKeyStartsNewLine (false);
                nameEdits[(size_t) busIdx].onTextChange = [this, busIdx]
                {
                    if (onRouteNameLiveChanged != nullptr)
                        onRouteNameLiveChanged (busIdx + 1, nameEdits[(size_t) busIdx].getText());
                };
                addAndMakeVisible (nameEdits[(size_t) busIdx]);
            }

            hwCombos[i].setTextWhenNothingSelected (showcontrol::localization::tr (u8"Chọn thiết bị Out..."));
            hwCombos[i].onChange = [this, i]
            {
                applyComboSelectionToLiveTable (i);
                if (onRoutingChanged != nullptr)
                    onRoutingChanged();
            };
            addAndMakeVisible (hwCombos[i]);
        }

        rescanAvailableOutputs();
        loadFromStoredPreferences();
        refreshRowLabels();
        applyRowTheme();
    }

    std::function<void (int routeId, const juce::String& text)> onRouteNameLiveChanged;
    std::function<void()> onRoutingChanged;

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
        preferencesChrome = enabled;
        applyRowTheme();
    }

    void rescanAvailableOutputs()
    {
        cachedOutputChoices = showcontrol::routing::scanAvailableOutputEndpoints (deviceManager);

        for (int i = 0; i < kNumRoutes; ++i)
        {
            const auto prevChoice = hwCombos[i].getText();
            hwCombos[i].clear (juce::dontSendNotification);

            for (int p = 0; p < cachedOutputChoices.size(); ++p)
                hwCombos[i].addItem (cachedOutputChoices[p], p + 1);

            syncComboFromOutputChoice (i, prevChoice);
        }
    }

    void rescanHardwareOutputPairs()
    {
        rescanAvailableOutputs();
    }

    void loadFromStoredPreferences()
    {
        const auto customNames = showcontrol::prefs::loadCustomBusNamesFromPrefs();
        const auto choices     = showcontrol::prefs::loadAllRouteOutputChoices();

        masterNameLabel.setText (showcontrol::routing::masterRouteDisplayName(),
                                 juce::dontSendNotification);

        for (int i = 0; i < showcontrol::routing::kMaxCustomBuses; ++i)
        {
            nameEdits[(size_t) i].setText (customNames[i], juce::dontSendNotification);
            nameEdits[(size_t) i].setTextToShowWhenEmpty (
                showcontrol::routing::defaultCustomBusName (i),
                juce::Colours::grey.withAlpha (0.45f));
        }

        for (int r = 0; r < kNumRoutes; ++r)
            syncComboFromOutputChoice (r, choices[(size_t) r]);

        showcontrol::prefs::loadDirectRoutingIntoLiveTable (&deviceManager);
    }

    void refreshRowLabels()
    {
        rowLabels[0].setText (showcontrol::localization::tr (u8"MASTER OUT"), juce::dontSendNotification);

        for (int i = 1; i < kNumRoutes; ++i)
            rowLabels[i].setText (showcontrol::localization::tr (u8"BUS ") + juce::String (i),
                                  juce::dontSendNotification);
    }

    void refreshLocalizedDefaultBusTexts()
    {
        for (int i = 0; i < showcontrol::routing::kMaxCustomBuses; ++i)
        {
            const auto current = nameEdits[(size_t) i].getText().trim();

            if (showcontrol::routing::isDefaultRouteName (i + 1, current))
                nameEdits[(size_t) i].setText ({}, juce::dontSendNotification);

            nameEdits[(size_t) i].setTextToShowWhenEmpty (
                showcontrol::routing::defaultCustomBusName (i),
                juce::Colours::grey.withAlpha (0.45f));
        }

        masterNameLabel.setText (showcontrol::routing::masterRouteDisplayName(),
                                 juce::dontSendNotification);
        refreshRowLabels();
        applyRowTheme();
    }

    juce::StringArray getRouteDisplayNames() const
    {
        juce::StringArray names;
        names.add (showcontrol::routing::masterRouteDisplayName());

        for (int i = 0; i < showcontrol::routing::kMaxCustomBuses; ++i)
        {
            const auto t = nameEdits[(size_t) i].getText().trim();
            names.add (t.isNotEmpty() ? t : showcontrol::routing::defaultCustomBusName (i));
        }

        return names;
    }

    void persistToPreferencesAndLiveTable()
    {
        std::array<juce::String, kNumRoutes> outputChoices {};
        juce::StringArray customNames;

        for (int r = 0; r < kNumRoutes; ++r)
            outputChoices[(size_t) r] = hwCombos[r].getText().trim();

        for (int i = 0; i < showcontrol::routing::kMaxCustomBuses; ++i)
            customNames.add (nameEdits[(size_t) i].getText().trim());

        showcontrol::prefs::saveDirectRoutingSettings (outputChoices, customNames);
        showcontrol::prefs::loadDirectRoutingIntoLiveTable (&deviceManager);
    }

    void applyThemeColoursDirectly (const juce::LookAndFeel& lf)
    {
        if (auto* showLaf = dynamic_cast<const ShowControlLookAndFeel*> (&lf))
            isDark = showLaf->isDarkMode();

        applyRowTheme();
        repaint();
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        constexpr int rowH = 34;
        constexpr int labelW = 92;
        constexpr int gap = 6;
        constexpr int minNameW = 88;
        constexpr int maxNameW = 118;
        const int nameW = juce::jlimit (minNameW, maxNameW,
                                        (int) std::round ((float) bounds.getWidth() * 0.16f));

        for (int i = 0; i < kNumRoutes; ++i)
        {
            auto row = bounds.removeFromTop (rowH).reduced (0, 2);
            rowLabels[i].setBounds (row.removeFromLeft (labelW));

            if (i == showcontrol::routing::kMasterRouteId)
                masterNameLabel.setBounds (row.removeFromLeft (nameW).reduced (0, 3));
            else
                nameEdits[(size_t) (i - 1)].setBounds (row.removeFromLeft (nameW).reduced (0, 3));

            row.removeFromLeft (gap);
            hwCombos[i].setBounds (row.reduced (0, 3));
        }
    }

private:
    void syncComboFromOutputChoice (int routeIndex, const juce::String& choice) noexcept
    {
        int idx = cachedOutputChoices.indexOf (choice.trim());

        if (idx < 0 && routeIndex < cachedOutputChoices.size())
            idx = routeIndex;

        if (idx < 0 && cachedOutputChoices.size() > 0)
            idx = 0;

        hwCombos[routeIndex].setSelectedItemIndex (
            juce::jlimit (0, juce::jmax (0, hwCombos[routeIndex].getNumItems() - 1), idx),
            juce::dontSendNotification);
    }

    void applyComboSelectionToLiveTable (int routeIndex) noexcept
    {
        const auto choice = hwCombos[routeIndex].getText().trim();
        showcontrol::routing::bindRouteOutputChoice (
            routeIndex, choice, deviceManager.getCurrentAudioDevice());
    }

    void applyRowTheme()
    {
        const auto& lf = getLookAndFeel();
        const auto labelCol = lf.findColour (ShowControlLookAndFeel::textSecondaryColourId);
        const auto textCol  = lf.findColour (juce::Label::textColourId);
        const auto fieldBg  = lf.findColour (juce::TextEditor::backgroundColourId);
        const auto fieldOut = lf.findColour (juce::TextEditor::outlineColourId);

        for (int i = 0; i < kNumRoutes; ++i)
            rowLabels[i].setColour (juce::Label::textColourId, labelCol);

        masterNameLabel.setColour (juce::Label::textColourId, textCol);

        for (auto& ed : nameEdits)
        {
            ed.setColour (juce::TextEditor::textColourId, textCol);
            ed.setColour (juce::TextEditor::backgroundColourId, fieldBg);
            ed.setColour (juce::TextEditor::outlineColourId, fieldOut);
        }

        for (auto& cb : hwCombos)
            cb.setColour (juce::ComboBox::textColourId, textCol);
    }

    juce::AudioDeviceManager& deviceManager;
    bool isDark = true;
    bool preferencesChrome = false;
    juce::StringArray cachedOutputChoices;

    std::array<juce::Label, kNumRoutes> rowLabels;
    juce::Label masterNameLabel;
    std::array<BusNameTextEditor, showcontrol::routing::kMaxCustomBuses> nameEdits;
    std::array<juce::ComboBox, kNumRoutes> hwCombos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DirectRoutingSettingsList)
};

//==============================================================================
/** ⚙ Audio: thiết bị + đặt tên output bus (message thread only). */
class AudioDeviceSettingsPanel : public juce::Component,
                                 private juce::ChangeListener
{
public:
    static constexpr int kNumRoutes = DirectRoutingSettingsList::kNumRoutes;
    static constexpr int kNumBuses  = kNumRoutes;

    static int getEmbeddedRoutingListHeight() noexcept
    {
        constexpr int rowH = 34;
        return rowH * kNumRoutes;
    }

    static int getPreferredEmbeddedHeight() noexcept
    {
        return 480;
    }

    AudioDeviceSettingsPanel (juce::AudioDeviceManager& deviceManagerIn,
                              bool darkMode,
                              const juce::StringArray& busNames,
                              bool embeddedInPreferences = false)
        : isDark (darkMode),
          embeddedInPreferences (embeddedInPreferences),
          deviceManager (deviceManagerIn),
          routingList (deviceManagerIn)
    {
        audioGroup.setText (showcontrol::localization::tr (u8"Cấu hình Audio"));
        audioGroup.setTextLabelPosition (juce::Justification::centredLeft);
        addAndMakeVisible (audioGroup);

        busSectionLabel.setText (showcontrol::localization::tr (u8"Định tuyến Output"),
                                 juce::dontSendNotification);
        busSectionLabel.setFont (showcontrol::audioSettings::labelFont().withHeight (13.0f));
        busSectionLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (busSectionLabel);

        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager, 0, 0, 2, 16, false, false, false, false);
        addAndMakeVisible (*deviceSelector);
        localiseAudioDeviceSelector (deviceSelector.get());

        routingList.setDarkMode (isDark);
        routingList.setPreferencesChrome (embeddedInPreferences);
        juce::ignoreUnused (busNames);
        addAndMakeVisible (routingList);

        routingList.onRoutingChanged = [this]
        {
            routingList.persistToPreferencesAndLiveTable();
        };

        okBtn.setButtonText (showcontrol::localization::tr (u8"Đóng"));
        addAndMakeVisible (okBtn);
        okBtn.onClick = [this] { closeAndApply(); };
        okBtn.setVisible (! embeddedInPreferences);

        applyTheme();
        setWantsKeyboardFocus (true);
        setSize (540, embeddedInPreferences ? getPreferredEmbeddedHeight() : 500);
        deviceManager.addChangeListener (this);
    }

    ~AudioDeviceSettingsPanel() override
    {
        deviceManager.removeChangeListener (this);
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
        routingList.onRouteNameLiveChanged = [this] (int routeId, const juce::String& text)
        {
            if (onBusNameLiveChanged != nullptr)
                onBusNameLiveChanged (routeId, text);
        };
    }

    void rescanHardwareRoutes()
    {
        routingList.rescanAvailableOutputs();
        showcontrol::prefs::loadDirectRoutingIntoLiveTable (&deviceManager);
    }

    void parentHierarchyChanged() override
    {
        juce::Component::parentHierarchyChanged();

        if (getParentComponent() == nullptr && ! hasAppliedOnClose)
            closeAndApply();
    }

    juce::StringArray getBusNames() const
    {
        return routingList.getRouteDisplayNames();
    }

    /** Manual Push — ép màu route và nhãn phân khu ngay lập tức. */
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

        routingList.applyThemeColoursDirectly (lf);

        if (deviceSelector != nullptr)
        {
            deviceSelector->lookAndFeelChanged();
            deviceSelector->repaint();
        }

        repaint();
    }

    void refreshLocalizedText()
    {
        audioGroup.setText (showcontrol::localization::tr (u8"Cấu hình Audio"));
        busSectionLabel.setText (showcontrol::localization::tr (u8"Định tuyến Output"),
                                 juce::dontSendNotification);
        okBtn.setButtonText (showcontrol::localization::tr (u8"Đóng"));
        routingList.refreshLocalizedDefaultBusTexts();

        if (deviceSelector != nullptr)
            localiseAudioDeviceSelector (deviceSelector.get());

        OutputBusNamingOverlay::refreshLocalizedActive();
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

    void visibilityChanged() override
    {
        juce::Component::visibilityChanged();

        if (isVisible())
            routingList.rescanAvailableOutputs();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        constexpr int outerPad   = 12;
        constexpr int sectionGap = 8;
        const int footerH = embeddedInPreferences ? 0 : 44;

        auto bounds = getLocalBounds().reduced (outerPad);

        if (! embeddedInPreferences)
        {
            auto footer = bounds.removeFromBottom (footerH);
            okBtn.setBounds (footer.removeFromRight (108).withHeight (32).reduced (0, 4));
        }

        if (embeddedInPreferences)
        {
            constexpr int busLabelH = 20;
            const int routingListH  = getEmbeddedRoutingListHeight();
            const int routeMinH     = busLabelH + 4 + routingListH;
            const int routeBlockH   = juce::jmax (routeMinH,
                                                  (int) std::round (bounds.getHeight() * 0.45f));

            auto routingBlock = bounds.removeFromTop (routeBlockH);
            busSectionLabel.setBounds (routingBlock.removeFromTop (busLabelH));
            routingBlock.removeFromTop (4);
            routingList.setBounds (routingBlock.removeFromTop (routingListH));
            bounds.removeFromTop (sectionGap);

            audioGroup.setBounds (bounds);
            deviceSelector->setBounds (audioGroup.getBounds().reduced (10, 14));
            return;
        }

        audioGroup.setBounds (bounds);

        auto audioInner = audioGroup.getBounds().reduced (10, 14);
        constexpr int busLabelH = 18;
        constexpr int routingListH = 124;
        constexpr int deviceMinH = 164;
        const int deviceH = juce::jmax (deviceMinH,
                                        audioInner.getHeight() - busLabelH - routingListH - sectionGap * 2);

        auto deviceArea = audioInner.removeFromTop (deviceH);
        deviceSelector->setBounds (deviceArea);

        audioInner.removeFromTop (sectionGap);
        busSectionLabel.setBounds (audioInner.removeFromTop (busLabelH));
        audioInner.removeFromTop (sectionGap);
        routingList.setBounds (audioInner.removeFromTop (routingListH));
    }

private:
    bool isDark = true;
    bool embeddedInPreferences = false;
    bool hasAppliedOnClose = false;

    juce::GroupComponent audioGroup;
    juce::Label busSectionLabel;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::TextButton okBtn;
    juce::AudioDeviceManager& deviceManager;
    DirectRoutingSettingsList routingList;
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

        audioGroup.setColour (juce::GroupComponent::textColourId, groupText);
        audioGroup.setColour (juce::GroupComponent::outlineColourId, groupOutline);

        busSectionLabel.setColour (juce::Label::textColourId,
                                   laf.findColour (ShowControlLookAndFeel::textSecondaryColourId));

        okBtn.setColour (juce::TextButton::buttonColourId, pal.buttonSecondary);
        okBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);

        routingList.setDarkMode (isDark);
        routingList.setPreferencesChrome (embeddedInPreferences);

        if (deviceSelector != nullptr)
            deviceSelector->lookAndFeelChanged();
    }

    void closeAndApply()
    {
        if (hasAppliedOnClose)
            return;

        hasAppliedOnClose = true;

        routingList.persistToPreferencesAndLiveTable();

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

    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source != &deviceManager)
            return;

        routingList.rescanAvailableOutputs();
        showcontrol::prefs::loadDirectRoutingIntoLiveTable (&deviceManager);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioDeviceSettingsPanel)
};
