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
    /** Thời gian phản hồi discovery (ms); -1 = chưa đo. */
    int discoveryMs = -1;
};

/** Một interface LAN dùng để gửi UDP broadcast discovery (ưu tiên Wi‑Fi / interface đang active). */
struct LanScanTarget
{
    juce::String interfaceAddress;
    juce::String broadcastAddress;
    /** Prefix CIDR từ hệ điều hành; 0 = suy ra từ IP + broadcast. */
    int prefix = 0;
};

/** Thông tin mạng LAN của interface đang dùng (hiển thị tab Mạng). */
struct LocalLanNetworkInfo
{
    juce::String ip;
    juce::String broadcast;
    juce::String subnetBase;
    juce::String subnetCidr;
    int prefix = 0;
};

inline uint32_t ipv4ToUint32 (const juce::String& text) noexcept
{
    juce::StringArray parts;
    parts.addTokens (text, ".", {});

    if (parts.size() != 4)
        return 0;

    uint32_t value = 0;

    for (int i = 0; i < 4; ++i)
    {
        const int octet = parts[i].getIntValue();

        if (octet < 0 || octet > 255)
            return 0;

        value = (value << 8) | (uint32_t) octet;
    }

    return value;
}

inline juce::String uint32ToIpv4 (uint32_t value)
{
    return juce::String ((int) ((value >> 24) & 0xff)) + "."
         + juce::String ((int) ((value >> 16) & 0xff)) + "."
         + juce::String ((int) ((value >> 8) & 0xff)) + "."
         + juce::String ((int) (value & 0xff));
}

inline int inferIpv4Prefix (uint32_t iface, uint32_t broadcast) noexcept
{
    if (iface == 0 || broadcast == 0)
        return 0;

    for (int prefix = 32; prefix >= 8; --prefix)
    {
        const uint32_t mask     = (prefix == 0) ? 0u : (~0u << (32 - prefix));
        const uint32_t network  = iface & mask;
        const uint32_t expected = network | ~mask;

        if (expected == broadcast)
            return prefix;
    }

    return 24;
}

inline LocalLanNetworkInfo makeLocalLanNetworkInfo (const LanScanTarget& target)
{
    LocalLanNetworkInfo info;
    info.ip         = target.interfaceAddress;
    info.broadcast  = target.broadcastAddress;

    const uint32_t iface = ipv4ToUint32 (info.ip);
    const uint32_t bcast = ipv4ToUint32 (info.broadcast);

    if (target.prefix > 0 && target.prefix <= 32)
        info.prefix = target.prefix;
    else
        info.prefix = inferIpv4Prefix (iface, bcast);

    if (info.prefix > 0)
    {
        const uint32_t mask    = (~0u << (32 - info.prefix));
        const uint32_t network = iface & mask;
        info.subnetBase        = uint32ToIpv4 (network);
        info.subnetCidr        = info.subnetBase + "/" + juce::String (info.prefix);
    }
    else
    {
        info.subnetBase = info.ip;
        info.subnetCidr = info.ip;
    }

    return info;
}

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

inline juce::String broadcastForInterfaceIpv4 (const juce::String& interfaceIp, int prefix = 24)
{
    if (interfaceIp.isEmpty())
        return {};

    const juce::IPAddress iface (interfaceIp);
    const auto juceBroadcast = juce::IPAddress::getInterfaceBroadcastAddress (iface);

    if (! juceBroadcast.isNull())
        return juceBroadcast.toString();

    const uint32_t address = ipv4ToUint32 (interfaceIp);

    if (address == 0)
        return {};

    const int usePrefix = juce::jlimit (8, 32, prefix);
    const uint32_t mask   = (usePrefix == 32) ? 0xffffffffu : (~0u << (32 - usePrefix));
    return uint32ToIpv4 (address | ~mask);
}

#if JUCE_WINDOWS
juce::Array<LanScanTarget> collectWindowsLanScanTargets();
#endif

/** Thu thập subnet broadcast từ interface LAN — ưu tiên interface mặc định của máy (Wi‑Fi/Ethernet đang dùng). */
inline juce::Array<LanScanTarget> collectLanScanTargets()
{
   #if JUCE_WINDOWS
    {
        const auto windowsTargets = collectWindowsLanScanTargets();

        if (windowsTargets.size() > 0)
            return windowsTargets;
    }
   #endif

    juce::Array<LanScanTarget> targets;
    juce::StringArray seenInterfaceIps;

    auto addInterface = [&] (const juce::IPAddress& iface, int prefix = 24)
    {
        if (! isUsableLanIpv4 (iface))
            return;

        const auto interfaceText = iface.toString();

        if (seenInterfaceIps.contains (interfaceText, true))
            return;

        const auto broadcastText = broadcastForInterfaceIpv4 (interfaceText, prefix);

        if (broadcastText.isEmpty())
            return;

        seenInterfaceIps.add (interfaceText);

        LanScanTarget target;
        target.interfaceAddress  = interfaceText;
        target.broadcastAddress  = broadcastText;
        target.prefix            = prefix;
        targets.add (target);
    };

    const auto preferred = juce::IPAddress::getLocalAddress (false);
    addInterface (preferred);

    for (const auto& addr : juce::IPAddress::getAllAddresses (false))
        addInterface (addr);

    return targets;
}

/** Cache ngắn cho announce định kỳ — tránh gọi GetAdaptersAddresses trên UI thread (Windows). */
inline juce::Array<LanScanTarget> collectLanScanTargetsCached (int maxAgeMs = 8000)
{
    static juce::Array<LanScanTarget> cached;
    static int lastRefreshMs = 0;
    const int now            = (int) juce::Time::getMillisecondCounter();

    if (cached.isEmpty() || now - lastRefreshMs >= maxAgeMs)
    {
        cached         = collectLanScanTargets();
        lastRefreshMs  = now;
    }

    return cached;
}

inline LocalLanNetworkInfo getPrimaryLocalLanNetworkInfo()
{
    const auto targets = collectLanScanTargets();

    if (targets.isEmpty())
        return {};

    return makeLocalLanNetworkInfo (targets.getReference (0));
}

/** IPv4 interface đang dùng cho bind socket LAN (OSC/discovery) — tránh VMware/virtual trên Windows. */
inline juce::String getLocalLanBindAddress()
{
    return getPrimaryLocalLanNetworkInfo().ip;
}

inline juce::String describeLocalLanNetwork (const LocalLanNetworkInfo& info)
{
    if (info.ip.isEmpty())
        return {};

    juce::String text = info.ip;

    if (info.subnetCidr.isNotEmpty())
        text += " · " + info.subnetCidr;

    if (info.broadcast.isNotEmpty())
        text += " · BC " + info.broadcast;

    return text;
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
    out.syncPort = parts[parts.size() - 1].getIntValue();

    if (parts.size() == 4)
    {
        out.hostName = parts[2];
    }
    else
    {
        juce::StringArray hostParts;

        for (int i = 2; i < parts.size() - 1; ++i)
            hostParts.add (parts[i]);

        out.hostName = hostParts.joinIntoString ("|");
    }

    return out.syncPort > 0;
}

inline bool roleMatchesDiscoverRequest (int ourRole, int wantRole) noexcept
{
    if (wantRole == 0)
        return ourRole == (int) Role::primary || ourRole == (int) Role::backup;

    return ourRole == wantRole;
}

inline void sendUnicastDiscoverSweep (juce::DatagramSocket& socket,
                                      const LanScanTarget& target,
                                      int discoveryPort,
                                      const char* data,
                                      int bytes)
{
    const uint32_t iface = ipv4ToUint32 (target.interfaceAddress);

    if (iface == 0)
        return;

    const int prefix = juce::jlimit (8, 30, target.prefix > 0 ? target.prefix : 24);
    const uint32_t mask     = (~0u << (32 - prefix));
    const uint32_t network  = iface & mask;
    const uint32_t bcast    = network | ~mask;

    for (uint32_t addr = network + 1; addr < bcast; ++addr)
    {
        if (addr == iface)
            continue;

        socket.write (uint32ToIpv4 (addr), discoveryPort, data, bytes);
    }
}

inline void sendDiscoverProbes (juce::DatagramSocket& socket,
                              const juce::Array<LanScanTarget>& targets,
                              int discoveryPort,
                              const juce::String& probe,
                              bool includeSubnetSweep = false)
{
    const int bytes = (int) probe.getNumBytesAsUTF8();
    const char* data = probe.toRawUTF8();

    for (const auto& target : targets)
    {
        socket.write (target.broadcastAddress, discoveryPort, data, bytes);
        socket.write ("255.255.255.255", discoveryPort, data, bytes);

        if (includeSubnetSweep)
            sendUnicastDiscoverSweep (socket, target, discoveryPort, data, bytes);
    }

    for (const auto& target : targets)
    {
        socket.write (target.broadcastAddress, discoveryPort, data, bytes);
        socket.write ("255.255.255.255", discoveryPort, data, bytes);
    }
}

inline void broadcastLanAnnounce (juce::DatagramSocket& socket,
                                  int role,
                                  const juce::String& hostName,
                                  int syncPort)
{
    const auto announce     = makeDiscoverAnnounce (role, hostName, syncPort);
    const int discoveryPort = discoveryPortForSyncPort (syncPort);
    const int bytes         = (int) announce.getNumBytesAsUTF8();
    const char* data        = announce.toRawUTF8();
    const auto targets      = collectLanScanTargetsCached();

    for (const auto& target : targets)
    {
        socket.write (target.broadcastAddress, discoveryPort, data, bytes);
        socket.write ("255.255.255.255", discoveryPort, data, bytes);
    }
}

inline juce::Array<LanPeerInfo> scanLanPeers (int wantRole,
                                              int syncPort,
                                              int timeoutMs = 4800)
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
    const int probeStartMs = (int) juce::Time::getMillisecondCounter();

    sendDiscoverProbes (socket, targets, discoveryPort, probe, false);

    const int deadline      = (int) juce::Time::getMillisecondCounter() + timeoutMs;
    const int reprobeAt     = (int) juce::Time::getMillisecondCounter() + timeoutMs / 2;
    bool reprobeSent        = false;
    bool sweepSent          = false;

    while ((int) juce::Time::getMillisecondCounter() < deadline)
    {
        if (! reprobeSent && (int) juce::Time::getMillisecondCounter() >= reprobeAt)
        {
            reprobeSent = true;
            sendDiscoverProbes (socket, targets, discoveryPort, probe, results.isEmpty());
        }

        if (! sweepSent && results.isEmpty()
            && (int) juce::Time::getMillisecondCounter() >= reprobeAt + 400)
        {
            sweepSent = true;
            sendDiscoverProbes (socket, targets, discoveryPort, probe, true);
        }

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

        const auto localInfo = getPrimaryLocalLanNetworkInfo();

        if (localInfo.ip.isNotEmpty() && peer.address == localInfo.ip)
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
        {
            peer.discoveryMs = juce::jmax (1,
                (int) juce::Time::getMillisecondCounter() - probeStartMs);
            results.add (peer);
        }
    }

    return results;
}

/** Gửi probe unicast tới một máy — trả về độ trễ ms hoặc -1 nếu không phản hồi. */
inline int pingShowCuePeer (const juce::String& address,
                            int syncPort,
                            int wantRole,
                            int timeoutMs = 400)
{
    if (address.trim().isEmpty())
        return -1;

    const int discoveryPort = discoveryPortForSyncPort (syncPort);
    const auto targets      = collectLanScanTargetsCached();

    if (targets.isEmpty())
        return -1;

    juce::DatagramSocket socket (true);

    const auto& primaryTarget = targets.getReference (0);
    bool bound = socket.bindToPort (0, primaryTarget.interfaceAddress);

    if (! bound)
        bound = socket.bindToPort (0);

    if (! bound)
        return -1;

    const int replyPort = socket.getBoundPort();
    const auto probe    = makeDiscoverProbe (wantRole, replyPort);
    const int bytes     = (int) probe.getNumBytesAsUTF8();
    const char* data    = probe.toRawUTF8();
    const int startMs   = (int) juce::Time::getMillisecondCounter();

    socket.write (address.trim(), discoveryPort, data, bytes);

    const int deadline = startMs + timeoutMs;

    while ((int) juce::Time::getMillisecondCounter() < deadline)
    {
        if (socket.waitUntilReady (true, 50) <= 0)
            continue;

        char buffer[512] = {};
        juce::String senderHost;
        int senderPort = 0;
        const int readBytes = socket.read (buffer, (int) sizeof (buffer) - 1, false,
                                           senderHost, senderPort);

        if (readBytes <= 0)
            continue;

        buffer[readBytes] = '\0';

        LanPeerInfo peer;

        if (! parseDiscoverAnnounce (juce::String::fromUTF8 (buffer), peer))
            continue;

        if (wantRole != 0 && peer.role != wantRole)
            continue;

        if (senderHost != address.trim())
            continue;

        return juce::jmax (1, (int) juce::Time::getMillisecondCounter() - startMs);
    }

    return -1;
}

} // namespace showcontrol::backup
