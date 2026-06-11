#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SoundPad.h"
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowGraphicsSafe.h"
#include "ShowFlatIcons.h"
#include "PadEqualizerDialog.h"
#include "ShowControlMacWindow.h"
#include "ShowOutputRouting.h"
#include "ShowLocalization.h"

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

    std::function<void(double)> onTrimStartChanged;
    std::function<void(double)> onTrimEndChanged;
    std::function<void(double)> onPlayheadScrubbed; 
    std::function<void()> onWaveformDoubleClicked; 

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDark);
        auto b = showcontrol::gfx::sanitise (getLocalBounds().toFloat());
        if (b.getWidth() <= 0.0f || b.getHeight() <= 0.0f)
            return;

        g.setColour (pal.listRowBg);
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (pal.border);
        g.drawRoundedRectangle (b, 5.0f, 1.0f);

        if (thumbnail == nullptr || thumbnail->getTotalLength() <= 0.0)
        {
            g.setColour (pal.border);
            g.setFont (ShowTheme::font (11.0f));
            g.drawText (juce::String::fromUTF8 (u8"Chưa có file âm thanh"), getLocalBounds(), juce::Justification::centred);
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

        g.setColour (pal.waveformFill);
        thumbnail->drawChannel (g, waveArea, viewStart, viewEnd, 0, 1.0f);

        if (totalLen > 0.0 && currentPos > viewStart)
        {
            const float ratio = showcontrol::gfx::timeToRatio (currentPos, viewStart, viewEnd);
            auto playedBounds = waveArea;
            playedBounds.setWidth (showcontrol::gfx::clampDimension ((int) std::round ((float) playedBounds.getWidth() * ratio)));

            if (showcontrol::gfx::canClip (playedBounds))
            {
                g.saveState();
                g.reduceClipRegion (playedBounds);
                g.setColour (pal.waveformPlayhead);
                thumbnail->drawChannel (g, waveArea, viewStart, viewEnd, 0, 1.0f);
                g.restoreState();
            }
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
                g.setFont (ShowTheme::fontBold (8.5f));
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
                g.setFont (ShowTheme::fontBold (8.5f));
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
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (thumbnail == nullptr || totalLen <= 0.0) return;
        dragMode = getDragMode (e.getPosition());
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

        if (dragMode == 1) {
            trimStart = juce::jlimit (0.0, effectiveEnd - 0.1, t);
            if (onTrimStartChanged) onTrimStartChanged (trimStart); repaint();
        } else if (dragMode == 2) {
            trimEnd = juce::jlimit (trimStart + 0.1, totalLen, t);
            if (onTrimEndChanged) onTrimEndChanged (trimEnd); repaint();
        } else if (dragMode == 0) {
            currentPos = juce::jlimit (0.0, totalLen, t);
            if (onPlayheadScrubbed) onPlayheadScrubbed (currentPos); repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override { dragMode = 0; }
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (getDragMode (e.getPosition()) != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }

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
        g.setFont (ShowTheme::font (9.0f));
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

    int getDragMode (juce::Point<int> mousePos) const {
        if (thumbnail == nullptr || totalLen <= 0.0 || (viewEnd <= viewStart)) return 0; 
        const auto waveArea = getWaveformBounds();
        if (! waveArea.contains (mousePos))
            return 0;

        double effectiveEnd = (trimEnd > 0.0) ? trimEnd : totalLen;
        const int startX = showcontrol::gfx::timeToPixelX (waveArea, trimStart, viewStart, viewEnd);
        const int endX   = showcontrol::gfx::timeToPixelX (waveArea, effectiveEnd, viewStart, viewEnd);

        if (std::abs (mousePos.x - startX) <= 12) return 1;
        if (std::abs (mousePos.x - endX) <= 12) return 2;
        return 0;
    }
    juce::AudioThumbnail* thumbnail = nullptr; double currentPos = 0.0, totalLen = 0.0, trimStart = 0.0, trimEnd = 0.0; bool isDark = true; int dragMode = 0;
    double viewStart = 0.0, viewEnd = 0.0;
    double fadeInSec = 0.0, fadeOutSec = 0.0, effectiveLen = 0.0;
    bool showTimeRuler = false;
};

//==============================================================================
class InspectorStyle : public juce::LookAndFeel_V4
{
public:
    void setDarkMode (bool dark) { isDarkMode = dark; }

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
        g.setFont (ShowTheme::font (11.0f));
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

        if (id == "sc_inspector_sliders")
        {
            showcontrol::icons::paintSlidersIcon (g, iconBounds, col);
            return;
        }

        LookAndFeel_V4::drawButtonText (g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& btn, bool isHighlighted, bool) override
    {
        const auto bounds = btn.getLocalBounds().toFloat();
        const bool toggled = btn.getToggleState();
        const auto pal = ShowTheme::get (isDarkMode);
        const auto id = btn.getComponentID();

        if (id == "sc_inspector_loop")
        {
            auto fullBounds = bounds;

            if (toggled)
            {
                g.setColour (pal.accent.withAlpha (isDarkMode ? 0.28f : 0.18f));
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
            auto iconArea = fullBounds.removeFromLeft (kIcon + 10.0f).withSizeKeepingCentre (kIcon, kIcon);
            const auto loopCol = toggled ? pal.accent : pal.textMuted;
            showcontrol::icons::paintLoopIcon (g, iconArea, loopCol, toggled);

            g.setColour (toggled ? pal.accent : pal.textPrimary);
            g.setFont (ShowTheme::fontBold (11.0f));
            g.drawText (btn.getButtonText(), fullBounds.withTrimmedLeft (kIcon + 6.0f),
                        juce::Justification::centredLeft);
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
                drawSegment (bounds.reduced (0.5f), pal.accent.brighter (isHighlighted ? (isDarkMode ? 0.12f : 0.08f) : 0.0f));
            else
                drawSegment (bounds.reduced (0.5f), segCols.background);

            juce::Path outline;
            const auto outlineBounds = bounds.reduced (0.5f);
            outline.addRoundedRectangle (outlineBounds.getX(), outlineBounds.getY(),
                                         outlineBounds.getWidth(), outlineBounds.getHeight(),
                                         r, r, curveLeft, curveRight, curveLeft, curveRight);
            g.setColour (pal.border);
            g.strokePath (outline, juce::PathStrokeType (1.0f));

            g.setColour (toggled ? pal.accentOnDark : segCols.text);
            g.setFont (ShowTheme::font (11.0f, toggled ? "Bold" : "Plain"));
            g.drawText (btn.getButtonText(), bounds, juce::Justification::centred);
            return;
        }

        const auto cols = showcontrol::ui::toggleButtonColours (isDarkMode, toggled, isHighlighted);
        g.setColour (cols.background);
        g.fillRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius);
        g.setColour (pal.inputOutline.withAlpha (0.85f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.0f);
        g.setColour (cols.text.withMultipliedAlpha (btn.isEnabled() ? 1.0f : 0.5f));
        g.setFont (ShowTheme::fontBold (11.0f));
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
        l->setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
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

        g.setColour (pal.accent);
        g.drawRoundedRectangle (bounds, kCapsuleRadius, 1.5f);
    }

private:
    bool isDarkMode = true;
};

//==============================================================================
class AdvancedTrimComponent : public juce::Component, public juce::Timer
{
public:
    AdvancedTrimComponent (SoundPad* pad, bool isDark, std::function<void()> onUpdateCallback)
        : currentPad (pad), isDarkTheme (isDark), onUpdate (onUpdateCallback)
    {
        setSize (780, 360);
        addAndMakeVisible (largeWaveform);
        largeWaveform.setThumbnail (&currentPad->getThumbnail());
        largeWaveform.setTrimPoints (currentPad->getTrimStart(), currentPad->getTrimEnd());
        
        double initialLen = currentPad->getPlaybackLength();
        double initialPos = currentPad->getPlaybackPosition();
        largeWaveform.setProgress (initialPos, initialLen);
        largeWaveform.setDarkMode (isDark);
        
        // Trimming realtime: kéo marker in/out trên waveform lớn.
        largeWaveform.onTrimStartChanged = [this] (double t) { if (currentPad) { currentPad->setTrimStart (t); currentPad->triggerTrimUpdateLive(); if (onUpdate) onUpdate(); } };
        largeWaveform.onTrimEndChanged = [this] (double t) { if (currentPad) { currentPad->setTrimEnd (t); currentPad->triggerTrimUpdateLive(); if (onUpdate) onUpdate(); } };
        largeWaveform.onPlayheadScrubbed = [this] (double t) { if (currentPad) currentPad->seekTo (t); };
        largeWaveform.setShowTimeRuler (true);

        addAndMakeVisible (infoLabel);
        infoLabel.setText (showcontrol::localization::tr (
                               u8"Kéo 2 marker vàng/đỏ để chọn điểm IN/OUT. Lăn chuột để zoom."),
                           juce::dontSendNotification);
        infoLabel.setFont (ShowTheme::fontBold (12.0f));
        const auto pal = ShowTheme::get (isDark);
        infoLabel.setColour (juce::Label::textColourId, pal.textSecondary);

        addAndMakeVisible (resetButton);
        resetButton.setButtonText (showcontrol::localization::tr (u8"ĐẶT LẠI"));
        resetButton.setColour (juce::TextButton::buttonColourId, pal.buttonSecondary);
        resetButton.setColour (juce::TextButton::textColourOffId, pal.textPrimary);
        resetButton.onClick = [this] { performTrimReset(); };

        addAndMakeVisible (closeBtn);
        closeBtn.setButtonText (showcontrol::localization::tr (u8"XÁC NHẬN & ĐÓNG BẢNG"));
        closeBtn.setColour (juce::TextButton::buttonColourId, pal.accentSoft); closeBtn.setColour (juce::TextButton::textColourOffId, isDark ? juce::Colours::white : pal.panelElevated);
        closeBtn.onClick = [this] { if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0); };

        startTimer (40);
    }
    ~AdvancedTrimComponent() override
    {
        stopTimer();
        largeWaveform.setThumbnail (nullptr);
    }

    void timerCallback() override {
        if (currentPad) largeWaveform.setProgress (currentPad->getPlaybackPosition(), currentPad->getPlaybackLength());
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
        const int footerH = 50;
        auto footer = b.removeFromBottom (footerH);

        largeWaveform.setBounds (b);
        infoLabel.setBounds (footer.removeFromTop (30));
        closeBtn.setBounds (footer.removeFromRight (200).withHeight (32));

        const auto confirmBounds = closeBtn.getBounds();
        resetButton.setBounds (confirmBounds.getX() - 130,
                               confirmBounds.getY(),
                               120,
                               confirmBounds.getHeight());
    }

private:
   #if JUCE_MAC
    static constexpr int kMacTopDragInset = 14;
   #else
    static constexpr int kMacTopDragInset = 0;
   #endif

    void performTrimReset()
    {
        if (currentPad == nullptr)
            return;

        const double totalLen = currentPad->getPlaybackLength();

        currentPad->setTrimStart (0.0);
        currentPad->setTrimEnd (totalLen);
        currentPad->triggerTrimUpdateLive();

        largeWaveform.setTrimPoints (0.0, totalLen);
        largeWaveform.setProgress (currentPad->getPlaybackPosition(), totalLen);

        if (onUpdate)
            onUpdate();

        repaint();
    }

    SoundPad* currentPad; bool isDarkTheme; std::function<void()> onUpdate;
    InspectorWaveform largeWaveform; juce::Label infoLabel;
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
class InspectorPanel : public juce::Component, public juce::Timer
{
public:
    InspectorPanel()
    {
        addAndMakeVisible (inspectorViewport);
        inspectorViewport.setViewedComponent (&inspectorContent, false);
        inspectorViewport.setScrollBarsShown (true, false);
        inspectorViewport.setScrollBarThickness (8);

        inspectorContent.addAndMakeVisible (nameLabel); nameLabel.setText (juce::String::fromUTF8 (u8"Name"), juce::dontSendNotification);
        nameLabel.setFont (ShowTheme::fontBold (11.5f));

        inspectorContent.addAndMakeVisible (nameEditor);
        nameEditor.setFont (ShowTheme::font (13.5f));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setIndents (8, 4);
        nameEditor.setText (juce::String::fromUTF8 (u8"Chưa chọn pad"), juce::dontSendNotification);
        nameEditor.setReturnKeyStartsNewLine (false);
        nameEditor.onTextChange = [this]
        {
            if (currentPad == nullptr)
                return;

            currentPad->setCustomName (nameEditor.getText());

            if (onTrackNameChanged != nullptr)
                onTrackNameChanged();
        };
        nameEditor.onReturnKey = [this] {
            if (currentPad != nullptr)
            {
                currentPad->setCustomName (nameEditor.getText());
                nameEditor.giveAwayKeyboardFocus();
                if (onTrackNameChanged != nullptr)
                    onTrackNameChanged();
                if (onProjectEdited) onProjectEdited();
            }
        };
        nameEditor.onFocusLost = [this] {
            if (currentPad != nullptr)
            {
                currentPad->setCustomName (nameEditor.getText());
                if (onTrackNameChanged != nullptr)
                    onTrackNameChanged();
                if (onProjectEdited) onProjectEdited();
            }
        };

        inspectorContent.addAndMakeVisible (waveformDisplay);
        waveformDisplay.setShowTimeRuler (false);
        
        // Trim nhỏ bên Inspector: chỉ kéo 2 marker, không quét vùng.
        waveformDisplay.onTrimStartChanged = [this] (double t) {
            if (currentPad)
            {
                currentPad->setTrimStart (t);
                currentPad->triggerTrimUpdateLive();
                trimStartLabel.setText (currentPad->formatTimeString (t), juce::dontSendNotification);
                if (onProjectEdited) onProjectEdited();
            }
        };
        waveformDisplay.onTrimEndChanged = [this] (double t) {
            if (currentPad)
            {
                currentPad->setTrimEnd (t);
                currentPad->triggerTrimUpdateLive();
                trimEndLabel.setText (currentPad->formatTimeString (t), juce::dontSendNotification);
                if (onProjectEdited) onProjectEdited();
            }
        };
        
        waveformDisplay.onWaveformDoubleClicked = [this] { openAdvancedTrimWindow(); };
        waveformDisplay.onPlayheadScrubbed = [this] (double t) { if (currentPad) currentPad->seekTo (t); };

        inspectorContent.addAndMakeVisible (trimStartLabel); trimStartLabel.setText ("00:00.0", juce::dontSendNotification);
        trimStartLabel.setFont (ShowTheme::timerFont (10.0f)); trimStartLabel.setJustificationType (juce::Justification::centredLeft);

        inspectorContent.addAndMakeVisible (trimEndLabel); trimEndLabel.setText ("00:00.0", juce::dontSendNotification);
        trimEndLabel.setFont (ShowTheme::timerFont (10.0f)); trimEndLabel.setJustificationType (juce::Justification::centredRight);

        inspectorContent.addAndMakeVisible (trimResetBtn); trimResetBtn.setButtonText (juce::String::fromUTF8 (u8"Reset"));
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
                    onPlayPadRequested (currentPad);
                currentPad->triggerPlay();
                refreshTransportUi();
            }
        };
        fadeOutBtn.setComponentID ("sc_inspector_sliders");
        fadeOutBtn.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (fadeOutBtn);
        fadeOutBtn.setButtonText ({});
        fadeOutBtn.onClick = [this] {
            if (isPlaybackCommandBlocked != nullptr && isPlaybackCommandBlocked())
                return;

            if (currentPad && currentPad->isPlaying())
            {
                if (onFadePadRequested)
                    onFadePadRequested (currentPad);
                currentPad->startFadeOut(); // dùng fadeOutMs của pad
            }
        };

        // Fade In duration slider (0 = tắt)
        inspectorContent.addAndMakeVisible (fadeInLabel);
        fadeInLabel.setText (juce::String::fromUTF8 (u8"Fade In"), juce::dontSendNotification);
        fadeInLabel.setFont (ShowTheme::font (10.5f));
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
        fadeOutLabel.setFont (ShowTheme::font (10.5f));
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
        inspectorContent.addAndMakeVisible (equalizerBtn);
        equalizerBtn.setButtonText (showcontrol::localization::tr (u8"Equalizer"));
        equalizerBtn.onClick = [this] { openEqualizerDialog(); };

        inspectorContent.addAndMakeVisible (timeLabel); timeLabel.setText ("00:00.0", juce::dontSendNotification);
        timeLabel.setFont (ShowTheme::timerFont (32.0f, true)); timeLabel.setJustificationType (juce::Justification::centred);

        inspectorContent.addAndMakeVisible (metaFormatLabel);
        metaFormatLabel.setFont (ShowTheme::font (11.0f));
        metaFormatLabel.setJustificationType (juce::Justification::centred);

        loopToggle.setComponentID ("sc_inspector_loop");
        loopToggle.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (loopToggle);
        loopToggle.onClick = [this]
        {
            if (currentPad != nullptr)
            {
                currentPad->setLooping (loopToggle.getToggleState());
                if (onProjectEdited) onProjectEdited();
            }
        };

        inspectorContent.addAndMakeVisible (volumeLabel);
        volumeLabel.setText (showcontrol::localization::tr (u8"Âm lượng (Volume):"), juce::dontSendNotification);
        volumeLabel.setFont (ShowTheme::font (10.5f));
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
        normalizeToggle.setButtonText (showcontrol::localization::tr (u8"Đồng bộ âm lượng"));
        normalizeToggle.setToggleState (true, juce::dontSendNotification);
        normalizeToggle.setLookAndFeel (&inspectorStyle);
        normalizeToggle.onClick = [this] {
            if (currentPad) {
                const bool state = normalizeToggle.getToggleState();
                currentPad->setAutoNormalize (state);
                if (state)
                {
                    applyNormalizeModeToCurrentPad();
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

        // Chọn chế độ chuẩn hoá: RMS (cổ điển) hoặc LUFS (broadcast AI)
        normalizeRmsBtn.setLookAndFeel (&inspectorStyle);
        normalizeLufsBtn.setLookAndFeel (&inspectorStyle);
        inspectorContent.addAndMakeVisible (normalizeRmsBtn);
        normalizeRmsBtn.setButtonText ("RMS");
        normalizeRmsBtn.setToggleState (true, juce::dontSendNotification);
        normalizeRmsBtn.onClick = [this] {
            normalizeRmsBtn.setToggleState (true, juce::dontSendNotification);
            normalizeLufsBtn.setToggleState (false, juce::dontSendNotification);
            normalizeModeIsLufs = false;
            applyNormalizeModeToCurrentPad();
            if (currentPad != nullptr && currentPad->applyVolumeSyncGainIfReady())
                volumeSlider.setValue (currentPad->getOutputGain(), juce::dontSendNotification);
            if (onDefaultNormalizeModeChanged) onDefaultNormalizeModeChanged (false);
            if (onProjectEdited) onProjectEdited();
        };
        inspectorContent.addAndMakeVisible (normalizeLufsBtn);
        normalizeLufsBtn.setButtonText ("LUFS");
        normalizeLufsBtn.setToggleState (false, juce::dontSendNotification);
        normalizeLufsBtn.onClick = [this] {
            normalizeLufsBtn.setToggleState (true, juce::dontSendNotification);
            normalizeRmsBtn.setToggleState (false, juce::dontSendNotification);
            normalizeModeIsLufs = true;
            applyNormalizeModeToCurrentPad();
            if (currentPad != nullptr && currentPad->applyVolumeSyncGainIfReady())
                volumeSlider.setValue (currentPad->getOutputGain(), juce::dontSendNotification);
            if (onDefaultNormalizeModeChanged) onDefaultNormalizeModeChanged (true);
            if (onProjectEdited) onProjectEdited();
        };

        inspectorContent.addAndMakeVisible (normalizeListBtn);
        normalizeListBtn.setButtonText (showcontrol::localization::tr (u8"Đồng bộ cả list"));
        normalizeListBtn.onClick = [this]
        {
            if (onNormalizeActiveListRequested)
                onNormalizeActiveListRequested (normalizeModeIsLufs);
        };

        inspectorContent.addAndMakeVisible (rmsLabel);
        rmsLabel.setText (juce::String::fromUTF8 (u8"—"), juce::dontSendNotification);
        rmsLabel.setFont (ShowTheme::font (10.5f));
        rmsLabel.setJustificationType (juce::Justification::centredLeft);
        rmsLabel.setVisible (false);

        inspectorContent.addAndMakeVisible (sectionDivider);

        metaBpmLabel.setVisible (false);

        // Hotkey Scope — ẩn khỏi UI, giữ logic callback để MainComponent vẫn hoạt động
        hotkeyScopeActiveBtn.setLookAndFeel (&inspectorStyle);
        hotkeyScopeGlobalBtn.setLookAndFeel (&inspectorStyle);
        hotkeyScopeLabel.setText ("Hotkey Scope", juce::dontSendNotification);
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
        outputBusLabel.setFont (ShowTheme::font (10.5f));
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
        stopTimer();
        detachFromPadResources();
        loopToggle.setLookAndFeel (nullptr);
        normalizeToggle.setLookAndFeel (nullptr);
        normalizeRmsBtn.setLookAndFeel (nullptr);
        normalizeLufsBtn.setLookAndFeel (nullptr);
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
    /** Gọi khi user đổi output bus của pad — MainComponent lưu project. */
    std::function<void(int busIndex)> onOutputBusChanged;

    /** Chuẩn hoá âm lượng toàn bộ pad có file trong list đang active (RMS hoặc LUFS). */
    std::function<void(bool useLufs)> onNormalizeActiveListRequested;

    /** Lưu RMS/LUFS mặc định cho project + pad mới. */
    std::function<void(bool useLufs)> onDefaultNormalizeModeChanged;

    bool getNormalizeModeIsLufs() const { return normalizeModeIsLufs; }

    void setDefaultNormalizeMode (bool useLufs)
    {
        normalizeModeIsLufs = useLufs;
        normalizeRmsBtn.setToggleState (! useLufs, juce::dontSendNotification);
        normalizeLufsBtn.setToggleState (useLufs, juce::dontSendNotification);
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
        normalizeListBtn.setButtonText (showcontrol::localization::tr (u8"Đồng bộ cả list"));
        volumeLabel.setText (showcontrol::localization::tr (u8"Âm lượng (Volume):"), juce::dontSendNotification);
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
        auto accent = pal.accent;
        auto btnSec = pal.buttonSecondary;

        nameLabel.setColour (juce::Label::textColourId, mutedCol);
        nameEditor.setColour (juce::TextEditor::backgroundColourId, editorBg);
        nameEditor.setColour (juce::TextEditor::textColourId, textCol);
        nameEditor.setColour (juce::TextEditor::outlineColourId, editorOutline);
        nameEditor.setColour (juce::CaretComponent::caretColourId, pal.accent);
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
        nameEditor.setColour (juce::TextEditor::highlightedTextColourId, textCol);
        waveformDisplay.setDarkMode (isDark);
        trimStartLabel.setColour (juce::Label::textColourId, pal.trimMarkerStart);
        trimEndLabel.setColour (juce::Label::textColourId, pal.trimMarkerEnd);
        trimResetBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        trimResetBtn.setColour (juce::TextButton::textColourOffId, mutedCol);
        backBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        backBtn.setColour (juce::TextButton::textColourOffId, textCol);
        playBtn.setColour (juce::TextButton::buttonColourId, accent);
        playBtn.setColour (juce::TextButton::textColourOffId, isDark ? juce::Colours::white : pal.panelElevated);
        fadeOutBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        fadeOutBtn.setColour (juce::TextButton::textColourOffId, textCol);
        normalizeListBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        normalizeListBtn.setColour (juce::TextButton::textColourOffId, textCol);
        timeLabel.setColour (juce::Label::textColourId, textCol);
        metaFormatLabel.setColour (juce::Label::textColourId, mutedCol);
        loopToggle.setColour (juce::ToggleButton::textColourId, textCol);
        volumeLabel.setColour (juce::Label::textColourId, mutedCol);
        equalizerBtn.setColour (juce::TextButton::buttonColourId, btnSec);
        equalizerBtn.setColour (juce::TextButton::textColourOffId, textCol);
        volumeSlider.setColour (juce::Slider::trackColourId, sliderTr);
        volumeSlider.setColour (juce::Slider::thumbColourId, pal.sliderThumb);
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
                               isDark ? pal.rowSelected : pal.accent.withAlpha (0.14f));
            combo->setColour (juce::PopupMenu::highlightedTextColourId, textCol);
        }
        fadeInLabel.setColour (juce::Label::textColourId, mutedCol);
        fadeOutLabel.setColour (juce::Label::textColourId, mutedCol);

        juce::Slider* fadeSliders[] = { &fadeInSlider, &fadeOutSlider };
        for (auto* slider : fadeSliders)
        {
            slider->setColour (juce::Slider::trackColourId, sliderTr);
            slider->setColour (juce::Slider::thumbColourId, pal.sliderThumb);
            // Màu capsule ms — hardcode từ ShowTheme, không findColour cross-class (tránh assert LnF:82).
            slider->setColour (juce::Slider::textBoxTextColourId,       textCol);
            slider->setColour (juce::Slider::textBoxBackgroundColourId, editorBg.brighter (0.05f));
            slider->setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
            slider->setColour (juce::Slider::textBoxHighlightColourId, pal.accentSoft.withAlpha (0.28f));
            slider->repaint();
        }

        updateEqualizerButtonStyle();
        repaint();
    }

    void openTrimEditor (juce::Component* positionAnchor = nullptr)
    {
        openAdvancedTrimWindow (positionAnchor);
    }

    void selectPad (SoundPad* newPad)
    {
        if (newPad == currentPad)
        {
            if (currentPad != nullptr)
                refreshTransportUi();

            return;
        }

        if (newPad != currentPad)
            waveformDisplay.setThumbnail (nullptr);

        currentPad = newPad;

        if (currentPad != nullptr)
        {
            nameEditor.setText (currentPad->getPadName(), juce::dontSendNotification);
            volumeSlider.setValue (currentPad->getOutputGain(), juce::dontSendNotification);
            outputBusCombo.setSelectedItemIndex (
                juce::jlimit (0, showcontrol::routing::kInspectorBusCount - 1, currentPad->getOutputBus()),
                juce::dontSendNotification);
            loopToggle.setButtonText (getLoopToggleLabel());
            loopToggle.setToggleState (currentPad->isLooping(), juce::dontSendNotification);
            normalizeToggle.setToggleState (currentPad->getAutoNormalize(), juce::dontSendNotification);
            normalizeModeIsLufs = currentPad->getNormalizeUseLufs();
            normalizeRmsBtn.setToggleState (! normalizeModeIsLufs, juce::dontSendNotification);
            normalizeLufsBtn.setToggleState (normalizeModeIsLufs, juce::dontSendNotification);
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
        }
        else
        {
            nameEditor.setText (juce::String::fromUTF8 (u8"Chưa chọn pad"), juce::dontSendNotification);
            timeLabel.setText ("00:00.0", juce::dontSendNotification);
            trimStartLabel.setText ("00:00.0", juce::dontSendNotification); trimEndLabel.setText ("00:00.0", juce::dontSendNotification);
            rmsLabel.setText (juce::String::fromUTF8 (u8"—"), juce::dontSendNotification);
            waveformDisplay.setThumbnail (nullptr);
            updateRmsLabelAppearance();
            updateFileInfoLabels ({});
        }
    }

    /** Gọi sau khi file load xong để cập nhật metadata mà không cần select lại. */
    void refreshMetadata()
    {
        if (currentPad != nullptr)
            updateFileInfoLabels (currentPad->getMetadata());
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
            rmsLabel.setText (juce::String::fromUTF8 (u8"Đang đo…"), juce::dontSendNotification);
            updateRmsLabelAppearance();
            return;
        }

        auto text = currentPad->getLoudnessDisplayString();
        if (currentPad->getAutoNormalize() && currentPad->getDspLufsSyncGain() > 0.001f)
        {
            const float db = juce::Decibels::gainToDecibels (currentPad->getDspLufsSyncGain());
            text += juce::String::fromUTF8 (u8" · LUFS sync ") + juce::String (db, 1) + " dB";
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
        updateEqualizerButtonStyle();
    }

    void updateEqualizerButtonStyle()
    {
        const auto pal = ShowTheme::get (isDarkTheme);
        const bool eqOn = currentPad != nullptr && currentPad->getDspEqEnabled();
        equalizerBtn.setColour (juce::TextButton::buttonColourId, eqOn ? pal.accentSoft : pal.buttonSecondary);
        equalizerBtn.setColour (juce::TextButton::textColourOffId,
                                eqOn ? (isDarkTheme ? juce::Colours::white : pal.panelElevated) : pal.textPrimary);
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

    void openAdvancedTrimWindow (juce::Component* positionAnchor = nullptr)
    {
        if (currentPad == nullptr || !currentPad->hasAudioFile()) return;

        auto* trimComp = new AdvancedTrimComponent (currentPad, isDarkTheme, [this] {
            waveformDisplay.setTrimPoints (currentPad->getTrimStart(), currentPad->getTrimEnd());
            trimStartLabel.setText (currentPad->formatTimeString (currentPad->getTrimStart()), juce::dontSendNotification);
            double len = currentPad->getPlaybackLength(); double te = currentPad->getTrimEnd();
            trimEndLabel.setText (currentPad->formatTimeString (te > 0.0 ? te : len), juce::dontSendNotification);
        });

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned (trimComp);
        options.dialogTitle = showcontrol::localization::tr (u8"Cắt gọt âm thanh");
        options.dialogBackgroundColour = ShowTheme::get (isDarkTheme).panelElevated;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;

        if (auto* dw = options.launchAsync())
        {
            dw->setUsingNativeTitleBar (true);
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

        const double pos = currentPad->getPlaybackPosition();
        const double fileLen = currentPad->getPlaybackLength();

        timeLabel.setText (currentPad->getTimeRemainingString(), juce::dontSendNotification);
        waveformDisplay.setProgress (pos, fileLen);
        const bool isLive = currentPad->isPlaying() || currentPad->isFading();
        if (currentPad->usesCuePauseResume())
            playBtn.setComponentID (isLive ? "sc_inspector_icon_pause" : "sc_inspector_icon_play");
        else
            playBtn.setComponentID (isLive ? "sc_inspector_icon_stop" : "sc_inspector_icon_play");

        playBtn.repaint();
        refreshLoudnessLabel();
    }

    void timerCallback() override
    {
        if (currentPad != nullptr) {
            loopToggle.setButtonText (getLoopToggleLabel());
            refreshTransportUi();

            if (currentPad->isTransportActive())
                waveformDisplay.setProgress (currentPad->getPlaybackPosition(),
                                             currentPad->getPlaybackLength());

            if (currentPad->getAutoNormalize())
                refreshLoudnessLabel();
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
        static constexpr int rowHName = 28;
        static constexpr int rowHWave = 72;
        static constexpr int rowHTrim = 16;
        static constexpr int rowHTransport = 28;
        static constexpr int rowHTime = 30;
        static constexpr int rowHMeta = 12;
        static constexpr int rowHLoopEq = 28;
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
            const int nameLabelW = measureInspectorLabelWidth (nameLabel, nameRow.getWidth() / 3);
            nameLabel.setBounds (nameRow.removeFromLeft (nameLabelW));
            nameEditor.setBounds (nameRow);
        }

        skipGap();
        waveformDisplay.setBounds (area.removeFromTop (rowHWave));
        skipGap();

        // --- Trim ---
        {
            auto trimRow = area.removeFromTop (rowHTrim);
            const int endW = juce::jmin (trimRow.getWidth() / 3, measureInspectorLabelWidth (trimEndLabel, trimRow.getWidth() / 3));
            const int resetMeasured = juce::GlyphArrangement::getStringWidthInt (ShowTheme::font (11.0f),
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
        skipGap();
        metaFormatLabel.setBounds (area.removeFromTop (rowHMeta));
        skipGap();

        // --- Loop / Equalizer ---
        {
            auto loopEqRow = area.removeFromTop (rowHLoopEq);
            const int halfW = juce::jmax (0, (loopEqRow.getWidth() - pairGap) / 2);
            loopToggle.setBounds (loopEqRow.removeFromLeft (halfW));
            loopEqRow.removeFromLeft (pairGap);
            equalizerBtn.setBounds (loopEqRow);
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

        // --- Đồng bộ âm lượng ---
        const bool normOn = normalizeToggle.getToggleState();
        placeFullWidth (normalizeToggle, rowHBtn);

        if (normOn)
        {
            skipGap();

            auto modeRow = area.removeFromTop (rowHBtn);
            const int halfWidth = juce::jmax (0, (modeRow.getWidth() - pairGap) / 2);
            normalizeRmsBtn.setBounds (modeRow.removeFromLeft (halfWidth));
            modeRow.removeFromLeft (pairGap);
            normalizeLufsBtn.setBounds (modeRow);
            normalizeRmsBtn.setVisible (true);
            normalizeLufsBtn.setVisible (true);

            skipGap();
            placeFullWidth (normalizeListBtn, rowHBtn);
            normalizeListBtn.setVisible (true);

            if (rmsLabel.isVisible())
            {
                skipGap();
                placeFullWidth (rmsLabel, rowHLoudness);
            }
            else
            {
                rmsLabel.setBounds ({});
            }
        }
        else
        {
            normalizeRmsBtn.setBounds ({});
            normalizeLufsBtn.setBounds ({});
            normalizeListBtn.setBounds ({});
            rmsLabel.setBounds ({});
            normalizeRmsBtn.setVisible (false);
            normalizeLufsBtn.setVisible (false);
            normalizeListBtn.setVisible (false);
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

        currentPad->setNormalizeUseLufs (normalizeModeIsLufs);

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
        normalizeListBtn.setTooltip (showcontrol::localization::tr (
            u8"Đồng bộ mức âm lượng chuẩn LUFS toàn danh sách"));
        normalizeRmsBtn.setTooltip (showcontrol::localization::tr (
            u8"Chuyển chế độ đo âm lượng theo công suất trung bình tín hiệu điện toán RMS."));
        normalizeLufsBtn.setTooltip (showcontrol::localization::tr (
            u8"Chuyển chế độ đo âm lượng theo thuật toán cảm nhận tai người chuẩn phát thanh LUFS."));
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
    juce::StringArray cachedInspectorBusNames;
    InspectorStyle inspectorStyle;
    juce::Label nameLabel, trimStartLabel, trimEndLabel, timeLabel, volumeLabel, rmsLabel, hotkeyScopeLabel;
    juce::TextButton normalizeListBtn;
    juce::TextEditor nameEditor;
    InspectorWaveform waveformDisplay;
    juce::TextButton trimResetBtn, backBtn, playBtn, fadeOutBtn;
    juce::ToggleButton loopToggle, normalizeToggle;
    juce::ToggleButton normalizeRmsBtn, normalizeLufsBtn;
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