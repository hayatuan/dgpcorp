#pragma once
#include <array>
#include <atomic>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include "ShowLocalization.h"

/** Định tuyến phẳng: Route (Master + Bus phụ) → thiết bị/kênh Out quét từ hệ thống. */
namespace showcontrol::routing
{
    inline constexpr int kMasterRouteId   = 0;
    inline constexpr int kMaxCustomBuses  = 3;
    inline constexpr int kRouteCount      = 1 + kMaxCustomBuses;
    inline constexpr int kInspectorBusCount = kRouteCount;

    /** routeId 0 = Master; 1..kMaxCustomBuses = Bus phụ (tên tùy biến). */
    struct PhysicalRoute
    {
        int routeId            = 0;
        juce::String customName;
        int startChannelIndex  = 0;
    };

    struct DirectRoutingTable
    {
        std::atomic<int> masterStartChannel { 0 };
        std::atomic<uint8_t> masterActiveOnCurrentDevice { 1 };
        std::array<std::atomic<int>, kMaxCustomBuses> customBusStartChannel {};
        std::array<std::atomic<uint8_t>, kMaxCustomBuses> customBusActiveOnCurrentDevice {};

        DirectRoutingTable() noexcept
        {
            for (int i = 0; i < kMaxCustomBuses; ++i)
            {
                customBusStartChannel[(size_t) i].store (juce::jmin (2 + i * 2, 6),
                                                           std::memory_order_relaxed);
                customBusActiveOnCurrentDevice[(size_t) i].store (1, std::memory_order_relaxed);
            }
        }
    };

    inline DirectRoutingTable& liveTable() noexcept
    {
        static DirectRoutingTable table;
        return table;
    }

    inline juce::String masterRouteDisplayName() noexcept
    {
        return showcontrol::localization::tr (u8"Master (FOH Chính)");
    }

    inline juce::String defaultCustomBusName (int customBusIndex) noexcept
    {
        return showcontrol::localization::tr (u8"Bus ") + juce::String (customBusIndex + 1);
    }

    inline const char* legacyBusDefaultKey (int routeId) noexcept
    {
        switch (routeId)
        {
            case 0: return u8"Đầu ra FOH Chính";
            case 1: return u8"Monitor Sân Khấu";
            case 2: return u8"AUX 2 (Ch 5-6)";
            case 3: return u8"AUX 3 (Ch 7-8)";
            default: return nullptr;
        }
    }

    inline juce::String getBusDisplayName (int routeId)
    {
        if (routeId == kMasterRouteId)
            return masterRouteDisplayName();

        if (routeId >= 1 && routeId < kRouteCount)
            return defaultCustomBusName (routeId - 1);

        return "Route " + juce::String (routeId);
    }

    inline bool isDefaultRouteName (int routeId, const juce::String& name) noexcept
    {
        const auto trimmed = name.trim();
        if (trimmed.isEmpty())
            return true;

        if (routeId == kMasterRouteId)
        {
            return trimmed == masterRouteDisplayName()
                || trimmed == "Main FOH"
                || trimmed == juce::String::fromUTF8 (u8"Main FOH (Ch 1-2)")
                || trimmed == juce::String::fromUTF8 (u8"Đầu ra FOH Chính")
                || trimmed == juce::String::fromUTF8 (u8"FOH Chính (Cố định)");
        }

        if (const char* key = legacyBusDefaultKey (routeId))
        {
            if (trimmed == juce::String::fromUTF8 (key))
                return true;
        }

        if (routeId >= 1 && routeId < kRouteCount)
        {
            return trimmed == defaultCustomBusName (routeId - 1)
                || trimmed == ("Bus " + juce::String (routeId));
        }

        return false;
    }

    inline juce::StringArray getInspectorRouteDisplayNames()
    {
        juce::StringArray names;
        for (int i = 0; i < kRouteCount; ++i)
            names.add (getBusDisplayName (i));
        return names;
    }

    /** Quét danh sách Out giống dropdown Output của AudioDeviceSelector (message thread). */
    inline juce::StringArray scanAvailableOutputEndpoints (juce::AudioDeviceManager& manager)
    {
        if (auto* type = manager.getCurrentDeviceTypeObject())
            type->scanForDevices();

        juce::StringArray endpoints;

        if (auto* type = manager.getCurrentDeviceTypeObject())
        {
            for (const auto& name : type->getDeviceNames (false))
            {
                if (name.trim().isNotEmpty())
                    endpoints.add (name);
            }
        }

        if (auto* dev = manager.getCurrentAudioDevice())
        {
            const auto devName  = dev->getName();
            const auto channels = dev->getOutputChannelNames();

            if (channels.size() > 2)
            {
                for (int i = 0; i < channels.size(); i += 2)
                {
                    juce::String pairLabel;

                    if (i + 1 < channels.size())
                        pairLabel = channels[i] + " + " + channels[i + 1];
                    else
                        pairLabel = channels[i] + showcontrol::localization::tr (u8" (Mono)");

                    if (devName.isNotEmpty())
                    {
                        pairLabel = devName + juce::String::fromUTF8 (u8" — ") + pairLabel;
                        endpoints.addIfNotAlreadyThere (pairLabel);
                    }
                    else
                    {
                        endpoints.addIfNotAlreadyThere (pairLabel);
                    }
                }
            }
        }

        if (endpoints.isEmpty())
            endpoints.add (showcontrol::localization::tr (u8"Mặc định Hệ thống"));

        return endpoints;
    }

    /** Quét cặp Out từ tên kênh JUCE (message thread). */
    inline juce::StringArray buildOutputPairLabels (const juce::StringArray& channelNames)
    {
        juce::StringArray pairs;

        if (channelNames.isEmpty())
        {
            pairs.add (showcontrol::localization::tr (u8"Mặc định Hệ thống (Ch 1-2)"));
            return pairs;
        }

        for (int i = 0; i < channelNames.size(); i += 2)
        {
            if (i + 1 < channelNames.size())
                pairs.add (channelNames[i] + " + " + channelNames[i + 1]);
            else
                pairs.add (channelNames[i] + showcontrol::localization::tr (u8" (Mono)"));
        }

        return pairs;
    }

    inline juce::String hardwarePairLabelFromIndex (int leftChannelIndex) noexcept
    {
        return juce::String::fromUTF8 (u8"Out ")
             + juce::String (leftChannelIndex + 1)
             + juce::String::fromUTF8 (u8" – ")
             + juce::String (leftChannelIndex + 2);
    }

    inline int pairListIndexToStartChannel (int pairIndex) noexcept
    {
        return juce::jmax (0, pairIndex) * 2;
    }

    inline int startChannelToPairListIndex (int startChannel) noexcept
    {
        return juce::jmax (0, startChannel / 2);
    }

    inline int getRouteStartChannel (int routeId) noexcept
    {
        const auto& table = liveTable();

        if (routeId == kMasterRouteId)
            return table.masterStartChannel.load (std::memory_order_relaxed);

        if (routeId > kMasterRouteId && routeId < kRouteCount)
            return table.customBusStartChannel[(size_t) (routeId - 1)].load (std::memory_order_relaxed);

        return 0;
    }

    inline bool isRouteActiveOnCurrentDevice (int routeId) noexcept
    {
        const auto& table = liveTable();

        if (routeId == kMasterRouteId)
            return table.masterActiveOnCurrentDevice.load (std::memory_order_relaxed) != 0;

        if (routeId > kMasterRouteId && routeId < kRouteCount)
            return table.customBusActiveOnCurrentDevice[(size_t) (routeId - 1)].load (std::memory_order_relaxed) != 0;

        return true;
    }

    inline bool outputChoiceTargetsCurrentDevice (const juce::String& choice,
                                                  juce::AudioIODevice* device) noexcept
    {
        if (device == nullptr || choice.trim().isEmpty())
            return true;

        const auto devName       = device->getName().trim();
        const auto trimmedChoice = choice.trim();

        if (trimmedChoice.equalsIgnoreCase (devName))
            return true;

        return trimmedChoice.startsWithIgnoreCase (devName + juce::String::fromUTF8 (u8" — "));
    }

    inline int resolveOutputChoiceToStartChannel (const juce::String& choice,
                                                  juce::AudioIODevice* device) noexcept
    {
        if (device == nullptr || choice.trim().isEmpty())
            return 0;

        const auto devName = device->getName();

        if (choice == devName)
            return 0;

        if (choice.startsWith (devName + juce::String::fromUTF8 (u8" — ")))
        {
            const auto suffix   = choice.fromFirstOccurrenceOf (juce::String::fromUTF8 (u8" — "),
                                                                false,
                                                                false);
            const auto channels = device->getOutputChannelNames();

            for (int i = 0; i < channels.size(); i += 2)
            {
                juce::String pairLabel;

                if (i + 1 < channels.size())
                    pairLabel = channels[i] + " + " + channels[i + 1];
                else
                    pairLabel = channels[i] + showcontrol::localization::tr (u8" (Mono)");

                if (suffix == pairLabel)
                    return i;
            }
        }

        return 0;
    }

    inline void bindRouteOutputChoice (int routeId,
                                       const juce::String& choice,
                                       juce::AudioIODevice* device) noexcept
    {
        auto& table  = liveTable();
        const bool active = outputChoiceTargetsCurrentDevice (choice, device);
        const int startCh   = resolveOutputChoiceToStartChannel (choice, device);

        if (routeId == kMasterRouteId)
        {
            table.masterStartChannel.store (startCh, std::memory_order_relaxed);
            table.masterActiveOnCurrentDevice.store (1, std::memory_order_relaxed);
        }
        else if (routeId > kMasterRouteId && routeId < kRouteCount)
        {
            const auto idx = (size_t) (routeId - 1);
            table.customBusStartChannel[idx].store (startCh, std::memory_order_relaxed);
            table.customBusActiveOnCurrentDevice[idx].store (active ? 1 : 0, std::memory_order_relaxed);
        }
    }

    inline juce::String outputDeviceNameFromChoice (const juce::String& choice) noexcept
    {
        const auto trimmed = choice.trim();

        if (trimmed.isEmpty())
            return {};

        if (trimmed.contains (juce::String::fromUTF8 (u8" — ")))
            return trimmed.upToFirstOccurrenceOf (juce::String::fromUTF8 (u8" — "), false, false).trim();

        return trimmed;
    }

    /** PFL chỉ khả dụng khi có ≥2 cặp Out vật lý khác nhau trên cùng thiết bị. */
    inline int resolvePflPreviewRouteId() noexcept
    {
        const int masterCh = getRouteStartChannel (kMasterRouteId);

        for (int r = kMasterRouteId + 1; r < kRouteCount; ++r)
        {
            if (getRouteStartChannel (r) != masterCh && isRouteActiveOnCurrentDevice (r))
                return r;
        }

        return -1;
    }

    inline bool isIndependentPflAvailable (juce::AudioIODevice* device) noexcept
    {
        if (device == nullptr)
            return false;

        if (device->getOutputChannelNames().size() <= 2)
            return false;

        return resolvePflPreviewRouteId() > kMasterRouteId;
    }

    inline int getEffectiveOutputRoute (int storedRouteId, bool pflPreviewActive) noexcept
    {
        if (! pflPreviewActive)
            return juce::jlimit (0, kRouteCount - 1, storedRouteId);

        const int pflRoute = resolvePflPreviewRouteId();

        if (pflRoute > kMasterRouteId)
            return pflRoute;

        return juce::jlimit (0, kRouteCount - 1, storedRouteId);
    }

    /**
     * Ánh xạ routeId → cặp kênh hardware.
     * Fallback về Master (0,1) nếu cổng không tồn tại — không mất tiếng.
     */
    inline void resolveHardwareForRoute (int routeId,
                                       int totalHardwareOutputs,
                                       int& targetLeftChannel,
                                       int& targetRightChannel) noexcept
    {
        targetLeftChannel  = 0;
        targetRightChannel = totalHardwareOutputs > 1 ? 1 : 0;

        if (totalHardwareOutputs <= 0)
            return;

        const int reqLeft  = getRouteStartChannel (routeId);
        const int reqRight = reqLeft + 1;

        if (reqRight < totalHardwareOutputs)
        {
            targetLeftChannel  = reqLeft;
            targetRightChannel = reqRight;
        }
    }

    /** Tương thích call site cũ — routeId chính là outputBus trên pad. */
    inline void resolveHardwareStereoChannels (int routeId,
                                               int totalHardwareOutputs,
                                               int& targetLeftChannel,
                                               int& targetRightChannel) noexcept
    {
        resolveHardwareForRoute (routeId, totalHardwareOutputs, targetLeftChannel, targetRightChannel);
    }

    inline bool isDefaultBusName (int routeId, const juce::String& name) noexcept
    {
        return isDefaultRouteName (routeId, name);
    }

    inline juce::StringArray getInspectorBusDisplayNames()
    {
        return getInspectorRouteDisplayNames();
    }
}
