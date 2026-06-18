#pragma once

#include <JuceHeader.h>
#include "ShowBackupLanDiscovery.h"
#include "ShowBackupMacNetworkBridge.h"

namespace showcontrol::backup::mac
{
inline void openLocalNetworkPrivacySettings()
{
   #if JUCE_MAC
    showcue_mac_open_local_network_settings();
   #endif
}

inline void requestLocalNetworkPermissionPrompt()
{
   #if JUCE_MAC
    showcue_mac_request_local_network_prompt();

    const auto targets = showcontrol::backup::collectLanScanTargets();

    if (targets.isEmpty())
        return;

    juce::DatagramSocket socket (true);

    if (! socket.bindToPort (0, targets.getReference (0).interfaceAddress))
        socket.bindToPort (0);

    const int replyPort = socket.getBoundPort();
    const auto probe    = showcontrol::backup::makeDiscoverProbe (0, replyPort);
    const int discoveryPort = showcontrol::backup::discoveryPortForSyncPort (
        showcontrol::backup::kDefaultSyncPort);

    showcontrol::backup::sendDiscoverProbes (socket, targets, discoveryPort, probe);
   #endif
}

inline void startLanServiceAdvertiser (int discoveryPort)
{
   #if JUCE_MAC
    showcue_mac_start_bonjour_advertiser (discoveryPort);
   #else
    juce::ignoreUnused (discoveryPort);
   #endif
}

inline void stopLanServiceAdvertiser()
{
   #if JUCE_MAC
    showcue_mac_stop_bonjour_advertiser();
   #endif
}

} // namespace showcontrol::backup::mac
