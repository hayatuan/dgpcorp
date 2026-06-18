#pragma once

#include <JuceHeader.h>
#include <functional>
#include "ShowBackupSync.h"

namespace showcontrol::osc
{
struct ShowOscCallbacks
{
    std::function<void()> onPanic;
    std::function<void (int listIndex, int padIndex, float preWaitMs, int playMode)> onGo;
    std::function<void()> onStopAll;
    std::function<void()> onPauseAll;
    std::function<void (int listIndex, int padIndex)> onStopCue;
    std::function<void (int listIndex, int padIndex)> onPauseCue;
    std::function<void (juce::uint32 sequence)> onHeartbeat;
    std::function<void (bool active)> onTakeover;
    std::function<void (int listIndex, int padIndex, int viewMode, const juce::Array<int>& multiIndices)> onSelection;
};

/** OSC Inbound — điều khiển show / đồng bộ Primary→Backup trên LAN. */
class ShowOscListener final : private juce::OSCReceiver,
                              private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
public:
    explicit ShowOscListener (ShowOscCallbacks callbacksIn)
        : callbacks (std::move (callbacksIn))
    {
    }

    ~ShowOscListener() override
    {
        disconnect();
        removeListener (this);
    }

    bool start (int port)
    {
        removeListener (this);
        disconnect();

        if (! connect (port))
            return false;

        addListener (this);
        listeningPort = port;
        return true;
    }

    void stop()
    {
        removeListener (this);
        disconnect();
        listeningPort = 0;
    }

    int getPort() const noexcept { return listeningPort; }

private:
    static int readIntArg (const juce::OSCMessage& message, int index, int fallback = 0)
    {
        if (index >= message.size())
            return fallback;

        const auto& arg = message[index];

        if (arg.isInt32())
            return arg.getInt32();

        if (arg.isFloat32())
            return (int) arg.getFloat32();

        return fallback;
    }

    static float readFloatArg (const juce::OSCMessage& message, int index, float fallback = 0.0f)
    {
        if (index >= message.size())
            return fallback;

        const auto& arg = message[index];

        if (arg.isFloat32())
            return arg.getFloat32();

        if (arg.isInt32())
            return (float) arg.getInt32();

        return fallback;
    }

    void dispatchGo (int listIndex, int padIndex, float preWaitMs, int playMode)
    {
        if (! callbacks.onGo)
            return;

        juce::MessageManager::callAsync ([cb = callbacks.onGo, listIndex, padIndex, preWaitMs, playMode]
        {
            cb (listIndex, padIndex, preWaitMs, playMode);
        });
    }

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        const auto address = message.getAddressPattern().toString();

        if (address == showcontrol::backup::addresses::panic
            || address == showcontrol::backup::addresses::legacyPanic)
        {
            if (callbacks.onPanic)
                juce::MessageManager::callAsync ([cb = callbacks.onPanic] { cb(); });
            return;
        }

        if (address == showcontrol::backup::addresses::go
            || address == showcontrol::backup::addresses::legacyGo)
        {
            int listIndex = 0;
            int padIndex  = 0;

            if (message.size() >= 2)
            {
                listIndex = readIntArg (message, 0, 0);
                padIndex  = readIntArg (message, 1, 0);
            }
            else if (message.size() == 1)
            {
                padIndex = readIntArg (message, 0, 0);
            }

            const float preWaitMs = readFloatArg (message, 2, 0.0f);
            const int playMode    = message.size() >= 4 ? readIntArg (message, 3, (int) showcontrol::backup::SyncPlayMode::legacy)
                                                        : (int) showcontrol::backup::SyncPlayMode::legacy;
            dispatchGo (listIndex, padIndex, preWaitMs, playMode);
            return;
        }

        if (address == showcontrol::backup::addresses::stopAll)
        {
            if (callbacks.onStopAll)
                juce::MessageManager::callAsync ([cb = callbacks.onStopAll] { cb(); });
            return;
        }

        if (address == showcontrol::backup::addresses::pauseAll)
        {
            if (callbacks.onPauseAll)
                juce::MessageManager::callAsync ([cb = callbacks.onPauseAll] { cb(); });
            return;
        }

        if (address == showcontrol::backup::addresses::stopCue)
        {
            if (callbacks.onStopCue)
            {
                const int listIndex = readIntArg (message, 0, 0);
                const int padIndex  = readIntArg (message, 1, 0);
                juce::MessageManager::callAsync ([cb = callbacks.onStopCue, listIndex, padIndex]
                {
                    cb (listIndex, padIndex);
                });
            }
            return;
        }

        if (address == showcontrol::backup::addresses::pauseCue)
        {
            if (callbacks.onPauseCue)
            {
                const int listIndex = readIntArg (message, 0, 0);
                const int padIndex  = readIntArg (message, 1, 0);
                juce::MessageManager::callAsync ([cb = callbacks.onPauseCue, listIndex, padIndex]
                {
                    cb (listIndex, padIndex);
                });
            }
            return;
        }

        if (address == showcontrol::backup::addresses::heartbeat)
        {
            if (callbacks.onHeartbeat)
            {
                const auto seq = (juce::uint32) readIntArg (message, 0, 0);
                juce::MessageManager::callAsync ([cb = callbacks.onHeartbeat, seq]
                {
                    cb (seq);
                });
            }
            return;
        }

        if (address == showcontrol::backup::addresses::takeover)
        {
            if (callbacks.onTakeover)
            {
                const bool active = readIntArg (message, 0, 0) != 0;
                juce::MessageManager::callAsync ([cb = callbacks.onTakeover, active]
                {
                    cb (active);
                });
            }

            return;
        }

        if (address == showcontrol::backup::addresses::selection)
        {
            if (! callbacks.onSelection)
                return;

            const int listIndex = readIntArg (message, 0, -1);
            const int padIndex  = readIntArg (message, 1, -1);
            const int viewMode  = readIntArg (message, 2, -1);
            const int count     = readIntArg (message, 3, 0);

            juce::Array<int> multiIndices;
            multiIndices.ensureStorageAllocated (count);

            for (int i = 0; i < count; ++i)
            {
                const int idx = readIntArg (message, 4 + i, -1);

                if (idx >= 0)
                    multiIndices.add (idx);
            }

            juce::MessageManager::callAsync ([cb = callbacks.onSelection, listIndex, padIndex, viewMode, multiIndices]
            {
                cb (listIndex, padIndex, viewMode, multiIndices);
            });
        }
    }

    ShowOscCallbacks callbacks;
    int listeningPort = 0;
};

} // namespace showcontrol::osc
