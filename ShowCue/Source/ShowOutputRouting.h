#pragma once
#include <juce_core/juce_core.h>

/** Ma trận định tuyến stereo bus → cặp kênh hardware (RT-safe helpers). */
namespace showcontrol::routing
{
    /** Số bus hiển thị trên Inspector (Main FOH + 3 AUX). */
    inline constexpr int kInspectorBusCount = 4;

    inline juce::String getBusDisplayName (int busIndex)
    {
        switch (busIndex)
        {
            case 0:  return juce::String::fromUTF8 (u8"Main FOH (Ch 1-2)");
            case 1:  return juce::String::fromUTF8 (u8"AUX 1 (Ch 3-4)");
            case 2:  return juce::String::fromUTF8 (u8"AUX 2 (Ch 5-6)");
            case 3:  return juce::String::fromUTF8 (u8"AUX 3 (Ch 7-8)");
            default: return "Bus " + juce::String (busIndex + 1);
        }
    }

    inline juce::StringArray getInspectorBusDisplayNames()
    {
        juce::StringArray names;
        for (int i = 0; i < kInspectorBusCount; ++i)
            names.add (getBusDisplayName (i));
        return names;
    }

    /**
     * Ánh xạ bus logic (0=Main, 1=AUX1…) sang cặp kênh output vật lý.
     * Nếu soundcard không đủ kênh → fallback an toàn về Main FOH (0, 1).
     * Gọi từ audio thread — không alloc.
     */
    inline void resolveHardwareStereoChannels (int busIndex,
                                               int totalHardwareOutputs,
                                               int& targetLeftChannel,
                                               int& targetRightChannel) noexcept
    {
        targetLeftChannel  = 0;
        targetRightChannel = totalHardwareOutputs > 1 ? 1 : 0;

        if (totalHardwareOutputs <= 0)
            return;

        const int safeBus = juce::jmax (0, busIndex);
        const int reqLeft  = safeBus * 2;
        const int reqRight = reqLeft + 1;

        if (reqRight < totalHardwareOutputs)
        {
            targetLeftChannel  = reqLeft;
            targetRightChannel = reqRight;
        }
        // else: giữ Main FOH (0, 1) — không mất tiếng, không tràn buffer
    }
}
