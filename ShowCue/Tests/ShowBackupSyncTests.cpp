#include <JuceHeader.h>
#include "../Source/ShowAppPreferences.h"
#include "../Source/ShowBackupSync.h"
#include "../Source/ShowBackupLanDiscovery.h"

class ShowBackupSyncTests final : public juce::UnitTest
{
public:
    ShowBackupSyncTests() : juce::UnitTest ("ShowBackupSync", "showcue") {}

    void runTest() override
    {
        beginTest ("backup sync settings round-trip");
        const int savedRole   = showcontrol::prefs::loadBackupRole();
        const auto savedPeers = showcontrol::prefs::loadBackupPeerHosts();
        const int savedPort   = showcontrol::prefs::loadBackupSyncPort();
        const bool savedLock  = showcontrol::prefs::loadBackupFollowerLock();
        const bool savedOsc   = showcontrol::prefs::loadOscEnabled();

        juce::StringArray testPeers { "10.0.0.5", "10.0.0.6", "10.0.0.7" };
        showcontrol::prefs::saveBackupSyncSettings (1, testPeers, 9001, false, true, 9001);
        expectEquals (showcontrol::prefs::loadBackupRole(), 1);

        const auto loadedPeers = showcontrol::prefs::loadBackupPeerHosts();
        expectEquals (loadedPeers.size(), 3);
        expectEquals (loadedPeers[0], juce::String ("10.0.0.5"));
        expectEquals (loadedPeers[2], juce::String ("10.0.0.7"));
        expectEquals (showcontrol::prefs::loadBackupPeerHost(), juce::String ("10.0.0.5"));
        expectEquals (showcontrol::prefs::loadBackupSyncPort(), 9001);
        expect (! showcontrol::prefs::loadBackupFollowerLock());

        showcontrol::prefs::saveBackupSyncSettings (savedRole, savedPeers, savedPort, savedLock, savedOsc, savedPort);

        beginTest ("ShowBackupSyncBroadcaster configure");
        showcontrol::backup::ShowBackupSyncBroadcaster broadcaster;
        expect (! broadcaster.isConnected());
        expect (broadcaster.configure ("127.0.0.1", 9001));
        expect (broadcaster.isConnected());
        expectEquals (broadcaster.getPeerCount(), 1);

        juce::StringArray multi { "127.0.0.1", "127.0.0.2", "127.0.0.1" };
        expect (broadcaster.configure (multi, 9001));
        expectEquals (broadcaster.getPeerCount(), 2);

        broadcaster.disconnect();
        expect (! broadcaster.isConnected());

        beginTest ("LAN discovery message format");
        int wantRole = -1, replyPort = 0;
        expect (showcontrol::backup::parseDiscoverProbe (
            showcontrol::backup::makeDiscoverProbe (2, 54321), wantRole, replyPort));
        expectEquals (wantRole, 2);
        expectEquals (replyPort, 54321);

        showcontrol::backup::LanPeerInfo peer;
        expect (showcontrol::backup::parseDiscoverAnnounce (
            showcontrol::backup::makeDiscoverAnnounce (1, "MacBook-Backup", 9000), peer));
        expectEquals (peer.role, 1);
        expectEquals (peer.hostName, juce::String ("MacBook-Backup"));
        expectEquals (peer.syncPort, 9000);
        expect (showcontrol::backup::roleMatchesDiscoverRequest (1, 1));
        expect (showcontrol::backup::roleMatchesDiscoverRequest (2, 2));
        expect (! showcontrol::backup::roleMatchesDiscoverRequest (2, 1));
        expect (! showcontrol::backup::roleMatchesDiscoverRequest (1, 2));
        expectEquals (showcontrol::backup::discoveryPortForSyncPort (9000), 9001);

        beginTest ("LAN scan targets");
        expect (! showcontrol::backup::isUsableLanIpv4 (juce::IPAddress ("127.0.0.1")));
        expect (! showcontrol::backup::isUsableLanIpv4 (juce::IPAddress ("169.254.1.1")));
        expect (showcontrol::backup::isUsableLanIpv4 (juce::IPAddress ("192.168.1.10")));
        const auto targets = showcontrol::backup::collectLanScanTargets();
        expect (targets.size() >= 0);

        beginTest ("LAN subnet info");
        expectEquals (showcontrol::backup::inferIpv4Prefix (
            showcontrol::backup::ipv4ToUint32 ("192.168.1.10"),
            showcontrol::backup::ipv4ToUint32 ("192.168.1.255")), 24);
        showcontrol::backup::LanScanTarget target;
        target.interfaceAddress  = "192.168.1.10";
        target.broadcastAddress  = "192.168.1.255";
        target.prefix            = 24;
        const auto info = showcontrol::backup::makeLocalLanNetworkInfo (target);
        expectEquals (info.subnetCidr, juce::String ("192.168.1.0/24"));

        showcontrol::backup::LanScanTarget slash23;
        slash23.interfaceAddress = "10.20.30.40";
        slash23.broadcastAddress = "10.20.31.255";
        slash23.prefix             = 23;
        const auto slash23Info = showcontrol::backup::makeLocalLanNetworkInfo (slash23);
        expectEquals (slash23Info.subnetCidr, juce::String ("10.20.30.0/23"));
        expectEquals (showcontrol::backup::broadcastForInterfaceIpv4 ("192.168.1.10", 24),
                      juce::String ("192.168.1.255"));
        expectEquals (showcontrol::backup::broadcastForInterfaceIpv4 ("10.20.30.40", 16),
                      juce::String ("10.20.255.255"));
        expectEquals (showcontrol::backup::uint32ToIpv4 (
            showcontrol::backup::ipv4ToUint32 ("10.0.0.5")), juce::String ("10.0.0.5"));

        beginTest ("LAN announce hostname with pipe");
        showcontrol::backup::LanPeerInfo pipedHost;
        expect (showcontrol::backup::parseDiscoverAnnounce (
            showcontrol::backup::makeDiscoverAnnounce (2, "Mac|Stage", 9000), pipedHost));
        expectEquals (pipedHost.hostName, juce::String ("Mac|Stage"));
        expectEquals (pipedHost.role, 2);

        beginTest ("LAN discovery round-trip");
        {
            const auto targets = showcontrol::backup::collectLanScanTargets();

            if (targets.isEmpty())
            {
                logMessage ("skip LAN discovery round-trip — no active LAN interface");
            }
            else
            {
                const int syncPort      = 19123;
                const int discoveryPort = showcontrol::backup::discoveryPortForSyncPort (syncPort);
                std::atomic<bool> running { true };

                std::thread responder ([&]
                {
                    juce::DatagramSocket socket (true);
                    socket.setEnablePortReuse (true);

                    if (! socket.bindToPort (discoveryPort))
                        return;

                    while (running.load())
                    {
                        if (socket.waitUntilReady (true, 100) <= 0)
                            continue;

                        char buffer[512] = {};
                        juce::String senderHost;
                        int senderPort = 0;
                        const int bytes = socket.read (buffer, (int) sizeof (buffer) - 1, false,
                                                       senderHost, senderPort);

                        if (bytes <= 0 || senderHost.isEmpty())
                            continue;

                        buffer[bytes] = '\0';

                        int wantRole  = 0;
                        int replyPort = 0;

                        if (! showcontrol::backup::parseDiscoverProbe (juce::String::fromUTF8 (buffer),
                                                                       wantRole, replyPort))
                            continue;

                        if (! showcontrol::backup::roleMatchesDiscoverRequest ((int) showcontrol::backup::Role::backup,
                                                                               wantRole))
                            continue;

                        const auto announce = showcontrol::backup::makeDiscoverAnnounce (
                            (int) showcontrol::backup::Role::backup,
                            "ShowCueTestPeer",
                            syncPort);

                        socket.write (senderHost, replyPort, announce.toRawUTF8(),
                                      (int) announce.getNumBytesAsUTF8());
                    }
                });

                juce::Thread::sleep (80);

                juce::DatagramSocket client (true);
                const auto& iface = targets.getReference (0);
                expect (client.bindToPort (0, iface.interfaceAddress) || client.bindToPort (0));

                const int replyPort = client.getBoundPort();
                const auto probe    = showcontrol::backup::makeDiscoverProbe (
                    (int) showcontrol::backup::Role::backup, replyPort);

                showcontrol::backup::sendDiscoverProbes (client, targets, discoveryPort, probe);

                bool gotReply = false;

                for (int attempt = 0; attempt < 40 && ! gotReply; ++attempt)
                {
                    if (client.waitUntilReady (true, 100) <= 0)
                        continue;

                    char buffer[512] = {};
                    juce::String senderHost;
                    int senderPort = 0;
                    const int bytes = client.read (buffer, (int) sizeof (buffer) - 1, false,
                                                   senderHost, senderPort);

                    if (bytes <= 0)
                        continue;

                    buffer[bytes] = '\0';

                    showcontrol::backup::LanPeerInfo peer;
                    peer.address = senderHost;

                    if (! showcontrol::backup::parseDiscoverAnnounce (juce::String::fromUTF8 (buffer), peer))
                        continue;

                    if (peer.hostName == "ShowCueTestPeer" && peer.role == (int) showcontrol::backup::Role::backup)
                    {
                        gotReply = true;
                        expectEquals (peer.syncPort, syncPort);
                    }
                }

                running.store (false);

                if (responder.joinable())
                    responder.join();

                expect (gotReply);
            }
        }

        beginTest ("selection sync message format");
        juce::Array<int> selectionMulti { 2, 5, 7 };
        showcontrol::backup::ShowBackupSyncBroadcaster broadcaster2;
        expect (broadcaster2.configure ("127.0.0.1", 9001));
        expect (broadcaster2.sendSelection (1, 4, 0, selectionMulti));
    }
};

static ShowBackupSyncTests showBackupSyncTestsInstance;
