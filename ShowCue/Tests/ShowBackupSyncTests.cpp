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

        beginTest ("selection sync message format");
        juce::Array<int> selectionMulti { 2, 5, 7 };
        showcontrol::backup::ShowBackupSyncBroadcaster broadcaster2;
        expect (broadcaster2.configure ("127.0.0.1", 9001));
        expect (broadcaster2.sendSelection (1, 4, 0, selectionMulti));
    }
};

static ShowBackupSyncTests showBackupSyncTestsInstance;
