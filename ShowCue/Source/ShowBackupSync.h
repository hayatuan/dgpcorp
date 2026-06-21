#pragma once

#include <JuceHeader.h>
#include "ShowBackupPadSync.h"

namespace showcontrol::backup
{
enum class Role : int
{
    standalone = 0,
    primary    = 1,
    backup     = 2
};

enum class SyncPlayMode : int
{
    legacy      = -1,
    padGo       = 0,
    cueListPlay = 1,
    bgmPlay     = 2
};

inline constexpr int kDefaultSyncPort           = 9000;
inline constexpr int kHeartbeatIntervalMs         = 400;
inline constexpr int kHeartbeatStaleThresholdMs   = 10000;
inline constexpr int kHeartbeatDegradedThresholdMs = 5000;
inline constexpr int kHeartbeatOfflineProbeMs     = 5000;
inline constexpr int kPeerHealthIntervalMs        = 1000;
inline constexpr int kPeerPingTimeoutMs           = 400;
inline constexpr int kLanAnnounceIntervalMs       = 1500;
inline constexpr int kPrimaryBroadcasterRefreshMs = 15000;
inline constexpr int kRestartBackupSyncDebounceMs = 3000;
inline constexpr int kPadPatchDebounceMs          = 60;
inline constexpr int kSelectionSyncDebounceMs     = 20;
inline constexpr int kMaxBackupPeers            = 16;

enum class LinkQuality : int
{
    unknown  = 0,
    good     = 1,
    degraded = 2,
    offline  = 3
};

struct PeerRuntimeStatus
{
    juce::String address;
    juce::String hostName;
    int latencyMs = -1;
    LinkQuality quality = LinkQuality::unknown;
};

namespace addresses
{
inline constexpr const char* panic      = "/showcue/sync/panic";
inline constexpr const char* go         = "/showcue/sync/go";
inline constexpr const char* stopAll    = "/showcue/sync/stopAll";
inline constexpr const char* pauseAll   = "/showcue/sync/pauseAll";
inline constexpr const char* stopCue    = "/showcue/sync/stopCue";
inline constexpr const char* pauseCue   = "/showcue/sync/pauseCue";
inline constexpr const char* heartbeat  = "/showcue/sync/heartbeat";
inline constexpr const char* takeover   = "/showcue/sync/takeover";
inline constexpr const char* selection  = "/showcue/sync/selection";
inline constexpr const char* padPatch   = "/showcue/sync/padPatch";
inline constexpr const char* padReorder = "/showcue/sync/padReorder";
inline constexpr const char* padOrder   = "/showcue/sync/padOrder";
inline constexpr const char* listPatch   = "/showcue/sync/listPatch";
inline constexpr const char* listReorder = "/showcue/sync/listReorder";
inline constexpr const char* configBegin = "/showcue/sync/configBegin";
inline constexpr const char* configChunk = "/showcue/sync/configChunk";
inline constexpr const char* configEnd   = "/showcue/sync/configEnd";
inline constexpr const char* legacyPanic = "/showcue/panic";
inline constexpr const char* legacyGo    = "/showcue/go";
} // namespace addresses

inline bool syncDebugLoggingEnabled() noexcept
{
    if (const char* env = std::getenv ("SHOWCUE_SYNC_DEBUG"))
        return env[0] != '0' && env[0] != '\0';

    return false;
}

inline void logSyncEvent (const juce::String& message)
{
    if (! syncDebugLoggingEnabled())
        return;

    std::cout << "[SYNC] " << message << std::endl;
}

/** Primary → Backup OSC broadcast (UDP unicast to one or more peers). */
class ShowBackupSyncBroadcaster
{
public:
    bool configure (const juce::StringArray& peerHosts, int port, const juce::String& localBindAddress = {})
    {
        peerSenders.clear();
        targetHosts.clear();
        targetPort = juce::jlimit (1024, 65535, port);
        localBind = localBindAddress.trim();

        for (const auto& host : peerHosts)
        {
            const auto trimmed = host.trim();

            if (trimmed.isEmpty())
                continue;

            bool duplicate = false;

            for (const auto& existing : targetHosts)
            {
                if (existing.equalsIgnoreCase (trimmed))
                {
                    duplicate = true;
                    break;
                }
            }

            if (! duplicate)
                targetHosts.add (trimmed);
        }

        connected = targetHosts.size() > 0;
        peerSenders.clear();

        for (const auto& host : targetHosts)
        {
            auto* peer = new PeerSender();
            peer->host = host;

            const bool bound = localBind.isNotEmpty()
                                   ? peer->socket.bindToPort (0, localBind)
                                   : peer->socket.bindToPort (0);

            if (! bound)
            {
                peer->socket.bindToPort (0);
                showcontrol::backup::logSyncEvent ("WARN OSC sender bind failed on "
                                                   + localBind + " — using OS default route");
            }

            if (peer->sender.connectToSocket (peer->socket, host, targetPort))
                peerSenders.add (peer);
            else
                delete peer;
        }

        connected = peerSenders.size() > 0;

        return connected;
    }

    bool configure (const juce::String& peerHost, int port, const juce::String& localBindAddress = {})
    {
        juce::StringArray hosts;

        if (peerHost.trim().isNotEmpty())
            hosts.add (peerHost.trim());

        return configure (hosts, port, localBindAddress);
    }

    void disconnect() noexcept
    {
        connected   = false;
        targetHosts.clear();
        peerSenders.clear();
    }

    bool isConnected() const noexcept { return connected && targetHosts.size() > 0; }

    int getPeerCount() const noexcept { return targetHosts.size(); }

    const juce::StringArray& getPeerHosts() const noexcept { return targetHosts; }

    bool sendPanic()
    {
        return sendEmpty (addresses::panic);
    }

    bool sendGo (int listIndex, int padIndex, float preWaitMs = 0.0f, int playMode = (int) SyncPlayMode::legacy)
    {
        juce::OSCMessage msg (addresses::go);
        msg.addInt32 (listIndex);
        msg.addInt32 (padIndex);
        msg.addFloat32 (preWaitMs);
        msg.addInt32 (playMode);
        return sendMessage (msg);
    }

    bool sendStopAll()
    {
        return sendEmpty (addresses::stopAll);
    }

    bool sendPauseAll()
    {
        return sendEmpty (addresses::pauseAll);
    }

    bool sendStopCue (int listIndex, int padIndex)
    {
        juce::OSCMessage msg (addresses::stopCue);
        msg.addInt32 (listIndex);
        msg.addInt32 (padIndex);
        return sendMessage (msg);
    }

    bool sendPauseCue (int listIndex, int padIndex)
    {
        juce::OSCMessage msg (addresses::pauseCue);
        msg.addInt32 (listIndex);
        msg.addInt32 (padIndex);
        return sendMessage (msg);
    }

    bool sendHeartbeat (juce::uint32 sequence)
    {
        juce::OSCMessage msg (addresses::heartbeat);
        msg.addInt32 ((int) sequence);
        return sendMessage (msg);
    }

    bool sendTakeover (bool active)
    {
        juce::OSCMessage msg (addresses::takeover);
        msg.addInt32 (active ? 1 : 0);
        return sendMessage (msg);
    }

    /** listIndex, padIndex, viewMode (-1=skip, 0=pad grid, 1=cue list), then multi-select pad indices. */
    bool sendSelection (int listIndex,
                        int padIndex,
                        int viewMode,
                        const juce::Array<int>& multiIndices)
    {
        juce::OSCMessage msg (addresses::selection);
        msg.addInt32 (listIndex);
        msg.addInt32 (padIndex);
        msg.addInt32 (viewMode);
        msg.addInt32 (multiIndices.size());

        for (const auto idx : multiIndices)
            msg.addInt32 (idx);

        return sendMessage (msg);
    }

    bool sendPadPatch (const showcontrol::backup::padpatch::PatchMessage& patch)
    {
        juce::OSCMessage msg (addresses::padPatch);
        msg.addInt32 (patch.listIndex);
        msg.addInt32 (patch.padIndex);
        msg.addInt32 ((int) patch.flags);

        if ((patch.flags & padpatch::kName) != 0)
            msg.addString (patch.name);

        if ((patch.flags & padpatch::kColour) != 0)
            msg.addInt32 ((int) patch.colourArgb);

        if ((patch.flags & padpatch::kGridPos) != 0)
        {
            msg.addInt32 (patch.gridRow);
            msg.addInt32 (patch.gridCol);
        }

        if ((patch.flags & padpatch::kVolume) != 0)
            msg.addFloat32 (patch.volume);

        if ((patch.flags & padpatch::kFadeIn) != 0)
            msg.addFloat32 (patch.fadeInMs);

        if ((patch.flags & padpatch::kFadeOut) != 0)
            msg.addFloat32 (patch.fadeOutMs);

        if ((patch.flags & padpatch::kLoop) != 0)
            msg.addInt32 (patch.loop);

        if ((patch.flags & padpatch::kOutputBus) != 0)
            msg.addInt32 (patch.outputBus);

        if ((patch.flags & padpatch::kTrim) != 0)
        {
            msg.addFloat32 ((float) patch.trimStart);
            msg.addFloat32 ((float) patch.trimEnd);
        }

        return sendMessage (msg);
    }

    bool sendPadReorder (int listIndex, int fromIndex, int toIndex)
    {
        juce::OSCMessage msg (addresses::padReorder);
        msg.addInt32 (listIndex);
        msg.addInt32 (fromIndex);
        msg.addInt32 (toIndex);
        return sendMessage (msg);
    }

    bool sendPadOrder (int listIndex, const juce::StringArray& padKeysInOrder)
    {
        juce::OSCMessage msg (addresses::padOrder);
        msg.addInt32 (listIndex);
        msg.addInt32 (padKeysInOrder.size());

        for (const auto& key : padKeysInOrder)
            msg.addString (key);

        return sendMessage (msg);
    }

    bool sendListPatch (const showcontrol::backup::listpatch::PatchMessage& patch)
    {
        juce::OSCMessage msg (addresses::listPatch);
        msg.addInt32 (patch.listIndex);
        msg.addInt32 ((int) patch.flags);

        if ((patch.flags & listpatch::kName) != 0)
            msg.addString (patch.name);

        if ((patch.flags & listpatch::kThemeColour) != 0)
            msg.addInt32 ((int) patch.colourArgb);

        return sendMessage (msg);
    }

    bool sendListReorder (int fromIndex, int toIndex)
    {
        juce::OSCMessage msg (addresses::listReorder);
        msg.addInt32 (fromIndex);
        msg.addInt32 (toIndex);
        return sendMessage (msg);
    }

    bool sendConfigBegin (int transferId, int totalBytes, int chunkCount)
    {
        juce::OSCMessage msg (addresses::configBegin);
        msg.addInt32 (transferId);
        msg.addInt32 (totalBytes);
        msg.addInt32 (chunkCount);
        return sendMessage (msg);
    }

    bool sendConfigChunk (int transferId, int chunkIndex, const juce::String& chunkUtf8)
    {
        juce::OSCMessage msg (addresses::configChunk);
        msg.addInt32 (transferId);
        msg.addInt32 (chunkIndex);
        msg.addString (chunkUtf8);
        return sendMessage (msg, false);
    }

    bool sendConfigEnd (int transferId)
    {
        juce::OSCMessage msg (addresses::configEnd);
        msg.addInt32 (transferId);
        return sendMessage (msg);
    }

private:
    struct PeerSender
    {
        juce::String host;
        juce::DatagramSocket socket { true };
        juce::OSCSender sender;
    };

    bool sendEmpty (const char* address)
    {
        return sendMessage (juce::OSCMessage (address));
    }

    bool sendMessage (const juce::OSCMessage& message, bool logTx = true)
    {
        if (! isConnected())
            return false;

        bool anyOk = false;

        for (auto* peer : peerSenders)
        {
            if (peer->sender.send (message))
            {
                anyOk = true;

                if (! logTx)
                    continue;

                const auto addr = message.getAddressPattern().toString();

                if (addr != addresses::heartbeat && addr != addresses::configChunk)
                {
                    logSyncEvent ("TX " + addr + " -> " + peer->host + ":"
                                  + juce::String (targetPort));
                }
            }
        }

        return anyOk;
    }

    juce::OwnedArray<PeerSender> peerSenders;
    juce::StringArray targetHosts;
    juce::String localBind;
    int targetPort = kDefaultSyncPort;
    bool connected = false;
};

} // namespace showcontrol::backup
