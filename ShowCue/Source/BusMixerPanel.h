#pragma once
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowGraphicsSafe.h"

//==============================================================================
/** 8 stereo bus — gain + VU (đọc peak atomics từ MultiOutputAudioCallback). */
class BusMixerPanel : public juce::Component, public juce::Timer
{
public:
    static constexpr int kNumBuses = 8;

    BusMixerPanel()
    {
        for (int i = 0; i < kNumBuses; ++i)
        {
            addAndMakeVisible (nameLabels[i]);
            nameLabels[i].setFont (ShowTheme::fontBold (9.0f));
            nameLabels[i].setJustificationType (juce::Justification::centred);

            addAndMakeVisible (gainSliders[i]);
            gainSliders[i].setSliderStyle (juce::Slider::LinearVertical);
            gainSliders[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            gainSliders[i].setRange (0.0, 1.0, 0.01);
            gainSliders[i].setValue (1.0, juce::dontSendNotification);

            const int bus = i;
            gainSliders[i].onValueChange = [this, bus]
            {
                if (onBusGainChanged)
                    onBusGainChanged (bus, (float) gainSliders[bus].getValue());
            };
        }

        startTimerHz (30);
    }

    ~BusMixerPanel() override { stopTimer(); }

    std::function<void(int busIndex, float gain)> onBusGainChanged;
    std::function<float(int busIndex, bool isLeft)> getBusPeak;
    std::function<juce::String(int busIndex)> getBusName;

    void setBusGain (int bus, float gain, juce::NotificationType notify = juce::dontSendNotification)
    {
        if (bus >= 0 && bus < kNumBuses)
            gainSliders[bus].setValue (gain, notify);
    }

    void updateTheme (bool isDark)
    {
        isDarkMode = isDark;
        const auto pal = ShowTheme::get (isDark);
        for (int i = 0; i < kNumBuses; ++i)
        {
            nameLabels[i].setColour (juce::Label::textColourId, pal.textMuted);
            gainSliders[i].setColour (juce::Slider::trackColourId, pal.panelElevated);
            gainSliders[i].setColour (juce::Slider::thumbColourId, i == 0 ? pal.accent : pal.accentSoft);
        }
        repaint();
    }

    void timerCallback() override
    {
        if (getBusName)
        {
            for (int i = 0; i < kNumBuses; ++i)
            {
                auto n = getBusName (i);
                nameLabels[i].setText (n.isNotEmpty() ? n : ("Bus " + juce::String (i)), juce::dontSendNotification);
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.fillAll (pal.panelBg);
        g.setColour (pal.borderSubtle);
        if (getWidth() > 0)
            g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        const int panelW = showcontrol::gfx::clampDimension (getWidth());
        if (panelW <= 0)
            return;

        const int colW = juce::jmax (1, panelW / kNumBuses);
        for (int i = 0; i < kNumBuses; ++i)
        {
            const int x = i * colW + 4;
            const int meterW = juce::jmax (4, colW / 2 - 6);
            const int meterH = juce::jmax (0, getHeight() - 52);
            const int meterY = 36;

            float peakL = 0.0f, peakR = 0.0f;
            if (getBusPeak)
            {
                peakL = getBusPeak (i, true);
                peakR = getBusPeak (i, false);
            }

            auto drawMeter = [&] (int mx, float peak)
            {
                showcontrol::gfx::safeFillRect (g, mx, meterY, meterW, meterH);
                const int fillH = showcontrol::gfx::clampDimension ((int) (meterH * juce::jlimit (0.0f, 1.0f, peak)));
                if (fillH > 0 && meterH > 0)
                {
                    g.setColour (peak > 0.9f ? pal.danger : (peak > 0.65f ? pal.warning : pal.success));
                    showcontrol::gfx::safeFillRect (g, mx, meterY + meterH - fillH, meterW, fillH);
                }
            };

            drawMeter (x, peakL);
            drawMeter (x + meterW + 2, peakR);
        }
    }

    void resized() override
    {
        const int colW = getWidth() / kNumBuses;
        for (int i = 0; i < kNumBuses; ++i)
        {
            auto col = juce::Rectangle<int> (i * colW, 0, colW, getHeight()).reduced (2, 4);
            nameLabels[i].setBounds (col.removeFromTop (14));
            col.removeFromTop (2);
            gainSliders[i].setBounds (col.removeFromBottom (juce::jmin (col.getHeight(), 48)));
        }
    }

private:
    bool isDarkMode = true;
    std::array<juce::Label, kNumBuses> nameLabels;
    std::array<juce::Slider, kNumBuses> gainSliders;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BusMixerPanel)
};
