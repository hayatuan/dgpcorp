#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

/** Tiện ích tránh juce_GraphicsContext assertion (w/h âm, NaN, Inf) — Debug JUCE. */
namespace showcontrol::gfx
{
inline bool isFinite (double v) noexcept { return std::isfinite (v); }
inline bool isFinite (float v) noexcept  { return std::isfinite (v); }

inline int clampDimension (int d) noexcept
{
    if (d < 0 || d > 0x3fffffff)
        return 0;

    return d;
}

inline int clampCoord (int c) noexcept
{
    if (c < -0x3fffffff || c > 0x3fffffff)
        return 0;

    return c;
}

inline juce::Rectangle<int> sanitise (juce::Rectangle<int> r) noexcept
{
    return { clampCoord (r.getX()),
             clampCoord (r.getY()),
             clampDimension (r.getWidth()),
             clampDimension (r.getHeight()) };
}

inline juce::Rectangle<float> sanitise (juce::Rectangle<float> r) noexcept
{
    if (! isFinite (r.getX()) || ! isFinite (r.getY())
        || ! isFinite (r.getWidth()) || ! isFinite (r.getHeight()))
        return {};

    return { r.getX(), r.getY(), juce::jmax (0.0f, r.getWidth()), juce::jmax (0.0f, r.getHeight()) };
}

inline void safeSetBounds (juce::Component& c, juce::Rectangle<int> r)
{
    c.setBounds (sanitise (r));
}

inline juce::Rectangle<int> safeRemoveFromTop (juce::Rectangle<int>& area, int height)
{
    height = clampDimension (height);
    if (area.getHeight() <= 0 || height <= 0)
        return {};

    return sanitise (area.removeFromTop (juce::jmin (height, area.getHeight())));
}

inline juce::Rectangle<int> safeRemoveFromLeft (juce::Rectangle<int>& area, int width)
{
    width = clampDimension (width);
    if (area.getWidth() <= 0 || width <= 0)
        return {};

    return sanitise (area.removeFromLeft (juce::jmin (width, area.getWidth())));
}

inline juce::Rectangle<int> safeRemoveFromRight (juce::Rectangle<int>& area, int width)
{
    width = clampDimension (width);
    if (area.getWidth() <= 0 || width <= 0)
        return {};

    return sanitise (area.removeFromRight (juce::jmin (width, area.getWidth())));
}

inline double safePositiveDuration (double seconds) noexcept
{
    return (isFinite (seconds) && seconds > 0.0) ? seconds : 0.0;
}

/** t trong [viewStart, viewEnd] → ratio 0..1; viewEnd <= viewStart → 0. */
inline float timeToRatio (double t, double viewStart, double viewEnd) noexcept
{
    const double span = viewEnd - viewStart;
    if (! isFinite (t) || ! isFinite (viewStart) || ! isFinite (viewEnd) || span <= 1.0e-12)
        return 0.0f;

    return (float) juce::jlimit (0.0, 1.0, (t - viewStart) / span);
}

inline float ratioFromMouseX (int mouseX, const juce::Rectangle<int>& area) noexcept
{
    const int w = area.getWidth();
    if (w <= 0)
        return 0.0f;

    return (float) juce::jlimit (0.0, 1.0, (mouseX - area.getX()) / (double) w);
}

inline int ratioToPixelX (const juce::Rectangle<int>& area, float ratio) noexcept
{
    const int w = area.getWidth();
    if (w <= 0 || ! isFinite (ratio))
        return area.getX();

    const float px = (float) area.getX() + juce::jlimit (0.0f, 1.0f, ratio) * (float) w;
    if (! isFinite (px))
        return area.getX();

    return (int) std::round (px);
}

inline int timeToPixelX (const juce::Rectangle<int>& area, double t,
                         double viewStart, double viewEnd) noexcept
{
    return ratioToPixelX (area, timeToRatio (t, viewStart, viewEnd));
}

inline void safeFillRect (juce::Graphics& g, const juce::Rectangle<int>& r)
{
    const auto safe = sanitise (r);
    if (safe.getWidth() > 0 && safe.getHeight() > 0)
        g.fillRect (safe);
}

inline void safeFillRect (juce::Graphics& g, int x, int y, int w, int h)
{
    safeFillRect (g, sanitise (juce::Rectangle<int> { x, y, w, h }));
}

inline void safeDrawVerticalLine (juce::Graphics& g, int x, float y1, float y2) noexcept
{
    if (! isFinite (y1) || ! isFinite (y2))
        return;

    const float top = juce::jmin (y1, y2);
    const float bottom = juce::jmax (y1, y2);

    if (bottom - top < 0.5f)
        return;

    g.drawVerticalLine (clampCoord (x), top, bottom);
}

inline bool canClip (const juce::Rectangle<int>& r) noexcept
{
    const auto s = sanitise (r);
    return s.getWidth() > 0 && s.getHeight() > 0;
}

inline bool canClip (const juce::Rectangle<float>& r) noexcept
{
    const auto s = sanitise (r);
    return s.getWidth() > 0.0f && s.getHeight() > 0.0f;
}

} // namespace showcontrol::gfx
