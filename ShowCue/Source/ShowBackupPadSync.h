#pragma once

#include <JuceHeader.h>
#include "SoundPad.h"

namespace showcontrol::backup::padpatch
{
inline constexpr const char* patchAddress   = "/showcue/sync/padPatch";
inline constexpr const char* reorderAddress = "/showcue/sync/padReorder";
inline constexpr const char* orderAddress   = "/showcue/sync/padOrder";

constexpr juce::uint32 kName      = 1u << 0;
constexpr juce::uint32 kColour    = 1u << 1;
constexpr juce::uint32 kGridPos   = 1u << 2;
constexpr juce::uint32 kVolume    = 1u << 3;
constexpr juce::uint32 kFadeIn    = 1u << 4;
constexpr juce::uint32 kFadeOut   = 1u << 5;
constexpr juce::uint32 kLoop      = 1u << 6;
constexpr juce::uint32 kOutputBus = 1u << 7;
constexpr juce::uint32 kTrim      = 1u << 8;

constexpr juce::uint32 kInspectorFields = kVolume | kFadeIn | kFadeOut | kLoop | kOutputBus | kTrim;

inline juce::String makePadSyncKey (const SoundPad* pad)
{
    if (pad == nullptr)
        return {};

    if (pad->hasAudioFile())
    {
        const auto path = pad->getFilePath();

        if (path.isNotEmpty())
            return path;
    }

    return "@" + pad->getPadName() + "#" + juce::String (pad->getPadIndex());
}

struct PatchMessage
{
    int listIndex = -1;
    int padIndex  = -1;
    juce::uint32 flags = 0;
    juce::String name;
    juce::uint32 colourArgb = 0;
    int gridRow = 0;
    int gridCol = 0;
    float volume = 1.0f;
    float fadeInMs = 0.0f;
    float fadeOutMs = 0.0f;
    int loop = 0;
    int outputBus = 0;
    double trimStart = 0.0;
    double trimEnd = 0.0;
};

inline PatchMessage patchFromPad (int listIndex, int padIndex, juce::uint32 flags, const SoundPad* pad)
{
    PatchMessage msg;
    msg.listIndex = listIndex;
    msg.padIndex  = padIndex;
    msg.flags     = flags;

    if (pad == nullptr)
        return msg;

    if ((flags & kName) != 0)
        msg.name = pad->getPadName();

    if ((flags & kColour) != 0)
        msg.colourArgb = (juce::uint32) pad->getTagColour().getARGB();

    if ((flags & kGridPos) != 0)
    {
        msg.gridRow = pad->getGridRow();
        msg.gridCol = pad->getGridCol();
    }

    if ((flags & kVolume) != 0)
        msg.volume = pad->getOutputGain();

    if ((flags & kFadeIn) != 0)
        msg.fadeInMs = (float) pad->getFadeInMs();

    if ((flags & kFadeOut) != 0)
        msg.fadeOutMs = (float) pad->getFadeOutMs();

    if ((flags & kLoop) != 0)
        msg.loop = pad->isLooping() ? 1 : 0;

    if ((flags & kOutputBus) != 0)
        msg.outputBus = pad->getOutputBus();

    if ((flags & kTrim) != 0)
    {
        msg.trimStart = pad->getTrimStart();
        msg.trimEnd   = pad->getTrimEnd();
    }

    return msg;
}

} // namespace showcontrol::backup::padpatch
