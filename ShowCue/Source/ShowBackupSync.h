#pragma once

#include <JuceHeader.h>

namespace showcontrol::backup
{
enum class Role : int
{
    standalone = 0,
    primary    = 1,
    backup     = 2
};

inline constexpr int kDefaultSyncPort           = 9000;
inline constexpr int kHeartbeatIntervalMs       = 1000;
inline constexpr int kHeartbeatStaleThresholdMs   = 3500;
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
inline constexpr const char* legacyPanic = "/showcue/panic";
inline constexpr const char* legacyGo    = "/showcue/go";
} // namespace addresses

/** Primary → Backup OSC broadcast (UDP unicast to one or more peers). */
class ShowBackupSyncBroadcaster
{
public:
    bool configure (const juce::StringArray& peerHosts, int port)
    {
        targetHosts.clear();
        targetPort = juce::jlimit (1024, 65535, port);

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
        return connected;
    }

    bool configure (const juce::String& peerHost, int port)
    {
        juce::StringArray hosts;

        if (peerHost.trim().isNotEmpty())
            hosts.add (peerHost.trim());

        return configure (hosts, port);
    }

    void disconnect() noexcept
    {
        connected   = false;
        targetHosts.clear();
    }

    bool isConnected() const noexcept { return connected && targetHosts.size() > 0; }

    int getPeerCount() const noexcept { return targetHosts.size(); }

    const juce::StringArray& getPeerHosts() const noexcept { return targetHosts; }

    bool sendPanic()
    {
        return sendEmpty (addresses::panic);
    }

    bool sendGo (int listIndex, int padIndex, float preWaitMs = 0.0f)
    {
        juce::OSCMessage msg (addresses::go);
        msg.addInt32 (listIndex);
        msg.addInt32 (padIndex);
        msg.addFloat32 (preWaitMs);
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

private:
    bool sendEmpty (const char* address)
    {
        return sendMessage (juce::OSCMessage (address));
    }

    bool sendMessage (const juce::OSCMessage& message)
    {
        if (! isConnected())
            return false;

        bool anyOk = false;

        for (const auto& host : targetHosts)
        {
            if (sender.connect (host, targetPort))
            {
                if (sender.send (message))
                    anyOk = true;

                sender.disconnect();
            }
        }

        return anyOk;
    }

    juce::OSCSender sender;
    juce::StringArray targetHosts;
    int targetPort = kDefaultSyncPort;
    bool connected = false;
};

} // namespace showcontrol::backup
