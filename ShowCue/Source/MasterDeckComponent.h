#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"
#include "ShowFlatIcons.h"

/**
 * Colour IDs + palette động cho MainDesk (MasterDeckPanel).
 * Message-thread only — không gọi từ audio callback.
 */
struct MasterDeckComponent
{
    enum ColourIds
    {
        backgroundColourId     = 0x2001000,
        panelBgColourId,
        panelBorderColourId,
        panelHeaderColourId,
        accentTextColourId,
        timeMainColourId,
        timeSecondaryColourId,
        standbyTextColourId,
        warningTimeColourId,
        stopButtonColourId,
        fadeButtonColourId,
        playButtonColourId,
        transportBgColourId,
        ledLiveColourId,
        thumbColourId,
        meterTrackColourId,
        meterFillLowColourId,
        meterFillMidColourId,
        meterFillHighColourId,
        waveInnerColourId,
        waveformPlayedColourId,
        playheadColourId,
        gearIconColourId,
        transportIconColourId,
        navButtonBgColourId
    };

    struct DeckPalette
    {
        juce::Colour background;
        juce::Colour panelBg;
        juce::Colour panelBorder;
        juce::Colour panelHeader;
        juce::Colour accentText;
        juce::Colour timeMain;
        juce::Colour timeSecondary;
        juce::Colour standbyText;
        juce::Colour warningTime;
        juce::Colour stopButton;
        juce::Colour fadeButton;
        juce::Colour playButton;
        juce::Colour transportBg;
        juce::Colour ledLive;
        juce::Colour thumb;
        juce::Colour meterTrack;
        juce::Colour meterFillLow;
        juce::Colour meterFillMid;
        juce::Colour meterFillHigh;
        juce::Colour waveInner;
        juce::Colour waveformPlayed;
        juce::Colour playhead;
        juce::Colour gearIcon;
        juce::Colour transportIcon;
        juce::Colour navButtonBg;
    };

    /** Ma trận deck đồng bộ 100% từ ShowTheme — không neon tự do. */
    static DeckPalette deckPalette (bool isDark) noexcept
    {
        const auto pal = ShowTheme::get (isDark);

        return {
            pal.windowBg,
            pal.panelBg,
            pal.border,
            pal.textPrimary,
            pal.accent,
            pal.textPrimary,
            pal.textSecondary,
            pal.textSecondary,
            pal.warning,
            pal.danger,
            pal.accentSoft,
            pal.success,
            pal.panelBg,
            pal.accent,
            pal.sliderThumb,
            pal.borderSubtle,
            pal.success,
            pal.warning,
            pal.danger,
            pal.centerBg,
            pal.accent.withAlpha (0.38f),
            pal.textPrimary,
            pal.textSecondary,
            pal.textPrimary,
            pal.navButtonBg
        };
    }

    template <typename ColourTarget>
    static void applyColoursTo (ColourTarget& target, bool isDark) noexcept
    {
        const auto d = deckPalette (isDark);

        target.setColour (backgroundColourId,     d.background);
        target.setColour (panelBgColourId,        d.panelBg);
        target.setColour (panelBorderColourId,    d.panelBorder);
        target.setColour (panelHeaderColourId,    d.panelHeader);
        target.setColour (accentTextColourId,     d.accentText);
        target.setColour (timeMainColourId,       d.timeMain);
        target.setColour (timeSecondaryColourId,  d.timeSecondary);
        target.setColour (standbyTextColourId,    d.standbyText);
        target.setColour (warningTimeColourId,    d.warningTime);
        target.setColour (stopButtonColourId,     d.stopButton);
        target.setColour (fadeButtonColourId,     d.fadeButton);
        target.setColour (playButtonColourId,     d.playButton);
        target.setColour (transportBgColourId,    d.transportBg);
        target.setColour (ledLiveColourId,       d.ledLive);
        target.setColour (thumbColourId,          d.thumb);
        target.setColour (meterTrackColourId,     d.meterTrack);
        target.setColour (meterFillLowColourId,   d.meterFillLow);
        target.setColour (meterFillMidColourId,    d.meterFillMid);
        target.setColour (meterFillHighColourId,   d.meterFillHigh);
        target.setColour (waveInnerColourId,       d.waveInner);
        target.setColour (waveformPlayedColourId,  d.waveformPlayed);
        target.setColour (playheadColourId,       d.playhead);
        target.setColour (gearIconColourId,       d.gearIcon);
        target.setColour (transportIconColourId,  d.transportIcon);
        target.setColour (navButtonBgColourId,    d.navButtonBg);
    }

    static juce::Colour findDeckColour (const juce::Component& source, int colourId) noexcept
    {
        return source.findColour (colourId);
    }

    /** lucide:settings — nút Cài đặt MainDesk (16×16 outline). */
    static void drawSettingsGearIcon (juce::Graphics& g,
                                      juce::Rectangle<float> area,
                                      juce::Colour colour,
                                      juce::Colour /*innerHole*/,
                                      bool highlighted) noexcept
    {
        if (highlighted)
            colour = colour.brighter (0.25f);

        showcontrol::icons::paintSettingsIcon (g,
                                               showcontrol::icons::centredIconIn (area, showcontrol::icons::kButtonIconSize),
                                               colour);
    }
};
