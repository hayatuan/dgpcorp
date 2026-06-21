#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ShowControlMacWindow.h"
#include "ShowDsp.h"
#include "ShowLocalization.h"
#include "ShowPluginHost.h"
#include "ShowTheme.h"

namespace showcontrol::ui
{

class ShowStockFxEditorDialog final : public juce::DocumentWindow
{
public:
    ShowStockFxEditorDialog (showcontrol::plugins::PadPluginSlot& slotIn,
                             juce::Component* centreRelativeTo,
                             std::function<void()> onClosedIn)
        : juce::DocumentWindow (slotIn.getStockFx().getMode() == PadStockFxProcessor::Mode::eq3
                                    ? showcontrol::localization::tr (u8"Stock EQ (3-Band)")
                                    : showcontrol::localization::tr (u8"Stock Limiter"),
                                juce::Desktop::getInstance().getDefaultLookAndFeel()
                                    .findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::closeButton),
          slot (&slotIn),
          onClosed (std::move (onClosedIn))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setContentOwned (new ContentPanel (*slot), true);
        centreWithSize (360, slotIn.getStockFx().getMode() == PadStockFxProcessor::Mode::eq3 ? 220 : 180);

        if (centreRelativeTo != nullptr)
            showcontrol::ui::centreFloatingWindowInMainApp (*this, centreRelativeTo);

        setVisible (true);
        toFront (true);
    }

    showcontrol::plugins::PadPluginSlot& getPluginSlot() noexcept { return *slot; }

    void closeButtonPressed() override
    {
        if (onClosed)
            onClosed();

        delete this;
    }

private:
    class ContentPanel final : public juce::Component
    {
    public:
        explicit ContentPanel (showcontrol::plugins::PadPluginSlot& slotIn)
            : slot (&slotIn)
        {
            auto& fx = slotIn.getStockFx();

            if (fx.getMode() == PadStockFxProcessor::Mode::eq3)
            {
                addSlider (lowLabel,  lowSlider,  showcontrol::localization::tr (u8"Low"),  fx.getEqLowGainDb(),  -12.0, 12.0,
                           [] (PadStockFxProcessor& p, double v) { p.setEqLowGainDb ((float) v); });
                addSlider (midLabel,  midSlider,  showcontrol::localization::tr (u8"Mid"),  fx.getEqMidGainDb(),  -12.0, 12.0,
                           [] (PadStockFxProcessor& p, double v) { p.setEqMidGainDb ((float) v); });
                addSlider (highLabel, highSlider, showcontrol::localization::tr (u8"High"), fx.getEqHighGainDb(), -12.0, 12.0,
                           [] (PadStockFxProcessor& p, double v) { p.setEqHighGainDb ((float) v); });
            }
            else
            {
                addSlider (thresholdLabel, thresholdSlider,
                           showcontrol::localization::tr (u8"Threshold (dB)"),
                           fx.getLimiterThresholdDb(), -12.0, 0.0,
                           [] (PadStockFxProcessor& p, double v) { p.setLimiterThresholdDb ((float) v); });
                addSlider (releaseLabel, releaseSlider,
                           showcontrol::localization::tr (u8"Release (ms)"),
                           fx.getLimiterReleaseMs(), 10.0, 500.0,
                           [] (PadStockFxProcessor& p, double v) { p.setLimiterReleaseMs ((float) v); });
            }

            setSize (340, fx.getMode() == PadStockFxProcessor::Mode::eq3 ? 200 : 160);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (ShowTheme::get (true).panelElevated);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (16);
            const int rowH = 34;

            for (auto* row : { &lowLabel, &midLabel, &highLabel, &thresholdLabel, &releaseLabel })
            {
                if (! row->isVisible())
                    continue;

                auto r = area.removeFromTop (rowH);
                row->setBounds (r.removeFromLeft (110));
                r.removeFromLeft (8);

                if (row == &lowLabel)       lowSlider.setBounds (r);
                else if (row == &midLabel)  midSlider.setBounds (r);
                else if (row == &highLabel) highSlider.setBounds (r);
                else if (row == &thresholdLabel) thresholdSlider.setBounds (r);
                else if (row == &releaseLabel)   releaseSlider.setBounds (r);
            }
        }

    private:
        template <typename Fn>
        void addSlider (juce::Label& label, juce::Slider& slider, const juce::String& text,
                        double value, double minVal, double maxVal, Fn&& applyFn)
        {
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centredLeft);
            label.setColour (juce::Label::textColourId, ShowTheme::get (true).textPrimary);
            addAndMakeVisible (label);

            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
            slider.setRange (minVal, maxVal, 0.1);
            slider.setValue (value, juce::dontSendNotification);
            slider.onValueChange = [this, s = &slider, applyFn]
            {
                if (slot != nullptr)
                    applyFn (slot->getStockFx(), s->getValue());
            };
            addAndMakeVisible (slider);
        }

        showcontrol::plugins::PadPluginSlot* slot = nullptr;
        juce::Label lowLabel, midLabel, highLabel, thresholdLabel, releaseLabel;
        juce::Slider lowSlider, midSlider, highSlider, thresholdSlider, releaseSlider;
    };

    showcontrol::plugins::PadPluginSlot* slot = nullptr;
    std::function<void()> onClosed;
};

} // namespace showcontrol::ui
