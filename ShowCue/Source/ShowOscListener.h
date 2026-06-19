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
    std::function<void (const showcontrol::backup::padpatch::PatchMessage&)> onPadPatch;
    std::function<void (int listIndex, int fromIndex, int toIndex)> onPadReorder;
    std::function<void (int listIndex, const juce::StringArray& padKeysInOrder)> onPadOrder;
    std::function<void (const showcontrol::backup::listpatch::PatchMessage&)> onListPatch;
    std::function<void (int fromIndex, int toIndex)> onListReorder;
};

/** OSC Inbound — điều khiển show / đồng bộ Primary→Backup trên LAN. */
class ShowOscListener final : private juce::OSCReceiver,
                              private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
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

    bool start (int port, const juce::String& bindAddress = {})
    {
        removeListener (this);
        disconnect();
        ownedSocket.reset();

        if (bindAddress.isNotEmpty())
        {
            ownedSocket = std::make_unique<juce::DatagramSocket> (true);
            ownedSocket->setEnablePortReuse (true);

            if (ownedSocket->bindToPort (port, bindAddress)
                && connectToSocket (*ownedSocket))
            {
                addListener (this);
                listeningPort = port;
                return true;
            }

            ownedSocket.reset();
        }

        auto fallbackSocket = std::make_unique<juce::DatagramSocket> (true);
        fallbackSocket->setEnablePortReuse (true);

        if (fallbackSocket->bindToPort (port) && connectToSocket (*fallbackSocket))
        {
            ownedSocket = std::move (fallbackSocket);
            addListener (this);
            listeningPort = port;
            return true;
        }

        return false;
    }

    void stop()
    {
        removeListener (this);
        disconnect();
        ownedSocket.reset();
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

    void oscMessageReceived (const juce::OSCMessage& message) override
    {
        const auto address = message.getAddressPattern().toString();

        if (address == showcontrol::backup::addresses::panic
            || address == showcontrol::backup::addresses::legacyPanic)
        {
            if (callbacks.onPanic)
                callbacks.onPanic();
            return;
        }

        if (address == showcontrol::backup::addresses::go
            || address == showcontrol::backup::addresses::legacyGo)
        {
            if (! callbacks.onGo)
                return;

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
            callbacks.onGo (listIndex, padIndex, preWaitMs, playMode);
            return;
        }

        if (address == showcontrol::backup::addresses::stopAll)
        {
            if (callbacks.onStopAll)
                callbacks.onStopAll();
            return;
        }

        if (address == showcontrol::backup::addresses::pauseAll)
        {
            if (callbacks.onPauseAll)
                callbacks.onPauseAll();
            return;
        }

        if (address == showcontrol::backup::addresses::stopCue)
        {
            if (callbacks.onStopCue)
            {
                const int listIndex = readIntArg (message, 0, 0);
                const int padIndex  = readIntArg (message, 1, 0);
                callbacks.onStopCue (listIndex, padIndex);
            }
            return;
        }

        if (address == showcontrol::backup::addresses::pauseCue)
        {
            if (callbacks.onPauseCue)
            {
                const int listIndex = readIntArg (message, 0, 0);
                const int padIndex  = readIntArg (message, 1, 0);
                callbacks.onPauseCue (listIndex, padIndex);
            }
            return;
        }

        if (address == showcontrol::backup::addresses::heartbeat)
        {
            if (callbacks.onHeartbeat)
            {
                const auto seq = (juce::uint32) readIntArg (message, 0, 0);
                callbacks.onHeartbeat (seq);
            }
            return;
        }

        if (address == showcontrol::backup::addresses::takeover)
        {
            if (callbacks.onTakeover)
            {
                const bool active = readIntArg (message, 0, 0) != 0;
                callbacks.onTakeover (active);
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

            callbacks.onSelection (listIndex, padIndex, viewMode, multiIndices);
            return;
        }

        if (address == showcontrol::backup::addresses::padPatch)
        {
            if (! callbacks.onPadPatch)
                return;

            showcontrol::backup::padpatch::PatchMessage patch;
            patch.listIndex = readIntArg (message, 0, -1);
            patch.padIndex  = readIntArg (message, 1, -1);
            patch.flags     = (juce::uint32) readIntArg (message, 2, 0);

            int arg = 3;

            if ((patch.flags & showcontrol::backup::padpatch::kName) != 0 && arg < message.size())
            {
                if (message[arg].isString())
                    patch.name = message[arg++].getString();
                else
                    ++arg;
            }

            if ((patch.flags & showcontrol::backup::padpatch::kColour) != 0 && arg < message.size())
                patch.colourArgb = (juce::uint32) readIntArg (message, arg++, 0);

            if ((patch.flags & showcontrol::backup::padpatch::kGridPos) != 0 && arg + 1 < message.size())
            {
                patch.gridRow = readIntArg (message, arg++, 0);
                patch.gridCol = readIntArg (message, arg++, 0);
            }

            if ((patch.flags & showcontrol::backup::padpatch::kVolume) != 0 && arg < message.size())
                patch.volume = readFloatArg (message, arg++, 1.0f);

            if ((patch.flags & showcontrol::backup::padpatch::kFadeIn) != 0 && arg < message.size())
                patch.fadeInMs = readFloatArg (message, arg++, 0.0f);

            if ((patch.flags & showcontrol::backup::padpatch::kFadeOut) != 0 && arg < message.size())
                patch.fadeOutMs = readFloatArg (message, arg++, 0.0f);

            if ((patch.flags & showcontrol::backup::padpatch::kLoop) != 0 && arg < message.size())
                patch.loop = readIntArg (message, arg++, 0);

            if ((patch.flags & showcontrol::backup::padpatch::kOutputBus) != 0 && arg < message.size())
                patch.outputBus = readIntArg (message, arg++, 0);

            if ((patch.flags & showcontrol::backup::padpatch::kTrim) != 0 && arg + 1 < message.size())
            {
                patch.trimStart = readFloatArg (message, arg++, 0.0f);
                patch.trimEnd   = readFloatArg (message, arg++, 0.0f);
            }

            callbacks.onPadPatch (patch);
            return;
        }

        if (address == showcontrol::backup::addresses::padReorder)
        {
            if (! callbacks.onPadReorder)
                return;

            const int listIndex = readIntArg (message, 0, -1);
            const int fromIndex = readIntArg (message, 1, -1);
            const int toIndex   = readIntArg (message, 2, -1);
            callbacks.onPadReorder (listIndex, fromIndex, toIndex);
            return;
        }

        if (address == showcontrol::backup::addresses::padOrder)
        {
            if (! callbacks.onPadOrder)
                return;

            const int listIndex = readIntArg (message, 0, -1);
            const int count     = readIntArg (message, 1, 0);

            juce::StringArray keys;
            keys.ensureStorageAllocated (count);

            for (int i = 0; i < count; ++i)
            {
                const int argIndex = 2 + i;

                if (argIndex < message.size() && message[argIndex].isString())
                    keys.add (message[argIndex].getString());
            }

            callbacks.onPadOrder (listIndex, keys);
            return;
        }

        if (address == showcontrol::backup::addresses::listPatch)
        {
            if (! callbacks.onListPatch)
                return;

            showcontrol::backup::listpatch::PatchMessage patch;
            patch.listIndex = readIntArg (message, 0, -1);
            patch.flags     = (juce::uint32) readIntArg (message, 1, 0);

            int arg = 2;

            if ((patch.flags & showcontrol::backup::listpatch::kName) != 0 && arg < message.size())
            {
                if (message[arg].isString())
                    patch.name = message[arg++].getString();
                else
                    ++arg;
            }

            if ((patch.flags & showcontrol::backup::listpatch::kThemeColour) != 0 && arg < message.size())
                patch.colourArgb = (juce::uint32) readIntArg (message, arg++, 0);

            callbacks.onListPatch (patch);
            return;
        }

        if (address == showcontrol::backup::addresses::listReorder)
        {
            if (! callbacks.onListReorder)
                return;

            const int fromIndex = readIntArg (message, 0, -1);
            const int toIndex   = readIntArg (message, 1, -1);
            callbacks.onListReorder (fromIndex, toIndex);
        }
    }

    ShowOscCallbacks callbacks;
    std::unique_ptr<juce::DatagramSocket> ownedSocket;
    int listeningPort = 0;
};

} // namespace showcontrol::osc
