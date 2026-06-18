#pragma once

#include <JuceHeader.h>
#include "ShowLocalization.h"
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowAppPreferences.h"
#include "ShowBackupSync.h"
#include "ShowBackupLanDiscovery.h"

/** Tab Mạng / Backup — vai trò Primary/Backup, OSC, danh sách máy peer. */
class BackupSyncPreferencesPanel : public juce::Component
{
public:
    std::function<void()> onSettingsChanged;
    std::function<void (int wantRole, std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone)> onScanLanPeers;

    BackupSyncPreferencesPanel()
        : configuredPeersModel (*this)
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

        peerLabel.setFont (showcontrol::preferences::sectionLabelFont());
        addAndMakeVisible (peerLabel);

        addAndMakeVisible (peerEditor);

        addPeerBtn.onClick = [this] { addManualPeer(); };
        addAndMakeVisible (addPeerBtn);

        removePeerBtn.onClick = [this] { removeSelectedPeer(); };
        addAndMakeVisible (removePeerBtn);

        scanPeerBtn.onClick = [this] { startLanScan(); };
        addAndMakeVisible (scanPeerBtn);

        configuredPeersList.setModel (&configuredPeersModel);
        configuredPeersList.setRowHeight (24);
        configuredPeersList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (configuredPeersList);

        scanResultsLabel.setFont (showcontrol::preferences::sectionLabelFont());
        addChildComponent (scanResultsLabel);

        addScanSelectedBtn.onClick = [this] { addSelectedScanResults(); };
        addChildComponent (addScanSelectedBtn);

        scanResultsViewport.setViewedComponent (&scanResultsContainer, false);
        scanResultsViewport.setScrollBarsShown (true, false);
        addChildComponent (scanResultsViewport);

        portLabel.setFont (showcontrol::preferences::sectionLabelFont());
        addAndMakeVisible (portLabel);

        portEditor.setInputRestrictions (5, "0123456789");
        portEditor.onTextChange = [this] { notifyChanged(); };
        addAndMakeVisible (portEditor);

        followerLockToggle.onClick = [this] { notifyChanged(); };
        addAndMakeVisible (followerLockToggle);

        oscEnableToggle.onClick = [this] { notifyChanged(); };
        addAndMakeVisible (oscEnableToggle);

        helpLabel.setFont (juce::FontOptions (13.0f));
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

        peerEditor.onTextChange = [this]
        {
            if (isPrimaryRole())
                return;

            notifyChanged();
        };

        loadFromPreferences();
        refreshLocalizedText();
        refreshRoleUi();
        refreshTakeoverButton();
    }

    void loadFromPreferences()
    {
        const int role = showcontrol::prefs::loadBackupRole();
        roleCombo.setSelectedId (role + 1, juce::dontSendNotification);

        configuredPeers = showcontrol::prefs::loadBackupPeerHosts();
        configuredPeersList.updateContent();
        configuredPeersList.repaint();
        selectedPeerIndex = -1;

        if (isBackupRole())
            peerEditor.setText (configuredPeers.size() > 0 ? configuredPeers[0] : juce::String(),
                                juce::dontSendNotification);
        else
            peerEditor.clear();

        portEditor.setText (juce::String (showcontrol::prefs::loadBackupSyncPort()), juce::dontSendNotification);
        followerLockToggle.setToggleState (showcontrol::prefs::loadBackupFollowerLock(), juce::dontSendNotification);
        oscEnableToggle.setToggleState (showcontrol::prefs::loadOscEnabled(), juce::dontSendNotification);
        clearScanResults();
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
        }
        else if (role == (int) showcontrol::backup::Role::primary)
        {
            peers = configuredPeers;
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
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20, 16);
        const int rowH = 28;
        const int gap  = 8;

        roleLabel.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        roleCombo.setBounds (area.removeFromTop (rowH));
        area.removeFromTop (gap);

        peerLabel.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);

        auto peerRow = area.removeFromTop (rowH);
        scanPeerBtn.setBounds (peerRow.removeFromRight (104));
        peerRow.removeFromRight (6);

        if (isPrimaryRole())
        {
            removePeerBtn.setBounds (peerRow.removeFromRight (72));
            peerRow.removeFromRight (6);
            addPeerBtn.setBounds (peerRow.removeFromRight (72));
            peerRow.removeFromRight (6);
        }

        peerEditor.setBounds (peerRow);
        area.removeFromTop (6);

        if (isPrimaryRole())
        {
            configuredPeersList.setBounds (area.removeFromTop (88));
            area.removeFromTop (6);
        }

        if (scanResultsVisible)
        {
            scanResultsLabel.setBounds (area.removeFromTop (20));
            area.removeFromTop (4);
            scanResultsViewport.setBounds (area.removeFromTop (juce::jmin (110, scanResultsHeight + 8)));
            area.removeFromTop (6);
            addScanSelectedBtn.setBounds (area.removeFromTop (rowH));
            area.removeFromTop (gap);
        }

        portLabel.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        portEditor.setBounds (area.removeFromTop (rowH));
        area.removeFromTop (gap);

        followerLockToggle.setBounds (area.removeFromTop (rowH));
        area.removeFromTop (6);
        oscEnableToggle.setBounds (area.removeFromTop (rowH));
        area.removeFromTop (gap);

        takeoverBtn.setBounds (area.removeFromTop (32));
        area.removeFromTop (gap);

        helpLabel.setBounds (area.removeFromTop (120));
    }

    void refreshSectionLabelColours()
    {
        refreshLocalizedText();
    }

    void refreshLocalizedText()
    {
        roleLabel.setText (showcontrol::localization::tr (u8"Vai trò máy"), juce::dontSendNotification);

        roleCombo.clear (juce::dontSendNotification);
        roleCombo.addItem (showcontrol::localization::tr (u8"Độc lập (Standalone)"), 1);
        roleCombo.addItem (showcontrol::localization::tr (u8"Máy chính (Primary)"), 2);
        roleCombo.addItem (showcontrol::localization::tr (u8"Máy phụ (Backup)"), 3);

        const int roleId = roleCombo.getSelectedId();

        if (roleId > 0)
            roleCombo.setSelectedId (roleId, juce::dontSendNotification);

        refreshPeerLabelText();
        peerEditor.setTextToShowWhenEmpty (showcontrol::localization::tr (u8"ví dụ: 192.168.1.50"), juce::Colours::grey);

        addPeerBtn.setButtonText (showcontrol::localization::tr (u8"Thêm"));
        removePeerBtn.setButtonText (showcontrol::localization::tr (u8"Xóa"));
        scanPeerBtn.setButtonText (showcontrol::localization::tr (u8"Quét LAN..."));
        scanResultsLabel.setText (showcontrol::localization::tr (u8"Máy tìm thấy trên LAN"), juce::dontSendNotification);
        addScanSelectedBtn.setButtonText (showcontrol::localization::tr (u8"Thêm đã chọn"));

        portLabel.setText (showcontrol::localization::tr (u8"Cổng đồng bộ UDP"), juce::dontSendNotification);
        followerLockToggle.setButtonText (showcontrol::localization::tr (
            u8"Khóa điều khiển local trên máy phụ (Follower)"));
        oscEnableToggle.setButtonText (showcontrol::localization::tr (u8"Bật nhận OSC / đồng bộ LAN"));
        helpLabel.setText (showcontrol::localization::tr (
            u8"Máy chính: gửi GO/Panic/Stop tới một hoặc nhiều IP máy phụ.\n"
            u8"Máy phụ: nhận lệnh, phát từ file local (mute FOH cho đến khi takeover).\n"
            u8"Máy phụ mirror vị trí chọn và chế độ xem từ máy chính (GO/Stop/Pause vẫn đồng bộ như trước).\n"
            u8"Quét LAN dùng subnet Wi‑Fi/Ethernet đang active. Cần bật Quyền Mạng cục bộ (macOS).\n"
            u8"Cả hai máy cần cùng media và cùng bản ShowCue."),
            juce::dontSendNotification);
        takeoverBtn.setButtonText (showcontrol::localization::tr (u8"Takeover — điều khiển local (máy phụ)"));

        refreshTakeoverButton();
        rebuildScanResultToggles();

        const auto col = getLookAndFeel().findColour (juce::Label::textColourId);
        roleLabel.setColour (juce::Label::textColourId, col);
        peerLabel.setColour (juce::Label::textColourId, col);
        portLabel.setColour (juce::Label::textColourId, col);
        scanResultsLabel.setColour (juce::Label::textColourId, col);
        helpLabel.setColour (juce::Label::textColourId, col.withAlpha (0.75f));
    }

private:
    struct ScanResultEntry
    {
        juce::String address;
        juce::String label;
        bool selected = true;
    };

    class ConfiguredPeersListModel final : public juce::ListBoxModel
    {
    public:
        explicit ConfiguredPeersListModel (BackupSyncPreferencesPanel& ownerIn) : owner (ownerIn) {}

        int getNumRows() override
        {
            return owner.configuredPeers.size();
        }

        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
        {
            if (! juce::isPositiveAndBelow (row, owner.configuredPeers.size()))
                return;

            if (rowIsSelected)
                g.fillAll (owner.findColour (juce::TextEditor::highlightColourId).withAlpha (0.35f));

            g.setColour (owner.findColour (juce::Label::textColourId));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (owner.configuredPeers[row], 6, 0, width - 12, height, juce::Justification::centredLeft);
        }

        void selectedRowsChanged (int lastRowSelected) override
        {
            owner.selectedPeerIndex = lastRowSelected;
        }

    private:
        BackupSyncPreferencesPanel& owner;
    };

    friend class ConfiguredPeersListModel;

    bool isPrimaryRole() const noexcept
    {
        return roleCombo.getSelectedId() == 2;
    }

    bool isBackupRole() const noexcept
    {
        return roleCombo.getSelectedId() == 3;
    }

    void refreshPeerLabelText()
    {
        if (isPrimaryRole())
            peerLabel.setText (showcontrol::localization::tr (u8"Danh sách máy phụ (LAN)"), juce::dontSendNotification);
        else if (isBackupRole())
            peerLabel.setText (showcontrol::localization::tr (u8"IP máy chính (LAN)"), juce::dontSendNotification);
        else
            peerLabel.setText (showcontrol::localization::tr (u8"IP máy đối tác (LAN)"), juce::dontSendNotification);
    }

    void refreshRoleUi()
    {
        refreshPeerLabelText();

        const bool primary = isPrimaryRole();
        const bool backup  = isBackupRole();
        const bool showPeerUi = primary || backup;

        peerLabel.setVisible (showPeerUi);
        peerEditor.setVisible (showPeerUi);
        scanPeerBtn.setVisible (showPeerUi);
        addPeerBtn.setVisible (primary);
        removePeerBtn.setVisible (primary);
        configuredPeersList.setVisible (primary);

        if (! primary)
            clearScanResults();

        if (backup && configuredPeers.size() > 0)
            peerEditor.setText (configuredPeers[0], juce::dontSendNotification);

        if (primary)
            peerEditor.clear();

        resized();
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

    void addPeerIfNew (const juce::String& ip)
    {
        const auto trimmed = ip.trim();

        if (trimmed.isEmpty())
            return;

        for (const auto& existing : configuredPeers)
        {
            if (existing.equalsIgnoreCase (trimmed))
                return;
        }

        if (configuredPeers.size() >= showcontrol::backup::kMaxBackupPeers)
            return;

        configuredPeers.add (trimmed);
        configuredPeersList.updateContent();
        configuredPeersList.repaint();
    }

    void addManualPeer()
    {
        if (! isPrimaryRole())
            return;

        addPeerIfNew (peerEditor.getText());
        peerEditor.clear();
        notifyChanged();
    }

    void removeSelectedPeer()
    {
        if (! isPrimaryRole())
            return;

        if (! juce::isPositiveAndBelow (selectedPeerIndex, configuredPeers.size()))
            return;

        configuredPeers.remove (selectedPeerIndex);
        selectedPeerIndex = -1;
        configuredPeersList.deselectAllRows();
        configuredPeersList.updateContent();
        configuredPeersList.repaint();
        notifyChanged();
    }

    void clearScanResults()
    {
        scanResults.clear();
        scanResultToggles.clear();
        scanResultsContainer.removeAllChildren();
        scanResultsHeight = 0;
        scanResultsVisible = false;
        scanResultsLabel.setVisible (false);
        scanResultsViewport.setVisible (false);
        addScanSelectedBtn.setVisible (false);
        resized();
    }

    void rebuildScanResultToggles()
    {
        scanResultToggles.clear();
        scanResultsContainer.removeAllChildren();

        int y = 0;
        const int rowH = 24;

        for (auto& entry : scanResults)
        {
            auto* toggle = new juce::ToggleButton (entry.label);
            toggle->setToggleState (entry.selected, juce::dontSendNotification);
            toggle->setBounds (0, y, juce::jmax (240, getWidth() - 56), rowH);
            toggle->onClick = [&entry, toggle]
            {
                entry.selected = toggle->getToggleState();
            };
            scanResultsContainer.addAndMakeVisible (toggle);
            scanResultToggles.add (toggle);
            y += rowH;
        }

        scanResultsHeight = y;
        scanResultsContainer.setSize (juce::jmax (240, getWidth() - 56), scanResultsHeight);
    }

    void showScanResults (const juce::Array<showcontrol::backup::LanPeerInfo>& peers)
    {
        scanResults.clear();

        for (const auto& peer : peers)
        {
            ScanResultEntry entry;
            entry.address  = peer.address;
            entry.label    = peer.address;
            entry.selected = true;

            if (peer.hostName.isNotEmpty())
                entry.label += " — " + peer.hostName;

            scanResults.add (entry);
        }

        scanResultsVisible = scanResults.size() > 0;
        scanResultsLabel.setVisible (scanResultsVisible);
        scanResultsViewport.setVisible (scanResultsVisible);
        addScanSelectedBtn.setVisible (scanResultsVisible && isPrimaryRole());
        rebuildScanResultToggles();
        resized();
    }

    void addSelectedScanResults()
    {
        if (! isPrimaryRole())
            return;

        for (int i = 0; i < scanResults.size(); ++i)
        {
            if (scanResults.getReference (i).selected)
                addPeerIfNew (scanResults.getReference (i).address);
        }

        notifyChanged();
        clearScanResults();
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

            if (safeThis->isPrimaryRole())
            {
                safeThis->showScanResults (peers);

                if (peers.isEmpty())
                    safeThis->showLanScanEmptyHint();

                return;
            }

            if (peers.size() > 0)
            {
                safeThis->peerEditor.setText (peers.getReference (0).address);
                safeThis->notifyChanged();
            }
            else
            {
                safeThis->showLanScanEmptyHint();
            }
        });
    }

    void showLanScanEmptyHint()
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon,
            showcontrol::localization::tr (u8"Quét LAN"),
            showcontrol::localization::tr (
                u8"Không tìm thấy máy ShowCue trên cùng subnet.\n\n"
                u8"• Cả hai máy cùng Wi‑Fi / LAN\n"
                u8"• Máy đối tác đang mở ShowCue (vai trò Primary/Backup)\n"
                u8"• Bật 「Bật nhận OSC / đồng bộ LAN」\n"
                u8"• macOS: Cài đặt hệ thống → Quyền riêng tư → Mạng cục bộ → bật ShowCue"),
            showcontrol::localization::tr (u8"Đã hiểu"));
    }

    juce::Label roleLabel, peerLabel, portLabel, helpLabel, scanResultsLabel;
    juce::ComboBox roleCombo;
    juce::TextEditor peerEditor, portEditor;
    juce::TextButton addPeerBtn, removePeerBtn, scanPeerBtn, addScanSelectedBtn, takeoverBtn;
    juce::ToggleButton followerLockToggle, oscEnableToggle;
    juce::ListBox configuredPeersList;
    ConfiguredPeersListModel configuredPeersModel;
    juce::Viewport scanResultsViewport;
    juce::Component scanResultsContainer;
    juce::OwnedArray<juce::ToggleButton> scanResultToggles;
    juce::Array<ScanResultEntry> scanResults;
    juce::StringArray configuredPeers;
    int selectedPeerIndex = -1;
    int scanResultsHeight = 0;
    bool scanResultsVisible = false;
    bool takeoverActive = false;
};
