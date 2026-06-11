#pragma once
#include <map>
#include <memory>
#include <juce_gui_basics/juce_gui_basics.h>

/** Icon outline 2D — Lucide (Iconify) path strings, stroke mảnh, không fill khối. */
namespace showcontrol::icons
{
    constexpr float kOutlineStroke     = 1.1f;
    constexpr float kOutlineStrokeBold = 1.2f;

    constexpr float kListIconSize      = 14.0f;
    constexpr float kButtonIconSize    = 16.0f;
    constexpr float kTabIconSize       = 18.0f;

    //==========================================================================
    /** Chuỗi d Flaticon/Iconify — prefix lucide, viewBox 24×24. */
    namespace Lucide
    {
        /** lucide:volume-2 — phễu loa + 2 sóng âm outline. */
        inline constexpr const char* volume2 =
            "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z"
            "M16 9a5 5 0 0 1 0 6"
            "M19.364 18.364a9 9 0 0 0 0-12.728";

        /** lucide:repeat — hai mũi tên vòng cung đuổi nhau. */
        inline constexpr const char* repeat =
            "m17 2l4 4l-4 4"
            " M3 11v-1a4 4 0 0 1 4-4h14"
            " M7 22l-4-4l4-4"
            " M21 13v1a4 4 0 0 1-4 4H3";

        /** lucide:check — dấu V phóng khoáng. */
        inline constexpr const char* check =
            "M20 6L9 17l-5-5";

        /** lucide:pause — hai thanh bo góc (chuyển từ rect → path để parseSVGPath). */
        inline constexpr const char* pause =
            "M 6 3 H 9 A 1 1 0 0 1 10 4 V 20 A 1 1 0 0 1 9 21 H 6 A 1 1 0 0 1 5 20 V 4 A 1 1 0 0 1 6 3"
            " M 15 3 H 18 A 1 1 0 0 1 19 4 V 20 A 1 1 0 0 1 18 21 H 15 A 1 1 0 0 1 14 20 V 4 A 1 1 0 0 1 15 3";

        /** lucide:play */
        inline constexpr const char* play =
            "M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z";

        /** lucide:skip-back */
        inline constexpr const char* skipBack =
            "M17.971 4.285A2 2 0 0 1 21 6v12a2 2 0 0 1-3.029 1.715l-9.997-5.998a2 2 0 0 1-.003-3.432zM3 20V4";

        /** lucide:sliders-vertical */
        inline constexpr const char* sliders =
            "M10 8h4m-2 13v-9m0-4V3m5 13h4m-2-4V3m0 18v-5M3 14h4m-2-4V3m0 18v-7";

        /** lucide:settings — bánh răng + vòng trong. */
        inline constexpr const char* settings =
            "M9.671 4.136a2.34 2.34 0 0 1 4.659 0a2.34 2.34 0 0 0 3.319 1.915a2.34 2.34 0 0 1 2.33 4.033a2.34 2.34 0 0 0 0 3.831a2.34 2.34 0 0 1-2.33 4.033a2.34 2.34 0 0 0-3.319 1.915a2.34 2.34 0 0 1-4.659 0a2.34 2.34 0 0 0-3.32-1.915a2.34 2.34 0 0 1-2.33-4.033a2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915"
            " M 12 9 A 3 3 0 1 1 11.99 9";

        /** lucide:monitor */
        inline constexpr const char* monitor =
            "M 4 3 H 20 A 2 2 0 0 1 22 5 V 15 A 2 2 0 0 1 20 17 H 4 A 2 2 0 0 1 2 15 V 5 A 2 2 0 0 1 4 3"
            " M 8 21 H 16 M 12 17 V 21";

        /** lucide:chevrons-left */
        inline constexpr const char* chevronsLeft =
            "m11 17l-5-5l5-5m7 10l-5-5l5-5";

        /** lucide:chevrons-right */
        inline constexpr const char* chevronsRight =
            "m6 17l5-5l-5-5m7 10l5-5l-5-5";

        /** lucide:square — stop outline. */
        inline constexpr const char* square =
            "M 5 5 H 19 A 2 2 0 0 1 21 7 V 19 A 2 2 0 0 1 19 21 H 5 A 2 2 0 0 1 3 19 V 7 A 2 2 0 0 1 5 5";

        /** lucide:headphones */
        inline constexpr const char* headphones =
            "M3 14h3a2 2 0 0 1 2 2v3a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-7a9 9 0 0 1 18 0v7a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3";

        /** lucide:layout (panels-top-left) */
        inline constexpr const char* layout =
            "M 5 3 H 19 A 2 2 0 0 1 21 5 V 19 A 2 2 0 0 1 19 21 H 5 A 2 2 0 0 1 3 19 V 5 A 2 2 0 0 1 5 3"
            " M 3 9 H 21 M 9 21 V 9";

        /** lucide:mic */
        inline constexpr const char* mic =
            "M 12 14a4 4 0 0 0 4-4V5a4 4 0 0 0-8 0v5a4 4 0 0 0 4 4z"
            " M 19 11a7 7 0 0 1-14 0"
            " M 12 18v4"
            " M 8 22h8";

        /** lucide:globe */
        inline constexpr const char* globe =
            "M 12 2a10 10 0 1 0 0 20a10 10 0 0 0 0-20"
            " M 2 12h20"
            " M 12 2a15.3 15.3 0 0 1 4 10a15.3 15.3 0 0 1-4 10a15.3 15.3 0 0 1-4-10a15.3 15.3 0 0 1 4-10";

        /** lucide:shield-check */
        inline constexpr const char* shieldCheck =
            "M 12 22s8-4 8-10V5l-8-3l-8 3v7c0 6 8 10 8 10"
            " M 9 12l2 2l4-4";

        /** Fade wedge outline — đường dốc mảnh. */
        inline constexpr const char* fadeSlope =
            "M 6 18 L 18 6";
    }

    /** Alias tương thích call-site cũ. */
    namespace Paths
    {
        inline constexpr const char* speaker   = Lucide::volume2;
        inline constexpr const char* pause     = Lucide::pause;
        inline constexpr const char* loop      = Lucide::repeat;
        inline constexpr const char* checkmark = Lucide::check;
    }

    //==========================================================================
    inline juce::PathStrokeType outlineStroke (float width = kOutlineStroke) noexcept
    {
        return { width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded };
    }

    /** Giải mã chuỗi d Iconify/Lucide → juce::Path (JUCE 8: Drawable::parseSVGPath). */
    inline juce::Path createFromSVGPathString (const juce::String& svgPathData)
    {
        return juce::Drawable::parseSVGPath (svgPathData);
    }

    inline const juce::Path& cachedPath (const char* cacheKey, const char* svgPathData)
    {
        static std::map<juce::String, juce::Path> pathCache;
        const juce::String key (cacheKey);

        if (auto it = pathCache.find (key); it != pathCache.end())
            return it->second;

        pathCache[key] = createFromSVGPathString (svgPathData);
        return pathCache[key];
    }

    /** Renderer chính — stroke outline, scale lọt lòng targetBounds. */
    inline void drawSVGPathIcon (juce::Graphics& g,
                                 const char* cacheKey,
                                 const char* svgPathData,
                                 juce::Rectangle<float> targetBounds,
                                 juce::Colour iconColour,
                                 float strokeWidth = kOutlineStroke)
    {
        const auto& path = cachedPath (cacheKey, svgPathData);
        if (path.isEmpty())
            return;

        const auto fit = targetBounds.reduced (1.0f);
        const auto transform = path.getTransformToScaleToFit (fit, true);

        g.setColour (iconColour);
        g.strokePath (path, outlineStroke (strokeWidth), transform);
    }

    inline void drawSVGPathIcon (juce::Graphics& g,
                                 const juce::String& svgPathData,
                                 juce::Rectangle<float> targetBounds,
                                 juce::Colour iconColour,
                                 float strokeWidth = kOutlineStroke)
    {
        drawSVGPathIcon (g, svgPathData.toRawUTF8(), svgPathData.toRawUTF8(),
                         targetBounds, iconColour, strokeWidth);
    }

    /** Selected = trắng căng; idle = xám mờ theo theme. */
    inline juce::Colour iconColourForListState (bool isSelected, bool isDarkTheme) noexcept
    {
        if (isSelected)
            return juce::Colours::white;

        return isDarkTheme ? juce::Colours::white.withAlpha (0.40f)
                           : juce::Colour (0xff6b7280);
    }

    /** Loa đang phát — xanh lá FOH hoặc trắng khi hàng được chọn. */
    inline juce::Colour speakerPlayingColour (bool isSelected) noexcept
    {
        return isSelected ? juce::Colours::white : juce::Colour (0xff00e676);
    }

    inline juce::Rectangle<float> centredIconIn (juce::Rectangle<float> area, float size) noexcept
    {
        return area.withSizeKeepingCentre (size, size);
    }

    //==========================================================================
    inline void paintSpeakerIcon (juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  juce::Colour colour,
                                  bool highlighted = false)
    {
        const auto col = highlighted ? colour : colour.withAlpha (0.72f);
        drawSVGPathIcon (g, "lucide:volume-2", Lucide::volume2, bounds, col, kOutlineStroke);
    }

    inline void paintPauseIcon (juce::Graphics& g,
                                juce::Rectangle<float> bounds,
                                juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:pause", Lucide::pause, bounds, colour, kOutlineStroke);
    }

    inline void paintLoopIcon (juce::Graphics& g,
                               juce::Rectangle<float> bounds,
                               juce::Colour colour,
                               bool active = false)
    {
        const auto col = active ? colour : colour.withAlpha (0.55f);
        drawSVGPathIcon (g, "lucide:repeat", Lucide::repeat, bounds, col,
                         active ? kOutlineStrokeBold : kOutlineStroke);
    }

    inline void paintCheckmark (juce::Graphics& g,
                                juce::Rectangle<float> bounds,
                                juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:check", Lucide::check, bounds, colour, kOutlineStroke);
    }

    inline void paintFlatCheckbox (juce::Graphics& g,
                                   juce::Rectangle<float> boxBounds,
                                   bool checked,
                                   juce::Colour outline,
                                   juce::Colour checkColour)
    {
        g.setColour (outline.withAlpha (checked ? 0.95f : 0.70f));
        g.drawRoundedRectangle (boxBounds, 2.0f, kOutlineStroke);

        if (checked)
            paintCheckmark (g, boxBounds.reduced (3.5f, 4.0f), checkColour);
    }

    inline void paintPlayIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:play", Lucide::play, bounds, colour, kOutlineStroke);
    }

    inline void paintStopIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:square", Lucide::square, bounds, colour, kOutlineStroke);
    }

    inline void paintSkipBackIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:skip-back", Lucide::skipBack, bounds, colour, kOutlineStroke);
    }

    inline void paintSlidersIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:sliders", Lucide::sliders, bounds, colour, kOutlineStroke);
    }

    inline void paintSettingsIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:settings", Lucide::settings, bounds, colour, kOutlineStroke);
    }

    inline void paintMonitorIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:monitor", Lucide::monitor, bounds, colour, kOutlineStroke);
    }

    inline void paintChevronsLeftIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:chevrons-left", Lucide::chevronsLeft, bounds, colour, kOutlineStroke);
    }

    inline void paintChevronsRightIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:chevrons-right", Lucide::chevronsRight, bounds, colour, kOutlineStroke);
    }

    inline void paintHeadphonesIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:headphones", Lucide::headphones, bounds, colour, kOutlineStroke);
    }

    inline void paintLayoutIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:layout", Lucide::layout, bounds, colour, kOutlineStroke);
    }

    inline void paintMicIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:mic", Lucide::mic, bounds, colour, kOutlineStroke);
    }

    inline void paintGlobeIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:globe", Lucide::globe, bounds, colour, kOutlineStroke);
    }

    inline void paintShieldCheckIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:shield-check", Lucide::shieldCheck, bounds, colour, kOutlineStroke);
    }

    inline void paintFadeSlopeIcon (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
    {
        drawSVGPathIcon (g, "lucide:fade-slope", Lucide::fadeSlope, bounds, colour, kOutlineStroke);
    }

    //==========================================================================
    /** Hạ tầng nạp file .svg ngoài (Iconify export) — dự phòng. */
    inline juce::File resolveSvgResourceFile (const juce::String& resourceName)
    {
        const auto fileName = resourceName.endsWithIgnoreCase (".svg") ? resourceName
                                                                       : resourceName + ".svg";

        const auto appFile = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
        if (appFile.existsAsFile())
        {
            const auto bundled = appFile.getSiblingFile ("Contents")
                                        .getChildFile ("Resources")
                                        .getChildFile ("icons")
                                        .getChildFile (fileName);
            if (bundled.existsAsFile())
                return bundled;
        }

        return juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                   .getParentDirectory()
                   .getChildFile ("Resources")
                   .getChildFile ("icons")
                   .getChildFile (fileName);
    }

    inline void tintDrawableRecursive (juce::Drawable& drawable, juce::Colour iconColour)
    {
        if (auto* shape = dynamic_cast<juce::DrawablePath*> (&drawable))
        {
            shape->setFill (juce::FillType (juce::Colours::transparentBlack));
            shape->setStrokeFill (juce::FillType (iconColour));
            if (shape->getStrokeType().getStrokeThickness() <= 0.0f)
                shape->setStrokeType (outlineStroke (kOutlineStroke));
        }
        else if (auto* composite = dynamic_cast<juce::DrawableComposite*> (&drawable))
        {
            for (int i = 0; i < composite->getNumChildComponents(); ++i)
                if (auto* child = dynamic_cast<juce::Drawable*> (composite->getChildComponent (i)))
                    tintDrawableRecursive (*child, iconColour);
        }
        else if (auto* rect = dynamic_cast<juce::DrawableRectangle*> (&drawable))
        {
            rect->setFill (juce::FillType (juce::Colours::transparentBlack));
            rect->setStrokeFill (juce::FillType (iconColour));
        }
    }

    inline juce::Drawable* getCachedSvgDrawable (const juce::String& resourceName)
    {
        static std::map<juce::String, std::unique_ptr<juce::Drawable>> cache;

        if (auto it = cache.find (resourceName); it != cache.end())
            return it->second.get();

        const auto svgFile = resolveSvgResourceFile (resourceName);
        if (! svgFile.existsAsFile())
            return nullptr;

        if (auto loaded = juce::Drawable::createFromSVGFile (svgFile))
        {
            auto* raw = loaded.get();
            cache.emplace (resourceName, std::move (loaded));
            return raw;
        }

        return nullptr;
    }

    inline void drawSVGIcon (juce::Graphics& g,
                             const juce::String& resourceName,
                             juce::Rectangle<float> bounds,
                             juce::Colour iconColour)
    {
        if (auto* source = getCachedSvgDrawable (resourceName))
        {
            if (auto copy = source->createCopy())
            {
                tintDrawableRecursive (*copy, iconColour);
                copy->drawWithin (g, bounds, juce::RectanglePlacement::centred
                                              | juce::RectanglePlacement::onlyReduceInSize,
                                  1.0f);
            }
        }
    }
}
