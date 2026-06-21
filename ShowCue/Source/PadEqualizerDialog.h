#pragma once

#include <array>
#include <cmath>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowDsp.h"
#include "ShowTheme.h"
#include "ShowControlMacWindow.h"
#include "ShowLocalization.h"
#include "SoundPad.h"

namespace showcontrol::ui
{

//==============================================================================
/** Lưới log + đường cong PEQ 6-band (kéo dọc để chỉnh gain dB). */
class Peq6CurveDisplay : public juce::Component
{
public:
    static constexpr float kMinHz   = 25.0f;
    static constexpr float kMaxHz   = 20000.0f;
    static constexpr float kMinDb   = -18.0f;
    static constexpr float kMaxDb   = 18.0f;
    static constexpr float kDragDb  = 12.0f;

    std::function<void(int band, float gainDb)> onBandGainChanged;
    std::function<void(int band, float gainDb)> onBandGainFinished;

    Peq6CurveDisplay()
    {
        static const juce::Colour bandCols[PadParametricEq6::kNumBands] =
        {
            juce::Colour (0xff5ec8e8), // HP
            juce::Colour (0xff3db8a8), // LS
            juce::Colour (0xff2a9d8f), // P1
            juce::Colour (0xffc45c9a), // P2
            juce::Colour (0xffe8a030), // HS
            juce::Colour (0xffe07040)  // LP
        };
        for (int i = 0; i < PadParametricEq6::kNumBands; ++i)
            bandColours[(size_t) i] = bandCols[i];
        rebuildCoeffCache();
    }

    void setDarkMode (bool dark) { isDark = dark; repaint(); }

    void setPreviewSampleRate (double sampleRate) noexcept
    {
        if (sampleRate <= 0.0)
            return;

        if (std::abs (previewSampleRate - sampleRate) < 0.5)
            return;

        previewSampleRate = sampleRate;
        rebuildCoeffCache();
        repaint();
    }

    void setBandGainsDb (const std::array<float, PadParametricEq6::kNumBands>& gainsDb)
    {
        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
            bandGainDb[(size_t) b] = gainsDb[(size_t) b];
        rebuildCoeffCache();
        repaint();
    }

    std::array<float, PadParametricEq6::kNumBands> getBandGainsDb() const { return bandGainDb; }

    void paint (juce::Graphics& g) override
    {
        auto plot = getPlotBounds();
        if (plot.getWidth() < 8 || plot.getHeight() < 8)
            return;

        const auto pal = ShowTheme::get (isDark);
        g.fillAll (pal.padGradientBottom);

        drawGrid (g, plot);

        juce::Path curve;
        buildResponseCurve (curve, plot);
        if (! curve.isEmpty())
        {
            juce::Path fill = curve;
            fill.lineTo ((float) plot.getRight(), (float) gainToY (0.0f, plot));
            fill.lineTo ((float) plot.getX(),     (float) gainToY (0.0f, plot));
            fill.closeSubPath();
            g.setColour (juce::Colour (0xffe8a030).withAlpha (0.42f));
            g.fillPath (fill);

            g.setColour (juce::Colour (0xfff0b860));
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        g.setColour (pal.textMuted.withAlpha (0.55f));
        g.drawHorizontalLine (gainToY (0.0f, plot), (float) plot.getX(), (float) plot.getRight());

        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
            drawBandNode (g, plot, b);

        static const char* bandLabels[] = { "HP", "LS", "P1", "P2", "HS", "LP" };
        g.setFont (ShowTheme::font (9.5f));
        g.setColour (pal.textSecondary.withAlpha (0.85f));

        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
        {
            const auto node = getBandNodeCentre (plot, b);
            g.drawText (bandLabels[b],
                        (int) node.x - 16, plot.getBottom() + 2, 32, 14,
                        juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = hitTestBand (e.position);
        if (dragBand >= 0)
        {
            lastPushedGainDb = bandGainDb[(size_t) dragBand];
            lastAudioPushMs = 0;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0)
            return;

        const auto plot = getPlotBounds();
        const float db = juce::jlimit (-kDragDb, kDragDb, yToGainDb (e.position.y, plot));
        bandGainDb[(size_t) dragBand] = db;
        rebuildCoeffCache();
        pushGainToAudio (false);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
        {
            const int band = dragBand;
            const float db = bandGainDb[(size_t) band];

            if (onBandGainFinished)
                onBandGainFinished (band, db);
            else
                pushGainToAudio (true);
        }

        dragBand = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int band = hitTestBand (e.position);
        if (band < 0)
            return;

        bandGainDb[(size_t) band] = 0.0f;
        rebuildCoeffCache();

        if (onBandGainFinished)
            onBandGainFinished (band, 0.0f);
        else
            pushGainToAudio (true);

        repaint();
    }

private:
    static constexpr uint32_t kDragAudioIntervalMs = 45;

    bool isDark = true;
    int dragBand = -1;
    uint32_t lastAudioPushMs = 0;
    float lastPushedGainDb = 0.0f;
    double previewSampleRate = 48000.0;
    std::array<float, PadParametricEq6::kNumBands> bandGainDb {};
    std::array<juce::Colour, PadParametricEq6::kNumBands> bandColours {};
    std::array<PadParametricEq6::EqCoeffPtr, PadParametricEq6::kNumBands> coeffCache {};

    static juce::String bandBadgeText (int band)
    {
        switch (band)
        {
            case 0: return "HP";
            case 5: return "LP";
            default: return juce::String (band);
        }
    }

    void pushGainToAudio (bool force) noexcept
    {
        if (dragBand < 0 || onBandGainChanged == nullptr)
            return;

        const float db = bandGainDb[(size_t) dragBand];
        const auto now = juce::Time::getMillisecondCounter();

        if (! force
            && now - lastAudioPushMs < kDragAudioIntervalMs
            && std::abs (db - lastPushedGainDb) < 0.03f)
            return;

        lastAudioPushMs = now;
        lastPushedGainDb = db;
        onBandGainChanged (dragBand, db);
    }

    juce::Rectangle<int> getPlotBounds() const
    {
        return getLocalBounds().reduced (12, 40);
    }

    static float freqToNorm (float hz) noexcept
    {
        hz = juce::jlimit (kMinHz, kMaxHz, hz);
        return (std::log10 (hz) - std::log10 (kMinHz))
             / (std::log10 (kMaxHz) - std::log10 (kMinHz));
    }

    int freqToX (float hz, juce::Rectangle<int> plot) const
    {
        return plot.getX() + (int) std::round (freqToNorm (hz) * (float) (plot.getWidth() - 1));
    }

    int gainToY (float db, juce::Rectangle<int> plot) const
    {
        const float t = juce::jmap (db, kMinDb, kMaxDb, 1.0f, 0.0f);
        return plot.getY() + (int) std::round (t * (float) (plot.getHeight() - 1));
    }

    float yToGainDb (float y, juce::Rectangle<int> plot) const
    {
        const float t = juce::jlimit (0.0f, 1.0f,
                                      (y - (float) plot.getY()) / (float) juce::jmax (1, plot.getHeight() - 1));
        return juce::jmap (t, 1.0f, 0.0f, kMinDb, kMaxDb);
    }

    void rebuildCoeffCache()
    {
        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
            coeffCache[(size_t) b] = PadParametricEq6::makeBandCoefficients (
                b, bandGainDb[(size_t) b], previewSampleRate);
    }

    float responseDbAtHz (float hz) const noexcept
    {
        double linear = 1.0;

        for (const auto& coeffs : coeffCache)
        {
            if (coeffs == nullptr)
                continue;

            linear *= (double) coeffs->getMagnitudeForFrequency ((double) hz, previewSampleRate);
        }

        const float db = juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, (float) linear));
        return juce::jlimit (kMinDb, kMaxDb, db);
    }

    float nodeDisplayDb (int band) const noexcept
    {
        return bandGainDb[(size_t) band];
    }

    void buildResponseCurve (juce::Path& path, juce::Rectangle<int> plot) const
    {
        const int samples = juce::jmax (256, plot.getWidth() * 3);
        std::vector<float> dbSamples ((size_t) samples);

        for (int i = 0; i < samples; ++i)
        {
            const float t = (float) i / (float) (samples - 1);
            const float hz = std::pow (10.0f,
                std::log10 (kMinHz) + t * (std::log10 (kMaxHz) - std::log10 (kMinHz)));
            dbSamples[(size_t) i] = responseDbAtHz (hz);
        }

        // Làm mượt nhẹ kiểu VEQ (3-point) — giữ độ cong IIR, bỏ răng cưa do lượng tử hoá.
        if (samples >= 5)
        {
            std::vector<float> smooth ((size_t) samples);
            smooth[0] = dbSamples[0];
            smooth[(size_t) samples - 1] = dbSamples[(size_t) samples - 1];

            for (int i = 1; i < samples - 1; ++i)
                smooth[(size_t) i] = (dbSamples[(size_t) (i - 1)] + dbSamples[(size_t) i] * 2.0f
                                      + dbSamples[(size_t) (i + 1)]) * 0.25f;

            dbSamples = std::move (smooth);
        }

        bool started = false;
        for (int i = 0; i < samples; ++i)
        {
            const float t = (float) i / (float) (samples - 1);
            const float x = (float) plot.getX() + t * (float) (plot.getWidth() - 1);
            const float y = (float) gainToY (dbSamples[(size_t) i], plot);

            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                path.lineTo (x, y);
            }
        }
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<int> plot) const
    {
        const auto pal = ShowTheme::get (isDark);
        g.setColour (pal.border.withAlpha (0.45f));

        static const float gridHz[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        for (float hz : gridHz)
        {
            const int x = freqToX (hz, plot);
            g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
        }

        for (float db = kMinDb; db <= kMaxDb; db += 6.0f)
        {
            const int y = gainToY (db, plot);
            g.drawHorizontalLine (y, (float) plot.getX(), (float) plot.getRight());
        }

        g.setFont (showcontrol::equalizer::gridFont());
        g.setColour (pal.textMuted);
        g.drawText ("Hz", plot.getX(), plot.getBottom() + 2, 28, 16, juce::Justification::centredLeft);
        g.drawText ("dB", plot.getX() - 2, plot.getY() - 2, 24, 14, juce::Justification::bottomRight);
    }

    juce::Point<float> getBandNodeCentre (juce::Rectangle<int> plot, int band) const
    {
        const float hz = PadParametricEq6::kBandFreqHz[band];
        const float db = nodeDisplayDb (band);
        return { (float) freqToX (hz, plot), (float) gainToY (db, plot) };
    }

    void drawBandNode (juce::Graphics& g, juce::Rectangle<int> plot, int band) const
    {
        const float hz = PadParametricEq6::kBandFreqHz[band];
        const float db = nodeDisplayDb (band);
        const int x = freqToX (hz, plot);
        const int y = gainToY (db, plot);
        const auto col = bandColours[(size_t) band];

        g.setColour (col.withAlpha (0.55f));
        g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());

        const auto pal = ShowTheme::get (isDark);
        auto freqBadge = juce::Rectangle<int> (x - 30, plot.getY() - 36, 60, 14);
        g.setColour (pal.textMuted);
        g.setFont (showcontrol::equalizer::gridFont());
        g.drawText (PadParametricEq6::formatBandFrequencyHz (hz),
                    freqBadge, juce::Justification::centred);

        auto badge = juce::Rectangle<int> (x - 12, plot.getY() - 20, 24, 17);
        g.setColour (col);
        g.fillRoundedRectangle (badge.toFloat(), 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (showcontrol::equalizer::bandBadgeFont());
        g.drawText (bandBadgeText (band), badge, juce::Justification::centred);

        const float r = 9.0f;
        g.setColour (col);
        g.fillEllipse ((float) x - r, (float) y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::white);
        g.setFont (showcontrol::equalizer::bandBadgeFont().withHeight (13.0f));
        g.drawText ("+", juce::Rectangle<int> ((int) ((float) x - r), (int) ((float) y - r),
                                              (int) (r * 2.0f), (int) (r * 2.0f)),
                   juce::Justification::centred);
    }

    int hitTestBand (juce::Point<float> pos) const
    {
        const auto plot = getPlotBounds();
        int best = -1;
        float bestD = 16.0f;

        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
        {
            const float hz = PadParametricEq6::kBandFreqHz[b];
            const float db = nodeDisplayDb (b);
            const int x = freqToX (hz, plot);
            const int y = gainToY (db, plot);
            const float d = pos.getDistanceFrom (juce::Point<float> ((float) x, (float) y));
            if (d < bestD)
            {
                bestD = d;
                best = b;
            }
        }
        return best;
    }
};

//==============================================================================
class PadEqualizerDialogContent : public juce::Component
{
public:
    PadEqualizerDialogContent (SoundPad* pad,
                               bool darkTheme,
                               std::function<void()> onEditCallback)
        : currentPad (pad), isDarkTheme (darkTheme), onEdit (std::move (onEditCallback))
    {
        setSize (560, 420);

        addAndMakeVisible (enableToggle);
        enableToggle.setButtonText (showcontrol::localization::tr (u8"Bật EQ"));
        enableToggle.setTooltip (showcontrol::localization::tr (u8"Bật/tắt EQ cho track này"));
        enableToggle.setToggleState (currentPad != nullptr && currentPad->getDspEqEnabled(),
                                   juce::dontSendNotification);
        enableToggle.onClick = [this] { applyEnableFromUi(); };

        addAndMakeVisible (resetBtn);
        resetBtn.setButtonText (showcontrol::localization::tr (u8"Reset mặc định"));
        resetBtn.onClick = [this]
        {
            if (currentPad == nullptr)
                return;

            currentPad->resetDspEqToDefaults();
            enableToggle.setToggleState (false, juce::dontSendNotification);
            syncGainsFromPad();
            if (onEdit)
                onEdit();
        };

        addAndMakeVisible (peqDisplay);
        peqDisplay.setDarkMode (isDarkTheme);
        syncGainsFromPad();

        peqDisplay.onBandGainChanged = [this] (int band, float gainDb)
        {
            applyBandGainFromUi (band, gainDb, false);
        };

        peqDisplay.onBandGainFinished = [this] (int band, float gainDb)
        {
            applyBandGainFromUi (band, gainDb, true);
        };

        addAndMakeVisible (hintLabel);
        hintLabel.setText (showcontrol::localization::tr (
                               u8"Kéo nút + theo chiều dọc · thấp → cao: HP → LS → P1 → P2 → HS → LP"),
                           juce::dontSendNotification);
        hintLabel.setFont (showcontrol::equalizer::hintFont());
        hintLabel.setJustificationType (juce::Justification::centredLeft);

        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
        {
            bandValueLabels[(size_t) b].setFont (showcontrol::equalizer::bandValueFont());
            bandValueLabels[(size_t) b].setJustificationType (juce::Justification::centred);
            addAndMakeVisible (bandValueLabels[(size_t) b]);
        }

        addAndMakeVisible (closeBtn);
        closeBtn.setButtonText (showcontrol::localization::tr (u8"Đóng"));
        closeBtn.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        };

        applyTheme();
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

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);
        windowDragActive = false;
    }

    void resized() override
    {
        auto bounds = getLocalBounds();

        if (kMacTopDragInset > 0)
            bounds.removeFromTop (kMacTopDragInset);

        auto b = bounds.reduced (14);

        auto headerRow = b.removeFromTop (30);
        enableToggle.setBounds (headerRow.removeFromLeft ((int) (headerRow.getWidth() * 0.52f)));
        resetBtn.setBounds (headerRow.reduced (6, 4));
        b.removeFromTop (6);
        peqDisplay.setBounds (b.removeFromTop (juce::jmax (220, b.getHeight() - 88)));
        b.removeFromTop (6);
        hintLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (4);

        auto valueRow = b.removeFromTop (20);
        const int colW = juce::jmax (1, valueRow.getWidth() / PadParametricEq6::kNumBands);
        for (int i = 0; i < PadParametricEq6::kNumBands; ++i)
        {
            auto col = (i == PadParametricEq6::kNumBands - 1)
                           ? valueRow
                           : valueRow.removeFromLeft (colW);
            bandValueLabels[(size_t) i].setBounds (col);
        }

        b.removeFromTop (8);
        closeBtn.setBounds (b.removeFromBottom (32).withSizeKeepingCentre (120, 32));
    }

private:
   #if JUCE_MAC
    static constexpr int kMacTopDragInset = 14;
   #else
    static constexpr int kMacTopDragInset = 0;
   #endif

    SoundPad* currentPad;
    bool isDarkTheme;
    std::function<void()> onEdit;

    juce::ToggleButton enableToggle;
    juce::TextButton resetBtn;
    Peq6CurveDisplay peqDisplay;
    juce::Label hintLabel;
    std::array<juce::Label, PadParametricEq6::kNumBands> bandValueLabels;
    juce::TextButton closeBtn;
    juce::ComponentDragger windowDragger;
    bool windowDragActive = false;

    void applyTheme()
    {
        const auto pal = ShowTheme::get (isDarkTheme);
        hintLabel.setColour (juce::Label::textColourId, pal.textMuted);
        enableToggle.setColour (juce::ToggleButton::textColourId, pal.textPrimary);
        enableToggle.setColour (juce::ToggleButton::tickColourId, pal.accent);
        enableToggle.setColour (juce::ToggleButton::tickDisabledColourId, pal.border);
        closeBtn.setColour (juce::TextButton::buttonColourId, pal.accentSoft);
        closeBtn.setColour (juce::TextButton::textColourOffId, isDarkTheme ? juce::Colours::white : pal.panelElevated);
        resetBtn.setColour (juce::TextButton::buttonColourId, pal.panelElevated);
        resetBtn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
        for (auto& lbl : bandValueLabels)
            lbl.setColour (juce::Label::textColourId, pal.textSecondary);
        updateBandValueLabels();
    }

    void refreshPeqPreviewFromPad()
    {
        if (currentPad == nullptr)
            return;

        const double sr = currentPad->getRealtimeSource().getDeviceSampleRate();
        if (sr > 0.0)
            peqDisplay.setPreviewSampleRate (sr);

        std::array<float, PadParametricEq6::kNumBands> gains {};
        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
            gains[(size_t) b] = currentPad->getDspEqBandGainDb (b);
        peqDisplay.setBandGainsDb (gains);
    }

    void syncGainsFromPad()
    {
        refreshPeqPreviewFromPad();
        updateBandValueLabels();
    }

    void applyEnableFromUi()
    {
        if (currentPad == nullptr)
            return;

        currentPad->setDspEqEnabled (enableToggle.getToggleState());
        refreshPeqPreviewFromPad();
        if (onEdit)
            onEdit();
    }

    void applyBandGainFromUi (int band, float gainDb, bool notifyEdit)
    {
        if (currentPad == nullptr)
            return;

        if (! currentPad->getDspEqEnabled())
        {
            currentPad->setDspEqEnabledNoCoeffBump (true);
            enableToggle.setToggleState (true, juce::dontSendNotification);
        }

        currentPad->setDspEqBandGainDb (band, gainDb);
        updateBandValueLabels();

        if (notifyEdit && onEdit)
            onEdit();
    }

    void updateBandValueLabels()
    {
        static const char* names[] = { "HP", "LS", "P1", "P2", "HS", "LP" };
        const auto gains = peqDisplay.getBandGainsDb();
        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
        {
            const auto freq = PadParametricEq6::formatBandFrequencyHz (PadParametricEq6::kBandFreqHz[b]);
            const auto gainStr = (gains[(size_t) b] >= 0.0f ? "+" : "")
                               + juce::String (gains[(size_t) b], 1) + " dB";
            bandValueLabels[(size_t) b].setText (freq + " · " + juce::String (names[b]) + " " + gainStr,
                                                  juce::dontSendNotification);
        }
    }

};

inline void showPadEqualizerDialog (juce::Component* parent,
                                    SoundPad* pad,
                                    bool darkMode,
                                    std::function<void()> onEdited)
{
    if (parent == nullptr || pad == nullptr)
        return;

    auto* content = new PadEqualizerDialogContent (pad, darkMode, std::move (onEdited));

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned (content);
    opt.dialogTitle = showcontrol::localization::tr (u8"Bộ cân bằng tần số (Equalizer)");
    opt.dialogBackgroundColour = parent->getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;

    if (auto* dw = opt.launchAsync())
    {
        dw->setUsingNativeTitleBar (true);
        showcontrol::ui::centreFloatingWindowInMainApp (*dw, parent);
       #if JUCE_MAC
        showcontrol::mac::deferFarragoFullSizeContentView (*dw);
       #endif
    }
}

} // namespace showcontrol::ui
