#include "ShowBackupLanDiscovery.h"

#if JUCE_WINDOWS

 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <winsock2.h>
 #include <iphlpapi.h>
 #include <ws2tcpip.h>

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

juce::String ipv4FromSockaddr (const SOCKADDR* sockaddr)
{
    if (sockaddr == nullptr || sockaddr->sa_family != AF_INET)
        return {};

    const auto* sin = reinterpret_cast<const SOCKADDR_IN*> (sockaddr);
    const uint32_t raw = ntohl (sin->sin_addr.S_un.S_addr);
    return uint32ToIpv4 (raw);
}
} // namespace

juce::Array<LanScanTarget> collectWindowsLanScanTargets()
{
    juce::Array<LanScanTarget> targets;
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
                                           | GAA_FLAG_INCLUDE_PREFIX,
                                       nullptr,
                                       buffer,
                                       &bufferSize);
    }

    if (result != NO_ERROR)
        return targets;

    for (auto* adapter = buffer.getData(); adapter != nullptr; adapter = adapter->Next)
    {
        if (! isActiveWindowsAdapter (adapter))
            continue;

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

            LanScanTarget target;
            target.interfaceAddress = interfaceIp;
            target.broadcastAddress = broadcastText;
            targets.add (target);
        }
    }

    if (targets.size() > 1)
    {
        const auto preferred = juce::IPAddress::getLocalAddress (false).toString();

        for (int i = 0; i < targets.size(); ++i)
        {
            if (targets.getReference (i).interfaceAddress == preferred)
            {
                const auto preferredTarget = targets.getReference (i);
                targets.remove (i);
                targets.insert (0, preferredTarget);
                break;
            }
        }
    }

    return targets;
}

} // namespace showcontrol::backup

#endif
