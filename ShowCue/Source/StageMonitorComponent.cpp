#include "StageMonitorComponent.h"
#include "ShowLocalization.h"

namespace showcontrol::stage_monitor
{
    static constexpr juce::uint32 kColourStandbyText  = 0xFFDFB24E;
    static constexpr juce::uint32 kColourRemaining   = 0xFF57D18C;
    static constexpr juce::uint32 kColourTrackName   = 0xFFF2C94C;
    static constexpr juce::uint32 kColourStatusLine = 0xFF0A84FF;
    static constexpr juce::uint32 kColourElapsed     = 0xFFE26A4A;
    static constexpr juce::uint32 kColourFooter     = 0xFF6E6E6E;
    static constexpr juce::uint32 kColourProgressBg    = 0xFF2A2A2A;
    static constexpr juce::uint32 kColourProgressFg    = 0xFF57D18C;
    static constexpr juce::uint32 kColourWarningBright = 0xFFFF3B30;
    static constexpr juce::uint32 kColourWarningDim    = 0x88FF3B30;
    static constexpr juce::uint32 kWarningFlashPeriodMs = 500;
    static constexpr int kWarningFlashTimerMs           = 250;

    static juce::Colour remainingClockColour (double timeRemaining) noexcept
    {
        if (timeRemaining > 5.0)
            return juce::Colour (kColourRemaining);

        if (timeRemaining > 0.0)
        {
            const bool flashBright = ((juce::Time::getMillisecondCounter() / kWarningFlashPeriodMs) & 1u) == 0u;
            return juce::Colour (flashBright ? kColourWarningBright : kColourWarningDim);
        }

        return juce::Colour (kColourRemaining);
    }
}

//==============================================================================
StageMonitorComponent::StageMonitorComponent()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
}

StageMonitorComponent::~StageMonitorComponent()
{
    stopTimer();
}

juce::Colour StageMonitorComponent::getRemainingClockColour (double timeRemaining) noexcept
{
    return showcontrol::stage_monitor::remainingClockColour (timeRemaining);
}

void StageMonitorComponent::syncWarningFlashTimer() noexcept
{
    const double timeRemaining = snapshot.remainingSeconds;
    const bool inWarningZone = snapshot.isPlaying
                            && timeRemaining <= 5.0
                            && timeRemaining > 0.0;

    if (inWarningZone)
    {
        if (! isTimerRunning())
            startTimer (showcontrol::stage_monitor::kWarningFlashTimerMs);
    }
    else
    {
        stopTimer();
    }
}

void StageMonitorComponent::timerCallback()
{
    const double timeRemaining = snapshot.remainingSeconds;

    if (! snapshot.isPlaying || timeRemaining > 5.0 || timeRemaining <= 0.0)
    {
        stopTimer();
        return;
    }

    repaint();
}

void StageMonitorComponent::setWindowControls (WindowControls controls)
{
    windowControls = std::move (controls);
}

void StageMonitorComponent::resized()
{
    recomputeLayout();
}

void StageMonitorComponent::recomputeLayout()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    const int w = bounds.getWidth();
    const int h = bounds.getHeight();
    const float fh = (float) h;

    layout.remainingFontH = fh * 0.35f;
    layout.trackFontH     = fh * 0.08f;
    layout.elapsedFontH   = fh * 0.16f;
    layout.statusFontH    = fh * 0.05f;
    layout.standbyFontH   = fh * 0.08f;
    layout.footerFontH    = fh * 0.03f;
    layout.progressThickness = juce::jlimit (6, 8, juce::jmax (6, (int) std::round (fh * 0.007f)));

    // --- Standby ---
    auto standbyArea = bounds;
    layout.standbyFooter = standbyArea.removeFromBottom ((int) (fh * 0.05f));
    layout.standbyMain   = standbyArea;

    // --- Playing: progress ghim đáy (margin 5%), cụm 3 tầng cuốn chiếu căn giữa vùng còn lại ---
    auto playing = bounds;

    juce::ignoreUnused (playing.removeFromBottom ((int) (fh * 0.05f)));

    auto progressBarArea = playing.removeFromBottom (layout.progressThickness);
    const int barW = (int) ((float) w * 0.9f);
    layout.progressBar = progressBarArea.withSizeKeepingCentre (barW, layout.progressThickness);

    const auto contentArea = playing;

    const int remainingBlockH = (int) std::ceil (layout.remainingFontH * 1.12f);
    const int trackGap        = (int) (fh * 0.04f);
    const int trackBlockH     = (int) std::ceil (layout.trackFontH * 1.35f);
    const int innerGap        = (int) (fh * 0.02f);
    const int statusBlockH    = snapshot.isLooping ? (int) std::ceil (layout.statusFontH * 1.45f) : 0;
    const int elapsedBlockH   = (int) std::ceil (layout.elapsedFontH * 1.12f);

    const int stackH = remainingBlockH + trackGap + trackBlockH
                     + (statusBlockH > 0 ? innerGap + statusBlockH : 0)
                     + innerGap + elapsedBlockH;

    int stackY = contentArea.getY()
               + juce::jmax (0, (contentArea.getHeight() - stackH) / 2);

    const int contentX = contentArea.getX();
    const int contentW = contentArea.getWidth();

    layout.remainingClock = { contentX, stackY, contentW, remainingBlockH };
    stackY += remainingBlockH + trackGap;

    layout.trackTitle = { contentX, stackY, contentW, trackBlockH };
    stackY += trackBlockH;

    if (statusBlockH > 0)
    {
        stackY += innerGap;
        layout.loopStatus = { contentX, stackY, contentW, statusBlockH };
        stackY += statusBlockH;
    }
    else
    {
        layout.loopStatus = {};
    }

    stackY += innerGap;
    layout.elapsedClock = { contentX, stackY, contentW, elapsedBlockH };
}

void StageMonitorComponent::updateDisplay (const StageMonitorSnapshot& newSnapshot)
{
    const bool layoutDirty = snapshot.isLooping != newSnapshot.isLooping
                          || snapshot.isPlaying != newSnapshot.isPlaying;

    if (snapshot == newSnapshot && ! layoutDirty)
        return;

    snapshot = newSnapshot;
    syncWarningFlashTimer();

    if (layoutDirty)
        recomputeLayout();

    repaint();
}

juce::String StageMonitorComponent::formatClock (double seconds) const
{
    seconds = juce::jmax (0.0, seconds);
    const int mins = (int) (seconds / 60.0);
    const int secs = (int) seconds % 60;
    const int ms   = (int) (seconds * 10.0) % 10;
    return juce::String::formatted ("%02d:%02d.%d", mins, secs, ms);
}

void StageMonitorComponent::paintStandby (juce::Graphics& g) const
{
    using namespace showcontrol::stage_monitor;

    g.fillAll (juce::Colours::black);

    g.setColour (juce::Colour (kColourStandbyText));
    g.setFont (ShowTheme::fontBold (layout.standbyFontH));
    g.drawText (showcontrol::localization::tr (u8"KHÔNG CÓ CUE"),
                layout.standbyMain, juce::Justification::centred, true);

    g.setColour (juce::Colour (kColourFooter));
    g.setFont (ShowTheme::font (layout.footerFontH));
    g.drawText (juce::String::fromUTF8 (u8"Developer by Hayatuan"),
                layout.standbyFooter, juce::Justification::centred);
}

void StageMonitorComponent::paintPlaying (juce::Graphics& g) const
{
    using namespace showcontrol::stage_monitor;

    g.fillAll (juce::Colours::black);

    const double timeRemaining = snapshot.remainingSeconds;
    g.setColour (getRemainingClockColour (timeRemaining));
    g.setFont (ShowTheme::timerFont (layout.remainingFontH, true));
    const auto hCentred = juce::Justification::horizontallyCentred;

    g.drawText (formatClock (timeRemaining),
                layout.remainingClock, hCentred);

    g.setColour (juce::Colour (kColourTrackName));
    g.setFont (ShowTheme::fontBold (layout.trackFontH));
    g.drawText (snapshot.trackName, layout.trackTitle, hCentred, true);

    if (snapshot.isLooping)
    {
        g.setColour (juce::Colour (kColourStatusLine));
        g.setFont (ShowTheme::font (layout.statusFontH));
        const auto status = snapshot.isCueMode
                                ? showcontrol::localization::tr (u8"CUE đang phát ở chế độ lặp lại")
                                : showcontrol::localization::tr (u8"BGM đang phát ở chế độ lặp lại");
        g.drawText (status, layout.loopStatus, hCentred);
    }

    g.setColour (juce::Colour (kColourElapsed));
    g.setFont (ShowTheme::timerFont (layout.elapsedFontH, true));
    g.drawText (formatClock (snapshot.elapsedSeconds),
                layout.elapsedClock, hCentred);

    const auto bar = layout.progressBar;
    const float radius = (float) layout.progressThickness * 0.5f;

    g.setColour (juce::Colour (kColourProgressBg));
    g.fillRoundedRectangle (bar.toFloat(), radius);

    auto fill = bar.toFloat();
    fill.setWidth (fill.getWidth() * juce::jlimit (0.0f, 1.0f, snapshot.progress));
    g.setColour (juce::Colour (kColourProgressFg));
    g.fillRoundedRectangle (fill, radius);
}

void StageMonitorComponent::paint (juce::Graphics& g)
{
    if (getLocalBounds().isEmpty())
        return;

    if (! snapshot.isPlaying)
        paintStandby (g);
    else
        paintPlaying (g);
}

void StageMonitorComponent::lookAndFeelChanged()
{
    juce::Component::lookAndFeelChanged();
    repaint();
}

void StageMonitorComponent::notifyLayoutAfterWindowChromeChange()
{
    recomputeLayout();
    repaint();
}

void StageMonitorComponent::toggleAlwaysOnTop()
{
    if (! windowControls.queryAlwaysOnTop || ! windowControls.applyAlwaysOnTop)
        return;

    windowControls.applyAlwaysOnTop (! windowControls.queryAlwaysOnTop());
    notifyLayoutAfterWindowChromeChange();
}

void StageMonitorComponent::toggleFullScreen()
{
    if (! windowControls.queryFullScreen || ! windowControls.applyFullScreen)
        return;

    windowControls.applyFullScreen (! windowControls.queryFullScreen());
    notifyLayoutAfterWindowChromeChange();
}

void StageMonitorComponent::showDisplayOptionsMenu()
{
    const bool pinned = windowControls.queryAlwaysOnTop != nullptr
                            && windowControls.queryAlwaysOnTop();
    const bool full   = windowControls.queryFullScreen != nullptr
                            && windowControls.queryFullScreen();

    juce::PopupMenu menu;
    menu.addItem (kMenuAlwaysOnTop,
                  showcontrol::localization::tr (u8"Luôn hiện trên cùng (Ghim Top)"),
                  true,
                  pinned);
    menu.addItem (kMenuFullScreen,
                  showcontrol::localization::tr (u8"Toàn màn hình (Full Screen)"),
                  true,
                  full);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (this)
                            .withMousePosition(),
                        [this] (int result) { handleMonitorMenuResult (result); });
}

void StageMonitorComponent::handleMonitorMenuResult (int result)
{
    if (result == kMenuAlwaysOnTop)
        toggleAlwaysOnTop();
    else if (result == kMenuFullScreen)
        toggleFullScreen();
}

void StageMonitorComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
    {
        showDisplayOptionsMenu();
        return;
    }

    grabKeyboardFocus();
}

bool StageMonitorComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (windowControls.queryFullScreen != nullptr && windowControls.queryFullScreen())
        {
            if (windowControls.applyFullScreen != nullptr)
                windowControls.applyFullScreen (false);

            notifyLayoutAfterWindowChromeChange();
            return true;
        }

        return false;
    }

    const auto ch = key.getTextCharacter();

    if (ch == 'f' || ch == 'F')
    {
        toggleFullScreen();
        return true;
    }

    if (ch == 't' || ch == 'T')
    {
        toggleAlwaysOnTop();
        return true;
    }

    return false;
}

//==============================================================================
StageMonitorWindow::StageMonitorWindow (std::function<void()> onWindowClosed)
    : DocumentWindow (juce::String::fromUTF8 (u8"Stage Monitor — ShowCue"),
                      juce::Colours::black,
                      DocumentWindow::allButtons),
      closedCallback (std::move (onWindowClosed))
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setBackgroundColour (juce::Colours::black);

    auto* monitor = new StageMonitorComponent();
    monitorComponent = monitor;
    setContentOwned (monitor, true);
    wireMonitorControls();
    setSize (960, 540);
    centreAroundComponent (nullptr, getWidth(), getHeight());
}

void StageMonitorWindow::wireMonitorControls()
{
    if (monitorComponent == nullptr)
        return;

    monitorComponent->setWindowControls ({
        [this] { return isAlwaysOnTopEnabled(); },
        [this] (bool on) { setAlwaysOnTopEnabled (on); },
        [this] { return isFullScreenEnabled(); },
        [this] (bool on) { setFullScreenEnabled (on); }
    });
}

void StageMonitorWindow::refreshMonitorLayout()
{
    if (monitorComponent == nullptr)
        return;

    monitorComponent->resized();
    monitorComponent->repaint();
}

void StageMonitorWindow::setAlwaysOnTopEnabled (bool shouldBeOnTop)
{
    alwaysOnTopEnabled = shouldBeOnTop;
    setAlwaysOnTop (shouldBeOnTop);
}

void StageMonitorWindow::setFullScreenEnabled (bool shouldBeFullScreen)
{
    setFullScreen (shouldBeFullScreen);
    syncDisplaySleepFromWindowState();
    refreshMonitorLayout();
}

bool StageMonitorWindow::isFullScreenEnabled() const noexcept
{
    return isFullScreen();
}

void StageMonitorWindow::resized()
{
    DocumentWindow::resized();
    syncDisplaySleepFromWindowState();
}

void StageMonitorWindow::maximiseButtonPressed()
{
    DocumentWindow::maximiseButtonPressed();
    syncDisplaySleepFromWindowState();
}

void StageMonitorWindow::syncDisplaySleepFromWindowState()
{
    displaySleepGuard.sync (isFullScreen());
}

void StageMonitorWindow::activeWindowStatusChanged()
{
    DocumentWindow::activeWindowStatusChanged();

    if (isActiveWindow() && monitorComponent != nullptr)
        monitorComponent->grabKeyboardFocus();
}

bool StageMonitorWindow::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && isFullScreen())
    {
        setFullScreenEnabled (false);
        return true;
    }

    if (monitorComponent != nullptr && monitorComponent->keyPressed (key))
        return true;

    return DocumentWindow::keyPressed (key);
}

void StageMonitorWindow::closeButtonPressed()
{
    if (closedCallback)
        closedCallback();

    setVisible (false);
}

void StageMonitorWindow::positionToSecondaryDisplay()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();

    if (displays.displays.size() > 1)
    {
        const auto& secondary = displays.displays.getReference (1);
        setBounds (secondary.userBounds.toNearestInt());
        setFullScreenEnabled (true);
        return;
    }

    setFullScreenEnabled (true);
}
