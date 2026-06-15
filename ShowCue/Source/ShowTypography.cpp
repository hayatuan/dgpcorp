#include "ShowTypography.h"
#include <BinaryData.h>

namespace showcontrol::typography
{
namespace
{
    juce::Typeface::Ptr& uiRegularCache()  { static juce::Typeface::Ptr p; return p; }
    juce::Typeface::Ptr& uiBoldCache()     { static juce::Typeface::Ptr p; return p; }
    juce::Typeface::Ptr& timerRegularCache() { static juce::Typeface::Ptr p; return p; }
    juce::Typeface::Ptr& timerBoldCache()    { static juce::Typeface::Ptr p; return p; }

    juce::Typeface::Ptr loadFromMemory (const void* data, size_t size)
    {
        if (data == nullptr || size == 0)
            return nullptr;

        return juce::Typeface::createSystemTypefaceFor (data, size);
    }

    juce::Typeface::Ptr loadSystemFamily (const juce::StringArray& families,
                                          const juce::String& style)
    {
        const auto available = juce::Font::findAllTypefaceNames();

        for (const auto& family : families)
        {
            if (! available.contains (family, true))
                continue;

            const juce::Font probe (juce::FontOptions().withName (family).withStyle (style).withHeight (16.0f));
            auto face = juce::Typeface::createSystemTypefaceFor (probe);

            if (face != nullptr)
                return face;
        }

        return nullptr;
    }

    juce::Typeface::Ptr loadUiFallback (const juce::String& style)
    {
       #if JUCE_WINDOWS
        return loadSystemFamily ({ "Segoe UI Variable", "Segoe UI", "Tahoma" }, style);
       #elif JUCE_MAC
        return loadSystemFamily ({ "SF Pro Text", "Helvetica Neue", "Arial" }, style);
       #else
        return loadSystemFamily ({ "DejaVu Sans", "Liberation Sans", "Arial" }, style);
       #endif
    }

    void loadUiTypefaces()
    {
        uiRegularCache() = loadFromMemory (BinaryData::RobotoRegular_ttf,
                                           BinaryData::RobotoRegular_ttfSize);
        uiBoldCache()    = loadFromMemory (BinaryData::RobotoBold_ttf,
                                           BinaryData::RobotoBold_ttfSize);

        if (uiRegularCache() == nullptr)
            uiRegularCache() = loadUiFallback ("Regular");

        if (uiBoldCache() == nullptr)
            uiBoldCache() = loadUiFallback ("Bold");
    }

    juce::Typeface::Ptr resolveTimerTypeface (bool bold)
    {
        auto& cache = bold ? timerBoldCache() : timerRegularCache();

        if (cache != nullptr)
            return cache;

        const juce::String style = bold ? "Bold" : "Regular";

        cache = loadSystemFamily ({
            "JetBrains Mono",
            "Roboto Mono",
            "SF Mono",
            "Menlo",
            "Consolas",
            "Courier New",
            juce::Font::getDefaultMonospacedFontName()
        }, style);

        if (cache == nullptr)
        {
            const juce::Font fallback (juce::FontOptions()
                                           .withName (juce::Font::getDefaultMonospacedFontName())
                                           .withStyle (style)
                                           .withHeight (16.0f));
            cache = juce::Typeface::createSystemTypefaceFor (fallback);
        }

        return cache;
    }

    void ensureTimerTypefaces()
    {
        resolveTimerTypeface (false);
        resolveTimerTypeface (true);
    }

    juce::Font makeFont (juce::Typeface::Ptr face, float heightPx)
    {
        if (face == nullptr)
            return juce::Font (juce::FontOptions().withHeight (heightPx));

        return juce::Font (juce::FontOptions (face).withHeight (heightPx));
    }
}

void ensureLoaded()
{
    if (uiRegularCache() == nullptr || uiBoldCache() == nullptr)
        loadUiTypefaces();

    ensureTimerTypefaces();
}

void reload()
{
    uiRegularCache() = nullptr;
    uiBoldCache() = nullptr;
    timerRegularCache() = nullptr;
    timerBoldCache() = nullptr;
    juce::Typeface::clearTypefaceCache();
    loadUiTypefaces();
    ensureTimerTypefaces();
}

void shutdown() noexcept
{
    uiRegularCache() = nullptr;
    uiBoldCache() = nullptr;
    timerRegularCache() = nullptr;
    timerBoldCache() = nullptr;
    juce::Typeface::clearTypefaceCache();
}

juce::Typeface::Ptr uiRegular()
{
    ensureLoaded();
    return uiRegularCache();
}

juce::Typeface::Ptr uiBold()
{
    ensureLoaded();
    return uiBoldCache();
}

juce::Typeface::Ptr timerFace (bool bold)
{
    ensureLoaded();
    return resolveTimerTypeface (bold);
}

juce::Font uiFont (float heightPx, const juce::String& style)
{
    ensureLoaded();
    const bool bold = style.equalsIgnoreCase ("Bold");
    return makeFont (bold ? uiBoldCache() : uiRegularCache(), heightPx);
}

juce::Font uiFontBold (float heightPx)
{
    return uiFont (heightPx, "Bold");
}

juce::Font timerFont (float heightPx, bool bold)
{
    ensureLoaded();
    return makeFont (resolveTimerTypeface (bold), heightPx);
}

bool isBoldRequest (const juce::Font& font) noexcept
{
    return font.isBold()
        || font.getTypefaceStyle().containsIgnoreCase ("bold");
}

bool isTimerFontRequest (const juce::Font& font) noexcept
{
    const auto name = font.getTypefaceName();

    if (name == "TimerFont")
        return true;

    if (name == juce::Font::getDefaultMonospacedFontName())
        return true;

    return false;
}

juce::Typeface::Ptr resolveForLookAndFeel (const juce::Font& font)
{
    ensureLoaded();

    if (isTimerFontRequest (font))
        return resolveTimerTypeface (isBoldRequest (font));

    if (isBoldRequest (font))
    {
        if (uiBoldCache() != nullptr)
            return uiBoldCache();
    }

    if (uiRegularCache() != nullptr)
        return uiRegularCache();

    return nullptr;
}

} // namespace showcontrol::typography
