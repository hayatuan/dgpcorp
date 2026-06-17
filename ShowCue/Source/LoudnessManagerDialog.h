#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowLocalization.h"
#include "ShowControlMacWindow.h"
#include "ShowLoudnessNormalize.h"

namespace showcontrol::ui
{

class LoudnessPreviewHeader : public juce::Component
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> paintContent;

    void paint (juce::Graphics& g) override
    {
        if (paintContent)
            paintContent (g, getLocalBounds());
    }
};

class LoudnessManagerDialogContent : public juce::Component,
                                   public juce::ListBoxModel,
                                   private juce::Timer
{
public:
    using PreviewFetcher = std::function<juce::Array<showcontrol::loudness::ListPreviewRow> (
        const showcontrol::loudness::LoudnessSettings&)>;

    LoudnessManagerDialogContent (showcontrol::loudness::LoudnessSettings initial,
                                  bool darkTheme,
                                  std::function<void (const showcontrol::loudness::LoudnessSettings&)> onLiveChangedIn,
                                  std::function<void (const showcontrol::loudness::LoudnessSettings&)> onApplyToListIn,
                                  PreviewFetcher fetchPreviewIn)
        : settings (initial),
          isDarkTheme (darkTheme),
          onLiveChanged (std::move (onLiveChangedIn)),
          onApplyToList (std::move (onApplyToListIn)),
          fetchPreview (std::move (fetchPreviewIn))
    {
        setSize (640, 600);

        addAndMakeVisible (enableToggle);
        enableToggle.setButtonText (showcontrol::localization::tr (u8"Bật đồng bộ âm lượng"));
        enableToggle.onClick = [this]
        {
            settings.enabled = enableToggle.getToggleState();
            emitLiveChanged();
        };

        addAndMakeVisible (presetLabel);
        presetLabel.setText (showcontrol::localization::tr (u8"Preset"), juce::dontSendNotification);
        addAndMakeVisible (presetCombo);
        presetCombo.addItem (showcontrol::loudness::presetDisplayName (showcontrol::loudness::Preset::musicStream), 1);
        presetCombo.addItem (showcontrol::loudness::presetDisplayName (showcontrol::loudness::Preset::liveShow), 2);
        presetCombo.addItem (showcontrol::loudness::presetDisplayName (showcontrol::loudness::Preset::speech), 3);
        presetCombo.addItem (showcontrol::loudness::presetDisplayName (showcontrol::loudness::Preset::custom), 4);
        presetCombo.onChange = [this]
        {
            settings.preset = (showcontrol::loudness::Preset) (presetCombo.getSelectedItemIndex());
            targetSlider.setEnabled (settings.preset == showcontrol::loudness::Preset::custom);
            emitLiveChanged();
        };

        addAndMakeVisible (profileLabel);
        profileLabel.setText (showcontrol::localization::tr (u8"Profile"), juce::dontSendNotification);
        addAndMakeVisible (profileCombo);
        for (int i = 0; i <= (int) showcontrol::loudness::ContentProfile::speech; ++i)
            profileCombo.addItem (showcontrol::loudness::profileDisplayName ((showcontrol::loudness::ContentProfile) i), i + 1);
        profileCombo.onChange = [this]
        {
            settings.profile = (showcontrol::loudness::ContentProfile) profileCombo.getSelectedItemIndex();
            emitLiveChanged();
        };

        addAndMakeVisible (modeRmsBtn);
        addAndMakeVisible (modeLufsBtn);
        modeRmsBtn.setButtonText ("RMS");
        modeLufsBtn.setButtonText ("LUFS");
        modeRmsBtn.onClick = [this]
        {
            modeRmsBtn.setToggleState (true, juce::dontSendNotification);
            modeLufsBtn.setToggleState (false, juce::dontSendNotification);
            settings.mode = showcontrol::loudness::MeasureMode::rms;
            emitLiveChanged();
        };
        modeLufsBtn.onClick = [this]
        {
            modeLufsBtn.setToggleState (true, juce::dontSendNotification);
            modeRmsBtn.setToggleState (false, juce::dontSendNotification);
            settings.mode = showcontrol::loudness::MeasureMode::lufs;
            emitLiveChanged();
        };

        addAndMakeVisible (safeModeToggle);
        safeModeToggle.setButtonText (showcontrol::localization::tr (u8"Safe mode (chống clip)"));
        safeModeToggle.onClick = [this]
        {
            settings.safeMode = safeModeToggle.getToggleState();
            emitLiveChanged();
        };

        addAndMakeVisible (abToggle);
        abToggle.setButtonText (showcontrol::localization::tr (u8"A/B nghe bản gốc"));
        abToggle.onClick = [this]
        {
            settings.abCompareOriginal = abToggle.getToggleState();
            emitLiveChanged();
        };

        addAndMakeVisible (targetLabel);
        targetLabel.setText (showcontrol::localization::tr (u8"Custom Target LUFS"), juce::dontSendNotification);
        addAndMakeVisible (targetSlider);
        targetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        targetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        targetSlider.setRange (-24.0, -10.0, 0.5);
        targetSlider.onValueChange = [this]
        {
            settings.customTargetLufs = targetSlider.getValue();
            emitLiveChanged();
        };

        addAndMakeVisible (previewTitleLabel);
        previewTitleLabel.setText (showcontrol::localization::tr (u8"Preview toàn list (trước / sau)"),
                                   juce::dontSendNotification);
        previewTitleLabel.setFont (ShowTheme::fontBold (14.0f));

        addAndMakeVisible (previewHeader);
        previewHeader.paintContent = [this] (juce::Graphics& g, juce::Rectangle<int> area)
        {
            const auto& pal = ShowTheme::get (isDarkTheme);
            g.setColour (pal.panelElevated.withAlpha (0.9f));
            g.fillRoundedRectangle (area.toFloat(), 4.0f);
            g.setColour (pal.border.withAlpha (0.8f));
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);

            g.setFont (ShowTheme::fontBold (12.0f));
            g.setColour (pal.textSecondary);

            const auto cols = getPreviewColumns (area.getWidth());
            int x = area.getX() + kCellPadX;
            g.drawText (showcontrol::localization::tr (u8"Tên"), x, area.getY(), cols.title, area.getHeight(), juce::Justification::centredLeft, true);
            x += cols.title;
            drawHeaderCell (g, area, x, cols.before, showcontrol::localization::tr (u8"Trước"), juce::Justification::centredRight); x += cols.before;
            drawHeaderCell (g, area, x, cols.after, showcontrol::localization::tr (u8"Sau"), juce::Justification::centredRight);     x += cols.after;
            drawHeaderCell (g, area, x, cols.gain, showcontrol::localization::tr (u8"Gain"), juce::Justification::centredRight);    x += cols.gain;
            drawHeaderCell (g, area, x, cols.peak, showcontrol::localization::tr (u8"Peak"), juce::Justification::centredRight);
        };

        addAndMakeVisible (previewList);
        previewList.setModel (this);
        previewList.setRowHeight (26);
        previewList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        previewList.setOutlineThickness (0);
        previewList.getViewport()->setScrollBarsShown (true, false);

        addAndMakeVisible (applyListBtn);
        applyListBtn.setButtonText (showcontrol::localization::tr (u8"Áp dụng toàn bộ list"));
        applyListBtn.onClick = [this]
        {
            if (onApplyToList)
                onApplyToList (settings);
            refreshPreviewRows();
        };

        addAndMakeVisible (closeBtn);
        closeBtn.setButtonText (showcontrol::localization::tr (u8"Đóng"));
        closeBtn.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        };

        syncFromState();
        refreshPreviewRows();
        startTimerHz (4);
    }

    ~LoudnessManagerDialogContent() override
    {
        stopTimer();
        previewList.setModel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ShowTheme::get (isDarkTheme).windowBg);
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

    void mouseUp (const juce::MouseEvent&) override
    {
        windowDragActive = false;
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (14);
        constexpr int kTopInset = 18;
        constexpr int kRowGap = 8;
        constexpr int kFieldLabelW = 56;
        constexpr int kPairGap = 10;

        b.removeFromTop (kTopInset);

        auto row = [&] (int h = 30) { return b.removeFromTop (h); };

        // Preset + Profile cùng một hàng
        {
            auto presetProfileRow = row();
            const int halfW = juce::jmax (0, (presetProfileRow.getWidth() - kPairGap) / 2);

            auto presetSide = presetProfileRow.removeFromLeft (halfW);
            presetLabel.setBounds (presetSide.removeFromLeft (kFieldLabelW));
            presetCombo.setBounds (presetSide);

            presetProfileRow.removeFromLeft (kPairGap);

            profileLabel.setBounds (presetProfileRow.removeFromLeft (kFieldLabelW));
            profileCombo.setBounds (presetProfileRow);
        }
        b.removeFromTop (kRowGap);

        auto r3 = row();
        modeRmsBtn.setBounds (r3.removeFromLeft (r3.getWidth() / 2).reduced (0, 2));
        modeLufsBtn.setBounds (r3.reduced (0, 2));
        b.removeFromTop (kRowGap);

        // Safe mode + A/B cùng một hàng
        {
            auto toggleRow = row();
            safeModeToggle.setBounds (toggleRow.removeFromLeft (toggleRow.getWidth() / 2).reduced (0, 2));
            abToggle.setBounds (toggleRow.reduced (0, 2));
        }
        b.removeFromTop (kRowGap);

        auto r4 = row();
        targetLabel.setBounds (r4.removeFromLeft (140));
        targetSlider.setBounds (r4);
        b.removeFromTop (10);

        previewTitleLabel.setBounds (b.removeFromTop (22));
        b.removeFromTop (4);
        previewHeader.setBounds (b.removeFromTop (24));
        b.removeFromTop (4);

        // Footer: 2 hàng — toggle trên, 3 nút hành động dưới
        auto footer = b.removeFromBottom (76);
        enableToggle.setBounds (footer.removeFromTop (32));
        footer.removeFromTop (8);

        auto actionRow = footer;
        const int actionBtnW = juce::jmax (0, (actionRow.getWidth() - 8) / 2);
        applyListBtn.setBounds (actionRow.removeFromLeft (actionBtnW));
        actionRow.removeFromLeft (8);
        closeBtn.setBounds (actionRow);

        b.removeFromBottom (8);
        previewList.setBounds (b);
    }

    int getNumRows() override { return previewRows.size(); }

    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (! juce::isPositiveAndBelow (rowNumber, previewRows.size()))
            return;

        const auto& pal = ShowTheme::get (isDarkTheme);
        const auto& row = previewRows.getReference (rowNumber);

        if (rowIsSelected)
            g.fillAll (pal.accent.withAlpha (0.18f));
        else if (rowNumber % 2 == 0)
            g.fillAll (pal.panelElevated.withAlpha (0.35f));

        g.setColour (row.analyzing ? pal.textMuted : pal.textPrimary);
        g.setFont (ShowTheme::font (12.0f));

        const auto cols = getPreviewColumns (width);

        int x = kCellPadX;
        g.drawText (row.title, x, 0, cols.title, height, juce::Justification::centredLeft, true);
        x += cols.title;
        const auto textColour = row.analyzing ? pal.textMuted : pal.textPrimary;
        drawBodyCell (g, row.beforeText, x, cols.before, height, textColour, juce::Justification::centredRight); x += cols.before;
        drawBodyCell (g, row.afterText, x, cols.after, height, textColour, juce::Justification::centredRight);   x += cols.after;
        drawBodyCell (g, row.gainText, x, cols.gain, height, textColour, juce::Justification::centredRight);     x += cols.gain;
        drawBodyCell (g, row.peakText, x, cols.peak, height, textColour, juce::Justification::centredRight);

        g.setColour (pal.borderSubtle.withAlpha (0.55f));
        g.drawHorizontalLine (height - 1.0f, 0.0f, (float) width);
    }

private:
    void timerCallback() override
    {
        refreshPreviewRows();
    }

    void syncFromState()
    {
        enableToggle.setToggleState (settings.enabled, juce::dontSendNotification);
        presetCombo.setSelectedItemIndex ((int) settings.preset, juce::dontSendNotification);
        profileCombo.setSelectedItemIndex ((int) settings.profile, juce::dontSendNotification);
        modeRmsBtn.setToggleState (settings.mode == showcontrol::loudness::MeasureMode::rms, juce::dontSendNotification);
        modeLufsBtn.setToggleState (settings.mode == showcontrol::loudness::MeasureMode::lufs, juce::dontSendNotification);
        safeModeToggle.setToggleState (settings.safeMode, juce::dontSendNotification);
        abToggle.setToggleState (settings.abCompareOriginal, juce::dontSendNotification);
        targetSlider.setValue (settings.customTargetLufs, juce::dontSendNotification);
        targetSlider.setEnabled (settings.preset == showcontrol::loudness::Preset::custom);

        previewHeader.repaint();
    }

    void refreshPreviewRows()
    {
        if (! fetchPreview)
            return;

        const auto next = fetchPreview (settings);
        if (next.size() == previewRows.size())
        {
            bool same = true;
            for (int i = 0; i < next.size(); ++i)
            {
                const auto& a = next.getReference (i);
                const auto& b = previewRows.getReference (i);
                if (a.title != b.title || a.beforeText != b.beforeText || a.afterText != b.afterText
                    || a.gainText != b.gainText || a.peakText != b.peakText || a.analyzing != b.analyzing)
                {
                    same = false;
                    break;
                }
            }

            if (same)
                return;
        }

        previewRows = next;
        previewList.updateContent();
        previewList.repaint();
    }

    void emitLiveChanged()
    {
        if (onLiveChanged)
            onLiveChanged (settings);

        refreshPreviewRows();
    }

    struct PreviewColumns
    {
        int title = 0;
        int before = 0;
        int after = 0;
        int gain = 0;
        int peak = 0;
    };

    PreviewColumns getPreviewColumns (int width) const
    {
        PreviewColumns cols;
        const int usable = juce::jmax (0, width - kCellPadX * 2);

        // Cột số cố định theo nội dung thực tế; Tên lấy phần còn lại.
        cols.peak   = 70;
        cols.gain   = 44;
        cols.before = 82;
        cols.after  = 96;

        const int metricsTotal = cols.peak + cols.gain + cols.before + cols.after;
        const int minTitle = juce::jmax (140, usable / 3);
        cols.title = juce::jmax (minTitle, usable - metricsTotal);

        if (cols.title + metricsTotal > usable)
        {
            const int overflow = cols.title + metricsTotal - usable;
            cols.after  = juce::jmax (84, cols.after - overflow / 2);
            cols.before = juce::jmax (72, cols.before - (overflow - overflow / 2));
            cols.title  = juce::jmax (minTitle, usable - cols.peak - cols.gain - cols.before - cols.after);
        }

        return cols;
    }

    void drawHeaderCell (juce::Graphics& g,
                         juce::Rectangle<int> area,
                         int x,
                         int width,
                         const juce::String& text,
                         juce::Justification justification = juce::Justification::centredLeft) const
    {
        const int pad = (justification == juce::Justification::centredRight) ? 4 : 0;
        g.drawText (text, x, area.getY(), width - pad, area.getHeight(), justification, true);
        g.setColour (ShowTheme::get (isDarkTheme).borderSubtle.withAlpha (0.6f));
        g.drawVerticalLine (x, (float) area.getY() + 4.0f, (float) area.getBottom() - 4.0f);
        g.setColour (ShowTheme::get (isDarkTheme).textSecondary);
    }

    void drawBodyCell (juce::Graphics& g,
                       const juce::String& text,
                       int x,
                       int width,
                       int height,
                       juce::Colour textColour,
                       juce::Justification justification = juce::Justification::centredLeft) const
    {
        const int pad = (justification == juce::Justification::centredRight) ? 4 : 0;
        g.drawText (text, x, 0, width - pad, height, justification, true);
        g.setColour (ShowTheme::get (isDarkTheme).borderSubtle.withAlpha (0.4f));
        g.drawVerticalLine (x, 4.0f, (float) height - 4.0f);
        g.setColour (textColour);
    }

    static constexpr int kCellPadX = 8;

    showcontrol::loudness::LoudnessSettings settings;
    bool isDarkTheme = true;
    std::function<void (const showcontrol::loudness::LoudnessSettings&)> onLiveChanged;
    std::function<void (const showcontrol::loudness::LoudnessSettings&)> onApplyToList;
    PreviewFetcher fetchPreview;
    juce::Array<showcontrol::loudness::ListPreviewRow> previewRows;

    juce::ToggleButton enableToggle;
    juce::Label presetLabel, profileLabel, targetLabel, previewTitleLabel;
    juce::ComboBox presetCombo, profileCombo;
    juce::ToggleButton modeRmsBtn, modeLufsBtn, safeModeToggle, abToggle;
    juce::Slider targetSlider;
    LoudnessPreviewHeader previewHeader;
    juce::ListBox previewList { "LoudnessPreviewList", nullptr };
    juce::TextButton applyListBtn, closeBtn;

    juce::ComponentDragger windowDragger;
    bool windowDragActive = false;
};

inline void showLoudnessManagerDialog (juce::Component* parent,
                                       const showcontrol::loudness::LoudnessSettings& current,
                                       bool darkMode,
                                       std::function<void (const showcontrol::loudness::LoudnessSettings&)> onLiveChanged,
                                       std::function<void (const showcontrol::loudness::LoudnessSettings&)> onApplyToList,
                                       LoudnessManagerDialogContent::PreviewFetcher fetchPreview)
{
    if (parent == nullptr)
        return;

    auto* content = new LoudnessManagerDialogContent (current,
                                                      darkMode,
                                                      std::move (onLiveChanged),
                                                      std::move (onApplyToList),
                                                      std::move (fetchPreview));

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned (content);
    opt.dialogTitle = showcontrol::localization::tr (u8"Quản lý đồng bộ âm lượng");
    opt.dialogBackgroundColour = parent->getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;

    if (auto* dw = opt.launchAsync())
    {
        dw->setUsingNativeTitleBar (true);
        dw->setResizable (true, false);
        dw->setSize (640, 600);
        showcontrol::ui::centreFloatingWindowInMainApp (*dw, parent);
       #if JUCE_MAC
        showcontrol::mac::deferFarragoFullSizeContentView (*dw);
       #endif
    }
}

} // namespace showcontrol::ui
