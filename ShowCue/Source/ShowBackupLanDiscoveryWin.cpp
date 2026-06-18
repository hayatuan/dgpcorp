#include "ShowBackupLanDiscovery.h"

#if JUCE_WINDOWS

 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <winsock2.h>
 #include <iphlpapi.h>
 #include <ws2tcpip.h>
 #include <algorithm>

namespace showcontrol::backup
{
namespace
{
bool isActiveWindowsAdapter (const IP_ADAPTER_ADDRESSES* adapter) noexcept
{
    if (adapter == nullptr)
        return false;

    if (adapter->OperStatus != IfOperStatusUp)
        return false;

    switch (adapter->IfType)
    {
        case IF_TYPE_ETHERNET_CSMACD:
        case IF_TYPE_IEEE80211:
        case IF_TYPE_GIGABITETHERNET:
            return true;
        default:
            break;
    }

    return false;
}

bool isLikelyVirtualWindowsAdapter (const IP_ADAPTER_ADDRESSES* adapter) noexcept
{
    if (adapter == nullptr)
        return false;

    const auto name = juce::String (adapter->FriendlyName).toLowerCase();
    const auto desc = juce::String (adapter->Description).toLowerCase();

    static const char* kVirtualHints[] =
    {
        "vmware", "virtualbox", "vbox", "hyper-v", "vethernet", "wsl",
        "vpn", "tunnel", "pseudo", "docker", "npcap", "tap-", "bluetooth"
    };

    for (const auto* hint : kVirtualHints)
    {
        if (name.contains (hint) || desc.contains (hint))
            return true;
    }

    return false;
}

juce::String ipv4FromSockaddr (const SOCKADDR* sockaddr)
{
    if (sockaddr == nullptr || sockaddr->sa_family != AF_INET)
        return {};

    const auto* sin  = reinterpret_cast<const SOCKADDR_IN*> (sockaddr);
    const uint32_t raw = ntohl (sin->sin_addr.S_un.S_addr);
    return uint32ToIpv4 (raw);
}

bool hasUsableIpv4Gateway (const IP_ADAPTER_ADDRESSES* adapter) noexcept
{
    if (adapter == nullptr)
        return false;

    for (auto* gateway = adapter->FirstGatewayAddress; gateway != nullptr; gateway = gateway->Next)
    {
        const juce::String gatewayIp = ipv4FromSockaddr (gateway->Address.lpSockaddr);

        if (gatewayIp.isNotEmpty() && gatewayIp != "0.0.0.0")
            return true;
    }

    return false;
}

ULONG getWindowsBestRouteInterfaceIndex() noexcept
{
    const ULONG dest = inet_addr ("8.8.8.8");
    ULONG ifIndex    = 0;

    if (dest == INADDR_NONE || dest == INADDR_ANY)
        return 0;

    if (GetBestInterface (dest, &ifIndex) != NO_ERROR)
        return 0;

    return ifIndex;
}

struct WindowsLanCandidate
{
    LanScanTarget target;
    ULONG ifIndex       = 0;
    bool hasGateway     = false;
    bool likelyVirtual  = false;
    ULONG ifType        = 0;
    ULONG ipv4Metric    = ULONG_MAX;
};

int candidatePriority (const WindowsLanCandidate& candidate, ULONG bestRouteIfIndex) noexcept
{
    int score = 0;

    if (bestRouteIfIndex != 0 && candidate.ifIndex == bestRouteIfIndex)
        score += 10000;

    if (candidate.hasGateway)
        score += 1000;

    if (! candidate.likelyVirtual)
        score += 500;

    if (candidate.ifType == IF_TYPE_IEEE80211)
        score += 100;

    if (candidate.ipv4Metric != ULONG_MAX)
        score += (int) juce::jmax (0, 1000 - (int) candidate.ipv4Metric);

    return score;
}
} // namespace

juce::Array<LanScanTarget> collectWindowsLanScanTargets()
{
    juce::Array<LanScanTarget> targets;
    juce::Array<WindowsLanCandidate> candidates;
    juce::StringArray seenInterfaceIps;

    ULONG bufferSize = 16 * 1024;
    HeapBlock<IP_ADAPTER_ADDRESSES> buffer;
    DWORD result = ERROR_BUFFER_OVERFLOW;

    for (int attempt = 0; attempt < 4 && result == ERROR_BUFFER_OVERFLOW; ++attempt)
    {
        buffer.malloc (bufferSize, 1);
        result = GetAdaptersAddresses (AF_INET,
                                       GAA_FLAG_SKIP_ANYCAST
                                           | GAA_FLAG_SKIP_MULTICAST
                                           | GAA_FLAG_INCLUDE_PREFIX
                                           | GAA_FLAG_INCLUDE_GATEWAYS,
                                       nullptr,
                                       buffer,
                                       &bufferSize);
    }

    if (result != NO_ERROR)
        return targets;

    const ULONG bestRouteIfIndex = getWindowsBestRouteInterfaceIndex();

    for (auto* adapter = buffer.getData(); adapter != nullptr; adapter = adapter->Next)
    {
        if (! isActiveWindowsAdapter (adapter))
            continue;

        const bool likelyVirtual = isLikelyVirtualWindowsAdapter (adapter);
        const bool hasGateway    = hasUsableIpv4Gateway (adapter);

        for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
        {
            const juce::String interfaceIp = ipv4FromSockaddr (unicast->Address.lpSockaddr);

            if (interfaceIp.isEmpty() || ! isUsableLanIpv4 (juce::IPAddress (interfaceIp)))
                continue;

            if (seenInterfaceIps.contains (interfaceIp, true))
                continue;

            int prefix = (int) unicast->OnLinkPrefixLength;

            if (prefix <= 0 || prefix > 32)
                prefix = 24;

            const auto broadcastText = broadcastForInterfaceIpv4 (interfaceIp, prefix);

            if (broadcastText.isEmpty())
                continue;

            seenInterfaceIps.add (interfaceIp);

            WindowsLanCandidate candidate;
            candidate.target.interfaceAddress = interfaceIp;
            candidate.target.broadcastAddress = broadcastText;
            candidate.target.prefix           = prefix;
            candidate.ifIndex                 = adapter->IfIndex;
            candidate.hasGateway              = hasGateway;
            candidate.likelyVirtual           = likelyVirtual;
            candidate.ifType                  = adapter->IfType;
            candidate.ipv4Metric              = adapter->Ipv4Metric;
            candidates.add (candidate);
        }
    }

    if (candidates.isEmpty())
        return targets;

    std::stable_sort (candidates.begin(), candidates.end(),
                      [bestRouteIfIndex] (const WindowsLanCandidate& a, const WindowsLanCandidate& b)
                      {
                          return candidatePriority (a, bestRouteIfIndex)
                               > candidatePriority (b, bestRouteIfIndex);
                      });

    for (const auto& candidate : candidates)
        targets.add (candidate.target);

    return targets;
}

} // namespace showcontrol::backup

#endif
