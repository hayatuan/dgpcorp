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

/** Một interface LAN dùng để gửi UDP broadcast discovery (ưu tiên Wi‑Fi / interface đang active). */
struct LanScanTarget
{
    juce::String interfaceAddress;
    juce::String broadcastAddress;
};

inline bool isUsableLanIpv4 (const juce::IPAddress& addr) noexcept
{
    if (addr.isNull() || addr.isIPv6)
        return false;

    const auto text = addr.toString();

    if (text.startsWith ("127."))
        return false;

    if (text.startsWith ("169.254."))
        return false;

    return true;
}

/** Thu thập subnet broadcast từ interface LAN — ưu tiên interface mặc định của máy (Wi‑Fi/Ethernet đang dùng). */
inline juce::Array<LanScanTarget> collectLanScanTargets()
{
    juce::Array<LanScanTarget> targets;
    juce::StringArray seenBroadcasts;

    auto addInterface = [&] (const juce::IPAddress& iface)
    {
        if (! isUsableLanIpv4 (iface))
            return;

        const auto broadcast = juce::IPAddress::getInterfaceBroadcastAddress (iface);

        if (broadcast.isNull())
            return;

        const auto broadcastText = broadcast.toString();

        if (seenBroadcasts.contains (broadcastText, true))
            return;

        seenBroadcasts.add (broadcastText);

        LanScanTarget target;
        target.interfaceAddress = iface.toString();
        target.broadcastAddress = broadcastText;
        targets.add (target);
    };

    const auto preferred = juce::IPAddress::getLocalAddress (false);
    addInterface (preferred);

    for (const auto& addr : juce::IPAddress::getAllAddresses (false))
        addInterface (addr);

    return targets;
}

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

inline void sendDiscoverProbes (juce::DatagramSocket& socket,
                              const juce::Array<LanScanTarget>& targets,
                              int discoveryPort,
                              const juce::String& probe)
{
    const int bytes = (int) probe.getNumBytesAsUTF8();
    const char* data = probe.toRawUTF8();

    for (const auto& target : targets)
        socket.write (target.broadcastAddress, discoveryPort, data, bytes);

    // Wi‑Fi đôi khi rớt gói broadcast đầu — gửi thêm một vòng.
    for (const auto& target : targets)
        socket.write (target.broadcastAddress, discoveryPort, data, bytes);
}

inline juce::Array<LanPeerInfo> scanLanPeers (int wantRole,
                                              int syncPort,
                                              int timeoutMs = 3200)
{
    juce::Array<LanPeerInfo> results;
    const int discoveryPort = discoveryPortForSyncPort (syncPort);
    const auto targets      = collectLanScanTargets();

    if (targets.isEmpty())
        return results;

    juce::DatagramSocket socket (true);

    const auto& primaryTarget = targets.getReference (0);
    bool bound = socket.bindToPort (0, primaryTarget.interfaceAddress);

    if (! bound)
        bound = socket.bindToPort (0);

    if (! bound)
        return results;

    const int replyPort = socket.getBoundPort();
    const auto probe    = makeDiscoverProbe (wantRole, replyPort);

    sendDiscoverProbes (socket, targets, discoveryPort, probe);

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
