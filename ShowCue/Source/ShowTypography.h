#pragma once

#include <juce_graphics/juce_graphics.h>

/** Roboto nhúng nhị phân — dùng Typeface::Ptr trực tiếp, không tra tên hệ thống. */
namespace showcontrol::typography
{
void ensureLoaded();
void reload();
void shutdown() noexcept;

juce::Typeface::Ptr uiRegular();
juce::Typeface::Ptr uiBold();
juce::Typeface::Ptr timerFace (bool bold);

juce::Font uiFont (float heightPx, const juce::String& style = {});
juce::Font uiFontBold (float heightPx);
juce::Font timerFont (float heightPx, bool bold = false);

bool isBoldRequest (const juce::Font& font) noexcept;
bool isTimerFontRequest (const juce::Font& font) noexcept;
juce::Typeface::Ptr resolveForLookAndFeel (const juce::Font& font);
}
