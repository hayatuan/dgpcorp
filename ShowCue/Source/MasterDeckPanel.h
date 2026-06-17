#pragma once
#include <cmath>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SoundPad.h"
#include "AudioMetadataReader.h"
#include "ShowTheme.h"
#include "ShowLocalization.h"
#include "ShowControlLookAndFeel.h"
#include "ShowGraphicsSafe.h"
#include "MasterDeckComponent.h"
#include "ShowControlMacWindow.h"
#include "ShowFlatIcons.h"
#include "ShowTagColors.h"

namespace MasterDeckUi
{
    inline void drawPanelFrame (juce::Graphics& g, juce::Rectangle<float> bounds,
                                const juce::Component& colourSource)
    {
        bounds = showcontrol::gfx::sanitise (bounds);
        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        const auto& laf = colourSource.getLookAndFeel();
        g.setColour (laf.findColour (juce::ListBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (laf.findColour (juce::ListBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }

    inline void drawPauseIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        showcontrol::icons::paintPauseIcon (g,
                                          showcontrol::icons::centredIconIn (area, showcontrol::icons::kListIconSize),
                                          colour);
    }

    inline void drawStopIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        showcontrol::icons::paintStopIcon (g,
                                         showcontrol::icons::centredIconIn (area, showcontrol::icons::kListIconSize),
                                         colour);
    }

    inline void drawFadeIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
    {
        showcontrol::icons::paintFadeSlopeIcon (g,
                                               showcontrol::icons::centredIconIn (area, showcontrol::icons::kListIconSize),
                                               colour);
    }

}

//==============================================================================
class HorizontalPeakMeter : public juce::Component, public juce::Timer
{
public:
    HorizontalPeakMeter() { startTimer (30); }
    ~HorizontalPeakMeter() override { stopTimer(); }

    void setColourHost (juce::Component* host) noexcept { colourHost = host; }

    void setLevels (float left, float right)
    {
        currentLeft  = juce::jmax (0.0f, left);
        currentRight = juce::jmax (0.0f, right);
        peakHoldLeft  = currentLeft;
        peakHoldRight = currentRight;

        if (left >= 1.0f)
            clipFramesLeft = 14;
        if (right >= 1.0f)
            clipFramesRight = 14;

        repaint();
    }

    void timerCallback() override
    {
        if (clipFramesLeft > 0)
            --clipFramesLeft;
        if (clipFramesRight > 0)
            --clipFramesRight;

        repaint();
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& src = (colourHost != nullptr ? *colourHost : *this);
        auto b = showcontrol::gfx::sanitise (getLocalBounds().toFloat().reduced (1.0f));
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
            return;

        const auto& laf = src.getLookAndFeel();
        g.setColour (laf.findColour (juce::ListBox::backgroundColourId));
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (laf.findColour (juce::ListBox::outlineColourId));
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        auto content = b.reduced (4.0f, 3.0f);
        auto labelCol = content.removeFromLeft (12.0f);
        auto meterArea = content;

        constexpr float kRowGap = 2.0f;
        const float rowH = juce::jmax (4.0f, (meterArea.getHeight() - kRowGap) * 0.5f);
        auto leftTrack  = meterArea.removeFromTop (rowH);
        meterArea.removeFromTop (kRowGap);
        auto rightTrack = meterArea.removeFromTop (rowH);

        const auto low   = src.findColour (MasterDeckComponent::meterFillLowColourId);
        const auto mid   = src.findColour (MasterDeckComponent::meterFillMidColourId);
        const auto high  = src.findColour (MasterDeckComponent::meterFillHighColourId);
        const auto track = src.findColour (MasterDeckComponent::meterTrackColourId);
        const auto label = src.findColour (MasterDeckComponent::standbyTextColourId);

        g.setColour (label);
        g.setFont (ShowTheme::fontBold (9.0f));
        g.drawText ("L", labelCol.removeFromTop ((int) rowH).toNearestInt(), juce::Justification::centred);
        g.drawText ("R", labelCol.removeFromTop ((int) kRowGap + (int) rowH).toNearestInt(), juce::Justification::centred);

        paintStereoTrack (g, leftTrack, currentLeft, peakHoldLeft, clipFramesLeft > 0, track, low, mid, high);
        paintStereoTrack (g, rightTrack, currentRight, peakHoldRight, clipFramesRight > 0, track, low, mid, high);

        paintDbGuide (g, leftTrack.getX(), rightTrack.getBottom(), leftTrack.getWidth(), label.withAlpha (0.28f));
    }

private:
    static float normaliseDb (float linear) noexcept
    {
        const float safe = juce::jmax (0.0001f, linear);
        const float db = 20.0f * std::log10 (safe);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }

    static juce::Colour levelColour (float norm, juce::Colour low, juce::Colour mid, juce::Colour high) noexcept
    {
        if (norm < 0.72f)
            return low.interpolatedWith (mid, norm / 0.72f);

        return mid.interpolatedWith (high, juce::jlimit (0.0f, 1.0f, (norm - 0.72f) / 0.28f));
    }

    static void paintDbGuide (juce::Graphics& g, float x, float yBottom, float width, juce::Colour guide) 
    {
        const float marks[] = { 0.70f, 0.90f, 1.0f }; // ~-18dB, -6dB, 0dB
        g.setColour (guide);
        for (float m : marks)
        {
            const float xPos = x + width * m;
            g.drawVerticalLine ((int) std::round (xPos), yBottom - 16.0f, yBottom);
        }
    }

    static void paintStereoTrack (juce::Graphics& g,
                                  juce::Rectangle<float> trackRect,
                                  float currentLevel,
                                  float holdLevel,
                                  bool clip,
                                  juce::Colour trackColour,
                                  juce::Colour low,
                                  juce::Colour mid,
                                  juce::Colour high)
    {
        g.setColour (trackColour);
        g.fillRoundedRectangle (trackRect, 1.6f);

        const float norm = normaliseDb (currentLevel);
        if (norm > 0.001f)
        {
            auto fill = trackRect.withWidth (trackRect.getWidth() * norm);
            g.setColour (levelColour (norm, low, mid, high).withAlpha (0.94f));
            g.fillRoundedRectangle (fill, 1.6f);
        }

        const float holdNorm = normaliseDb (holdLevel);
        if (holdNorm > 0.02f)
        {
            const float holdX = trackRect.getX() + trackRect.getWidth() * holdNorm;
            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.drawVerticalLine ((int) std::round (holdX), trackRect.getY() + 0.5f, trackRect.getBottom() - 0.5f);
        }

        if (clip)
        {
            auto clipArea = trackRect.removeFromRight (juce::jmax (4.0f, trackRect.getWidth() * 0.035f));
            g.setColour (juce::Colour (0xffff3b30).withAlpha (0.95f));
            g.fillRoundedRectangle (clipArea, 1.2f);
        }
    }

    juce::Component* colourHost = nullptr;
    float currentLeft = 0.0f;
    float currentRight = 0.0f;
    float peakHoldLeft = 0.0f;
    float peakHoldRight = 0.0f;
    int clipFramesLeft = 0;
    int clipFramesRight = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizontalPeakMeter)
};

//==============================================================================
/** Scrub vô hình trên waveform — paint() vẽ playhead; slider chỉ nhận chuột. */
class DeckPositionSliderLook : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        juce::ignoreUnused (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
};

//==============================================================================
class DeckVolumeSliderLook : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        juce::ignoreUnused (minSliderPos, maxSliderPos, slider);

        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                              minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto& src = slider.getLookAndFeel();
        const float trackW = juce::jlimit (4.0f, 6.0f, (float) width * 0.42f);
        auto track = juce::Rectangle<float> ((float) x + ((float) width - trackW) * 0.5f,
                                             (float) y + 2.0f,
                                             trackW,
                                             (float) height - 4.0f);
        g.setColour (src.findColour (MasterDeckComponent::meterTrackColourId));
        g.fillRoundedRectangle (track, trackW * 0.5f);
        g.setColour (src.findColour (MasterDeckComponent::panelBorderColourId));
        g.drawRoundedRectangle (track, track.getWidth() * 0.5f, 1.0f);

        const float thumbR = juce::jmax (5.0f, track.getWidth() * 0.55f);
        const float thumbY = juce::jlimit (track.getY() + thumbR,
                                           track.getBottom() - thumbR,
                                           sliderPos);
        g.setColour (src.findColour (MasterDeckComponent::thumbColourId));
        g.fillEllipse (track.getCentreX() - thumbR, thumbY - thumbR, thumbR * 2.0f, thumbR * 2.0f);
        g.setColour (src.findColour (MasterDeckComponent::playheadColourId).withAlpha (0.18f));
        g.drawEllipse (track.getCentreX() - thumbR, thumbY - thumbR, thumbR * 2.0f, thumbR * 2.0f, 1.0f);
    }
};

//==============================================================================
class DeckTransportButtonLook : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& /*backgroundColour*/,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        auto bounds = showcontrol::gfx::sanitise (button.getLocalBounds().toFloat().reduced (1.0f));
        if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f)
            return;

        const auto& src = button.getLookAndFeel();
        const auto id = button.getComponentID();
        juce::Colour bg = src.findColour (MasterDeckComponent::transportBgColourId);

        if (id == "deck_bgm_prev" || id == "deck_bgm_next")
            bg = src.findColour (MasterDeckComponent::navButtonBgColourId);
        else if (id == "deck_stop")
            bg = src.findColour (MasterDeckComponent::stopButtonColourId);
        else if (id == "deck_fade")
            bg = src.findColour (MasterDeckComponent::fadeButtonColourId);
        else if (id == "deck_bgm_play")
            bg = src.findColour (MasterDeckComponent::playButtonColourId);
        else if (id == "deck_gear")
        {
            bg = src.findColour (MasterDeckComponent::navButtonBgColourId).withAlpha (0.28f);
            if (shouldDrawButtonAsHighlighted)
                bg = bg.brighter (0.22f).withAlpha (0.50f);
        }

        if (id != "deck_gear")
        {
            if (shouldDrawButtonAsDown)
                bg = bg.darker (0.18f);
            else if (shouldDrawButtonAsHighlighted)
                bg = bg.brighter (0.12f);
        }

        if (! button.isEnabled())
            bg = bg.withMultipliedAlpha (0.45f);

        g.setColour (bg);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (src.findColour (MasterDeckComponent::panelBorderColourId));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool /*shouldDrawButtonAsDown*/) override
    {
        const auto& src = button.getLookAndFeel();
        const auto id = button.getComponentID();
        auto area = showcontrol::gfx::sanitise (button.getLocalBounds().toFloat().reduced (4.0f));
        const auto iconBase = src.findColour (MasterDeckComponent::transportIconColourId);
        const auto iconCol = button.isEnabled() ? iconBase : iconBase.withAlpha (0.45f);

        const auto iconBounds = showcontrol::icons::centredIconIn (area, showcontrol::icons::kListIconSize);
        const auto deckIconBounds = showcontrol::icons::centredIconIn (area, showcontrol::icons::kButtonIconSize);

        if (id == "deck_pause")
        {
            MasterDeckUi::drawPauseIcon (g, area, iconCol);
            return;
        }

        if (id == "deck_bgm_play")
        {
            const bool isPlaying = button.getProperties().getWithDefault ("sc_playing", false);
            if (isPlaying)
                MasterDeckUi::drawPauseIcon (g, area, iconCol);
            else
                showcontrol::icons::paintPlayIcon (g, iconBounds, iconCol);
            return;
        }

        if (id == "deck_stop")
        {
            MasterDeckUi::drawStopIcon (g, area, iconCol);
            return;
        }

        if (id == "deck_fade")
        {
            MasterDeckUi::drawFadeIcon (g, area, iconCol);
            return;
        }

        if (id == "deck_bgm_prev")
        {
            showcontrol::icons::paintChevronsLeftIcon (g, iconBounds, iconCol);
            return;
        }

        if (id == "deck_bgm_next")
        {
            showcontrol::icons::paintChevronsRightIcon (g, iconBounds, iconCol);
            return;
        }

        if (id == "deck_gear")
        {
            auto gearCol = src.findColour (MasterDeckComponent::gearIconColourId);
            if (shouldDrawButtonAsHighlighted)
                gearCol = gearCol.brighter (0.25f);
            showcontrol::icons::paintSettingsIcon (g, deckIconBounds, gearCol);
            return;
        }

        if (id == "deck_monitor")
        {
            const auto iconArea = deckIconBounds.withX (area.getX() + 4.0f);
            showcontrol::icons::paintMonitorIcon (g, iconArea, iconCol);
            g.setColour (iconCol);
            g.setFont (showcontrol::masterDeck::monitorButtonFont());
            g.drawText (button.getButtonText(), area.withTrimmedLeft (deckIconBounds.getWidth() + 6.0f).toNearestInt(),
                        juce::Justification::centredLeft);
            return;
        }

        g.setColour (iconCol);
        g.setFont (ShowTheme::timerFont (12.0f, true));
        g.drawText (button.getButtonText(), area.toNearestInt(), juce::Justification::centred);
    }
};

//==============================================================================
class MasterDeckPanel : public juce::Component, public juce::Timer
{
public:
    MasterDeckPanel() : isDarkMode(true), activePad(nullptr), isDraggingPlayhead(false), isPaused(false), isBgmMode(false)
    {
        addAndMakeVisible (remainingTimeLabel);
        remainingTimeLabel.setText ("00:00.0", juce::dontSendNotification);
        remainingTimeLabel.setFont (showcontrol::masterDeck::remainingTimeFont());
        remainingTimeLabel.setMinimumHorizontalScale (0.88f);
        remainingTimeLabel.setJustificationType (juce::Justification::centred);
        remainingTimeLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        remainingTimeLabel.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (totalTimeLabel);
        totalTimeLabel.setText ("00:00.0", juce::dontSendNotification);
        totalTimeLabel.setFont (showcontrol::masterDeck::totalTimeFont());
        totalTimeLabel.setMinimumHorizontalScale (1.0f);
        totalTimeLabel.setJustificationType (juce::Justification::centred);
        totalTimeLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        totalTimeLabel.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (trackMetaLabel);
        trackMetaLabel.setText ({}, juce::dontSendNotification);
        trackMetaLabel.setFont (showcontrol::masterDeck::trackMetaFont());
        trackMetaLabel.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (positionSlider);
        positionSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        positionSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        positionSlider.setRange (0.0, 1.0, 0.001);
        positionSlider.onValueChange = [this] {
            if (activePad && positionSlider.isMouseButtonDown()) {
                const double tStart = activePad->getTrimStart();
                const double tLen = activePad->getEffectiveLength();
                if (tLen > 0.0)
                    activePad->seekTo (tStart + positionSlider.getValue() * tLen);
            }
        };

        addAndMakeVisible (masterVolSlider);
        masterVolSlider.setSliderStyle (juce::Slider::LinearVertical);
        masterVolSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        masterVolSlider.setRange (0.0, 1.0, 0.01); masterVolSlider.setValue (1.0);
        masterVolSlider.onValueChange = [this] {
            float vol = static_cast<float> (masterVolSlider.getValue());
            if (onVolumeChanged) onVolumeChanged (vol);
            if (! volValueLabel.isBeingEdited())
                volValueLabel.setText (juce::String (juce::roundToInt (vol * 100.0f)) + "%", juce::dontSendNotification);
        };

        addAndMakeVisible (systemTimeLabel);
        systemTimeLabel.setText ("00:00:00", juce::dontSendNotification);
        systemTimeLabel.setFont (showcontrol::masterDeck::systemTimeFont());
        systemTimeLabel.setMinimumHorizontalScale (1.0f);
        systemTimeLabel.setJustificationType (juce::Justification::centredRight);
        systemTimeLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        systemTimeLabel.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);

        addAndMakeVisible (volLabel); 
        volLabel.setText (juce::String::fromUTF8 (u8"VOL"), juce::dontSendNotification);
        volLabel.setFont (ShowTheme::fontBold (9.0f));
        volLabel.setJustificationType (juce::Justification::centred);

        addAndMakeVisible (volValueLabel);
        volValueLabel.setText ("100%", juce::dontSendNotification);
        volValueLabel.setFont (showcontrol::masterDeck::volumeValueFont());
        volValueLabel.setJustificationType (juce::Justification::centred);
        volValueLabel.setEditable (true, true, false);
        volValueLabel.onTextChange = [this] {
            juce::String text = volValueLabel.getText().replace ("%", "").trim();
            int percentValue = text.getIntValue();
            percentValue = juce::jlimit (0, 100, percentValue);
            masterVolSlider.setValue (percentValue / 100.0, juce::sendNotification);
        };

        addAndMakeVisible (horizontalPeakMeter);
        horizontalPeakMeter.setColourHost (this);

        deckTransportLook = std::make_unique<DeckTransportButtonLook>();
        deckVolumeLook    = std::make_unique<DeckVolumeSliderLook>();
        deckPositionLook  = std::make_unique<DeckPositionSliderLook>();
        MasterDeckComponent::applyColoursTo (*deckTransportLook, true);
        MasterDeckComponent::applyColoursTo (*deckVolumeLook, true);

        positionSlider.setLookAndFeel (deckPositionLook.get());

        addAndMakeVisible (pauseAllBtn);
        pauseAllBtn.setComponentID ("deck_pause");
        pauseAllBtn.setButtonText ({});
        pauseAllBtn.setLookAndFeel (deckTransportLook.get());

        addAndMakeVisible (stopAllBtn);
        stopAllBtn.setComponentID ("deck_stop");
        stopAllBtn.setButtonText ({});
        stopAllBtn.setLookAndFeel (deckTransportLook.get());

        addAndMakeVisible (fadeAllBtn);
        fadeAllBtn.setComponentID ("deck_fade");
        fadeAllBtn.setButtonText ({});
        fadeAllBtn.setLookAndFeel (deckTransportLook.get());

        for (auto* transportBtn : { &pauseAllBtn, &stopAllBtn, &fadeAllBtn,
                                    &bgmPrevBtn, &bgmPlayPauseBtn, &bgmNextBtn })
        {
            transportBtn->setWantsKeyboardFocus (false);
            transportBtn->setMouseClickGrabsKeyboardFocus (false);
        }

        pauseAllBtn.onClick = [this] { if (onPauseAll) onPauseAll(); };
        stopAllBtn.onClick = [this] { if (onStopAll) onStopAll(); };
        fadeAllBtn.onClick = [this] { if (onFadeAll) onFadeAll(); };

        addAndMakeVisible (bgmPrevBtn);
        bgmPrevBtn.setComponentID ("deck_bgm_prev");
        bgmPrevBtn.setButtonText ({});
        bgmPrevBtn.setLookAndFeel (deckTransportLook.get());
        bgmPrevBtn.onClick = [this] { if (onBgmPrev) onBgmPrev(); };

        addAndMakeVisible (bgmPlayPauseBtn);
        bgmPlayPauseBtn.setComponentID ("deck_bgm_play");
        bgmPlayPauseBtn.setButtonText ({});
        bgmPlayPauseBtn.getProperties().set ("sc_playing", false);
        bgmPlayPauseBtn.setLookAndFeel (deckTransportLook.get());
        bgmPlayPauseBtn.onClick = [this] { if (onBgmPlayPause) onBgmPlayPause(); };

        addAndMakeVisible (bgmNextBtn);
        bgmNextBtn.setComponentID ("deck_bgm_next");
        bgmNextBtn.setButtonText ({});
        bgmNextBtn.setLookAndFeel (deckTransportLook.get());
        bgmNextBtn.onClick = [this] { if (onBgmNext) onBgmNext(); };

        bgmPrevBtn.setVisible (false);
        bgmPlayPauseBtn.setVisible (false);
        bgmNextBtn.setVisible (false);

        addAndMakeVisible (stageMonitorBtn);
        stageMonitorBtn.setComponentID ("deck_monitor");
        stageMonitorBtn.setButtonText (showcontrol::localization::tr (u8"Monitor"));
        pauseAllBtn.setTooltip (showcontrol::localization::tr (
            u8"Kích hoạt phanh tay tạm dừng hoặc phát tiếp tục tại chỗ"));
        stageMonitorBtn.setLookAndFeel (deckTransportLook.get());
        stageMonitorBtn.onClick = [this] { if (onStageMonitorToggleRequested) onStageMonitorToggleRequested(); };

        addAndMakeVisible (audioSettingsBtn);
        audioSettingsBtn.setComponentID ("deck_gear");
        audioSettingsBtn.setButtonText ({});
        stageMonitorBtn.setTooltip (showcontrol::localization::tr (
            u8"Bật/Tắt màn hình phụ giám sát đếm ngược dành cho sân khấu và đạo diễn kịch bản."));
        audioSettingsBtn.setTooltip (showcontrol::localization::tr (
            u8"Cài đặt — Thiết bị âm thanh, Output Bus và Giao diện"));
        audioSettingsBtn.setLookAndFeel (deckTransportLook.get());
        audioSettingsBtn.onClick = [this] { if (onAudioSettingsRequested) onAudioSettingsRequested(); };

        masterVolSlider.setLookAndFeel (deckVolumeLook.get());

        setOpaque (true);
        updateThemeColors (true);
        startTimerHz (60);
    }

    ~MasterDeckPanel() override
    {
        stopTimer();
        pauseAllBtn.setLookAndFeel (nullptr);
        stopAllBtn.setLookAndFeel (nullptr);
        fadeAllBtn.setLookAndFeel (nullptr);
        bgmPrevBtn.setLookAndFeel (nullptr);
        bgmPlayPauseBtn.setLookAndFeel (nullptr);
        bgmNextBtn.setLookAndFeel (nullptr);
        stageMonitorBtn.setLookAndFeel (nullptr);
        audioSettingsBtn.setLookAndFeel (nullptr);
        masterVolSlider.setLookAndFeel (nullptr);
        positionSlider.setLookAndFeel (nullptr);
    }

    std::function<void(float)> onVolumeChanged;
    std::function<void()> onPauseAll;
    std::function<void()> onStopAll;
    std::function<void()> onFadeAll;
    std::function<void()> onBgmPrev;
    std::function<void()> onBgmPlayPause;
    std::function<void()> onBgmNext;
    std::function<float()> getMasterLevelLeft;
    std::function<float()> getMasterLevelRight;
    /** PAD → CUE → BGM: đồng bộ deck với pad đang phát — độc lập sự kiện Sidebar. */
    std::function<SoundPad*()> resolveLiveTransportPad;
    std::function<void()> onAudioSettingsRequested;
    std::function<void()> onStageMonitorToggleRequested;

    void setMasterVolumeValue (float volume, juce::NotificationType notify = juce::dontSendNotification)
    {
        masterVolSlider.setValue (volume, notify);
        if (! volValueLabel.isBeingEdited())
            volValueLabel.setText (juce::String (juce::roundToInt (volume * 100.0f)) + "%", juce::dontSendNotification);
    }

    void setActivePad (SoundPad* pad)
    {
        const bool padChanged = (activePad != pad);
        activePad = pad;
        setTrackAccentFromPad (pad);

        if (padChanged)
        {
            refreshTransportLabels();
            repaint();
        }

        if (activePad == nullptr)
        {
            remainingTimeLabel.setText ("00:00.0", juce::dontSendNotification);
            totalTimeLabel.setText ("00:00.0", juce::dontSendNotification);
            trackMetaLabel.setText ({}, juce::dontSendNotification);
            positionSlider.setValue (0.0, juce::dontSendNotification);
        }
    }

    void setTrackAccentFromPad (SoundPad* pad) noexcept
    {
        const auto pal = ShowTheme::get (isDarkMode);
        juce::Colour accent = pal.accent;

        if (pad != nullptr && ! showcontrol::colours::isDefaultTagColour (pad->getTagColour()))
            accent = pad->getTagColour();

        setTrackAccentColour (accent);
    }

    void setTrackAccentColour (juce::Colour accent) noexcept
    {
        if (trackAccent == accent)
            return;

        trackAccent = accent;
        applyTrackAccentToUi();
        repaintTransportCluster();
    }

    void setTrackMetadata (const AudioMetadata& meta)
    {
        juce::ignoreUnused (meta);
        trackMetaLabel.setText ({}, juce::dontSendNotification);
        trackMetaLabel.setVisible (false);
    }

    void refreshTransportLabels()
    {
        if (activePad == nullptr || ! activePad->hasAudioFile())
            return;

        const double total = activePad->getEffectiveLength();
        const double remaining = activePad->getRemainingSeconds();

        remainingTimeLabel.setText (activePad->formatTimeString (remaining), juce::dontSendNotification);
        totalTimeLabel.setText (activePad->formatTimeString (total), juce::dontSendNotification);

        if (total > 0.0 && ! positionSlider.isMouseButtonDown())
        {
            const double elapsed = activePad->getElapsedSeconds();
            const double pct = juce::jlimit (0.0, 1.0, elapsed / total);
            positionSlider.setValue (pct, juce::dontSendNotification);
        }
    }

    void refreshTypography()
    {
        remainingTimeLabel.setFont (showcontrol::masterDeck::remainingTimeFont());
        totalTimeLabel.setFont (showcontrol::masterDeck::totalTimeFont());
        systemTimeLabel.setFont (showcontrol::masterDeck::systemTimeFont());
    }

    void refreshLocalizedText()
    {
        stageMonitorBtn.setButtonText (showcontrol::localization::tr (u8"Monitor"));
        pauseAllBtn.setTooltip (showcontrol::localization::tr (
            u8"Kích hoạt phanh tay tạm dừng hoặc phát tiếp tục tại chỗ"));
        stageMonitorBtn.setTooltip (showcontrol::localization::tr (
            u8"Bật/Tắt màn hình phụ giám sát đếm ngược dành cho sân khấu và đạo diễn kịch bản."));
        audioSettingsBtn.setTooltip (showcontrol::localization::tr (
            u8"Cài đặt — Thiết bị âm thanh, Output Bus và Giao diện"));
        repaint();
    }

    void lookAndFeelChanged() override
    {
        updateThemeColors (resolveActiveDarkMode());
        juce::Component::lookAndFeelChanged();
        repaintTransportCluster();
        refreshLocalizedText();
    }

    void updateThemeColors (bool isDark)
    {
        isDarkMode = isDark;
        const auto pal = ShowTheme::get (isDark);

        MasterDeckComponent::applyColoursTo (*this, isDark);
        refreshTypography();

        if (deckTransportLook != nullptr)
            MasterDeckComponent::applyColoursTo (*deckTransportLook, isDark);

        if (deckVolumeLook != nullptr)
            MasterDeckComponent::applyColoursTo (*deckVolumeLook, isDark);

        volLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::standbyTextColourId));
        remainingTimeLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::timeMainColourId));
        systemTimeLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::timeMainColourId).withAlpha (0.82f));
        totalTimeLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::timeSecondaryColourId));
        trackMetaLabel.setColour (juce::Label::textColourId, pal.textSecondary);
        volValueLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::timeMainColourId).withAlpha (0.75f));
        volValueLabel.setColour (juce::Label::textWhenEditingColourId, findColour (MasterDeckComponent::timeMainColourId));
        volValueLabel.setColour (juce::Label::backgroundWhenEditingColourId, findColour (MasterDeckComponent::panelBgColourId));
        masterVolSlider.setColour (juce::Slider::trackColourId, findColour (MasterDeckComponent::meterTrackColourId));
        masterVolSlider.setColour (juce::Slider::backgroundColourId, findColour (MasterDeckComponent::panelBgColourId));
        masterVolSlider.setColour (juce::Slider::thumbColourId, findColour (MasterDeckComponent::thumbColourId));
        applyTrackAccentToUi();
        repaintTransportCluster();
    }

    void resetPauseState() {}

    /** Gọi khi chuyển giữa Cue list (isBgm=false) và BGM list (isBgm=true).
        Hiển thị/ẩn các button phù hợp và cập nhật layout. */
    void setListMode (bool bgmMode)
    {
        isBgmMode = bgmMode;
        pauseAllBtn.setVisible (!bgmMode);
        pauseAllBtn.setEnabled (!bgmMode);
        bgmPrevBtn.setVisible (bgmMode);
        bgmPlayPauseBtn.setVisible (bgmMode);
        bgmNextBtn.setVisible (bgmMode);
        resized();
        repaint();
    }

    /** Cập nhật trạng thái icon và enabled của BGM transport buttons. */
    void setBgmState (bool isPlaying, bool hasPrev, bool hasNext)
    {
        bgmPlayPauseBtn.getProperties().set ("sc_playing", isPlaying);
        bgmPlayPauseBtn.repaint();
        bgmPrevBtn.setEnabled (hasPrev);
        bgmNextBtn.setEnabled (hasNext);
        bgmPrevBtn.setAlpha (hasPrev ? 1.0f : 0.45f);
        bgmNextBtn.setAlpha (hasNext ? 1.0f : 0.45f);
    }

    /** Kept for backward-compat. */
    void setCueTransportControlsVisible (bool showCueControls) { setListMode (!showCueControls); }

    void setBackupRoleStatusText (const juce::String& text)
    {
        if (backupRoleStatusText == text)
            return;

        backupRoleStatusText = text;
        repaint();
    }

    void timerCallback() override
    {
        systemTimeLabel.setText (juce::Time::getCurrentTime().formatted ("%H:%M:%S"), juce::dontSendNotification);

        if (resolveLiveTransportPad != nullptr)
        {
            if (auto* livePad = resolveLiveTransportPad())
            {
                if (livePad != activePad)
                    setActivePad (livePad);
            }
        }

        const bool transportActive = activePad != nullptr
                                         && activePad->hasAudioFile()
                                         && activePad->isTransportActive();

        if (activePad != nullptr && activePad->hasAudioFile())
        {
            refreshTransportLabels();

            if (transportActive)
            {
                const double remaining = activePad->getRemainingSeconds();

                if (remaining <= 5.0)
                {
                    if ((juce::Time::getMillisecondCounter() % 300) < 150)
                        remainingTimeLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::warningTimeColourId));
                    else
                        remainingTimeLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::timeMainColourId));
                }
                else
                {
                    remainingTimeLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::timeMainColourId));
                }

                totalTimeLabel.setColour (juce::Label::textColourId, findColour (MasterDeckComponent::timeSecondaryColourId));
            }
        }

        if (transportActive && ! isDraggingPlayhead)
        {
            repaint (getWaveformComponentBounds());
            repaint (getLeftColumnBounds());
        }
        else if (wasTransportAnimatingLastTick && ! transportActive)
        {
            repaint (getWaveformComponentBounds());
            repaint (getLeftColumnBounds());
        }

        wasTransportAnimatingLastTick = transportActive;

        if (getMasterLevelLeft && getMasterLevelRight)
        {
            const float levelL = juce::jlimit (0.0f, 1.0f, getMasterLevelLeft());
            const float levelR = juce::jlimit (0.0f, 1.0f, getMasterLevelRight());
            horizontalPeakMeter.setLevels (levelL, levelR);
        }
        else
        {
            horizontalPeakMeter.setLevels (0.0f, 0.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        constexpr int kLocalTopDragBand = 31;

        if (e.y < kLocalTopDragBand && e.mods.isLeftButtonDown())
        {
            if (! showcontrol::mac::isMouseOverInteractiveDescendant (*this, e.getPosition()))
            {
                if (auto* topLevel = getTopLevelComponent())
                {
                    if (auto* peer = topLevel->getPeer())
                    {
                       #if JUCE_MAC
                        if (showcontrol::mac::startDraggingWindow (*peer, e))
                            return;
                       #endif
                    }
                }
            }
        }

        auto waveBounds = getWaveBounds();

        if (waveBounds.contains (e.getPosition()) && activePad)
        {
            isDraggingPlayhead = true;
            updatePlayheadPosition (e.x, waveBounds);
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (isDraggingPlayhead) updatePlayheadPosition (e.x, getWaveBounds()); }
    void mouseUp (const juce::MouseEvent&) override { isDraggingPlayhead = false; }

    void paint (juce::Graphics& g) override
    {
        auto b = showcontrol::gfx::sanitise (getLocalBounds().toFloat());
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
            return;

        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        g.setColour (getLookAndFeel().findColour (juce::ListBox::outlineColourId));
        g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

        MasterDeckUi::drawPanelFrame (g, getLeftColumnBounds().toFloat(), *this);
        MasterDeckUi::drawPanelFrame (g, getWaveColumnBounds().toFloat(), *this);
        MasterDeckUi::drawPanelFrame (g, getTransportColumnBounds().toFloat(), *this);
        MasterDeckUi::drawPanelFrame (g, getVolStripBounds().toFloat().reduced (0.5f), *this);

        auto waveBounds = getWaveBounds();
        if (waveBounds.getWidth() <= 0 || waveBounds.getHeight() <= 0)
            return;

        const auto leftCol = getLeftColumnBounds();

        g.setColour (findColour (MasterDeckComponent::panelHeaderColourId).withAlpha (0.92f));
        g.setFont (ShowTheme::fontBold (13.5f));
        const juce::String currentName = activePad ? activePad->getPadName()
                                                   : showcontrol::localization::tr (u8"KHÔNG CÓ BÀI HÁT ĐANG PHÁT");
        g.drawFittedText (currentName, leftCol.getX() + 8, leftCol.getY() + 6,
                          leftCol.getWidth() - 16, 28, juce::Justification::topLeft, 2, 0.90f);

        auto waveInner = waveBounds.reduced (1).toFloat();
        g.setColour (findColour (MasterDeckComponent::waveInnerColourId));
        g.fillRoundedRectangle (waveInner, 5.0f);

        paintWaveformLive (g, waveBounds);

        paintVolumeFaderTicks (g);
        paintBackupRoleStatus (g);
    }

    void paintBackupRoleStatus (juce::Graphics& g)
    {
        if (backupRoleStatusText.isEmpty())
            return;

        const auto leftCol = getLeftColumnBounds();
        g.setColour (findColour (MasterDeckComponent::accentTextColourId).withAlpha (0.88f));
        g.setFont (ShowTheme::fontBold (10.5f));
        g.drawText (backupRoleStatusText,
                    leftCol.getX() + 8, leftCol.getBottom() - 20,
                    leftCol.getWidth() - 16, 16,
                    juce::Justification::centredRight);
    }

    void paintWaveformLive (juce::Graphics& g, juce::Rectangle<int> waveBounds)
    {
        const auto pal = ShowTheme::get (resolveActiveDarkMode());
        const auto leftCol = getLeftColumnBounds();

        if (activePad != nullptr && activePad->hasAudioFile())
        {
            auto& thumb = activePad->getThumbnail();
            const double currentPos = activePad->getPlaybackPosition();
            double tStart = 0.0, tEnd = 0.0;
            activePad->getTrimmedDisplayRange (tStart, tEnd);
            const double effectiveLen = juce::jmax (0.0, tEnd - tStart);

            juce::Colour waveInk = pal.waveformFill.withAlpha (0.62f);
            juce::Colour playedInk = findColour (MasterDeckComponent::waveformPlayedColourId).withAlpha (0.96f);

            if (! showcontrol::colours::isDefaultTagColour (activePad->getTagColour()))
            {
                const auto tag = activePad->getTagColour();
                waveInk = tag.interpolatedWith (pal.waveformFill, 0.55f).darker (0.34f).withAlpha (0.58f);
                playedInk = tag.brighter (0.24f).withAlpha (0.97f);
            }

            g.setColour (waveInk);
            thumb.drawChannel (g, waveBounds.reduced (2), tStart, tEnd, 0, 1.0f);

            const double relativePos = juce::jlimit (tStart, tEnd, currentPos) - tStart;
            const float progress = (effectiveLen > 0.0)
                ? juce::jlimit (0.0f, 1.0f, static_cast<float> (relativePos / effectiveLen))
                : 0.0f;

            if (progress > 0.0f)
            {
                auto playedRect = showcontrol::gfx::sanitise (waveBounds.reduced (2));
                playedRect.setWidth (showcontrol::gfx::clampDimension ((int) std::round ((float) playedRect.getWidth() * progress)));

                if (showcontrol::gfx::canClip (playedRect))
                {
                    g.saveState();
                    g.reduceClipRegion (playedRect);
                    g.setColour (playedInk);
                    thumb.drawChannel (g, waveBounds.reduced (2), tStart, tEnd, 0, 1.0f);
                    g.restoreState();
                }
            }

            if (effectiveLen > 0.0)
            {
                const auto waveArea = waveBounds.reduced (2).toFloat();
                const float playheadX = waveArea.getX() + waveArea.getWidth() * progress;

                g.saveState();
                g.reduceClipRegion (waveBounds);

                if (progress > 0.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.12f));
                    g.fillRect (waveArea.withWidth (playheadX - waveArea.getX()));
                }

                g.setColour (juce::Colours::white.withAlpha (0.94f));
                showcontrol::gfx::safeDrawVerticalLine (g, (int) std::round (playheadX),
                                                        waveArea.getY(),
                                                        waveArea.getBottom());

                // Giữ playhead nằm trong waveform để tránh ghost khi repaint vùng hẹp.
                const float dotR = 2.75f;
                const float dotY = waveArea.getY() + 2.0f;
                g.fillEllipse (playheadX - dotR, dotY, dotR * 2.0f, dotR * 2.0f);
                g.setColour (playedInk.withAlpha (0.95f));
                g.drawEllipse (playheadX - dotR - 1.0f, dotY - 1.0f, dotR * 2.0f + 2.0f, dotR * 2.0f + 2.0f, 1.0f);

                g.restoreState();
            }

            if (activePad->isLooping())
            {
                g.setColour (findColour (MasterDeckComponent::accentTextColourId));
                g.setFont (ShowTheme::fontBold (10.5f));
                g.drawText (juce::String::fromUTF8 (u8"LOOP"),
                            leftCol.getX() + 8, leftCol.getBottom() - 20,
                            leftCol.getWidth() - 16, 16, juce::Justification::centredLeft);
            }
        }
        else
        {
            g.setColour (findColour (MasterDeckComponent::standbyTextColourId));
            g.setFont (ShowTheme::fontBold (12.0f));
            g.drawText (juce::String::fromUTF8 (u8"SYSTEM STANDBY"),
                        waveBounds, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto leftCol = getLeftColumnBounds();
        const auto waveBounds = getWaveBounds();

        leftCol.removeFromTop (34);
        auto timeArea = leftCol.reduced (8, 4);
        remainingTimeLabel.setBounds (timeArea.removeFromTop (juce::jmin (showcontrol::masterDeck::kRemainingTimeBlockHeight,
                                                                          timeArea.getHeight() * 3 / 5)));
        totalTimeLabel.setBounds (timeArea.removeFromTop (showcontrol::masterDeck::kTotalTimeBlockHeight));
        trackMetaLabel.setBounds (0, 0, 0, 0);
        trackMetaLabel.setVisible (false);

        positionSlider.setBounds (waveBounds);

        layoutTransportAndVolume();
    }

    void layoutTransportAndVolume()
    {
        constexpr int kMeterH      = 22;
        constexpr int kMeterGap      = 7;
        constexpr int kBtnGap        = 6;
        constexpr int kBtnH          = 30;
        constexpr int kBgmRowGap     = 4;
        constexpr int kAdminRowH     = showcontrol::masterDeck::kAdminRowHeight;
        constexpr int kAdminGap      = 6;
        constexpr int kAudioBtnW     = 34;
        constexpr int kMonitorBtnW   = 92;
        constexpr int kClockW        = showcontrol::masterDeck::kSystemClockWidth;
        constexpr int kVolValueH     = 14;
        constexpr int kVolLabelH     = 12;
        constexpr int kVolPad        = 3;
        constexpr int kSliderW       = 15;

        auto transport = getTransportColumnBounds().reduced (8, 6);
        auto volStrip  = getVolStripBounds().reduced (6, 8);

        const auto meterBounds = transport.removeFromBottom (kMeterH);
        transport.removeFromBottom (kMeterGap);

        const int controlBlockH = isBgmMode ? (kBtnH + kBgmRowGap + kBtnH) : kBtnH;

        auto topAdminRow = transport.removeFromTop (kAdminRowH);
        auto clockArea = topAdminRow.removeFromRight (kClockW);
        systemTimeLabel.setBounds (clockArea);

        auto audioArea = topAdminRow.removeFromLeft (kAudioBtnW);
        topAdminRow.removeFromLeft (kAdminGap);
        auto monitorArea = topAdminRow.removeFromLeft (kMonitorBtnW);

        audioSettingsBtn.setBounds (audioArea.withWidth (kAudioBtnW).withHeight (kAdminRowH));
        stageMonitorBtn.setBounds (monitorArea.withHeight (kAdminRowH));

        auto buttonBlock = transport.removeFromBottom (controlBlockH);

        if (isBgmMode)
        {
            auto row1 = buttonBlock.removeFromTop (kBtnH);
            const int bw3 = (row1.getWidth() - 2 * kBtnGap) / 3;
            bgmPrevBtn.setBounds      (row1.removeFromLeft (bw3));
            row1.removeFromLeft (kBtnGap);
            bgmPlayPauseBtn.setBounds (row1.removeFromLeft (bw3));
            row1.removeFromLeft (kBtnGap);
            bgmNextBtn.setBounds (row1);

            buttonBlock.removeFromTop (kBgmRowGap);
            auto row2 = buttonBlock;
            const int bw2 = (row2.getWidth() - kBtnGap) / 2;
            stopAllBtn.setBounds (row2.removeFromLeft (bw2));
            row2.removeFromLeft (kBtnGap);
            fadeAllBtn.setBounds (row2);
            pauseAllBtn.setBounds (0, 0, 0, 0);
        }
        else
        {
            auto row1 = buttonBlock;
            const int bw3 = (row1.getWidth() - 2 * kBtnGap) / 3;
            pauseAllBtn.setBounds (row1.removeFromLeft (bw3));
            row1.removeFromLeft (kBtnGap);
            stopAllBtn.setBounds (row1.removeFromLeft (bw3));
            row1.removeFromLeft (kBtnGap);
            fadeAllBtn.setBounds (row1);
            bgmPrevBtn.setBounds (0, 0, 0, 0);
            bgmPlayPauseBtn.setBounds (0, 0, 0, 0);
            bgmNextBtn.setBounds (0, 0, 0, 0);
        }

        horizontalPeakMeter.setBounds (meterBounds);

        volValueLabel.setBounds (volStrip.removeFromTop (kVolValueH));
        volStrip.removeFromTop (kVolPad);
        volLabel.setBounds (volStrip.removeFromBottom (kVolLabelH));
        volStrip.removeFromBottom (kVolPad);

        masterVolSlider.setBounds (volStrip.getCentreX() - kSliderW / 2,
                                   volStrip.getY(),
                                   kSliderW,
                                   volStrip.getHeight());
    }

    void paintVolumeFaderTicks (juce::Graphics& g) const
    {
        const auto sliderBounds = masterVolSlider.getBounds();
        if (sliderBounds.getHeight() < 8)
            return;

        const auto tickCol = findColour (MasterDeckComponent::standbyTextColourId).withAlpha (0.40f);
        g.setColour (tickCol);

        const float sliderTop    = (float) sliderBounds.getY();
        const float sliderHeight = (float) sliderBounds.getHeight();
        const float sliderRight  = (float) sliderBounds.getRight();
        const float sliderLeft   = (float) sliderBounds.getX();

        for (int i = 0; i <= 10; ++i)
        {
            const float yPos = sliderTop + sliderHeight * ((float) i / 10.0f);
            g.drawHorizontalLine (yPos, sliderLeft - 6.0f, sliderLeft - 2.0f);
            g.drawHorizontalLine (yPos, sliderRight + 2.0f, sliderRight + 6.0f);
        }
    }

    void applyTrackAccentToUi()
    {
        setColour (MasterDeckComponent::thumbColourId, trackAccent);
        setColour (MasterDeckComponent::playheadColourId, trackAccent);
        setColour (MasterDeckComponent::waveformPlayedColourId, trackAccent);
        setColour (MasterDeckComponent::accentTextColourId, trackAccent);

        masterVolSlider.setColour (juce::Slider::thumbColourId, trackAccent);

        if (deckVolumeLook != nullptr)
        {
            deckVolumeLook->setColour (MasterDeckComponent::thumbColourId, trackAccent);
            deckVolumeLook->setColour (MasterDeckComponent::playheadColourId, trackAccent);
        }
    }

    bool resolveActiveDarkMode() const noexcept
    {
        if (auto* showLaf = dynamic_cast<const ShowControlLookAndFeel*> (&getLookAndFeel()))
            return showLaf->isDarkMode();

        if (auto* parent = getParentComponent())
            if (auto* parentLaf = dynamic_cast<const ShowControlLookAndFeel*> (&parent->getLookAndFeel()))
                return parentLaf->isDarkMode();

        return isDarkMode;
    }

    void repaintTransportCluster()
    {
        horizontalPeakMeter.repaint();
        masterVolSlider.repaint();
        volLabel.repaint();
        volValueLabel.repaint();

        for (auto* btn : { &pauseAllBtn, &stopAllBtn, &fadeAllBtn,
                           &bgmPrevBtn, &bgmPlayPauseBtn, &bgmNextBtn,
                           &stageMonitorBtn, &audioSettingsBtn })
            btn->repaint();

        repaint();
    }

private:
    struct DeckColumns
    {
        juce::Rectangle<int> left;
        juce::Rectangle<int> wave;
        juce::Rectangle<int> right;
    };

    struct DeckLayout
    {
        int pad          = 12;
        int innerGap     = 6;
        int meterW       = 14;
        int volStripMinW = 44;
        int leftPct      = 20;
        int wavePct      = 50;
        int rightPct     = 30;
    };

    DeckLayout layout;

    juce::Rectangle<int> getContentBounds() const
    {
        return getLocalBounds().reduced (layout.pad);
    }

    DeckColumns getDeckColumns() const
    {
        auto content = getContentBounds();
        const int gap = layout.innerGap;
        const int usableW = juce::jmax (0, content.getWidth() - 2 * gap);

        int leftW  = usableW * layout.leftPct / 100;
        int waveW  = usableW * layout.wavePct / 100;
        int rightW = usableW - leftW - waveW;

        if (rightW < 0)
        {
            rightW = 0;
            const int overflow = leftW + waveW - usableW;
            waveW = juce::jmax (0, waveW - overflow);
        }

        int x = content.getX();
        const int y = content.getY();
        const int h = content.getHeight();

        DeckColumns cols;
        cols.left  = { x, y, leftW, h };
        x += leftW + gap;
        cols.wave  = { x, y, waveW, h };
        x += waveW + gap;
        cols.right = { x, y, rightW, h };
        return cols;
    }

    juce::Rectangle<int> getLeftColumnBounds() const  { return getDeckColumns().left; }
    juce::Rectangle<int> getWaveColumnBounds() const   { return getDeckColumns().wave; }
    juce::Rectangle<int> getRightColumnBounds() const  { return getDeckColumns().right; }

    juce::Rectangle<int> getTransportColumnBounds() const
    {
        auto right = getRightColumnBounds();
        const int volW = juce::jmax (layout.volStripMinW, right.getWidth() * 17 / 100);
        return { right.getX(), right.getY(), juce::jmax (0, right.getWidth() - volW - layout.innerGap), right.getHeight() };
    }

    juce::Rectangle<int> getVolStripBounds() const
    {
        auto right = getRightColumnBounds();
        const int volW = juce::jmax (layout.volStripMinW, right.getWidth() * 17 / 100);
        return { right.getRight() - volW, right.getY(), volW, right.getHeight() };
    }

    juce::Rectangle<int> getWaveBounds() const
    {
        const auto col = getWaveColumnBounds();
        if (col.isEmpty())
            return {};

        const int innerPad = 10;
        const int waveW = juce::jmax (10, col.getWidth() - innerPad * 2);
        const int waveH = juce::jmax (44, col.getHeight() - innerPad * 2);
        const int y = col.getY() + (col.getHeight() - waveH) / 2;

        return { col.getX() + innerPad, y, waveW, waveH };
    }

    juce::Rectangle<int> getWaveformComponentBounds() const { return getWaveBounds(); }
    
    void updatePlayheadPosition (int mouseX, const juce::Rectangle<int>& waveBounds)
    {
        if (activePad == nullptr)
            return;

        const float ratio = showcontrol::gfx::ratioFromMouseX (mouseX, waveBounds);
        const double tStart = activePad->getTrimStart();
        const double tLen   = showcontrol::gfx::safePositiveDuration (activePad->getEffectiveLength());

        if (tLen > 0.0)
            activePad->seekTo (tStart + (double) ratio * tLen);
    }

    bool isDarkMode, isDraggingPlayhead, isPaused, isBgmMode = false;
    bool wasTransportAnimatingLastTick = false;
    juce::String backupRoleStatusText;
    SoundPad* activePad;
    juce::Colour trackAccent { showcontrol::colours::tagColourAt (7) };
    juce::Label remainingTimeLabel, totalTimeLabel, trackMetaLabel, volLabel, volValueLabel, systemTimeLabel;
    juce::Slider masterVolSlider, positionSlider;
    juce::TextButton pauseAllBtn, stopAllBtn, fadeAllBtn;
    juce::TextButton bgmPrevBtn, bgmPlayPauseBtn, bgmNextBtn;
    juce::TextButton stageMonitorBtn;
    juce::TextButton audioSettingsBtn;
    HorizontalPeakMeter horizontalPeakMeter;
    std::unique_ptr<DeckTransportButtonLook> deckTransportLook;
    std::unique_ptr<DeckVolumeSliderLook> deckVolumeLook;
    std::unique_ptr<DeckPositionSliderLook> deckPositionLook;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterDeckPanel)
};