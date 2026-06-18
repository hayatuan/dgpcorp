#pragma once

#include <JuceHeader.h>
#include "ShowLocalization.h"
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowAppPreferences.h"
#include "ShowBackupSync.h"
#include "ShowBackupLanDiscovery.h"

/** Tab Mạng / Backup — vai trò Primary/Backup, quét LAN, danh sách máy dự phòng. */
class BackupSyncPreferencesPanel : public juce::Component,
                                     private juce::Timer
{
public:
    std::function<void()> onSettingsChanged;
    std::function<void (int wantRole, std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone)> onScanLanPeers;
    std::function<juce::Array<showcontrol::backup::PeerRuntimeStatus>()> queryPeerRuntimeStatus;

    BackupSyncPreferencesPanel()
        : scanResultsModel (*this),
          activePeersModel (*this)
    {
        roleLabel.setFont (showcontrol::preferences::sectionLabelFont());
        roleLabel.setColour (juce::Label::textColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (roleLabel);
        addAndMakeVisible (roleCombo);
        roleCombo.onChange = [this]
        {
            refreshRoleUi();
            refreshTakeoverButton();
            notifyChanged();
        };

        localMachineTitle.setFont (showcontrol::preferences::sectionLabelFont());
        addChildComponent (localMachineTitle);

        localMachineInfoLabel.setFont (juce::FontOptions (12.5f));
        localMachineInfoLabel.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (localMachineInfoLabel);

        scanSectionLabel.setFont (showcontrol::preferences::sectionLabelFont());
        addChildComponent (scanSectionLabel);

        scanPeerBtn.onClick = [this] { startLanScan(); };
        addChildComponent (scanPeerBtn);

        scanResultsList.setModel (&scanResultsModel);
        scanResultsList.setRowHeight (26);
        scanResultsList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        scanResultsList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addChildComponent (scanResultsList);

        connectBtn.onClick = [this] { connectSelectedPeers(); };
        addChildComponent (connectBtn);

        activePeersLabel.setFont (showcontrol::preferences::sectionLabelFont());
        addChildComponent (activePeersLabel);

        activePeersList.setModel (&activePeersModel);
        activePeersList.setRowHeight (28);
        activePeersList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        activePeersList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addChildComponent (activePeersList);

        removePeerBtn.onClick = [this] { removeSelectedActivePeer(); };
        addChildComponent (removePeerBtn);

        peerEditor.setVisible (false);
        addChildComponent (peerEditor);
        peerEditor.onTextChange = [this]
        {
            if (isBackupRole())
                notifyChanged();
        };

        portEditor.setInputRestrictions (5, "0123456789");
        portEditor.onTextChange = [this]
        {
            refreshLocalMachineDisplay();
            notifyChanged();
        };
        addChildComponent (portLabel);
        addChildComponent (portEditor);

        followerLockToggle.onClick = [this] { notifyChanged(); };
        addAndMakeVisible (followerLockToggle);

        oscEnableToggle.onClick = [this] { notifyChanged(); };
        addAndMakeVisible (oscEnableToggle);

        helpLabel.setFont (juce::FontOptions (12.0f));
        helpLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (helpLabel);

        takeoverBtn.onClick = [this]
        {
            takeoverActive = ! takeoverActive;
            refreshTakeoverButton();
            if (onSettingsChanged != nullptr)
                onSettingsChanged();
        };
        addAndMakeVisible (takeoverBtn);

        loadFromPreferences();
        refreshLocalizedText();
        refreshLocalMachineDisplay();
        refreshRoleUi();
        refreshTakeoverButton();
        startTimerHz (1);
    }

    void loadFromPreferences()
    {
        const int role = showcontrol::prefs::loadBackupRole();
        roleCombo.setSelectedId (role + 1, juce::dontSendNotification);

        configuredPeerEntries.clear();
        const auto hosts = showcontrol::prefs::loadBackupPeerHosts();

        for (const auto& host : hosts)
        {
            ConfiguredPeerEntry entry;
            entry.ip = host;
            configuredPeerEntries.add (entry);
        }

        if (isBackupRole() && configuredPeerEntries.size() > 0)
            peerEditor.setText (configuredPeerEntries.getReference (0).ip, juce::dontSendNotification);

        portEditor.setText (juce::String (showcontrol::prefs::loadBackupSyncPort()), juce::dontSendNotification);
        followerLockToggle.setToggleState (showcontrol::prefs::loadBackupFollowerLock(), juce::dontSendNotification);
        oscEnableToggle.setToggleState (showcontrol::prefs::loadOscEnabled(), juce::dontSendNotification);
        clearScanResults();
        refreshActivePeerList();
    }

    void saveToPreferences() const
    {
        const int role = juce::jlimit (0, 2, roleCombo.getSelectedId() - 1);
        const int port = portEditor.getText().getIntValue();

        juce::StringArray peers;

        if (role == (int) showcontrol::backup::Role::backup)
        {
            if (peerEditor.getText().trim().isNotEmpty())
                peers.add (peerEditor.getText().trim());
            else if (configuredPeerEntries.size() > 0)
                peers.add (configuredPeerEntries.getReference (0).ip);
        }
        else if (role == (int) showcontrol::backup::Role::primary)
        {
            for (const auto& entry : configuredPeerEntries)
                peers.add (entry.ip);
        }

        showcontrol::prefs::saveBackupSyncSettings (
            role,
            peers,
            port > 0 ? port : (int) showcontrol::backup::kDefaultSyncPort,
            followerLockToggle.getToggleState(),
            oscEnableToggle.getToggleState(),
            port > 0 ? port : (int) showcontrol::backup::kDefaultSyncPort);
    }

    bool isTakeoverActive() const noexcept { return takeoverActive; }

    void setTakeoverActive (bool active)
    {
        takeoverActive = active;
        refreshTakeoverButton();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));

        if (! localMachineCardBounds.isEmpty())
        {
            const auto cardBg = findColour (juce::TextEditor::backgroundColourId).withAlpha (0.28f);
            const auto border = findColour (juce::Label::outlineColourId).withAlpha (0.28f);

            g.setColour (cardBg);
            g.fillRoundedRectangle (localMachineCardBounds.toFloat(), 6.0f);
            g.setColour (border);
            g.drawRoundedRectangle (localMachineCardBounds.toFloat().reduced (0.5f), 6.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20, 14);
        const int rowH = 28;
        const int gap  = 8;
        const int compactGap = 4;

        roleLabel.setBounds (area.removeFromTop (20));
        area.removeFromTop (compactGap);
        roleCombo.setBounds (area.removeFromTop (rowH));
        area.removeFromTop (gap);

        const bool showNetworkUi = isPrimaryRole() || isBackupRole();

        localMachineTitle.setVisible (showNetworkUi);
        localMachineInfoLabel.setVisible (showNetworkUi);

        if (showNetworkUi)
        {
            localMachineTitle.setBounds (area.removeFromTop (18));
            area.removeFromTop (compactGap);

            localMachineCardBounds = area.removeFromTop (30);
            localMachineInfoLabel.setBounds (localMachineCardBounds.reduced (12, 6));
            area.removeFromTop (gap);

            auto scanRow = area.removeFromTop (rowH);

            if (removePeerBtn.isVisible())
            {
                removePeerBtn.setBounds (scanRow.removeFromRight (58));
                scanRow.removeFromRight (compactGap);
            }

            if (scanPeerBtn.isVisible())
            {
                scanPeerBtn.setBounds (scanRow.removeFromRight (104));
                scanRow.removeFromRight (compactGap);
            }

            portEditor.setBounds (scanRow.removeFromRight (52));
            scanRow.removeFromRight (compactGap);

            const int portLabelW = juce::jmin (132,
                juce::GlyphArrangement::getStringWidthInt (portLabel.getFont(), portLabel.getText()) + 6);
            portLabel.setBounds (scanRow.removeFromRight (juce::jmin (portLabelW, scanRow.getWidth())));
            scanRow.removeFromRight (compactGap);
            scanSectionLabel.setBounds (scanRow);

            area.removeFromTop (compactGap);

            if (scanResultsVisible)
            {
                const int scanListH = juce::jlimit (52, 150, scanResults.size() * 26 + 4);
                scanResultsList.setBounds (area.removeFromTop (scanListH));
                area.removeFromTop (compactGap);
                connectBtn.setBounds (area.removeFromTop (rowH));
                area.removeFromTop (gap);
            }

            activePeersLabel.setBounds (area.removeFromTop (18));
            area.removeFromTop (compactGap);

            const int peerListMinH = isPrimaryRole() ? 72 : 34;
            const int peerListH = juce::jmax (peerListMinH, configuredPeerEntries.size() * 28 + 4);
            activePeersList.setBounds (area.removeFromTop (juce::jmin (peerListH, juce::jmax (peerListMinH, area.getHeight() / 3))));
            area.removeFromTop (gap);
        }
        else
        {
            localMachineCardBounds = {};

            auto portRow = area.removeFromTop (rowH);
            portEditor.setBounds (portRow.removeFromRight (72));
            portRow.removeFromRight (compactGap);
            portLabel.setBounds (portRow);
            area.removeFromTop (gap);
        }

        followerLockToggle.setBounds (area.removeFromTop (rowH));
        area.removeFromTop (compactGap);
        oscEnableToggle.setBounds (area.removeFromTop (rowH));
        area.removeFromTop (gap);

        if (takeoverBtn.isVisible())
        {
            takeoverBtn.setBounds (area.removeFromTop (32));
            area.removeFromTop (gap);
        }

        helpLabel.setBounds (area.removeFromTop (juce::jmax (56, area.getHeight())));
    }

    void refreshSectionLabelColours() { refreshLocalizedText(); }

    void refreshNetworkInfo()
    {
        refreshLocalMachineDisplay();
        refreshActivePeerList();
    }

    void refreshLocalizedText()
    {
        roleLabel.setText (showcontrol::localization::tr (u8"Vai trò máy"), juce::dontSendNotification);

        const int roleIndex = roleCombo.getSelectedId() > 0
            ? juce::jlimit (0, 2, roleCombo.getSelectedId() - 1)
            : showcontrol::prefs::loadBackupRole();

        roleCombo.clear (juce::dontSendNotification);
        roleCombo.addItem (showcontrol::localization::tr (u8"Độc lập (Standalone)"), 1);
        roleCombo.addItem (showcontrol::localization::tr (u8"Máy chính (Primary)"), 2);
        roleCombo.addItem (showcontrol::localization::tr (u8"Máy phụ (Backup)"), 3);
        roleCombo.setSelectedId (roleIndex + 1, juce::dontSendNotification);

        localMachineTitle.setText (showcontrol::localization::tr (u8"Máy này"), juce::dontSendNotification);

        scanSectionLabel.setText (showcontrol::localization::tr (u8"Quét mạng LAN"), juce::dontSendNotification);
        scanPeerBtn.setButtonText (showcontrol::localization::tr (u8"Quét LAN..."));
        connectBtn.setButtonText (showcontrol::localization::tr (u8"Kết nối"));

        portLabel.setText (showcontrol::localization::tr (u8"Cổng đồng bộ UDP"), juce::dontSendNotification);

        if (isPrimaryRole())
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy dự phòng đang chạy"), juce::dontSendNotification);
        else if (isBackupRole())
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy chính đã kết nối"), juce::dontSendNotification);
        else
            activePeersLabel.setText ({}, juce::dontSendNotification);

        removePeerBtn.setButtonText (showcontrol::localization::tr (u8"Gỡ"));

        followerLockToggle.setButtonText (showcontrol::localization::tr (
            u8"Khóa điều khiển local trên máy phụ (Follower)"));
        oscEnableToggle.setButtonText (showcontrol::localization::tr (u8"Bật nhận OSC / đồng bộ LAN"));
        helpLabel.setText (showcontrol::localization::tr (
            u8"Chọn vai trò → xem IP/Subnet/Port máy này → Quét LAN → chọn máy → Kết nối.\n"
            u8"Máy chính điều khiển nhiều máy phụ; máy phụ mirror vị trí chọn và GO/Stop/Panic.\n"
            u8"Trạng thái kết nối hiển thị trên màn hình chính (xanh / vàng / đỏ)."),
            juce::dontSendNotification);
        takeoverBtn.setButtonText (showcontrol::localization::tr (u8"Takeover — điều khiển local (máy phụ)"));

        refreshLocalMachineDisplay();
        refreshTakeoverButton();
        scanResultsList.updateContent();
        activePeersList.updateContent();

        const auto col = getLookAndFeel().findColour (juce::Label::textColourId);
        roleLabel.setColour (juce::Label::textColourId, col);
        localMachineTitle.setColour (juce::Label::textColourId, col);
        localMachineInfoLabel.setColour (juce::Label::textColourId, col);
        scanSectionLabel.setColour (juce::Label::textColourId, col);
        activePeersLabel.setColour (juce::Label::textColourId, col);
        portLabel.setColour (juce::Label::textColourId, col);
        helpLabel.setColour (juce::Label::textColourId, col.withAlpha (0.75f));
    }

private:
    struct ScanResultEntry
    {
        juce::String address;
        juce::String hostName;
        int discoveryMs = -1;
        bool selected = false;
    };

    struct ConfiguredPeerEntry
    {
        juce::String ip;
        juce::String hostName;
    };

    class ScanResultsListModel final : public juce::ListBoxModel
    {
    public:
        explicit ScanResultsListModel (BackupSyncPreferencesPanel& ownerIn) : owner (ownerIn) {}

        int getNumRows() override { return owner.scanResults.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
        {
            if (! juce::isPositiveAndBelow (row, owner.scanResults.size()))
                return;

            const auto& entry = owner.scanResults.getReference (row);
            const auto col = owner.findColour (juce::Label::textColourId);

            if (rowIsSelected)
                g.fillAll (owner.findColour (juce::TextEditor::highlightColourId).withAlpha (0.18f));

            const int boxSize = 14;
            const int boxX = 8;
            const int boxY = (height - boxSize) / 2;
            g.setColour (entry.selected ? juce::Colour (0xff3ecf6a) : col.withAlpha (0.25f));
            g.fillRoundedRectangle ((float) boxX, (float) boxY, (float) boxSize, (float) boxSize, 3.0f);
            g.setColour (col.withAlpha (0.55f));
            g.drawRoundedRectangle ((float) boxX, (float) boxY, (float) boxSize, (float) boxSize, 3.0f, 1.0f);

            if (entry.selected)
            {
                g.setColour (juce::Colours::white);
                g.drawLine ((float) boxX + 3.0f, (float) boxY + 7.0f,
                            (float) boxX + 6.0f, (float) boxY + 10.0f, 1.6f);
                g.drawLine ((float) boxX + 6.0f, (float) boxY + 10.0f,
                            (float) boxX + 11.0f, (float) boxY + 4.0f, 1.6f);
            }

            const int nameX = 30;
            const int latencyW = 56;
            const int ipW = 108;
            const int nameW = width - nameX - ipW - latencyW - 10;

            g.setFont (ShowTheme::fontBold (12.0f));
            g.setColour (col);
            const auto displayName = entry.hostName.isNotEmpty() ? entry.hostName : entry.address;
            g.drawText (displayName, nameX, 0, nameW, height, juce::Justification::centredLeft, true);

            g.setFont (ShowTheme::font (11.5f));
            g.setColour (col.withAlpha (0.82f));
            g.drawText (entry.address, nameX + nameW, 0, ipW, height, juce::Justification::centredLeft, true);

            juce::String latency = "—";
            if (entry.discoveryMs > 0)
                latency = juce::String (entry.discoveryMs) + " ms";

            g.setColour (col.withAlpha (0.68f));
            g.drawText (latency, width - latencyW - 6, 0, latencyW, height, juce::Justification::centredRight, true);
        }

        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            if (! juce::isPositiveAndBelow (row, owner.scanResults.size()))
                return;

            if (e.x < 28)
            {
                auto& entry = owner.scanResults.getReference (row);
                entry.selected = ! entry.selected;
                owner.scanResultsList.repaintRow (row);
            }
        }
    private:
        BackupSyncPreferencesPanel& owner;
    };

    class ActivePeersListModel final : public juce::ListBoxModel
    {
    public:
        explicit ActivePeersListModel (BackupSyncPreferencesPanel& ownerIn) : owner (ownerIn) {}

        int getNumRows() override { return owner.configuredPeerEntries.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
        {
            if (! juce::isPositiveAndBelow (row, owner.configuredPeerEntries.size()))
                return;

            const auto& entry = owner.configuredPeerEntries.getReference (row);
            const auto col = owner.findColour (juce::Label::textColourId);
            const auto status = owner.lookupRuntimeStatus (entry.ip);

            if (rowIsSelected)
                g.fillAll (owner.findColour (juce::TextEditor::highlightColourId).withAlpha (0.22f));

            const float dotR = 5.0f;
            const float dotX = 10.0f;
            const float dotY = (float) height * 0.5f;
            g.setColour (owner.linkQualityColour (status.quality));
            g.fillEllipse (dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

            const int nameX = 24;
            const int latencyW = 56;
            const int ipW = 108;
            const int nameW = width - nameX - ipW - latencyW - 10;

            g.setFont (ShowTheme::fontBold (12.0f));
            g.setColour (col);
            const auto displayName = entry.hostName.isNotEmpty() ? entry.hostName
                                 : (status.hostName.isNotEmpty() ? status.hostName : entry.ip);
            g.drawText (displayName, nameX, 0, nameW, height, juce::Justification::centredLeft, true);

            g.setFont (ShowTheme::font (11.5f));
            g.setColour (col.withAlpha (0.82f));
            g.drawText (entry.ip, nameX + nameW, 0, ipW, height, juce::Justification::centredLeft, true);

            juce::String latency = "—";
            if (status.latencyMs > 0)
                latency = juce::String (status.latencyMs) + " ms";
            else if (entry.ip.isNotEmpty() && status.quality == showcontrol::backup::LinkQuality::offline)
                latency = showcontrol::localization::tr (u8"Mất");

            g.setColour (col.withAlpha (0.68f));
            g.drawText (latency, width - latencyW - 6, 0, latencyW, height, juce::Justification::centredRight, true);
        }

        void selectedRowsChanged (int lastRowSelected) override
        {
            owner.selectedActivePeerIndex = lastRowSelected;
            owner.updateActionButtonVisibility();
        }

    private:
        BackupSyncPreferencesPanel& owner;
    };

    friend class ScanResultsListModel;
    friend class ActivePeersListModel;

    bool isPrimaryRole() const noexcept { return roleCombo.getSelectedId() == 2; }
    bool isBackupRole() const noexcept { return roleCombo.getSelectedId() == 3; }

    showcontrol::backup::PeerRuntimeStatus lookupRuntimeStatus (const juce::String& ip) const
    {
        if (queryPeerRuntimeStatus == nullptr)
            return {};

        for (const auto& status : queryPeerRuntimeStatus())
        {
            if (status.address.equalsIgnoreCase (ip))
                return status;
        }

        return {};
    }

    juce::Colour linkQualityColour (showcontrol::backup::LinkQuality quality) const
    {
        switch (quality)
        {
            case showcontrol::backup::LinkQuality::good:     return juce::Colour (0xff3ecf6a);
            case showcontrol::backup::LinkQuality::degraded: return juce::Colour (0xfff0b429);
            case showcontrol::backup::LinkQuality::offline:  return juce::Colour (0xffe05252);
            default:                                         return juce::Colour (0xff8a8f98);
        }
    }

    void refreshLocalMachineDisplay()
    {
        const auto info = showcontrol::backup::getPrimaryLocalLanNetworkInfo();
        const auto col  = getLookAndFeel().findColour (juce::Label::textColourId);
        const int port  = portEditor.getText().getIntValue() > 0
                        ? portEditor.getText().getIntValue()
                        : (int) showcontrol::backup::kDefaultSyncPort;

        if (info.ip.isEmpty())
        {
            localMachineInfoLabel.setText (showcontrol::localization::tr (u8"Không phát hiện giao diện mạng LAN"),
                                           juce::dontSendNotification);
            localMachineInfoLabel.setColour (juce::Label::textColourId, col.withAlpha (0.45f));
            return;
        }

        const auto subnet = info.subnetCidr.isNotEmpty() ? info.subnetCidr : "—";
        localMachineInfoLabel.setText (info.ip + "  ·  " + subnet + "  ·  UDP " + juce::String (port),
                                       juce::dontSendNotification);
        localMachineInfoLabel.setColour (juce::Label::textColourId, col);
    }

    void updateActionButtonVisibility()
    {
        const bool showNetworkUi = isPrimaryRole() || isBackupRole();
        scanPeerBtn.setVisible (showNetworkUi);
        removePeerBtn.setVisible (isPrimaryRole() && selectedActivePeerIndex >= 0);
        portLabel.setVisible (true);
        portEditor.setVisible (true);
        scanSectionLabel.setVisible (showNetworkUi);
        resized();
    }

    void refreshRoleUi()
    {
        const bool primary = isPrimaryRole();
        const bool backup  = isBackupRole();
        const bool showNetworkUi = primary || backup;

        localMachineTitle.setVisible (showNetworkUi);
        localMachineInfoLabel.setVisible (showNetworkUi);
        scanSectionLabel.setVisible (showNetworkUi);
        scanResultsList.setVisible (showNetworkUi && scanResultsVisible);
        connectBtn.setVisible (showNetworkUi && scanResultsVisible);
        activePeersLabel.setVisible (showNetworkUi);
        activePeersList.setVisible (showNetworkUi);
        portLabel.setVisible (true);
        portEditor.setVisible (true);

        if (! showNetworkUi)
            clearScanResults();

        if (backup && configuredPeerEntries.size() > 0)
            peerEditor.setText (configuredPeerEntries.getReference (0).ip, juce::dontSendNotification);

        if (primary)
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy dự phòng đang chạy"), juce::dontSendNotification);
        else if (backup)
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy chính đã kết nối"), juce::dontSendNotification);
        else
            activePeersLabel.setText ({}, juce::dontSendNotification);

        refreshLocalMachineDisplay();
        refreshActivePeerList();
        updateActionButtonVisibility();
    }

    void notifyChanged()
    {
        saveToPreferences();

        if (onSettingsChanged != nullptr)
            onSettingsChanged();
    }

    void refreshTakeoverButton()
    {
        const bool isBackup = isBackupRole();
        takeoverBtn.setVisible (isBackup);
        takeoverBtn.setToggleState (takeoverActive, juce::dontSendNotification);

        if (takeoverActive)
            takeoverBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc45c26));
        else
            takeoverBtn.setColour (juce::TextButton::buttonColourId,
                                   findColour (juce::TextButton::buttonColourId));
    }

    void addPeerIfNew (const juce::String& ip, const juce::String& hostName = {})
    {
        const auto trimmed = ip.trim();
        if (trimmed.isEmpty())
            return;

        for (const auto& existing : configuredPeerEntries)
        {
            if (existing.ip.equalsIgnoreCase (trimmed))
                return;
        }

        if (configuredPeerEntries.size() >= showcontrol::backup::kMaxBackupPeers)
            return;

        ConfiguredPeerEntry entry;
        entry.ip       = trimmed;
        entry.hostName = hostName;
        configuredPeerEntries.add (entry);
        refreshActivePeerList();
    }

    void removeSelectedActivePeer()
    {
        if (! isPrimaryRole())
            return;

        if (! juce::isPositiveAndBelow (selectedActivePeerIndex, configuredPeerEntries.size()))
            return;

        configuredPeerEntries.remove (selectedActivePeerIndex);
        selectedActivePeerIndex = -1;
        activePeersList.deselectAllRows();
        refreshActivePeerList();
        updateActionButtonVisibility();
        notifyChanged();
    }

    void clearScanResults()
    {
        scanResults.clear();
        scanResultsVisible = false;
        scanResultsList.setVisible (false);
        connectBtn.setVisible (false);
        scanResultsList.updateContent();
        resized();
    }

    void showScanResults (const juce::Array<showcontrol::backup::LanPeerInfo>& peers)
    {
        scanResults.clear();

        for (const auto& peer : peers)
        {
            ScanResultEntry entry;
            entry.address     = peer.address;
            entry.hostName    = peer.hostName;
            entry.discoveryMs = peer.discoveryMs;
            entry.selected    = true;
            scanResults.add (entry);
        }

        scanResultsVisible = scanResults.size() > 0;
        scanResultsList.setVisible (scanResultsVisible);
        connectBtn.setVisible (scanResultsVisible);
        scanResultsList.updateContent();
        resized();
    }

    void connectSelectedPeers()
    {
        if (isPrimaryRole())
        {
            for (const auto& entry : scanResults)
            {
                if (entry.selected)
                    addPeerIfNew (entry.address, entry.hostName);
            }

            notifyChanged();
            clearScanResults();
            return;
        }

        if (isBackupRole())
        {
            for (const auto& entry : scanResults)
            {
                if (! entry.selected)
                    continue;

                configuredPeerEntries.clear();
                addPeerIfNew (entry.address, entry.hostName);
                peerEditor.setText (entry.address, juce::dontSendNotification);
                notifyChanged();
                clearScanResults();
                return;
            }
        }
    }

    void refreshActivePeerList()
    {
        activePeersList.updateContent();
        activePeersList.repaint();
    }

    void startLanScan()
    {
        if (onScanLanPeers == nullptr)
            return;

        const int role = juce::jlimit (0, 2, roleCombo.getSelectedId() - 1);

        if (role == (int) showcontrol::backup::Role::standalone)
            return;

        const int wantRole = (role == (int) showcontrol::backup::Role::primary)
                           ? (int) showcontrol::backup::Role::backup
                           : (int) showcontrol::backup::Role::primary;

        scanPeerBtn.setEnabled (false);
        scanPeerBtn.setButtonText (showcontrol::localization::tr (u8"Đang quét..."));

        onScanLanPeers (wantRole, [safeThis = juce::Component::SafePointer<BackupSyncPreferencesPanel> (this)]
                        (const juce::Array<showcontrol::backup::LanPeerInfo>& peers)
        {
            if (safeThis == nullptr)
                return;

            safeThis->scanPeerBtn.setEnabled (true);
            safeThis->scanPeerBtn.setButtonText (showcontrol::localization::tr (u8"Quét LAN..."));
            safeThis->refreshLocalMachineDisplay();

            if (peers.isEmpty())
            {
                safeThis->showLanScanEmptyHint();
                return;
            }

            safeThis->showScanResults (peers);
        });
    }

    void showLanScanEmptyHint()
    {
        juce::String body = showcontrol::localization::tr (
            u8"Không tìm thấy máy ShowCue trên cùng subnet.\n\n"
            u8"• Cả hai máy cùng Wi‑Fi / LAN\n"
            u8"• Máy đối tác đang mở ShowCue (vai trò Primary/Backup)\n"
            u8"• Bật 「Bật nhận OSC / đồng bộ LAN」\n"
            u8"• macOS: Cài đặt hệ thống → Quyền riêng tư → Mạng cục bộ → bật ShowCue");

        const auto info = showcontrol::backup::getPrimaryLocalLanNetworkInfo();

        if (info.ip.isNotEmpty())
        {
            body = showcontrol::localization::tr (u8"IP máy này") + ": "
                 + showcontrol::backup::describeLocalLanNetwork (info)
                 + "\n\n" + body;
        }

        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon,
            showcontrol::localization::tr (u8"Quét LAN"),
            body,
            showcontrol::localization::tr (u8"Đã hiểu"));
    }

    void timerCallback() override
    {
        if (isShowing() && (isPrimaryRole() || isBackupRole()))
            refreshActivePeerList();
    }

    juce::Label roleLabel, portLabel, helpLabel, localMachineTitle, localMachineInfoLabel;
    juce::Label scanSectionLabel, activePeersLabel;
    juce::ComboBox roleCombo;
    juce::TextEditor peerEditor, portEditor;
    juce::TextButton scanPeerBtn, connectBtn, removePeerBtn, takeoverBtn;
    juce::ToggleButton followerLockToggle, oscEnableToggle;
    juce::ListBox scanResultsList, activePeersList;
    ScanResultsListModel scanResultsModel;
    ActivePeersListModel activePeersModel;
    juce::Array<ScanResultEntry> scanResults;
    juce::Array<ConfiguredPeerEntry> configuredPeerEntries;
    juce::Rectangle<int> localMachineCardBounds;
    int selectedActivePeerIndex = -1;
    bool scanResultsVisible = false;
    bool takeoverActive = false;
};
