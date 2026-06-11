#pragma once
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ShowTheme.h"

/** Snapshot UI — chỉ message thread ghi/đọc, không mutex audio. */
struct StageMonitorSnapshot
{
    bool isPlaying = false;
    bool isLooping = false;
    bool isCueMode = false;
    juce::String trackName;
    double remainingSeconds = 0.0;
    double elapsedSeconds   = 0.0;
    double totalSeconds     = 0.0;
    float  progress         = 0.0f;

    bool operator== (const StageMonitorSnapshot& o) const noexcept
    {
        return isPlaying == o.isPlaying
            && isLooping == o.isLooping
            && isCueMode == o.isCueMode
            && trackName == o.trackName
            && std::abs (remainingSeconds - o.remainingSeconds) < 0.05
            && std::abs (elapsedSeconds - o.elapsedSeconds) < 0.05
            && std::abs (totalSeconds - o.totalSeconds) < 0.05
            && std::abs (progress - o.progress) < 0.002f;
    }

    bool operator!= (const StageMonitorSnapshot& o) const noexcept { return ! (*this == o); }
};

//==============================================================================
/** Vẽ giám sát sân khấu — message thread only. */
class StageMonitorComponent : public juce::Component,
                              private juce::Timer
{
public:
    struct WindowControls
    {
        std::function<bool()> queryAlwaysOnTop;
        std::function<void (bool)> applyAlwaysOnTop;
        std::function<bool()> queryFullScreen;
        std::function<void (bool)> applyFullScreen;
    };

    StageMonitorComponent();
    ~StageMonitorComponent() override;

    void setWindowControls (WindowControls controls);
    void updateDisplay (const StageMonitorSnapshot& snapshot);

    void resized() override;
    void paint (juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    void mouseDown (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    static constexpr int kMenuAlwaysOnTop = 1;
    static constexpr int kMenuFullScreen  = 2;

    struct LayoutMetrics
    {
        juce::Rectangle<int> standbyMain;
        juce::Rectangle<int> standbyFooter;
        juce::Rectangle<int> trackTitle;
        juce::Rectangle<int> remainingClock;
        juce::Rectangle<int> progressBar;
        juce::Rectangle<int> elapsedClock;
        juce::Rectangle<int> loopStatus;
        float standbyFontH    = 48.0f;
        float footerFontH     = 12.0f;
        float remainingFontH  = 0.0f;
        float trackFontH      = 0.0f;
        float elapsedFontH    = 0.0f;
        float statusFontH     = 0.0f;
        int progressThickness = 8;
    };

    StageMonitorSnapshot snapshot;
    LayoutMetrics layout;
    WindowControls windowControls;

    void recomputeLayout();
    juce::String formatClock (double seconds) const;
    void paintStandby (juce::Graphics& g) const;
    void paintPlaying (juce::Graphics& g) const;
    void showDisplayOptionsMenu();
    void handleMonitorMenuResult (int result);
    void toggleAlwaysOnTop();
    void toggleFullScreen();
    void notifyLayoutAfterWindowChromeChange();
    void timerCallback() override;
    void syncWarningFlashTimer() noexcept;
    static juce::Colour getRemainingClockColour (double timeRemaining) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageMonitorComponent)
};

//==============================================================================
/** Cửa sổ vật lý DocumentWindow — đa màn hình, giải phóng qua unique_ptr. */
class StageMonitorWindow final : public juce::DocumentWindow
{
public:
    explicit StageMonitorWindow (std::function<void()> onWindowClosed);

    void closeButtonPressed() override;
    void activeWindowStatusChanged() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void positionToSecondaryDisplay();

    void setAlwaysOnTopEnabled (bool shouldBeOnTop);
    void setFullScreenEnabled (bool shouldBeFullScreen);
    bool isAlwaysOnTopEnabled() const noexcept { return alwaysOnTopEnabled; }
    bool isFullScreenEnabled() const noexcept;

    StageMonitorComponent* getMonitorComponent() noexcept { return monitorComponent; }

private:
    void wireMonitorControls();
    void refreshMonitorLayout();

    StageMonitorComponent* monitorComponent = nullptr;
    std::function<void()> closedCallback;
    bool alwaysOnTopEnabled = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageMonitorWindow)
};
