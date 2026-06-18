#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "ShowBackupSync.h"

namespace showcontrol::backup
{
inline constexpr int kDiscoveryPortOffset = 1;

inline int discoveryPortForSyncPort (int syncPort) noexcept
{
    return juce::jlimit (1024, 65535, syncPort + kDiscoveryPortOffset);
}

struct LanPeerInfo
{
    juce::String address;
    juce::String hostName;
    int role = 0;
    int syncPort = kDefaultSyncPort;
};

inline juce::String makeDiscoverProbe (int wantRole, int replyPort)
{
    return "SHOWCUE_DISCOVER|" + juce::String (wantRole) + "|" + juce::String (replyPort);
}

inline bool parseDiscoverProbe (const juce::String& text, int& wantRoleOut, int& replyPortOut)
{
    if (! text.startsWith ("SHOWCUE_DISCOVER|"))
        return false;

    juce::StringArray parts;
    parts.addTokens (text, "|", {});

    if (parts.size() < 3)
        return false;

    wantRoleOut  = parts[1].getIntValue();
    replyPortOut = parts[2].getIntValue();
    return replyPortOut > 0;
}

inline juce::String makeDiscoverAnnounce (int role, const juce::String& hostName, int syncPort)
{
    return "SHOWCUE_ANNOUNCE|" + juce::String (role) + "|" + hostName + "|" + juce::String (syncPort);
}

inline bool parseDiscoverAnnounce (const juce::String& text, LanPeerInfo& out)
{
    if (! text.startsWith ("SHOWCUE_ANNOUNCE|"))
        return false;

    juce::StringArray parts;
    parts.addTokens (text, "|", {});

    if (parts.size() < 4)
        return false;

    out.role     = parts[1].getIntValue();
    out.hostName = parts[2];
    out.syncPort = parts[3].getIntValue();
    return true;
}

inline bool roleMatchesDiscoverRequest (int ourRole, int wantRole) noexcept
{
    if (wantRole == 0)
        return ourRole == (int) Role::primary || ourRole == (int) Role::backup;

    return ourRole == wantRole;
}

inline juce::Array<LanPeerInfo> scanLanPeers (int wantRole,
                                              int syncPort,
                                              int timeoutMs = 2500)
{
    juce::Array<LanPeerInfo> results;
    const int discoveryPort = discoveryPortForSyncPort (syncPort);

    juce::DatagramSocket socket;

    if (! socket.bindToPort (0))
        return results;

    const int replyPort = socket.getBoundPort();
    const auto probe    = makeDiscoverProbe (wantRole, replyPort);

    socket.write ("255.255.255.255", discoveryPort, probe.toRawUTF8(),
                  (int) probe.getNumBytesAsUTF8());

    const int deadline = (int) juce::Time::getMillisecondCounter() + timeoutMs;

    while ((int) juce::Time::getMillisecondCounter() < deadline)
    {
        if (socket.waitUntilReady (true, 80) <= 0)
            continue;

        char buffer[512] = {};
        juce::String senderHost;
        int senderPort = 0;
        const int bytes = socket.read (buffer, (int) sizeof (buffer) - 1, false,
                                       senderHost, senderPort);

        if (bytes <= 0)
            continue;

        buffer[bytes] = '\0';

        LanPeerInfo peer;
        peer.address = senderHost;

        if (! parseDiscoverAnnounce (juce::String::fromUTF8 (buffer), peer))
            continue;

        if (wantRole != 0 && peer.role != wantRole)
            continue;

        bool duplicate = false;

        for (const auto& existing : results)
        {
            if (existing.address == peer.address)
            {
                duplicate = true;
                break;
            }
        }

        if (! duplicate)
            results.add (peer);
    }

    return results;
}

} // namespace showcontrol::backup
