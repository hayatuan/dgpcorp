#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SoundPad.h"
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowGraphicsSafe.h"
#include "ShowFlatIcons.h"
#include "PadEqualizerDialog.h"
#include "LoudnessManagerDialog.h"
#include "ShowControlMacWindow.h"
#include "ShowOutputRouting.h"
#include "ShowLocalization.h"
#include "ShowTagColors.h"
#include "ConfirmDeleteDialog.h"

//==============================================================================
class InspectorWaveform : public juce::Component,
                         public juce::ChangeListener
{
public:
    InspectorWaveform() {}

    ~InspectorWaveform() override
    {
        abandonThumbnailLink();
    }

    void setThumbnail (juce::AudioThumbnail* thumb)
    {
        if (thumbnail == thumb)
            return;

        if (thumbnail != nullptr)
            thumbnail->removeChangeListener (this);

        thumbnail = thumb;

        if (thumbnail != nullptr)
            thumbnail->addChangeListener (this);

        repaint();
    }

    /** Chỉ xóa con trỏ cục bộ — dùng khi đối tượng thumbnail có thể đã bị hủy (destructor). */
    void abandonThumbnailLink() noexcept
    {
        thumbnail = nullptr;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // AudioThumbnail broadcast từ background — chỉ repaint trên message thread.
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            repaint();
        else
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<InspectorWaveform> (this)]
            {
                if (safe != nullptr)
                    safe->repaint();
            });
    }
    void setDarkMode  (bool dark)                    { isDark = dark; repaint(); }
    void setShowTimeRuler (bool shouldShow)          { showTimeRuler = shouldShow; repaint(); }
    void setItemInkColour (juce::Colour c) noexcept  { itemInk = c; repaint(); }

    /** Cập nhật vùng fade-in / fade-out để vẽ overlay lên waveform. */
    void setFadeRegions (double fadeInMs, double fadeOutMs, double effectiveLengthSec)
    {
        fadeInSec  = fadeInMs  * 0.001;
        fadeOutSec = fadeOutMs * 0.001;
        effectiveLen = effectiveLengthSec;
        repaint();
    }
    void setTrimPoints (double start, double end)
    {
        trimStart = start;
        trimEnd = end;

        if (totalLen > 0.0)
        {
            const double effectiveEnd = (trimEnd > 0.0) ? trimEnd : totalLen;
            viewStart = trimStart;
            viewEnd   = effectiveEnd;
        }

        repaint();
    }

    void setCutSelectionEnabled (bool enabled) noexcept
    {
        if (cutSelectionEnabled == enabled)
            return;

        cutSelectionEnabled = enabled;

        if (! enabled)
            clearCutSelection();

        repaint();
    }

    bool hasCutSelection() const noexcept
    {
        return cutSelEnd >= 0.0 && (cutSelEnd - cutSelStart) > 0.005;
    }

    void getCutSelection (double& outStart, double& outEnd) const noexcept
    {
        outStart = cutSelStart;
        outEnd   = cutSelEnd;
    }

    void clearCutSelection()
    {
        cutSelStart = -1.0;
        cutSelEnd   = -1.0;
        cutSelDragging = false;
        repaint();
    }

    void setCutSelection (double start, double end)
    {
        const double lo = juce::jlimit (0.0, totalLen, juce::jmin (start, end));
        const double hi = juce::jlimit (0.0, totalLen, juce::jmax (start, end));
        cutSelStart = lo;
        cutSelEnd   = hi;
        repaint();

        if (onCutSelectionChanged)
            onCutSelectionChanged();
    }

    void setCutStartAtCurrentPosition()
    {
        if (totalLen <= 0.0)
            return;

        if (cutSelEnd < 0.0)
            cutSelEnd = totalLen;

        cutSelStart = juce::jlimit (0.0, cutSelEnd - 0.005, currentPos);
        repaint();

        if (onCutSelectionChanged)
            onCutSelectionChanged();
    }

    void setCutEndAtCurrentPosition()
    {
        if (totalLen <= 0.0)
            return;

        if (cutSelStart < 0.0)
            cutSelStart = 0.0;

        cutSelEnd = juce::jlimit (cutSelStart + 0.005, totalLen, currentPos);
        repaint();

        if (onCutSelectionChanged)
            onCutSelectionChanged();
    }

    void setProgress (double pos, double len) 
    { 
        currentPos = pos; 
        if (std::abs (totalLen - len) > 0.001) 
        {
            totalLen = len; 
            viewStart = 0.0; 
            viewEnd = len; 
        } 
        repaint(); 
    }

    void resetPlaybackProgress() noexcept
    {
        currentPos = viewStart;
        repaint();
    }

    void hardKillPlaybackVisual() noexcept
    {
        currentPos = viewStart;
        repaint();
    }

    std::function<void(double)> onTrimStartChanged;
    std::function<void(double)> onTrimEndChanged;
    std::function<void()> onTrimGestureFinished;
    std::function<void(double)> onPlayheadScrubbed; 
    std::function<void()> onWaveformDoubleClicked;
    std::function<void()> onCutSelectionChanged;

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDark);
        auto b = showcontrol::gfx::sanitise (getLocalBounds().toFloat());
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
            return;

        g.setColour (isDark ? pal.listRowBg : juce::Colours::white);
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (isDark ? pal.border : juce::Colour (0xFFE5E5EA));
        g.drawRoundedRectangle (b, 5.0f, 1.0f);

        if (thumbnail == nullptr)
        {
            g.setColour (pal.border);
            g.setFont (showcontrol::inspector::waveEmptyHintFont());
            g.drawText (juce::String::fromUTF8 (u8"Chưa có file âm thanh"), getLocalBounds(), juce::Justification::centred);
            return;
        }

        if (thumbnail->getTotalLength() <= 0.0)
        {
            g.setColour (pal.textMuted);
            g.setFont (ShowTheme::font (11.0f));
            g.drawText (showcontrol::localization::tr (u8"Đang nạp..."), getLocalBounds(), juce::Justification::centred);
            return;
        }

        if (viewEnd <= viewStart)
        {
            viewStart = 0.0;
            viewEnd   = showcontrol::gfx::safePositiveDuration (totalLen);
        }

        if (viewEnd <= viewStart)
            return;

        const auto waveArea = showcontrol::gfx::sanitise (getWaveformBounds());
        if (waveArea.getWidth() <= 0 || waveArea.getHeight() <= 0)
            return;

        if (showTimeRuler)
            drawTimeRuler (g, showcontrol::gfx::sanitise (getRulerBounds()));

        const auto waveBg = isDark ? pal.listRowBg : juce::Colours::white;
        g.setColour (waveBg);
        g.fillRect (waveArea);

        const auto dimInk = showcontrol::colours::opaqueWaveformInk (
            waveBg, itemInk, showcontrol::colours::kInspectorWaveformInkAlpha);
        const auto playedInk = showcontrol::colours::opaqueWaveformInk (
            waveBg, itemInk, showcontrol::colours::kInspectorWaveformPlayedAlpha);

        if (totalLen > 0.0 && currentPos > viewStart)
        {
            const float ratio = showcontrol::gfx::timeToRatio (currentPos, viewStart, viewEnd);
            const int splitX = showcontrol::gfx::clampDimension (
                waveArea.getX() + (int) std::round ((float) waveArea.getWidth() * ratio));

            if (splitX > waveArea.getX())
            {
                auto playedClip = waveArea;
                playedClip.setWidth (splitX - waveArea.getX());

                if (showcontrol::gfx::canClip (playedClip))
                {
                    g.saveState();
                    g.reduceClipRegion (playedClip);
                    g.setColour (waveBg);
                    g.fillRect (waveArea);
                    g.setColour (playedInk);
                    thumbnail->drawChannel (g, waveArea, viewStart, viewEnd, 0, 1.0f);
                    g.restoreState();
                }
            }

            if (splitX < waveArea.getRight())
            {
                auto unplayedClip = waveArea;
                unplayedClip.setLeft (splitX);

                if (showcontrol::gfx::canClip (unplayedClip))
                {
                    g.saveState();
                    g.reduceClipRegion (unplayedClip);
                    g.setColour (dimInk);
                    thumbnail->drawChannel (g, waveArea, viewStart, viewEnd, 0, 1.0f);
                    g.restoreState();
                }
            }
        }
        else
        {
            g.setColour (dimInk);
            thumbnail->drawChannel (g, waveArea, viewStart, viewEnd, 0, 1.0f);
        }

        const double effectiveEnd = (trimEnd > 0.0) ? trimEnd : totalLen;
        const int startX = showcontrol::gfx::timeToPixelX (waveArea, trimStart, viewStart, viewEnd);
        const int endX   = showcontrol::gfx::timeToPixelX (waveArea, effectiveEnd, viewStart, viewEnd);

        g.setColour (juce::Colours::black.withAlpha (0.58f));
        if (startX > waveArea.getX())
            showcontrol::gfx::safeFillRect (g, waveArea.getX(), waveArea.getY(), startX - waveArea.getX(), waveArea.getHeight());
        if (endX < waveArea.getRight())
            showcontrol::gfx::safeFillRect (g, endX, waveArea.getY(), waveArea.getRight() - endX, waveArea.getHeight());

        if (startX >= waveArea.getX() && startX <= waveArea.getRight())
        {
            g.setColour (pal.trimMarkerStart);
            showcontrol::gfx::safeDrawVerticalLine (g, startX, (float) waveArea.getY(), (float) waveArea.getBottom());
            g.fillRect (showcontrol::gfx::sanitise (juce::Rectangle<float> ((float) startX - 4.0f, (float) waveArea.getY(), 8.0f, 8.0f)));

            if (showTimeRuler)
            {
                g.setFont (showcontrol::trimEditor::markerLabelFont());
                g.drawText (showcontrol::localization::tr (u8"Điểm đầu"),
                            startX + 6, waveArea.getY() + 10, 72, 12,
                            juce::Justification::centredLeft);
            }
        }

        if (endX >= waveArea.getX() && endX <= waveArea.getRight())
        {
            g.setColour (pal.trimMarkerEnd);
            showcontrol::gfx::safeDrawVerticalLine (g, endX, (float) waveArea.getY(), (float) waveArea.getBottom());
            g.fillRect (showcontrol::gfx::sanitise (juce::Rectangle<float> ((float) endX - 4.0f, (float) waveArea.getY(), 8.0f, 8.0f)));

            if (showTimeRuler)
            {
                g.setFont (showcontrol::trimEditor::markerLabelFont());
                g.drawText (showcontrol::localization::tr (u8"Điểm cuối"),
                            endX - 78, waveArea.getY() + 10, 72, 12,
                            juce::Justification::centredRight);
            }
        }

        if (currentPos >= viewStart && currentPos <= viewEnd)
        {
            const int px = showcontrol::gfx::timeToPixelX (waveArea, currentPos, viewStart, viewEnd);
            g.setColour (pal.danger);
            showcontrol::gfx::safeDrawVerticalLine (g, px, (float) waveArea.getY(), (float) waveArea.getBottom());
            g.fillEllipse (showcontrol::gfx::sanitise (juce::Rectangle<float> ((float) px - 3.5f, (float) waveArea.getY() - 4.0f, 7.0f, 7.0f)));
        }

        if (hasCutSelection())
        {
            const int selX0 = showcontrol::gfx::timeToPixelX (waveArea, cutSelStart, viewStart, viewEnd);
            const int selX1 = showcontrol::gfx::timeToPixelX (waveArea, cutSelEnd, viewStart, viewEnd);
            const int left  = juce::jmin (selX0, selX1);
            const int right = juce::jmax (selX0, selX1);

            if (right > left)
            {
                g.setColour (pal.danger.withAlpha (0.28f));
                showcontrol::gfx::safeFillRect (g, left, waveArea.getY(), right - left, waveArea.getHeight());
                g.setColour (pal.danger.withAlpha (0.85f));
                showcontrol::gfx::safeDrawVerticalLine (g, left,  (float) waveArea.getY(), (float) waveArea.getBottom());
                showcontrol::gfx::safeDrawVerticalLine (g, right, (float) waveArea.getY(), (float) waveArea.getBottom());
            }
        }

        if (fadeInSec > 0.0 && fadeInSec > viewStart)
        {
            const double fiEnd = std::min (fadeInSec, viewEnd);
            const int x0 = waveArea.getX();
            const int x1 = showcontrol::gfx::timeToPixelX (waveArea, fiEnd, viewStart, viewEnd);
            if (x1 > x0)
            {
                juce::ColourGradient grad (pal.success.withAlpha (0.28f), (float) x0, 0,
                                           pal.success.withAlpha (0.0f),  (float) x1, 0, false);
                g.setGradientFill (grad);
                showcontrol::gfx::safeFillRect (g, x0, waveArea.getY(), x1 - x0, waveArea.getHeight());
            }
        }

        if (fadeOutSec > 0.0 && effectiveLen > 0.0)
        {
            const double foStart = std::max (viewStart, effectiveLen - fadeOutSec);
            if (foStart < viewEnd)
            {
                const int x0 = showcontrol::gfx::timeToPixelX (waveArea, foStart, viewStart, viewEnd);
                const int x1 = waveArea.getRight();
                if (x1 > x0)
                {
                    juce::ColourGradient grad (pal.danger.withAlpha (0.0f),  (float) x0, 0,
                                               pal.danger.withAlpha (0.28f), (float) x1, 0, false);
                    g.setGradientFill (grad);
                    showcontrol::gfx::safeFillRect (g, x0, waveArea.getY(), x1 - x0, waveArea.getHeight());
                }
            }
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override { juce::ignoreUnused (e); if (onWaveformDoubleClicked) onWaveformDoubleClicked(); }

    std::function<void()> onViewRangeChanged;

    bool canScrollHorizontally() const noexcept
    {
        return totalLen > 0.01 && (viewEnd - viewStart) < totalLen - 0.01;
    }

    double getViewScrollRatio() const noexcept
    {
        if (! canScrollHorizontally())
            return 0.0;

        const double range = viewEnd - viewStart;
        const double maxStart = totalLen - range;
        return maxStart > 0.0 ? viewStart / maxStart : 0.0;
    }

    void setViewScrollRatio (double ratio)
    {
        if (! canScrollHorizontally())
            return;

        const double range = viewEnd - viewStart;
        const double maxStart = juce::jmax (0.0, totalLen - range);
        const double start = juce::jlimit (0.0, maxStart, ratio * maxStart);
        viewStart = start;
        viewEnd   = start + range;
        repaint();
    }

    double getVisibleDuration() const noexcept { return viewEnd - viewStart; }
    double getTotalLength() const noexcept { return totalLen; }
    double getCurrentPosition() const noexcept { return currentPos; }

    /** True while pointer is down on waveform (scrub / marker / cut-select drag). */
    bool isPointerInteractionActive() const noexcept { return pointerInteractionActive; }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (thumbnail == nullptr || totalLen <= 0.0) return;

        const auto waveArea = getWaveformBounds();
        const float mouseRatio = showcontrol::gfx::ratioFromMouseX (e.x, waveArea);
        double mouseTime = viewStart + (double) mouseRatio * (viewEnd - viewStart);
        
        double currentRange = viewEnd - viewStart;
        double zoomAmount = wheel.deltaY * 0.28f; 
        double newRange = currentRange * (1.0 - zoomAmount);
        
        newRange = juce::jlimit (0.5, totalLen, newRange); 
        
        viewStart = mouseTime - mouseRatio * newRange;
        viewEnd = viewStart + newRange;
        
        if (viewStart < 0.0) { viewStart = 0.0; viewEnd = newRange; }
        if (viewEnd > totalLen) { viewEnd = totalLen; viewStart = totalLen - newRange; }
        
        repaint();

        if (onViewRangeChanged)
            onViewRangeChanged();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (thumbnail == nullptr || totalLen <= 0.0) return;

        pointerInteractionActive = true;
        const bool allowCutSelect = cutSelectionEnabled && e.mods.isShiftDown();
        dragMode = getDragMode (e.getPosition(), allowCutSelect);
        trimEditedDuringDrag = false;
        cutSelDragging = false;

        if (dragMode == 3)
        {
            const auto waveArea = getWaveformBounds();
            const float ratio = showcontrol::gfx::ratioFromMouseX (e.x, waveArea);
            cutSelAnchor = viewStart + (double) ratio * (viewEnd - viewStart);
            cutSelStart = cutSelEnd = cutSelAnchor;
            repaint();
            return;
        }

        if (dragMode == 0) {
            const auto waveArea = getWaveformBounds();
            const float ratio = showcontrol::gfx::ratioFromMouseX (e.x, waveArea);
            currentPos = viewStart + (double) ratio * (viewEnd - viewStart);
            if (onPlayheadScrubbed) onPlayheadScrubbed (currentPos);
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (thumbnail == nullptr || totalLen <= 0.0) return;
        const auto waveArea = getWaveformBounds();
        const float ratio = showcontrol::gfx::ratioFromMouseX (e.x, waveArea);
        double t = viewStart + (double) ratio * (viewEnd - viewStart);
        double effectiveEnd = (trimEnd > 0.0) ? trimEnd : totalLen;

        constexpr double kTrimMinGapSec = 0.01;
        if (dragMode == 1) {
            const double next = juce::jlimit (0.0, effectiveEnd - kTrimMinGapSec, t);
            if (std::abs (next - trimStart) >= 0.0005)
            {
                trimStart = next;
                trimEditedDuringDrag = true;
                if (onTrimStartChanged) onTrimStartChanged (trimStart);
                repaint();
            }
        } else if (dragMode == 2) {
            const double next = juce::jlimit (trimStart + kTrimMinGapSec, totalLen, t);
            if (std::abs (next - trimEnd) >= 0.0005)
            {
                trimEnd = next;
                trimEditedDuringDrag = true;
                if (onTrimEndChanged) onTrimEndChanged (trimEnd);
                repaint();
            }
        } else if (dragMode == 0) {
            currentPos = juce::jlimit (0.0, totalLen, t);
            if (onPlayheadScrubbed) onPlayheadScrubbed (currentPos); repaint();
        } else if (dragMode == 3) {
            cutSelDragging = true;
            cutSelStart = juce::jlimit (0.0, totalLen, juce::jmin (cutSelAnchor, t));
            cutSelEnd   = juce::jlimit (0.0, totalLen, juce::jmax (cutSelAnchor, t));
            repaint();

            if (onCutSelectionChanged)
                onCutSelectionChanged();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool wasTrimDrag = (dragMode == 1 || dragMode == 2);
        const bool wasCutSelect = (dragMode == 3);

        if (wasCutSelect && ! cutSelDragging && thumbnail != nullptr && totalLen > 0.0)
        {
            const auto waveArea = getWaveformBounds();
            const float ratio = showcontrol::gfx::ratioFromMouseX (e.x, waveArea);
            currentPos = juce::jlimit (0.0, totalLen, viewStart + (double) ratio * (viewEnd - viewStart));

            if (onPlayheadScrubbed)
                onPlayheadScrubbed (currentPos);

            clearCutSelection();
            repaint();
        }

        dragMode = 0;
        cutSelDragging = false;
        pointerInteractionActive = false;

        if (wasTrimDrag && trimEditedDuringDrag && onTrimGestureFinished)
            onTrimGestureFinished();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool allowCutSelect = cutSelectionEnabled && e.mods.isShiftDown();
        const int mode = getDragMode (e.getPosition(), allowCutSelect);

        if (mode == 1 || mode == 2)
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else if (mode == 3)
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
        else
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

private:
    juce::Rectangle<int> getRulerBounds() const
    {
        auto b = getLocalBounds().reduced (2, 2);
        return b.removeFromTop (18);
    }

    juce::Rectangle<int> getWaveformBounds() const
    {
        auto b = getLocalBounds().reduced (3, 4);
        if (showTimeRuler)
            b.removeFromTop (18);
        return b;
    }

    void drawTimeRuler (juce::Graphics& g, const juce::Rectangle<int>& r) const
    {
        if (r.getWidth() <= 0 || viewEnd <= viewStart)
            return;

        const auto pal = ShowTheme::get (isDark);
        g.setColour (pal.borderSubtle);
        showcontrol::gfx::safeFillRect (g, r);
        g.setColour (pal.border);
        g.drawHorizontalLine ((float) r.getBottom(), (float) r.getX(), (float) r.getRight());

        const double visibleDuration = viewEnd - viewStart;
        if (visibleDuration <= 1.0e-12)
            return;

        double tickStepSec = 1.0;
        if (visibleDuration > 40.0) tickStepSec = 5.0;
        if (visibleDuration > 120.0) tickStepSec = 10.0;

        const double firstTick = std::floor (viewStart / tickStepSec) * tickStepSec;
        g.setFont (showcontrol::trimEditor::rulerFont());
        g.setColour (pal.textMuted);
        for (double t = firstTick; t <= viewEnd; t += tickStepSec)
        {
            const int x = showcontrol::gfx::timeToPixelX (r, t, viewStart, viewEnd);
            showcontrol::gfx::safeDrawVerticalLine (g, x, (float) r.getY() + 10.0f, (float) r.getBottom());
            const int secs = (int) std::max (0.0, t);
            const juce::String label = juce::String::formatted ("%02d:%02d", secs / 60, secs % 60);
            g.drawText (label, x + 3, r.getY(), 48, 10, juce::Justification::centredLeft, false);
        }
    }

    int getDragMode (juce::Point<int> mousePos, bool allowCutSelect) const {
        if (thumbnail == nullptr || totalLen <= 0.0 || (viewEnd <= viewStart)) return 0;

        const auto waveArea = getWaveformBounds();
        auto hitArea = waveArea;

        if (showTimeRuler)
            hitArea = getRulerBounds().getUnion (waveArea);

        if (! hitArea.contains (mousePos))
            return 0;

        double effectiveEnd = (trimEnd > 0.0) ? trimEnd : totalLen;
        const int startX = showcontrol::gfx::timeToPixelX (waveArea, trimStart, viewStart, viewEnd);
        const int endX   = showcontrol::gfx::timeToPixelX (waveArea, effectiveEnd, viewStart, viewEnd);

        constexpr int kMarkerHitPx = 14;
        if (std::abs (mousePos.x - startX) <= kMarkerHitPx) return 1;
        if (std::abs (mousePos.x - endX) <= kMarkerHitPx) return 2;

        if (! waveArea.contains (mousePos))
            return 0;

        if (allowCutSelect) return 3;
        return 0;
    }
    juce::AudioThumbnail* thumbnail = nullptr; double currentPos = 0.0, totalLen = 0.0, trimStart = 0.0, trimEnd = 0.0; bool isDark = true; int dragMode = 0;
    bool pointerInteractionActive = false;
    double viewStart = 0.0, viewEnd = 0.0;
    double fadeInSec = 0.0, fadeOutSec = 0.0, effectiveLen = 0.0;
    bool showTimeRuler = false;
    bool trimEditedDuringDrag = false;
    bool cutSelectionEnabled = false;
    bool cutSelDragging = false;
    double cutSelStart = -1.0, cutSelEnd = -1.0, cutSelAnchor = 0.0;
    juce::Colour itemInk { showcontrol::colours::tagColourAt (7) };
};

//==============================================================================
class InspectorStyle : public juce::LookAndFeel_V4
{
public:
    void setDarkMode (bool dark) { isDarkMode = dark; }

    void setItemAccentColour (juce::Colour c) noexcept { itemAccent = c; }

    juce::Colour getItemAccent() const noexcept { return itemAccent; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        const auto id = button.getComponentID();

        if (id == "sc_inspector_icon_play" || id == "sc_inspector_icon_pause" || id == "sc_inspector_icon_stop")
        {
            juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
            const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (backgroundColour);
            g.fillRoundedRectangle (bounds, 5.0f);
            return;
        }

        LookAndFeel_V4::drawButtonBackground (g, button, backgroundColour,
                                              shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        juce::ignoreUnused (minSliderPos, maxSliderPos);

        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                              minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const bool isHoriz = (style == juce::Slider::LinearHorizontal);
        constexpr float trackH = 3.0f;
        constexpr float thumbD = 10.0f;
        const float thumbR = thumbD * 0.5f;
        const auto trackCol = slider.findColour (juce::Slider::trackColourId);
        const auto thumbCol = slider.findColour (juce::Slider::thumbColourId);

        juce::Rectangle<float> track;
        if (isHoriz)
            track = { (float) x + thumbR, (float) y + (float) height * 0.5f - trackH * 0.5f,
                      (float) width - thumbD, trackH };
        else
            track = { (float) x + (float) width * 0.5f - trackH * 0.5f, (float) y + thumbR,
                      trackH, (float) height - thumbD };

        g.setColour (trackCol);
        g.fillRoundedRectangle (track, trackH * 0.5f);

        if (isHoriz)
        {
            const float fillW = juce::jlimit (0.0f, track.getWidth(), sliderPos - track.getX());
            if (fillW > 0.5f)
            {
                g.setColour (thumbCol.withAlpha (0.55f));
                g.fillRoundedRectangle (track.withWidth (fillW), trackH * 0.5f);
            }

            const float thumbX = juce::jlimit (track.getX(), track.getRight() - thumbD, sliderPos - thumbR);
            g.setColour (thumbCol);
            g.fillEllipse (thumbX, track.getCentreY() - thumbR, thumbD, thumbD);
        }
        else
        {
            const float fillH = juce::jlimit (0.0f, track.getHeight(), track.getBottom() - sliderPos);
            if (fillH > 0.5f)
            {
                g.setColour (thumbCol.withAlpha (0.55f));
                g.fillRoundedRectangle (track.withTop (sliderPos).withHeight (fillH), trackH * 0.5f);
            }

            const float thumbY = juce::jlimit (track.getY(), track.getBottom() - thumbD, sliderPos - thumbR);
            g.setColour (thumbCol);
            g.fillEllipse (track.getCentreX() - thumbR, thumbY, thumbD, thumbD);
        }
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
        const bool highlighted = box.isMouseOver();
        g.setColour (showcontrol::ui::comboBoxBackground (isDarkMode, highlighted));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        const auto textArea = bounds.reduced (6.0f, 2.0f).withTrimmedRight (16.0f);
        g.setColour (box.findColour (juce::ComboBox::textColourId));
        g.setFont (showcontrol::inspector::paramLabelFont());
        g.drawText (box.getText(), textArea.toNearestInt(), juce::Justification::centredLeft, true);

        juce::Path p;
        p.addTriangle ((float) width - 16.0f, (float) height * 0.5f - 2.0f,
                       (float) width - 20.0f, (float) height * 0.5f + 3.0f,
                       (float) width - 12.0f, (float) height * 0.5f + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (p);
    }

    /** Tránh vẽ chữ 2 lần (Label mặc định của ComboBox + drawComboBox). */
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        juce::ignoreUnused (box);
        label.setBounds (0, 0, 0, 0);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        const auto id = button.getComponentID();
        auto col = button.findColour (juce::TextButton::textColourOffId);
        if (! button.isEnabled())
            col = col.withAlpha (0.45f);

        const auto iconBounds = showcontrol::icons::centredIconIn (button.getLocalBounds().toFloat(),
                                                                   showcontrol::icons::kListIconSize);

        if (id == "sc_inspector_skip_back")
        {
            showcontrol::icons::paintSkipBackIcon (g, iconBounds, col);
            return;
        }

        if (id == "sc_inspector_icon_play")
        {
            showcontrol::icons::paintPlayIcon (g, iconBounds, col);
            return;
        }

        if (id == "sc_inspector_icon_pause")
        {
            showcontrol::icons::paintPauseIcon (g, iconBounds, col);
            return;
        }

        if (id == "sc_inspector_icon_stop")
        {
            showcontrol::icons::paintStopIcon (g, iconBounds, col);
            return;
        }

        if (id == "sc_inspector_fade_out")
        {
            showcontrol::icons::paintFadeSlopeIcon (g, iconBounds, col);
            return;
        }

        g.setColour (col);
        g.setFont (showcontrol::inspector::buttonFont());
        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn, bool isHighlighted, bool) override
    {
        const auto bounds = btn.getLocalBounds().toFloat();
        const bool toggled = btn.getToggleState();
        const auto pal = ShowTheme::get (isDarkMode);
        const auto accent = itemAccent;
        const auto id = btn.getComponentID();

        if (id == "sc_inspector_loop")
        {
            auto fullBounds = bounds;

            if (toggled)
            {
                g.setColour (accent.withAlpha (isDarkMode ? 0.28f : 0.18f));
                g.fillRoundedRectangle (fullBounds, 5.0f);
            }
            else
            {
                const auto cols = showcontrol::ui::toggleButtonColours (isDarkMode, false, isHighlighted);
                g.setColour (cols.background);
                g.fillRoundedRectangle (fullBounds, ShowTheme::kPanelCornerRadius);
                g.setColour (pal.inputOutline.withAlpha (0.85f));
                g.drawRoundedRectangle (fullBounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.0f);
            }

            constexpr float kIcon = showcontrol::icons::kButtonIconSize;
            constexpr float kIconSlot = kIcon + 8.0f;
            auto iconArea = fullBounds.removeFromLeft (kIconSlot).withSizeKeepingCentre (kIcon, kIcon);
            const auto loopCol = toggled ? accent : pal.textMuted;
            showcontrol::icons::paintLoopIcon (g, iconArea, loopCol, toggled);

            g.setColour (toggled ? accent : pal.textPrimary);
            g.setFont (showcontrol::inspector::buttonFont());
            g.drawText (btn.getButtonText(), fullBounds.reduced (2.0f, 0.0f),
                        juce::Justification::centred);
            return;
        }

        if (id == "sc_inspector_equalizer")
        {
            auto fullBounds = bounds;

            if (toggled)
            {
                g.setColour (accent.withAlpha (isDarkMode ? 0.28f : 0.18f));
                g.fillRoundedRectangle (fullBounds, 5.0f);
            }
            else
            {
                const auto cols = showcontrol::ui::toggleButtonColours (isDarkMode, false, isHighlighted);
                g.setColour (cols.background);
                g.fillRoundedRectangle (fullBounds, ShowTheme::kPanelCornerRadius);
                g.setColour (pal.inputOutline.withAlpha (0.85f));
                g.drawRoundedRectangle (fullBounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.0f);
            }

            constexpr float kIcon = showcontrol::icons::kButtonIconSize;
            constexpr float kIconSlot = kIcon + 8.0f;
            auto iconArea = fullBounds.removeFromLeft (kIconSlot).withSizeKeepingCentre (kIcon, kIcon);
            const auto iconCol = toggled ? accent : pal.textPrimary;
            showcontrol::icons::paintSlidersIcon (g, iconArea, iconCol);

            g.setColour (toggled ? accent : pal.textPrimary);
            g.setFont (showcontrol::inspector::buttonFont());
            g.drawText (btn.getButtonText(), fullBounds.reduced (2.0f, 0.0f),
                        juce::Justification::centred);
            return;
        }

        if (id == "sc_inspector_normalize")
        {
            if (toggled)
            {
                g.setColour (accent);
                g.fillRoundedRectangle (bounds, 5.0f);
            }
            else
            {
                const auto cols = showcontrol::ui::toggleButtonColours (isDarkMode, false, isHighlighted);
                g.setColour (cols.background);
                g.fillRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius);
                g.setColour (pal.inputOutline.withAlpha (0.85f));
                g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.0f);
            }

            g.setColour (toggled ? juce::Colours::white : pal.textPrimary);
            g.setFont (showcontrol::inspector::buttonFont());
            g.drawText (btn.getButtonText(), bounds, juce::Justification::centred);
            return;
        }

        if (id.startsWith ("theme_seg_"))
        {
            const float r = ShowTheme::kPanelCornerRadius;
            const bool curveLeft  = (id == "theme_seg_left");
            const bool curveRight = (id == "theme_seg_right");

            auto drawSegment = [&] (juce::Rectangle<float> area, juce::Colour col)
            {
                juce::Path p;
                p.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                                       r, r, curveLeft, curveRight, curveLeft, curveRight);
                g.setColour (col);
                g.fillPath (p);
            };

            drawSegment (bounds, pal.panelBg);

            const auto segCols = showcontrol::ui::toggleButtonColours (isDarkMode, toggled, isHighlighted && ! toggled);

            if (toggled)
                drawSegment (bounds.reduced (0.5f), accent.brighter (isHighlighted ? (isDarkMode ? 0.12f : 0.08f) : 0.0f));
            else
                drawSegment (bounds.reduced (0.5f), segCols.background);

            juce::Path outline;
            const auto outlineBounds = bounds.reduced (0.5f);
            outline.addRoundedRectangle (outlineBounds.getX(), outlineBounds.getY(),
                                         outlineBounds.getWidth(), outlineBounds.getHeight(),
                                         r, r, curveLeft, curveRight, curveLeft, curveRight);
            g.setColour (pal.border);
            g.strokePath (outline, juce::PathStrokeType (1.0f));

            g.setColour (toggled ? juce::Colours::white : segCols.text);
            g.setFont (ShowTheme::font (showcontrol::inspector::kParamLabelFontSize,
                                        toggled ? "Bold" : "Plain"));
            g.drawText (btn.getButtonText(), bounds, juce::Justification::centred);
            return;
        }

        const auto cols = showcontrol::ui::toggleButtonColours (isDarkMode, toggled, isHighlighted);
        g.setColour (cols.background);
        g.fillRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius);
        g.setColour (pal.inputOutline.withAlpha (0.85f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.0f);
        g.setColour (cols.text.withMultipliedAlpha (btn.isEnabled() ? 1.0f : 0.5f));
        g.setFont (showcontrol::inspector::buttonFont());
        g.drawText (btn.getButtonText(), bounds, juce::Justification::centred);
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (slider);

        l->setJustificationType (juce::Justification::centred);
        l->setBorderSize ({ 0, 6, 0, 6 });

        const auto pal = ShowTheme::get (isDarkMode);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::TextEditor::focusedOutlineColourId, itemAccent);
        l->setColour (juce::TextEditor::backgroundColourId,
                      slider.findColour (juce::Slider::textBoxBackgroundColourId));

        return l;
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        constexpr float kCapsuleRadius = 5.0f;
        const auto bounds = label.getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (label.findColour (juce::Label::backgroundColourId));
        g.fillRoundedRectangle (bounds, kCapsuleRadius);

        if (! label.isBeingEdited())
        {
            const float alpha = label.isEnabled() ? 1.0f : 0.5f;
            const juce::Font font (getLabelFont (label));

            g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
            g.setFont (font);

            const auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());

            if (font.getHeight() >= 18.0f)
                g.drawText (label.getText(), textArea, label.getJustificationType(), true);
            else
                g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                                  1, label.getMinimumHorizontalScale());
        }
    }

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
    {
        constexpr float kCapsuleRadius = 5.0f;
        g.setColour (textEditor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, kCapsuleRadius);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& textEditor) override
    {
        if (! textEditor.hasKeyboardFocus (true))
            return;

        constexpr float kCapsuleRadius = 5.0f;
        const auto pal = ShowTheme::get (isDarkMode);
        const auto bounds = juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);

        g.setColour (itemAccent);
        g.drawRoundedRectangle (bounds, kCapsuleRadius, 1.5f);
    }

private:
    bool isDarkMode = true;
    juce::Colour itemAccent { showcontrol::colours::tagColourAt (7) };
};

//==============================================================================
//==============================================================================
struct AdvancedTrimEditorCallbacks
{
    std::function<void()> onInspectorSync;
    std::function<void()> onGestureFinished;
    std::function<void (SoundPad*, double, double, std::function<void (bool)>)> onCutRequested;
    std::function<bool()> performUndo;
    std::function<bool()> performRedo;
    std::function<bool()> canUndo;
    std::function<bool()> canRedo;
};

class AdvancedTrimComponent : public juce::Component,
                            public juce::Timer,
                            private juce::ScrollBar::Listener
{
public:
    AdvancedTrimComponent (SoundPad* pad, bool isDark, AdvancedTrimEditorCallbacks callbacksIn)
        : currentPad (pad), isDarkTheme (isDark), callbacks (std::move (callbacksIn))
    {
        onUpdate = callbacks.onInspectorSync;
        onGestureFinished = callbacks.onGestureFinished;

        if (currentPad != nullptr)
            currentPad->setThumbnailLoadAllowed (true);

        setSize (960, 520);
        addAndMakeVisible (largeWaveform);
        largeWaveform.setThumbnail (&currentPad->getThumbnail());
        largeWaveform.setTrimPoints (currentPad->getTrimStart(), currentPad->getTrimEnd());
        largeWaveform.setCutSelectionEnabled (true);

        addAndMakeVisible (hScrollBar);
        hScrollBar.setSingleStepSize (0.015);
        hScrollBar.setAutoHide (false);
        hScrollBar.addListener (this);
        largeWaveform.onViewRangeChanged = [this]
        {
            syncWaveformScrollBar();
            resized();
        };

        double initialLen = currentPad->getPlaybackLength();
        editorPlayheadPos = currentPad->getPlaybackPosition();
        largeWaveform.setProgress (editorPlayheadPos, initialLen);
        largeWaveform.setDarkMode (isDark);

        largeWaveform.onTrimStartChanged = [this] (double t) {
            if (currentPad != nullptr)
                currentPad->setTrimStart (t);
        };
        largeWaveform.onTrimEndChanged = [this] (double t) {
            if (currentPad != nullptr)
                currentPad->setTrimEnd (t);
        };
        largeWaveform.onTrimGestureFinished = [this] { finalizeTrimGesture(); };
        largeWaveform.onPlayheadScrubbed = [this] (double t)
        {
            editorPlayheadPos = t;

            if (currentPad != nullptr)
                currentPad->seekTo (t);
        };
        largeWaveform.onCutSelectionChanged = [this] { updateCutControls(); };
        largeWaveform.setShowTimeRuler (true);

        addAndMakeVisible (infoLabel);
        infoLabel.setText (showcontrol::localization::tr (
                               u8"IN/OUT: kéo marker vàng/đỏ. Playhead: click/kéo trên waveform. "
                               u8"Chọn vùng cắt: giữ Shift + kéo. Zoom: lăn chuột (thanh ngang khi zoom)."),
                           juce::dontSendNotification);
        infoLabel.setFont (showcontrol::trimEditor::infoFont());
        const auto pal = ShowTheme::get (isDark);
        infoLabel.setColour (juce::Label::textColourId, pal.textSecondary);

        auto styleSecondaryBtn = [&] (juce::TextButton& btn)
        {
            btn.setColour (juce::TextButton::buttonColourId, pal.buttonSecondary);
            btn.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
        };

        addAndMakeVisible (cutStartBtn);
        cutStartBtn.setButtonText (showcontrol::localization::tr (u8"IN (điểm cắt)"));
        styleSecondaryBtn (cutStartBtn);
        cutStartBtn.onClick = [this]
        {
            largeWaveform.setCutStartAtCurrentPosition();
            updateCutControls();
        };

        addAndMakeVisible (cutEndBtn);
        cutEndBtn.setButtonText (showcontrol::localization::tr (u8"OUT (điểm cắt)"));
        styleSecondaryBtn (cutEndBtn);
        cutEndBtn.onClick = [this]
        {
            largeWaveform.setCutEndAtCurrentPosition();
            updateCutControls();
        };

        addAndMakeVisible (clearSelBtn);
        clearSelBtn.setButtonText (showcontrol::localization::tr (u8"Xóa chọn"));
        styleSecondaryBtn (clearSelBtn);
        clearSelBtn.onClick = [this]
        {
            largeWaveform.clearCutSelection();
            updateCutControls();
        };

        addAndMakeVisible (cutBtn);
        cutBtn.setButtonText (showcontrol::localization::tr (u8"Xóa đoạn"));
        cutBtn.setColour (juce::TextButton::buttonColourId, showcontrol::ui::destructiveActionColour (getLookAndFeel()));
        cutBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        cutBtn.onClick = [this] { requestAudioCut(); };

        addAndMakeVisible (undoBtn);
        undoBtn.setButtonText (showcontrol::localization::tr (u8"Hoàn tác"));
        styleSecondaryBtn (undoBtn);
        undoBtn.onClick = [this]
        {
            if (callbacks.performUndo && callbacks.performUndo())
                reloadFromPad();
        };

        addAndMakeVisible (redoBtn);
        redoBtn.setButtonText (showcontrol::localization::tr (u8"Làm lại"));
        styleSecondaryBtn (redoBtn);
        redoBtn.onClick = [this]
        {
            if (callbacks.performRedo && callbacks.performRedo())
                reloadFromPad();
        };

        addAndMakeVisible (resetButton);
        resetButton.setButtonText (showcontrol::localization::tr (u8"ĐẶT LẠI"));
        styleSecondaryBtn (resetButton);
        resetButton.onClick = [this] { performTrimReset(); };

        addAndMakeVisible (closeBtn);
        closeBtn.setButtonText (showcontrol::localization::tr (u8"XÁC NHẬN & ĐÓNG"));
        closeBtn.setColour (juce::TextButton::buttonColourId, pal.accentSoft);
        closeBtn.setColour (juce::TextButton::textColourOffId, isDark ? juce::Colours::white : pal.panelElevated);
        closeBtn.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };

        updateCutControls();
        syncWaveformScrollBar();
        startTimer (40);
    }
    ~AdvancedTrimComponent() override
    {
        stopTimer();
        hScrollBar.removeListener (this);
        largeWaveform.setThumbnail (nullptr);
    }

    void scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override
    {
        juce::ignoreUnused (scrollBarThatHasMoved);

        if (updatingScrollFromWaveform)
            return;

        const double total = juce::jmax (0.001, largeWaveform.getTotalLength());
        const double proportion = juce::jlimit (0.05, 1.0, largeWaveform.getVisibleDuration() / total);
        const double maxStart = 1.0 - proportion;
        largeWaveform.setViewScrollRatio (maxStart > 0.0 ? newRangeStart / maxStart : 0.0);
    }

    void timerCallback() override
    {
        if (currentPad != nullptr)
        {
            const double len = currentPad->getPlaybackLength();

            if (currentPad->isPlaybackPositionLive())
                editorPlayheadPos = currentPad->getPlaybackPosition();

            if (! largeWaveform.isPointerInteractionActive())
                largeWaveform.setProgress (editorPlayheadPos, len);
        }

        updateUndoRedoButtons();
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

    void paint (juce::Graphics& g) override { g.fillAll (ShowTheme::get (isDarkTheme).windowBg); }
    void resized() override
    {
        auto bounds = getLocalBounds();

        if (kMacTopDragInset > 0)
            bounds.removeFromTop (kMacTopDragInset);

        auto b = bounds.reduced (20);
        const int footerH = 108;
        const int scrollH = (largeWaveform.canScrollHorizontally() || hScrollBar.isVisible()) ? 16 : 0;
        auto footer = b.removeFromBottom (footerH);

        if (scrollH > 0)
        {
            hScrollBar.setVisible (true);
            hScrollBar.setBounds (b.removeFromBottom (scrollH).reduced (0, 1));
        }
        else
        {
            hScrollBar.setVisible (false);
        }

        largeWaveform.setBounds (b);
        infoLabel.setBounds (footer.removeFromTop (22));

        auto buttonArea = footer;
        const int btnH = 32;
        const int rowGap = 6;

        struct BtnSpec { juce::Button* btn; int width; };
        const BtnSpec row1[] = {
            { &cutStartBtn, 108 }, { &cutEndBtn, 112 }, { &clearSelBtn, 88 }, { &cutBtn, 96 }
        };
        const BtnSpec row2[] = {
            { &undoBtn, 80 }, { &redoBtn, 80 }, { &resetButton, 96 }, { &closeBtn, 168 }
        };

        auto layoutCenteredRow = [&] (juce::Rectangle<int> row, const BtnSpec* specs, int count)
        {
            const int gap = 6;
            int totalW = 0;

            for (int i = 0; i < count; ++i)
                totalW += specs[i].width + gap;

            totalW -= gap;
            int x = row.getCentreX() - totalW / 2;

            for (int i = 0; i < count; ++i)
            {
                specs[i].btn->setBounds (x, row.getY(), specs[i].width, btnH);
                x += specs[i].width + gap;
            }
        };

        auto row1Area = buttonArea.removeFromTop (btnH);
        layoutCenteredRow (row1Area, row1, 4);
        buttonArea.removeFromTop (rowGap);
        auto row2Area = buttonArea.removeFromTop (btnH);
        layoutCenteredRow (row2Area, row2, 4);
    }

private:
    void syncWaveformScrollBar()
    {
        updatingScrollFromWaveform = true;

        if (largeWaveform.canScrollHorizontally())
        {
            hScrollBar.setVisible (true);

            const double total = juce::jmax (0.001, largeWaveform.getTotalLength());
            const double proportion = juce::jlimit (0.05, 1.0, largeWaveform.getVisibleDuration() / total);
            const double maxStart = 1.0 - proportion;
            const double start = largeWaveform.getViewScrollRatio() * maxStart;

            hScrollBar.setRangeLimits (0.0, maxStart);
            hScrollBar.setCurrentRange (start, proportion, juce::dontSendNotification);
        }
        else
        {
            hScrollBar.setVisible (false);
        }

        updatingScrollFromWaveform = false;

        if (largeWaveform.canScrollHorizontally())
            hScrollBar.toFront (false);
    }

private:
   #if JUCE_MAC
    static constexpr int kMacTopDragInset = 14;
   #else
    static constexpr int kMacTopDragInset = 0;
   #endif

    void reloadFromPad()
    {
        if (currentPad == nullptr)
            return;

        currentPad->reloadThumbnailImmediately();
        largeWaveform.clearCutSelection();
        largeWaveform.setTrimPoints (currentPad->getTrimStart(), currentPad->getTrimEnd());
        editorPlayheadPos = currentPad->getPlaybackPosition();
        largeWaveform.setProgress (editorPlayheadPos, currentPad->getPlaybackLength());
        updateCutControls();

        if (onUpdate)
            onUpdate();
    }

    void updateCutControls()
    {
        const bool hasSel = largeWaveform.hasCutSelection();
        cutBtn.setEnabled (hasSel && ! cutInProgress);
        clearSelBtn.setEnabled (hasSel && ! cutInProgress);
        cutStartBtn.setEnabled (! cutInProgress);
        cutEndBtn.setEnabled (! cutInProgress);
        updateUndoRedoButtons();
    }

    void updateUndoRedoButtons()
    {
        const bool busy = cutInProgress;
        undoBtn.setEnabled (! busy && callbacks.canUndo && callbacks.canUndo());
        redoBtn.setEnabled (! busy && callbacks.canRedo && callbacks.canRedo());
    }

    void requestAudioCut()
    {
        if (currentPad == nullptr || cutInProgress || ! largeWaveform.hasCutSelection())
            return;

        double cutStart = 0.0, cutEnd = 0.0;
        largeWaveform.getCutSelection (cutStart, cutEnd);

        const auto durationText = currentPad->formatTimeString (cutEnd - cutStart);
        const auto title = showcontrol::localization::tr (u8"Cắt & xóa đoạn âm thanh");
        const auto subtext = showcontrol::localization::tr (
            u8"Xóa vĩnh viễn đoạn đã chọn (%TIME%). File gốc không bị xóa — bản chỉnh được lưu riêng. Có thể Hoàn tác sau khi cắt.")
                                 .replace ("%TIME%", durationText);

        showcontrol::ui::showConfirmDeleteDialog (
            this,
            title,
            subtext,
            showcontrol::localization::tr (u8"Cắt"),
            [this, cutStart, cutEnd] (bool confirmed)
            {
                if (! confirmed || callbacks.onCutRequested == nullptr || currentPad == nullptr)
                    return;

                cutInProgress = true;
                updateCutControls();

                juce::Component::SafePointer<AdvancedTrimComponent> safe (this);
                callbacks.onCutRequested (currentPad, cutStart, cutEnd,
                                          [safe] (bool success)
                                          {
                                              juce::MessageManager::callAsync ([safe, success]
                                              {
                                                  if (safe == nullptr)
                                                      return;

                                                  safe->cutInProgress = false;

                                                  if (success)
                                                      safe->reloadFromPad();
                                                  else
                                                      safe->updateCutControls();
                                              });
                                          });
            });
    }

    void finalizeTrimGesture()
    {
        if (currentPad == nullptr)
            return;

        currentPad->syncPlaybackPositionToTrimRange();
        currentPad->triggerTrimUpdateLive();

        if (onUpdate)
            onUpdate();

        if (onGestureFinished)
            onGestureFinished();
    }

    void performTrimReset()
    {
        if (currentPad == nullptr)
            return;

        const double totalLen = currentPad->getPlaybackLength();

        currentPad->setTrimStart (0.0);
        currentPad->setTrimEnd (totalLen);
        currentPad->triggerTrimUpdateLive();

        largeWaveform.setTrimPoints (0.0, totalLen);
        editorPlayheadPos = 0.0;
        largeWaveform.setProgress (editorPlayheadPos, totalLen);

        if (onUpdate)
            onUpdate();

        repaint();
    }

    SoundPad* currentPad = nullptr;
    bool isDarkTheme = true;
    double editorPlayheadPos = 0.0;
    bool cutInProgress = false;
    bool updatingScrollFromWaveform = false;
    juce::ScrollBar hScrollBar { false };
    AdvancedTrimEditorCallbacks callbacks;
    std::function<void()> onUpdate;
    std::function<void()> onGestureFinished;
    InspectorWaveform largeWaveform;
    juce::Label infoLabel;
    juce::TextButton cutStartBtn, cutEndBtn, clearSelBtn, cutBtn, undoBtn, redoBtn;
    juce::TextButton resetButton, closeBtn;
    juce::ComponentDragger windowDragger;
    bool windowDragActive = false;
};

//==============================================================================
class InspectorSectionDivider : public juce::Component
{
public:
    void setDarkMode (bool dark) noexcept
    {
        if (isDarkMode == dark)
            return;

        isDarkMode = dark;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (ShowTheme::get (isDarkMode).border.withAlpha (0.55f));
        g.fillRect (0, getHeight() / 2, getWidth(), 1);
    }

private:
    bool isDarkMode = true;
};

//==============================================================================
class InspectorPanel : public juce::Component,
                       public juce::Timer,
                       private juce::Label::Listener
{
public:
    InspectorPanel()
    {
        addAndMakeVisible (inspectorViewport);
        inspectorViewport.setViewedComponent (&inspectorContent, false);
        inspectorViewport.setScrollBarsShown (true, false);
        inspectorViewport.setScrollBarThickness (8);

        inspectorContent.addAndMakeVisible (nameLabel);
        nameLabel.setText (juce::String::fromUTF8 (u8"Name"), juce::dontSendNotification);
        nameLabel.setFont (showcontrol::inspector::paramLabelFont());
        nameLabel.setJustificationType (juce::Justification::centredLeft);

        inspectorContent.addAndMakeVisible (nameValueLabel);
        nameValueLabel.setMinimumHorizontalScale (1.0f);
        nameValueLabel.setFont (showcontrol::inspector::trackNameValueFont());
        nameValueLabel.setJustificationType (juce::Justification::centredLeft);
        nameValueLabel.setBorderSize ({ 0, 2, 0, 0 });
        nameValueLabel.setEditable (true, true, false);
        nameValueLabel.setText (juce::String::fromUTF8 (u8"Chưa chọn pad"), juce::dontSendNotification);
        nameValueLabel.addListener (this);

        inspectorContent.addAndMakeVisible (waveformDisplay);
        waveformDisplay.setShowTimeRuler (false);
        
        // Trim nhỏ bên Inspector: chỉ kéo 2 marker, không quét vùng.
        waveformDisplay.onTrimStartChanged = [this] (double t) {
            if (currentPad)
            {
                currentPad->setTrimStart (t);
                trimStartLabel.setText (currentPad->formatTimeString (t), juce::dontSendNotification);
            }
        };
        waveformDisplay.onTrimEndChanged = [this] (double t) {
            if (currentPad)
            {
                currentPad->setTrimEnd (t);
                trimEndLabel.setText (currentPad->formatTimeString (t), juce::dontSendNotification);
            }
        };
        waveformDisplay.onTrimGestureFinished = [this]
        {
            if (currentPad == nullptr)
                return;

            currentPad->syncPlaybackPositionToTrimRange();
            currentPad->triggerTrimUpdateLive();

            if (onProjectEdited)
                onProjectEdited();
        };
        
        waveformDisplay.onWaveformDoubleClicked = [this] { openAdvancedTrimWindow(); };
        waveformDisplay.onPlayheadScrubbed = [this] (double t) { if (currentPad) currentPad->seekTo (t); };

        inspectorContent.addAndMakeVisible (trimStartLabel); trimStartLabel.setText ("00:00.0", juce::dontSendNotification);
        trimStartLabel.setFont (ShowTheme::timerFont (10.0f)); trimStartLabel.setJustificationType (juce::Justification::centredLeft);

        inspectorContent.addAndMakeVisible (trimEndLabel); trimEndLabel.setText ("00:00.0", juce::dontSendNotification);
        trimEndLabel.setFont (ShowTheme::timerFont (10.0f)); trimEndLabel.setJustificationType (juce::Justification::centredRight);

        inspectorContent.addAndMakeVisible (trimResetBtn);
        trimResetBtn.setLookAndFeel (&inspectorStyle);
        trimResetBtn.setButtonText (juce::String::fromUTF8 (u8"Reset"));
        trimResetBtn.onClick = [this] {
            if (currentPad) {
                currentPad->setTrimStart (0.0); currentPad->setTrimEnd (0.0); 
                currentPad->triggerTrimUpdateLive(); // Đồng bộ trả dải nền phẳng lặng
                waveformDisplay.setTrimPoints (0.0, 0.0);
                trimStartLabel.setText ("00:00.0", juce::dontSendNotification); double len = currentPad->getPlaybackLength();
                trimEndLabel.setText (currentPad->formatTimeString (len), juce::dontSendNotification);
                if (onProjectEdited) onProjectEdited();
            }
        };

        backBtn.setComponentID ("sc_inspector_skip_back");
        backBtn.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (backBtn);
        backBtn.setButtonText ({});
        backBtn.onClick = [this] { if (currentPad) currentPad->seekTo (currentPad->getTrimStart()); };

        playBtn.setComponentID ("sc_inspector_icon_play");
        playBtn.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (playBtn);
        playBtn.setButtonText ({});
        playBtn.setWantsKeyboardFocus (false);
        playBtn.onClick = [this]
        {
            if (isPlaybackCommandBlocked != nullptr && isPlaybackCommandBlocked())
                return;

            if (currentPad != nullptr)
            {
                if (onPlayPadRequested)
                {
                    onPlayPadRequested (currentPad);
                    refreshTransportUi();
                }
                else
                {
                    currentPad->triggerPlay();
                    refreshTransportUi();
                }
            }
        };
        fadeOutBtn.setComponentID ("sc_inspector_fade_out");
        fadeOutBtn.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (fadeOutBtn);
        fadeOutBtn.setButtonText ({});
        fadeOutBtn.onClick = [this] {
            if (isPlaybackCommandBlocked != nullptr && isPlaybackCommandBlocked())
                return;

            if (currentPad && currentPad->isPlaying())
            {
                if (onFadePadRequested)
                {
                    onFadePadRequested (currentPad);
                    refreshTransportUi();
                }
                else
                {
                    currentPad->stopTransportWithConfiguredFade();
                    refreshTransportUi();
                }
            }
        };

        // Fade In duration slider (0 = tắt)
        inspectorContent.addAndMakeVisible (fadeInLabel);
        fadeInLabel.setText (juce::String::fromUTF8 (u8"Fade In"), juce::dontSendNotification);
        fadeInLabel.setFont (showcontrol::inspector::paramLabelFont());
        fadeInLabel.setJustificationType (juce::Justification::centredRight);

        fadeInSlider.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (fadeInSlider);
        fadeInSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        fadeInSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 26);
        fadeInSlider.setRange (0.0, 10000.0, 50.0);
        fadeInSlider.setValue (0.0);
        fadeInSlider.setNumDecimalPlacesToDisplay (0);
        fadeInSlider.setTextValueSuffix (" ms");
        fadeInSlider.setDoubleClickReturnValue (true, 0.0);
        fadeInSlider.onValueChange = [this] {
            if (currentPad) {
                currentPad->setFadeInMs (fadeInSlider.getValue());
                if (onProjectEdited) onProjectEdited();
                waveformDisplay.setFadeRegions (currentPad->getFadeInMs(), currentPad->getFadeOutMs(), currentPad->getEffectiveLength());
            }
        };

        // Fade Out duration slider
        inspectorContent.addAndMakeVisible (fadeOutLabel);
        fadeOutLabel.setText (juce::String::fromUTF8 (u8"Fade Out"), juce::dontSendNotification);
        fadeOutLabel.setFont (showcontrol::inspector::paramLabelFont());
        fadeOutLabel.setJustificationType (juce::Justification::centredRight);

        fadeOutSlider.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (fadeOutSlider);
        fadeOutSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        fadeOutSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 26);
        fadeOutSlider.setRange (0.0, 15000.0, 50.0);
        fadeOutSlider.setValue (0.0);
        fadeOutSlider.setNumDecimalPlacesToDisplay (0);
        fadeOutSlider.setTextValueSuffix (" ms");
        fadeOutSlider.setDoubleClickReturnValue (true, 0.0);
        fadeOutSlider.onValueChange = [this] {
            if (currentPad) {
                currentPad->setFadeOutMs (fadeOutSlider.getValue());
                if (onProjectEdited) onProjectEdited();
                waveformDisplay.setFadeRegions (currentPad->getFadeInMs(), currentPad->getFadeOutMs(), currentPad->getEffectiveLength());
            }
        };

        equalizerBtn.setLookAndFeel (&inspectorStyle);
        equalizerBtn.setComponentID ("sc_inspector_equalizer");
        inspectorContent.addAndMakeVisible (equalizerBtn);
        equalizerBtn.setButtonText (showcontrol::localization::tr (u8"Equalizer"));
        equalizerBtn.onClick = [this] { openEqualizerDialog(); };

        inspectorContent.addAndMakeVisible (timeLabel); timeLabel.setText ("00:00.0", juce::dontSendNotification);
        timeLabel.setFont (showcontrol::inspector::timeRemainingFont());
        timeLabel.setMinimumHorizontalScale (1.0f);
        timeLabel.setJustificationType (juce::Justification::centred);

        inspectorContent.addAndMakeVisible (metaFormatLabel);
        metaFormatLabel.setFont (showcontrol::inspector::paramLabelFont());
        metaFormatLabel.setJustificationType (juce::Justification::centred);

        loopToggle.setComponentID ("sc_inspector_loop");
        loopToggle.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (loopToggle);
        loopToggle.onClick = [this]
        {
            if (currentPad != nullptr)
            {
                currentPad->setLooping (loopToggle.getToggleState());
                if (onPadLoopChanged)
                    onPadLoopChanged (currentPad);
                if (onProjectEdited) onProjectEdited();
            }
        };

        inspectorContent.addAndMakeVisible (volumeLabel);
        volumeLabel.setText (showcontrol::localization::tr (u8"Âm lượng:"), juce::dontSendNotification);
        volumeLabel.setFont (showcontrol::inspector::paramLabelFont());
        volumeLabel.setJustificationType (juce::Justification::centredLeft);

        volumeSlider.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (volumeSlider); volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal); volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volumeSlider.setRange (0.0, 1.0, 0.01); volumeSlider.setValue (1.0);
        volumeSlider.onValueChange = [this] {
            if (currentPad)
            {
                currentPad->setOutputGain ((float) volumeSlider.getValue());
                if (onProjectEdited) onProjectEdited();
            }
        };

        // Auto Normalize Toggle với 3 chế độ AI
        inspectorContent.addAndMakeVisible (normalizeToggle);
        normalizeToggle.setComponentID ("sc_inspector_normalize");
        normalizeToggle.setButtonText (showcontrol::localization::tr (u8"Đồng bộ âm lượng"));
        normalizeToggle.setToggleState (true, juce::dontSendNotification);
        normalizeToggle.setLookAndFeel (&inspectorStyle);
        normalizeToggle.onClick = [this] {
            if (currentPad) {
                const bool state = normalizeToggle.getToggleState();
                auto settings = currentPad->getLoudnessSettings();
                settings.enabled = state;
                currentPad->setLoudnessSettings (settings);

                if (state)
                {
                    if (! currentPad->applyVolumeSyncGainIfReady())
                        currentPad->requestNormalization();
                    if (currentPad->applyVolumeSyncGainIfReady())
                        volumeSlider.setValue (currentPad->getOutputGain(), juce::dontSendNotification);
                }
                else
                {
                    currentPad->setOutputGain (1.0f);
                    volumeSlider.setValue (1.0f, juce::dontSendNotification);
                    refreshLoudnessLabel();
                }
                if (onProjectEdited) onProjectEdited();
                updateNormalizeModeVisibility();
            }
        };

        inspectorContent.addAndMakeVisible (normalizeAdvancedBtn);
        normalizeAdvancedBtn.setLookAndFeel (&inspectorStyle);
        normalizeAdvancedBtn.setButtonText (showcontrol::localization::tr (u8"Cài đặt nâng cao..."));
        normalizeAdvancedBtn.onClick = [this] { openLoudnessManagerDialog(); };

        inspectorContent.addAndMakeVisible (rmsLabel);
        rmsLabel.setText (juce::String::fromUTF8 (u8"—"), juce::dontSendNotification);
        rmsLabel.setFont (showcontrol::inspector::paramLabelFont());
        rmsLabel.setJustificationType (juce::Justification::centredLeft);
        rmsLabel.setVisible (false);

        inspectorContent.addAndMakeVisible (sectionDivider);

        metaBpmLabel.setVisible (false);

        // Hotkey Scope — ẩn khỏi UI, giữ logic callback để MainComponent vẫn hoạt động
        hotkeyScopeActiveBtn.setLookAndFeel (&inspectorStyle);
        hotkeyScopeGlobalBtn.setLookAndFeel (&inspectorStyle);
        hotkeyScopeLabel.setText ("Hotkey Scope", juce::dontSendNotification);
        hotkeyScopeLabel.setFont (showcontrol::inspector::paramLabelFont());
        hotkeyScopeActiveBtn.setButtonText ("Active List");
        hotkeyScopeGlobalBtn.setButtonText ("Global");
        setHotkeyScopeSelectionId (1, juce::dontSendNotification);

        auto applyHotkeyScope = [this] (int id)
        {
            setHotkeyScopeSelectionId (id, juce::dontSendNotification);
            if (onHotkeyScopeChanged)
                onHotkeyScopeChanged (id);
        };
        hotkeyScopeActiveBtn.onClick = [applyHotkeyScope] { applyHotkeyScope (1); };
        hotkeyScopeGlobalBtn.onClick = [applyHotkeyScope] { applyHotkeyScope (2); };

        // Output Bus selector — điều hướng tín hiệu pad ra cổng output riêng
        inspectorContent.addAndMakeVisible (outputBusLabel);
        outputBusLabel.setText (showcontrol::localization::tr (u8"Đầu ra Audio (Bus):"), juce::dontSendNotification);
        outputBusLabel.setFont (showcontrol::inspector::paramLabelFont());
        outputBusLabel.setJustificationType (juce::Justification::centredLeft);

        outputBusCombo.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (outputBusCombo);
        setBusNames (showcontrol::routing::getInspectorBusDisplayNames());
        outputBusCombo.onChange = [this]
        {
            if (currentPad == nullptr)
                return;

            const int bus = juce::jlimit (0, showcontrol::routing::kInspectorBusCount - 1,
                                          outputBusCombo.getSelectedItemIndex());
            currentPad->setOutputBus (bus);
            if (onOutputBusChanged)
                onOutputBusChanged (bus);
            if (onProjectEdited)
                onProjectEdited();
        };

        assignInspectorTooltips();

        setOpaque (true);
        updateThemeColors (true);
        startTimer (50);
    }

    ~InspectorPanel() override
    {
        nameValueLabel.removeListener (this);
        stopTimer();
        detachFromPadResources();
        trimResetBtn.setLookAndFeel (nullptr);
        normalizeAdvancedBtn.setLookAndFeel (nullptr);
        loopToggle.setLookAndFeel (nullptr);
        normalizeToggle.setLookAndFeel (nullptr);
        hotkeyScopeActiveBtn.setLookAndFeel (nullptr);
        hotkeyScopeGlobalBtn.setLookAndFeel (nullptr);
        outputBusCombo.setLookAndFeel (nullptr);
        fadeInSlider.setLookAndFeel (nullptr);
        fadeOutSlider.setLookAndFeel (nullptr);
        volumeSlider.setLookAndFeel (nullptr);
        equalizerBtn.setLookAndFeel (nullptr);
        backBtn.setLookAndFeel (nullptr);
        playBtn.setLookAndFeel (nullptr);
        fadeOutBtn.setLookAndFeel (nullptr);
    }

    std::function<void(int)> onHotkeyScopeChanged;
    /** MainComponent đồng bộ selection trước khi trigger Play từ Inspector. */
    std::function<void(SoundPad*)> onPlayPadRequested;
    std::function<bool()> isPlaybackCommandBlocked;
    /** MainComponent đồng bộ selection trước khi Fade từ Inspector. */
    std::function<void(SoundPad*)> onFadePadRequested;
    /** Gọi khi tên/trim/volume/normalize thay đổi — MainComponent lưu project. */
    std::function<void()> onProjectEdited;
    /** Live update — tên pad đổi trong nameEditor, MainComponent refresh CUE/BGM list. */
    std::function<void()> onTrackNameChanged;
    std::function<void()> onInspectorGestureBegan;
    std::function<void()> onInspectorGestureEnded;
    std::function<void (SoundPad*)> onPadLoopChanged;
    /** Gọi khi user đổi output bus của pad — MainComponent lưu project. */
    std::function<void(int busIndex)> onOutputBusChanged;

    /** Cắt & xóa đoạn âm thanh trong Trim Editor — MainComponent xử lý undo. */
    std::function<void (SoundPad*, double, double, std::function<void (bool)>)> onPadAudioCutRequested;
    std::function<bool()> onApplicationUndoRequested;
    std::function<bool()> onApplicationRedoRequested;
    std::function<bool()> onApplicationCanUndo;
    std::function<bool()> onApplicationCanRedo;

    /** Đồng bộ màu tag PAD ↔ Cuelist + lưu JSON. */
    std::function<void(SoundPad*, juce::Colour)> onTagColourChanged;
    std::function<void()> onActivePadChanged;

    /** Chuẩn hoá âm lượng toàn bộ pad có file trong list đang active. */
    std::function<void (const showcontrol::loudness::LoudnessSettings&)> onNormalizeActiveListRequested;

    /** Preview trước/sau cho toàn list — dùng trong Loudness Manager dialog. */
    std::function<juce::Array<showcontrol::loudness::ListPreviewRow> (
        const showcontrol::loudness::LoudnessSettings&)> onFetchLoudnessListPreview;

    /** Lưu RMS/LUFS mặc định cho project + pad mới. */
    std::function<void(bool useLufs)> onDefaultNormalizeModeChanged;

    bool getNormalizeModeIsLufs() const { return normalizeModeIsLufs; }

    void setDefaultNormalizeMode (bool useLufs)
    {
        normalizeModeIsLufs = useLufs;
    }
    SoundPad* getCurrentPad() const noexcept { return currentPad; }

    /** Gỡ ChangeListener + con trỏ thumbnail trước khi hủy SoundPad hoặc thoát app. */
    void detachFromPadResources() noexcept
    {
        waveformDisplay.setThumbnail (nullptr);
        currentPad = nullptr;
    }

    void setBusNames (const juce::StringArray& names)
    {
        cachedInspectorBusNames.clear();

        for (int i = 0; i < showcontrol::routing::kInspectorBusCount; ++i)
        {
            if (i < names.size() && names[i].isNotEmpty())
                cachedInspectorBusNames.add (names[i]);
            else
                cachedInspectorBusNames.add (showcontrol::routing::getBusDisplayName (i));
        }

        rebuildOutputBusCombo();
    }

    void refreshLocalizedText()
    {
        equalizerBtn.setButtonText (showcontrol::localization::tr (u8"Equalizer"));
        normalizeToggle.setButtonText (showcontrol::localization::tr (u8"Đồng bộ âm lượng"));
        normalizeAdvancedBtn.setButtonText (showcontrol::localization::tr (u8"Cài đặt nâng cao..."));
        volumeLabel.setText (showcontrol::localization::tr (u8"Âm lượng:"), juce::dontSendNotification);
        outputBusLabel.setText (showcontrol::localization::tr (u8"Đầu ra Audio (Bus):"), juce::dontSendNotification);
        loopToggle.setButtonText (getLoopToggleLabel());
        assignInspectorTooltips();
        rebuildOutputBusCombo();
        repaint();
    }

    int getHotkeyScopeSelectionId() const
    {
        return hotkeyScopeGlobalBtn.getToggleState() ? 2 : 1;
    }

    void setHotkeyScopeSelectionId (int id, juce::NotificationType notify = juce::dontSendNotification)
    {
        const bool isGlobal = (id == 2);
        hotkeyScopeActiveBtn.setToggleState (! isGlobal, juce::dontSendNotification);
        hotkeyScopeGlobalBtn.setToggleState (isGlobal, juce::dontSendNotification);

        if (notify == juce::sendNotification && onHotkeyScopeChanged)
            onHotkeyScopeChanged (isGlobal ? 2 : 1);
    }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();

        if (auto* showLaf = dynamic_cast<ShowControlLookAndFeel*> (&getLookAndFeel()))
            updateThemeColors (showLaf->isDarkMode());

        refreshLocalizedText();
    }

    void updateThemeColors (bool isDark)
    {
        isDarkTheme = isDark;
        inspectorStyle.setDarkMode (isDark);
        const auto pal = ShowTheme::get (isDark);
        auto textCol  = pal.textPrimary;
        auto mutedCol = pal.textSecondary;
        auto editorBg = pal.inputBg;
        auto editorOutline = pal.inputOutline;
        auto sliderTr = pal.sliderTrack;
        auto btnSec = pal.buttonSecondary;

        nameLabel.setColour (juce::Label::textColourId, mutedCol.withAlpha (0.6f));
        nameValueLabel.setColour (juce::Label::textColourId, textCol);
        nameValueLabel.setFont (showcontrol::inspector::trackNameValueFont());
        nameValueLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        nameValueLabel.setColour (juce::Label::backgroundWhenEditingColourId, editorBg);
        nameValueLabel.setColour (juce::Label::outlineWhenEditingColourId, editorOutline);
        nameValueLabel.setColour (juce::Label::textWhenEditingColourId, textCol);
        waveformDisplay.setDarkMode (isDark);
        trimStartLabel.setColour (juce::Label::textColourId, pal.trimMarkerStart);
        trimEndLabel.setColour (juce::Label::textColourId, pal.trimMarkerEnd);
        trimResetBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        trimResetBtn.setColour (juce::TextButton::textColourOffId, mutedCol);
        backBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        backBtn.setColour (juce::TextButton::textColourOffId, textCol);
        fadeOutBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        fadeOutBtn.setColour (juce::TextButton::textColourOffId, textCol);
        normalizeAdvancedBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        normalizeAdvancedBtn.setColour (juce::TextButton::textColourOffId, textCol);
        metaFormatLabel.setColour (juce::Label::textColourId, mutedCol);
        loopToggle.setColour (juce::ToggleButton::textColourId, textCol);
        volumeLabel.setColour (juce::Label::textColourId, mutedCol);
        normalizeToggle.setColour (juce::ToggleButton::textColourId, textCol);
        outputBusLabel.setColour (juce::Label::textColourId, mutedCol);
        sectionDivider.setDarkMode (isDark);
        updateRmsLabelAppearance();

        const auto comboBg = editorBg;
        const auto comboText = textCol;
        for (auto* combo : { &outputBusCombo })
        {
            combo->setColour (juce::ComboBox::backgroundColourId, comboBg);
            combo->setColour (juce::ComboBox::textColourId, comboText);
            combo->setColour (juce::ComboBox::outlineColourId, pal.border);
            combo->setColour (juce::ComboBox::arrowColourId, mutedCol);
            combo->setColour (juce::PopupMenu::backgroundColourId, comboBg);
            combo->setColour (juce::PopupMenu::textColourId, comboText);
            combo->setColour (juce::PopupMenu::highlightedBackgroundColourId,
                               isDark ? pal.rowSelected : getItemAccentColour().withAlpha (0.14f));
            combo->setColour (juce::PopupMenu::highlightedTextColourId, textCol);
        }
        fadeInLabel.setColour (juce::Label::textColourId, mutedCol);
        fadeOutLabel.setColour (juce::Label::textColourId, mutedCol);

        juce::Slider* fadeSliders[] = { &fadeInSlider, &fadeOutSlider };
        for (auto* slider : fadeSliders)
        {
            slider->setColour (juce::Slider::textBoxTextColourId,       textCol);
            slider->setColour (juce::Slider::textBoxBackgroundColourId, editorBg.brighter (0.05f));
            slider->setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
            slider->repaint();
        }

        applyItemAccentColours();
    }

    void openTrimEditor (juce::Component* positionAnchor = nullptr)
    {
        openAdvancedTrimWindow (positionAnchor);
    }

    void refreshPadDisplayName()
    {
        if (currentPad == nullptr)
            return;

        nameValueLabel.setText (currentPad->getPadName(), juce::dontSendNotification);
    }

    void labelTextChanged (juce::Label* labelThatHasChanged) override
    {
        if (labelThatHasChanged != &nameValueLabel || currentPad == nullptr)
            return;

        currentPad->setCustomName (nameValueLabel.getText());

        if (onTrackNameChanged != nullptr)
            onTrackNameChanged();

        if (onInspectorGestureEnded != nullptr)
            onInspectorGestureEnded();

        if (onProjectEdited != nullptr)
            onProjectEdited();
    }

    void editorShown (juce::Label* label, juce::TextEditor& editor) override
    {
        if (label != &nameValueLabel)
            return;

        if (onInspectorGestureBegan)
            onInspectorGestureBegan();

        editor.setFont (showcontrol::inspector::trackNameValueFont());
        editor.setJustification (juce::Justification::centredLeft);
        editor.setIndents (2, 0);
        editor.setBorder (juce::BorderSize<int> (4, 2, 4, 2));
    }

    void selectPad (SoundPad* newPad)
    {
        if (newPad == currentPad)
        {
            if (currentPad != nullptr)
                refreshWaveformFromPad();

            syncInspectorColourFromPad();
            applyItemAccentColours();
            return;
        }

        if (newPad != currentPad)
            waveformDisplay.setThumbnail (nullptr);

        currentPad = newPad;

        if (currentPad != nullptr)
        {
            nameValueLabel.setText (currentPad->getPadName(), juce::dontSendNotification);
            volumeSlider.setValue (currentPad->getOutputGain(), juce::dontSendNotification);
            outputBusCombo.setSelectedItemIndex (
                juce::jlimit (0, showcontrol::routing::kInspectorBusCount - 1, currentPad->getOutputBus()),
                juce::dontSendNotification);
            loopToggle.setButtonText (getLoopToggleLabel());
            loopToggle.setToggleState (currentPad->isLooping(), juce::dontSendNotification);
            normalizeToggle.setToggleState (currentPad->getAutoNormalize(), juce::dontSendNotification);
            normalizeModeIsLufs = currentPad->getNormalizeUseLufs();
            syncNormalizeRowsLayout();
            refreshLoudnessLabel();
            waveformDisplay.setThumbnail (&currentPad->getThumbnail());
            waveformDisplay.setTrimPoints (currentPad->getTrimStart(), currentPad->getTrimEnd());
            trimStartLabel.setText (currentPad->formatTimeString (currentPad->getTrimStart()), juce::dontSendNotification);
            double len = currentPad->getPlaybackLength(); double te = currentPad->getTrimEnd();
            trimEndLabel.setText (currentPad->formatTimeString (te > 0.0 ? te : len), juce::dontSendNotification);
            waveformDisplay.setProgress (currentPad->getPlaybackPosition(), len);
            refreshTransportUi();
            updateFileInfoLabels (currentPad->getMetadata());
            fadeInSlider.setValue (currentPad->getFadeInMs(), juce::dontSendNotification);
            fadeOutSlider.setValue (currentPad->getFadeOutMs(), juce::dontSendNotification);
            waveformDisplay.setFadeRegions (currentPad->getFadeInMs(), currentPad->getFadeOutMs(), currentPad->getEffectiveLength());
            syncDspControlsFromPad();
            currentPad->scheduleDeferredInspectorLoads();
            applyItemAccentColours();
        }
        else
        {
            nameValueLabel.setText (juce::String::fromUTF8 (u8"Chưa chọn pad"), juce::dontSendNotification);
            timeLabel.setText ("00:00.0", juce::dontSendNotification);
            trimStartLabel.setText ("00:00.0", juce::dontSendNotification); trimEndLabel.setText ("00:00.0", juce::dontSendNotification);
            rmsLabel.setText (juce::String::fromUTF8 (u8"—"), juce::dontSendNotification);
            waveformDisplay.setThumbnail (nullptr);
            updateRmsLabelAppearance();
            updateFileInfoLabels ({});
            applyItemAccentColours();
        }

        if (onActivePadChanged)
            onActivePadChanged();
    }

    void refreshTagColourUi()
    {
        syncInspectorColourFromPad();
        applyItemAccentColours();
    }

    /** Gọi sau khi file load xong để cập nhật metadata mà không cần select lại. */
    void refreshMetadata()
    {
        if (currentPad != nullptr)
            updateFileInfoLabels (currentPad->getMetadata());
    }

    /** Đồng bộ waveform + trim + transport Inspector khi pad đổi file hoặc undo/redo trên cùng pad. */
    void refreshWaveformFromPad()
    {
        if (currentPad == nullptr)
            return;

        refreshPadDisplayName();
        volumeSlider.setValue (currentPad->getOutputGain(), juce::dontSendNotification);
        fadeInSlider.setValue (currentPad->getFadeInMs(), juce::dontSendNotification);
        fadeOutSlider.setValue (currentPad->getFadeOutMs(), juce::dontSendNotification);
        loopToggle.setToggleState (currentPad->isLooping(), juce::dontSendNotification);

        waveformDisplay.setThumbnail (&currentPad->getThumbnail());
        waveformDisplay.setTrimPoints (currentPad->getTrimStart(), currentPad->getTrimEnd());
        trimStartLabel.setText (currentPad->formatTimeString (currentPad->getTrimStart()), juce::dontSendNotification);

        const double len = currentPad->getPlaybackLength();
        const double te  = currentPad->getTrimEnd();
        trimEndLabel.setText (currentPad->formatTimeString (te > 0.0 ? te : len), juce::dontSendNotification);
        waveformDisplay.setProgress (currentPad->getPlaybackPosition(), len);
        waveformDisplay.setFadeRegions (currentPad->getFadeInMs(), currentPad->getFadeOutMs(),
                                       currentPad->getEffectiveLength());
        updateFileInfoLabels (currentPad->getMetadata());
        refreshTransportUi();
        refreshLoudnessLabel();
        waveformDisplay.repaint();
    }

    AudioMetadata getCurrentMetadata() const
    {
        return currentPad != nullptr ? currentPad->getMetadata() : AudioMetadata {};
    }

    void refreshLoudnessLabel()
    {
        if (currentPad == nullptr)
        {
            rmsLabel.setText (juce::String::fromUTF8 (u8"—"), juce::dontSendNotification);
            updateRmsLabelAppearance();
            return;
        }

        if (currentPad->isNormalizationInProgress())
        {
            rmsLabel.setText (showcontrol::localization::tr (u8"Đang đo…"), juce::dontSendNotification);
            updateRmsLabelAppearance();
            return;
        }

        auto text = currentPad->getLoudnessDisplayString();
        if (currentPad->getAutoNormalize() && currentPad->getDspLufsSyncGain() > 0.001f)
        {
            const float db = juce::Decibels::gainToDecibels (currentPad->getDspLufsSyncGain());
            text += showcontrol::localization::tr (u8" · LUFS sync ") + juce::String (db, 1) + " dB";
        }
        rmsLabel.setText (text.isNotEmpty() ? text : juce::String::fromUTF8 (u8"—"),
                          juce::dontSendNotification);
        updateRmsLabelAppearance();
    }

    void updateRmsLabelAppearance()
    {
        const auto pal = ShowTheme::get (isDarkTheme);
        const auto placeholder = juce::String::fromUTF8 (u8"—");
        const bool hasReading = rmsLabel.getText().isNotEmpty()
                                 && rmsLabel.getText() != placeholder;
        const bool show = normalizeToggle.getToggleState() && hasReading;

        rmsLabel.setVisible (show);
        rmsLabel.setColour (juce::Label::textColourId, pal.success);
    }

    void syncDspControlsFromPad()
    {
        applyItemAccentColours();
    }

    void updateEqualizerButtonStyle()
    {
        const bool eqOn = currentPad != nullptr && currentPad->getDspEqEnabled();
        equalizerBtn.setToggleState (eqOn, juce::dontSendNotification);
        equalizerBtn.repaint();
    }

    juce::Colour getItemAccentColour() const noexcept
    {
        const auto& pal = ShowTheme::get (isDarkTheme);

        if (showcontrol::colours::isDefaultTagColour (currentInspectorColour))
            return pal.accent;

        return currentInspectorColour;
    }

    void syncInspectorColourFromPad()
    {
        if (currentPad == nullptr)
        {
            currentInspectorColour = showcontrol::colours::defaultTagColour();
            return;
        }

        currentInspectorColour = showcontrol::colours::snapToPalette (currentPad->getTagColour());
    }

    void applyItemAccentColours()
    {
        syncInspectorColourFromPad();

        const auto accent = getItemAccentColour();
        const auto& pal = ShowTheme::get (isDarkTheme);

        inspectorStyle.setItemAccentColour (accent);
        waveformDisplay.setItemInkColour (accent);

        nameValueLabel.setColour (juce::Label::outlineWhenEditingColourId, accent);
        nameValueLabel.setColour (juce::Label::textWhenEditingColourId, pal.textPrimary);

        playBtn.setColour (juce::TextButton::buttonColourId, accent);
        playBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

        timeLabel.setColour (juce::Label::textColourId, accent);

        volumeSlider.setColour (juce::Slider::trackColourId, pal.sliderTrack);
        volumeSlider.setColour (juce::Slider::thumbColourId, accent);
        volumeSlider.repaint();

        juce::Slider* accentSliders[] = { &fadeInSlider, &fadeOutSlider };
        for (auto* slider : accentSliders)
        {
            slider->setColour (juce::Slider::trackColourId, pal.sliderTrack);
            slider->setColour (juce::Slider::thumbColourId, accent);
            slider->setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.28f));
            slider->repaint();
        }

        loopToggle.repaint();
        normalizeToggle.repaint();
        updateEqualizerButtonStyle();
        playBtn.repaint();
        waveformDisplay.repaint();
        inspectorContent.repaint();
        repaint();
    }

    void openEqualizerDialog (juce::Component* positionAnchor = nullptr)
    {
        if (currentPad == nullptr)
            return;

        showcontrol::ui::showPadEqualizerDialog (positionAnchor != nullptr ? positionAnchor : this,
                                                 currentPad,
                                                 isDarkTheme,
                                                 [this]
        {
            syncDspControlsFromPad();
            if (onProjectEdited)
                onProjectEdited();
        });
    }

    void openLoudnessManagerDialog (juce::Component* positionAnchor = nullptr)
    {
        if (currentPad == nullptr)
            return;

        showcontrol::ui::showLoudnessManagerDialog (positionAnchor != nullptr ? positionAnchor : this,
                                                    currentPad->getLoudnessSettings(),
                                                    isDarkTheme,
                                                    [this] (const showcontrol::loudness::LoudnessSettings& settings)
        {
            if (currentPad == nullptr)
                return;

            currentPad->setLoudnessSettings (settings);
            normalizeToggle.setToggleState (settings.enabled, juce::dontSendNotification);
            normalizeModeIsLufs = (settings.mode == showcontrol::loudness::MeasureMode::lufs);

            if (settings.enabled)
            {
                if (! currentPad->applyVolumeSyncGainIfReady())
                    currentPad->requestNormalization();
            }
            else
            {
                currentPad->setOutputGain (1.0f);
            }

            volumeSlider.setValue (currentPad->getOutputGain(), juce::dontSendNotification);
            refreshLoudnessLabel();
            updateNormalizeModeVisibility();

            if (onDefaultNormalizeModeChanged)
                onDefaultNormalizeModeChanged (normalizeModeIsLufs);
            if (onProjectEdited)
                onProjectEdited();
        },
                                                    [this] (const showcontrol::loudness::LoudnessSettings& settings)
        {
            if (onDefaultNormalizeModeChanged)
                onDefaultNormalizeModeChanged (settings.mode == showcontrol::loudness::MeasureMode::lufs);
            if (onNormalizeActiveListRequested)
                onNormalizeActiveListRequested (settings);
            if (onProjectEdited)
                onProjectEdited();
        },
                                                    [this] (const showcontrol::loudness::LoudnessSettings& settings)
        {
            if (onFetchLoudnessListPreview)
                return onFetchLoudnessListPreview (settings);

            return juce::Array<showcontrol::loudness::ListPreviewRow> {};
        });
    }

    void openAdvancedTrimWindow (juce::Component* positionAnchor = nullptr)
    {
        if (currentPad == nullptr || !currentPad->hasAudioFile()) return;

        AdvancedTrimEditorCallbacks trimCallbacks;
        trimCallbacks.onInspectorSync = [this]
        {
            waveformDisplay.setTrimPoints (currentPad->getTrimStart(), currentPad->getTrimEnd());
            trimStartLabel.setText (currentPad->formatTimeString (currentPad->getTrimStart()), juce::dontSendNotification);
            const double len = currentPad->getPlaybackLength();
            const double te = currentPad->getTrimEnd();
            trimEndLabel.setText (currentPad->formatTimeString (te > 0.0 ? te : len), juce::dontSendNotification);
        };
        trimCallbacks.onGestureFinished = [this]
        {
            if (onProjectEdited)
                onProjectEdited();
        };
        trimCallbacks.onCutRequested = [this] (SoundPad* pad, double cutStart, double cutEnd, auto onDone)
        {
            if (onPadAudioCutRequested)
                onPadAudioCutRequested (pad, cutStart, cutEnd, std::move (onDone));
            else if (onDone)
                onDone (false);
        };
        trimCallbacks.performUndo = [this] { return onApplicationUndoRequested && onApplicationUndoRequested(); };
        trimCallbacks.performRedo = [this] { return onApplicationRedoRequested && onApplicationRedoRequested(); };
        trimCallbacks.canUndo = [this] { return onApplicationCanUndo && onApplicationCanUndo(); };
        trimCallbacks.canRedo = [this] { return onApplicationCanRedo && onApplicationCanRedo(); };

        auto* trimComp = new AdvancedTrimComponent (currentPad, isDarkTheme, std::move (trimCallbacks));

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (trimComp);
        options.dialogTitle = showcontrol::localization::tr (u8"Cắt gọt âm thanh");
        options.dialogBackgroundColour = ShowTheme::get (isDarkTheme).panelElevated;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;

        if (auto* dw = options.launchAsync())
        {
            dw->setUsingNativeTitleBar (true);
            dw->setResizable (true, false);
            dw->setSize (960, 520);
            dw->setResizeLimits (720, 480, 1600, 620);
            showcontrol::ui::centreFloatingWindowInMainApp (*dw, positionAnchor != nullptr ? positionAnchor : this);
           #if JUCE_MAC
            showcontrol::mac::deferFarragoFullSizeContentView (*dw);
           #endif
        }
    }

    void updateFileInfoLabels (const AudioMetadata& meta)
    {
        juce::String line = meta.getFormatInfo();

        if (meta.bpm > 0.0)
        {
            const auto tempo = juce::String (juce::roundToInt (meta.bpm)) + " BPM";
            line = line.isNotEmpty() ? (tempo + "  |  " + line) : tempo;
        }

        metaFormatLabel.setText (line, juce::dontSendNotification);
        metaFormatLabel.setVisible (line.isNotEmpty());
        metaBpmLabel.setVisible (false);
    }

    void refreshTransportUi()
    {
        if (currentPad == nullptr)
            return;

        const bool transportLive = currentPad->isPlaying() || currentPad->isPaused();
        const double fileLen = currentPad->getPlaybackLength();
        const double pos = transportLive ? currentPad->getPlaybackPosition() : currentPad->getTrimStart();

        timeLabel.setText (transportLive ? currentPad->getTimeRemainingString()
                                        : currentPad->formatTimeString (fileLen),
                         juce::dontSendNotification);

        if (transportLive)
            waveformDisplay.setProgress (pos, fileLen);
        else
            waveformDisplay.hardKillPlaybackVisual();

        const bool isLive = currentPad->isPlaying() || currentPad->isFading();
        if (currentPad->usesCuePauseResume())
            playBtn.setComponentID (isLive ? "sc_inspector_icon_pause" : "sc_inspector_icon_play");
        else
            playBtn.setComponentID (isLive ? "sc_inspector_icon_stop" : "sc_inspector_icon_play");

        playBtn.repaint();
        refreshLoudnessLabel();
    }

    void shortCircuitTransportVisuals() noexcept
    {
        if (currentPad != nullptr)
        {
            currentPad->getRealtimeSource().snapPublishedUiToTrimOnMessageThread();
            waveformDisplay.hardKillPlaybackVisual();
            refreshTransportUi();
        }
        else
        {
            waveformDisplay.hardKillPlaybackVisual();
        }

        waveformDisplay.repaint();
    }

    void timerCallback() override
    {
        if (currentPad != nullptr) {
            loopToggle.setButtonText (getLoopToggleLabel());

            if (currentPad->isPlaybackPositionLive())
            {
                refreshTransportUi();
                waveformDisplay.setProgress (currentPad->getPlaybackPosition(),
                                             currentPad->getPlaybackLength());

                if (currentPad->getAutoNormalize())
                    refreshLoudnessLabel();
            }
            else
            {
                shortCircuitTransportVisuals();
            }
        }
    }

    void resized() override
    {
        inspectorViewport.setBounds (getLocalBounds());
        layoutScrollContent();
    }

    static int measureInspectorLabelWidth (const juce::Label& label, int maxWidth) noexcept
    {
        if (maxWidth <= 0)
            return 0;

        const auto text = label.getText();
        if (text.isEmpty())
            return juce::jmin (maxWidth, 40);

        const int measured = juce::GlyphArrangement::getStringWidthInt (label.getFont(), text) + 12;
        return juce::jlimit (40, maxWidth, measured);
    }

    void layoutScrollContent()
    {
        static constexpr int marginH = 16;
        static constexpr int marginV = 12;
        static constexpr int marginBottom = 10;
        static constexpr int gap = 8;
        static constexpr int pairGap = 8;
        static constexpr int rowHName = 34;
        static constexpr int rowHWave = 72;
        static constexpr int rowHTrim = 16;
        static constexpr int rowHTransport = 28;
        static constexpr int rowHTime = 36;
        static constexpr int rowHMeta = 14;
        static constexpr int rowHLoopEq = 32;
        static constexpr int rowHBus = 28;
        static constexpr int rowHBusLabel = 110;
        static constexpr int rowHVolume = 28;
        static constexpr int rowHBtn = 28;
        static constexpr int rowHFade = 26;
        static constexpr int rowHLoudness = 14;

        const int viewW = inspectorViewport.getMaximumVisibleWidth();
        const int contentW = juce::jmax (280, viewW > 0 ? viewW : getWidth());
        const int componentWidth = juce::jmax (0, contentW - marginH * 2);

        auto area = juce::Rectangle<int> (marginH, marginV, componentWidth, 10000);

        const auto skipGap = [&] { area.removeFromTop (gap); };

        const auto placeFullWidth = [&] (juce::Component& comp, int height)
        {
            comp.setBounds (area.removeFromTop (height));
        };

        const auto placeLabelControlRow = [&] (juce::Label& label, juce::Component& control, int height)
        {
            auto row = area.removeFromTop (height);
            const int labelW = measureInspectorLabelWidth (label, row.getWidth() / 2);
            label.setBounds (row.removeFromLeft (labelW));
            control.setBounds (row);
        };

        // --- Tên file ---
        {
            auto nameRow = area.removeFromTop (rowHName);
            constexpr int nameLabelW = 48;
            nameLabel.setBounds (nameRow.removeFromLeft (nameLabelW));
            nameValueLabel.setBounds (nameRow);
        }

        skipGap();
        waveformDisplay.setBounds (area.removeFromTop (rowHWave));
        skipGap();

        // --- Trim ---
        {
            auto trimRow = area.removeFromTop (rowHTrim);
            const int endW = juce::jmin (trimRow.getWidth() / 3, measureInspectorLabelWidth (trimEndLabel, trimRow.getWidth() / 3));
            const int resetMeasured = juce::GlyphArrangement::getStringWidthInt (showcontrol::inspector::buttonFont(),
                                                                                 trimResetBtn.getButtonText()) + 16;
            const int resetW = juce::jmin (trimRow.getWidth() / 4, juce::jlimit (40, trimRow.getWidth() / 4, resetMeasured));
            const int rightClusterW = endW + pairGap + resetW;
            trimEndLabel.setBounds (trimRow.removeFromRight (endW));
            trimRow.removeFromRight (pairGap);
            trimResetBtn.setBounds (trimRow.removeFromRight (resetW));
            trimStartLabel.setBounds (trimRow);
        }

        skipGap();

        // --- Transport ---
        {
            auto transportRow = area.removeFromTop (rowHTransport);
            const int iconBtnW = juce::jmin (rowHTransport, transportRow.getWidth() / 5);
            backBtn.setBounds (transportRow.removeFromLeft (iconBtnW));
            fadeOutBtn.setBounds (transportRow.removeFromRight (iconBtnW));

            const int playW = juce::jmin (iconBtnW + 8, juce::jmax (0, transportRow.getWidth()));
            const int playX = transportRow.getX() + (transportRow.getWidth() - playW) / 2;
            showcontrol::gfx::safeSetBounds (playBtn, { playX, transportRow.getY(), playW, transportRow.getHeight() });
        }

        skipGap();
        timeLabel.setBounds (area.removeFromTop (rowHTime));
        metaFormatLabel.setBounds (area.removeFromTop (rowHMeta));
        skipGap();

        // --- Loop / Equalizer — 2 nút ngang full width ---
        {
            auto buttonRow = area.removeFromTop (rowHLoopEq);
            const int totalWidth = buttonRow.getWidth();
            const int singleButtonW = juce::jmax (1, (totalWidth - pairGap) / 2);

            loopToggle.setBounds (buttonRow.removeFromLeft (singleButtonW).reduced (2, 0));
            buttonRow.removeFromLeft (pairGap);
            equalizerBtn.setBounds (buttonRow.reduced (2, 0));
        }

        skipGap();

        // --- Bus: nhãn + ComboBox cùng một hàng ngang ---
        {
            auto busRow = area.removeFromTop (rowHBus);
            const int busLabelW = juce::jmin (busRow.getWidth() - 60,
                                              juce::jmax (rowHBusLabel,
                                                          measureInspectorLabelWidth (outputBusLabel, busRow.getWidth() / 2)));
            outputBusLabel.setBounds (busRow.removeFromLeft (busLabelW));
            outputBusCombo.setBounds (busRow);
        }

        skipGap();
        placeLabelControlRow (volumeLabel, volumeSlider, rowHVolume);
        skipGap();

        // --- Đồng bộ âm lượng — 2 hàng full width để chữ không bị cắt ---
        {
            normalizeToggle.setBounds (area.removeFromTop (rowHBtn));
            skipGap();
            normalizeAdvancedBtn.setBounds (area.removeFromTop (rowHBtn));
        }
        normalizeAdvancedBtn.setVisible (true);

        const bool normOn = normalizeToggle.getToggleState();

        if (normOn && rmsLabel.isVisible())
        {
            skipGap();
            placeFullWidth (rmsLabel, rowHLoudness);
        }
        else
        {
            rmsLabel.setBounds ({});
            if (! normOn)
                rmsLabel.setVisible (false);
        }

        skipGap();
        placeFullWidth (sectionDivider, 1);
        sectionDivider.setVisible (true);
        skipGap();

        // --- Fade In / Fade Out ---
        {
            const int fadeTextBoxW = juce::jlimit (52, componentWidth / 3, componentWidth / 4);
            fadeInSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, fadeTextBoxW, rowHFade);
            fadeOutSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, fadeTextBoxW, rowHFade);

            const int fadeLabelW = juce::jmax (measureInspectorLabelWidth (fadeInLabel, componentWidth / 2),
                                             measureInspectorLabelWidth (fadeOutLabel, componentWidth / 2));

            auto fiRow = area.removeFromTop (rowHFade);
            fadeInLabel.setBounds (fiRow.removeFromLeft (fadeLabelW));
            fadeInSlider.setBounds (fiRow);

            skipGap();

            auto foRow = area.removeFromTop (rowHFade);
            fadeOutLabel.setBounds (foRow.removeFromLeft (fadeLabelW));
            fadeOutSlider.setBounds (foRow);
        }

        const int contentBottom = area.getY() + marginBottom;
        inspectorContent.setSize (contentW, showcontrol::gfx::clampDimension (contentBottom));

        const int viewH = inspectorViewport.getMaximumVisibleHeight();
        inspectorViewport.setScrollBarsShown (contentBottom > viewH && viewH > 0, false);
    }

    void paint (juce::Graphics& g) override {
        const auto cols = showcontrol::ui::ThemePaintColours::read (*this);
        isDarkTheme = cols.isDark;
        g.fillAll (cols.panelBg);
        g.setColour (getLookAndFeel().findColour (ShowControlLookAndFeel::panelBorderColourId));
        if (getHeight() > 0)
            showcontrol::gfx::safeDrawVerticalLine (g, 0, 0.0f, (float) getHeight());
    }

    void updateNormalizeModeVisibility()
    {
        syncNormalizeRowsLayout();
    }

    /** Chỉ layout lại khi hàng RMS/LUFS thực sự đổi trạng thái — không resized() toàn Inspector. */
    void syncNormalizeRowsLayout()
    {
        updateRmsLabelAppearance();
        const bool normOn = normalizeToggle.getToggleState();

        if (normOn != normModeRowsVisible)
        {
            normModeRowsVisible = normOn;
            layoutScrollContent();
        }

        repaint();
    }

    void applyNormalizeModeToCurrentPad()
    {
        if (currentPad == nullptr || ! currentPad->hasAudioFile())
            return;

        auto settings = currentPad->getLoudnessSettings();
        settings.mode = normalizeModeIsLufs ? showcontrol::loudness::MeasureMode::lufs
                                            : showcontrol::loudness::MeasureMode::rms;
        currentPad->setLoudnessSettings (settings);

        if (normalizeToggle.getToggleState())
        {
            if (! currentPad->applyVolumeSyncGainIfReady())
                currentPad->requestNormalization();
        }
    }

    void rebuildOutputBusCombo()
    {
        const int prevBus = currentPad != nullptr
            ? juce::jlimit (0, showcontrol::routing::kInspectorBusCount - 1, currentPad->getOutputBus())
            : outputBusCombo.getSelectedItemIndex();

        outputBusCombo.clear (juce::dontSendNotification);

        for (int i = 0; i < showcontrol::routing::kInspectorBusCount; ++i)
        {
            const juce::String label = (i < cachedInspectorBusNames.size())
                ? cachedInspectorBusNames[i]
                : showcontrol::routing::getBusDisplayName (i);
            outputBusCombo.addItem (label, i + 1);
        }

        outputBusCombo.setSelectedItemIndex (
            juce::jlimit (0, juce::jmax (0, outputBusCombo.getNumItems() - 1), prevBus),
            juce::dontSendNotification);
    }

    void assignInspectorTooltips()
    {
        normalizeToggle.setTooltip (showcontrol::localization::tr (
            u8"Đồng bộ mức Gain chuẩn hóa dựa trên cấu hình phân tích RMS hoặc LUFS."));
        normalizeAdvancedBtn.setTooltip (showcontrol::localization::tr (
            u8"Mở hộp thoại quản lý đồng bộ âm lượng nâng cao và áp dụng tức thì."));
        loopToggle.setTooltip (showcontrol::localization::tr (
            u8"Bật/Tắt chế độ phát lặp lại tuần hoàn cho riêng track nhạc hoặc ô PAD này."));
        equalizerBtn.setTooltip (showcontrol::localization::tr (
            u8"Mở bảng cấu hình bộ lọc tần số và cân bằng âm sắc cho track."));
        fadeInSlider.setTooltip (showcontrol::localization::tr (
            u8"Thời gian lịm tiếng nhỏ đến to khi bắt đầu phát nhạc (tính bằng mili-giây)."));
        fadeOutSlider.setTooltip (showcontrol::localization::tr (
            u8"Thời gian lịm tiếng to đến nhỏ khi chủ động dừng phát nhạc (tính bằng mili-giây)."));
        outputBusCombo.setTooltip (showcontrol::localization::tr (
            u8"Định tuyến đầu ra stereo của pad/track ra Main FOH hoặc AUX trên soundcard/Dante. "
            u8"Nếu thiết bị không đủ kênh, tự động fallback về Main FOH (Ch 1-2)."));
    }

    juce::String getLoopToggleLabel() const
    {
        if (currentPad != nullptr && currentPad->usesCuePauseResume())
            return showcontrol::localization::tr (u8"Loop cue");

        return showcontrol::localization::tr (u8"Loop bài");
    }

private:
    SoundPad* currentPad = nullptr;
    bool isDarkTheme = true;
    bool normalizeModeIsLufs = false;
    bool normModeRowsVisible = false;
    juce::Colour currentInspectorColour = showcontrol::colours::defaultTagColour();
    juce::StringArray cachedInspectorBusNames;
    InspectorStyle inspectorStyle;
    juce::Label nameLabel, trimStartLabel, trimEndLabel, timeLabel, volumeLabel, rmsLabel, hotkeyScopeLabel;
    juce::TextButton normalizeAdvancedBtn;
    TrackNameEditLabel nameValueLabel;
    InspectorWaveform waveformDisplay;
    juce::TextButton trimResetBtn, backBtn, playBtn, fadeOutBtn;
    juce::ToggleButton loopToggle, normalizeToggle;
    juce::ToggleButton hotkeyScopeActiveBtn, hotkeyScopeGlobalBtn;
    juce::Slider volumeSlider;
    juce::Label outputBusLabel;
    juce::ComboBox outputBusCombo;
    juce::Label metaSeparatorLabel, metaArtistLabel, metaAlbumLabel, metaBpmLabel, metaFormatLabel;
    juce::Label fadeInLabel, fadeOutLabel;
    juce::Slider fadeInSlider, fadeOutSlider;
    juce::Viewport inspectorViewport;
    juce::Component inspectorContent;
    InspectorSectionDivider sectionDivider;
    juce::ToggleButton equalizerBtn;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorPanel)
};