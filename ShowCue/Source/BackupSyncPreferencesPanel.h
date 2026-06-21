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
    std::function<void()> onPreferredHeightChanged;
    std::function<void()> onTakeoverToggled;
    std::function<void()> onSyncConfigRequested;
    std::function<void()> onReconnectRequested;
    std::function<void (int wantRole, std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone)> onScanLanPeers;
    std::function<juce::Array<showcontrol::backup::PeerRuntimeStatus>()> queryPeerRuntimeStatus;

    BackupSyncPreferencesPanel()
        : scanResultsModel (*this),
          activePeersModel (*this)
    {
        panelTitleText = showcontrol::localization::tr (u8"Đồng bộ mạng");

        roleLabel.setFont (showcontrol::preferences::hintFont());
        roleLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (roleLabel);
        addAndMakeVisible (roleCombo);
        roleCombo.onChange = [this]
        {
            refreshRoleUi();
            refreshTakeoverButton();
            notifyChanged();
        };

        machineInfoLabel.setFont (showcontrol::preferences::hintFont());
        machineInfoLabel.setJustificationType (juce::Justification::centred);
        addChildComponent (machineInfoLabel);

        scanSectionLabel.setFont (showcontrol::preferences::hintFont());
        scanSectionLabel.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (scanSectionLabel);

        portLabel.setFont (showcontrol::preferences::hintFont());
        portLabel.setJustificationType (juce::Justification::centredRight);
        addChildComponent (portLabel);

        portEditor.setFont (showcontrol::preferences::hintFont());

        scanPeerBtn.onClick = [this] { startLanScan(); };
        addChildComponent (scanPeerBtn);

        scanResultsList.setModel (&scanResultsModel);
        scanResultsList.setRowHeight (26);
        scanResultsList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        scanResultsList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addChildComponent (scanResultsList);

        connectBtn.onClick = [this] { connectSelectedPeers(); };
        addChildComponent (connectBtn);

        activePeersLabel.setFont (showcontrol::preferences::hintFont());
        activePeersLabel.setJustificationType (juce::Justification::centred);
        addChildComponent (activePeersLabel);

        activePeersList.setModel (&activePeersModel);
        activePeersList.setRowHeight (26);
        activePeersList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        activePeersList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addChildComponent (activePeersList);

        removePeerBtn.onClick = [this] { removeSelectedActivePeer(); };
        addChildComponent (removePeerBtn);

        reconnectBtn.onClick = [this]
        {
            if (onReconnectRequested != nullptr)
                onReconnectRequested();
        };
        addChildComponent (reconnectBtn);

        syncConfigBtn.onClick = [this]
        {
            if (onSyncConfigRequested != nullptr)
                onSyncConfigRequested();
        };
        addChildComponent (syncConfigBtn);

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
        addChildComponent (portEditor);

        followerLockToggle.onClick = [this] { notifyChanged(); };
        addChildComponent (followerLockToggle);

        oscEnableToggle.onClick = [this] { notifyChanged(); };
        addAndMakeVisible (oscEnableToggle);

        helpLabel.setFont (showcontrol::preferences::hintFont().withHeight (14.0f));
        helpLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (helpLabel);

        takeoverBtn.onClick = [this]
        {
            takeoverActive = ! takeoverActive;
            refreshTakeoverButton();

            if (onTakeoverToggled != nullptr)
                onTakeoverToggled();
        };
        addChildComponent (takeoverBtn);

        loadFromPreferences();
        refreshLocalizedText();
        refreshLocalMachineDisplay();
        refreshRoleUi();
        refreshTakeoverButton();
    }

    void visibilityChanged() override
    {
        juce::Component::visibilityChanged();

        if (isVisible() && isShowing())
            startTimerHz (1);
        else
            haltActiveTimers();
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
        clearScanResults (false);
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

        const auto& laf = getLookAndFeel();
        const auto border = laf.findColour (juce::Label::outlineColourId).withAlpha (0.35f);
        const auto titleCol = laf.findColour (juce::Label::textColourId);
        const auto windowBg = findColour (juce::ResizableWindow::backgroundColourId);

        auto paintBorder = [&] (juce::Rectangle<int> bounds)
        {
            if (bounds.isEmpty())
                return;

            g.setColour (border);
            g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 8.0f, 1.0f);
        };

        auto paintCenteredLegend = [&] (juce::Rectangle<int> bounds, const juce::String& text)
        {
            if (bounds.isEmpty() || text.isEmpty())
                return;

            g.setFont (showcontrol::preferences::sectionLabelFont().withHeight (14.0f));
            const int textW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 18;
            auto legend = juce::Rectangle<int> (bounds.getCentreX() - textW / 2,
                                                bounds.getY() - 9,
                                                textW,
                                                18);
            g.setColour (windowBg);
            g.fillRect (legend);
            g.setColour (titleCol.withAlpha (0.92f));
            g.drawText (text, legend, juce::Justification::centred, false);
        };

        paintBorder (mainPanelBounds);
        paintCenteredLegend (mainPanelBounds, panelTitleText);

        paintBorder (helpPanelBounds);
        paintCenteredLegend (helpPanelBounds, helpTitleText);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (16, 12);
        const int rowH = 30;
        const int gap  = 8;

        const int helpH = juce::jmax (kHelpPanelH, (int) (bounds.getHeight() * 0.20f));
        helpPanelBounds = bounds.removeFromBottom (helpH);
        bounds.removeFromBottom (10);
        mainPanelBounds = bounds;

        if (! helpPanelBounds.isEmpty())
        {
            auto helpInner = helpPanelBounds.reduced (16, 12);
            helpInner.removeFromTop (10);
            helpLabel.setBounds (helpInner);
        }

        if (mainPanelBounds.isEmpty())
            return;

        auto inner = mainPanelBounds.reduced (16, 14);
        stackAreaBounds = inner;

        const int stackH = measureContentHeight (rowH, gap);
        const int contentW = juce::jmin (kStackMaxW, stackAreaBounds.getWidth());
        auto block = inner.removeFromTop (juce::jmin (stackH, inner.getHeight()));
        block = block.withSizeKeepingCentre (contentW, block.getHeight());

        layoutContentStack (block, rowH, gap);
    }

    void refreshSectionLabelColours() { refreshLocalizedText(); }

    void haltActiveTimers() noexcept { stopTimer(); }

    void lookAndFeelChanged() override
    {
        juce::Component::lookAndFeelChanged();
        refreshListChrome();
        repaint();
    }

    void refreshNetworkInfo()
    {
        refreshLocalMachineDisplay();
        refreshActivePeerList();
    }

    void refreshLocalizedText()
    {
        panelTitleText = showcontrol::localization::tr (u8"Đồng bộ mạng");
        helpTitleText  = showcontrol::localization::tr (u8"Hướng dẫn kết nối");
        roleLabel.setText (showcontrol::localization::tr (u8"Vai trò máy"), juce::dontSendNotification);

        const int roleIndex = roleCombo.getSelectedId() > 0
            ? juce::jlimit (0, 2, roleCombo.getSelectedId() - 1)
            : showcontrol::prefs::loadBackupRole();

        roleCombo.clear (juce::dontSendNotification);
        roleCombo.addItem (showcontrol::localization::tr (u8"Độc lập"), 1);
        roleCombo.addItem (showcontrol::localization::tr (u8"Máy chính"), 2);
        roleCombo.addItem (showcontrol::localization::tr (u8"Máy phụ"), 3);
        roleCombo.setSelectedId (roleIndex + 1, juce::dontSendNotification);

        scanSectionLabel.setText (showcontrol::localization::tr (u8"Quét mạng LAN"), juce::dontSendNotification);
        scanPeerBtn.setButtonText (showcontrol::localization::tr (u8"Quét LAN..."));
        connectBtn.setButtonText (showcontrol::localization::tr (u8"Kết nối"));
        portLabel.setText (showcontrol::localization::tr (u8"Cổng UDP"), juce::dontSendNotification);
        removePeerBtn.setButtonText (showcontrol::localization::tr (u8"Gỡ máy đã chọn"));
        reconnectBtn.setButtonText (showcontrol::localization::tr (u8"Kết nối lại"));
        syncConfigBtn.setButtonText (showcontrol::localization::tr (u8"Đồng bộ cấu hình sang máy phụ"));

        followerLockToggle.setButtonText (showcontrol::localization::tr (
            u8"Khóa điều khiển trên máy phụ"));
        oscEnableToggle.setButtonText (showcontrol::localization::tr (u8"Nhận OSC / đồng bộ LAN"));
        takeoverBtn.setButtonText (showcontrol::localization::tr (u8"Takeover (máy phụ)"));

        if (isPrimaryRole())
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy dự phòng đã kết nối"), juce::dontSendNotification);
        else if (isBackupRole())
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy chính đã kết nối"), juce::dontSendNotification);
        else
            activePeersLabel.setText ({}, juce::dontSendNotification);

        refreshHelpText();
        refreshLocalMachineDisplay();
        refreshTakeoverButton();
        scanResultsList.updateContent();
        activePeersList.updateContent();

        const auto col = getLookAndFeel().findColour (juce::Label::textColourId);
        roleLabel.setColour (juce::Label::textColourId, col);
        machineInfoLabel.setColour (juce::Label::textColourId, col);
        scanSectionLabel.setColour (juce::Label::textColourId, col);
        activePeersLabel.setColour (juce::Label::textColourId, col);
        portLabel.setColour (juce::Label::textColourId, col);
        helpLabel.setColour (juce::Label::textColourId, col.withAlpha (0.88f));
        refreshListChrome();
    }

    void refreshListChrome()
    {
        const auto listBg    = findColour (juce::TextEditor::backgroundColourId).withAlpha (0.20f);
        const auto listBorder = findColour (juce::Label::outlineColourId).withAlpha (0.30f);

        scanResultsList.setColour (juce::ListBox::backgroundColourId, listBg);
        scanResultsList.setColour (juce::ListBox::outlineColourId, listBorder);
        activePeersList.setColour (juce::ListBox::backgroundColourId, listBg);
        activePeersList.setColour (juce::ListBox::outlineColourId, listBorder);
    }

    int getPreferredEmbeddedHeight() const noexcept
    {
        constexpr int rowH = 30;
        constexpr int gap  = 8;
        constexpr int outerVPad = 24;
        constexpr int mainInnerVPad = 28;
        constexpr int betweenPanels = 10;
        return outerVPad + mainInnerVPad + measureContentHeight (rowH, gap)
             + betweenPanels + kHelpPanelH;
    }

private:
    static constexpr int kStackMaxW   = 460;
    static constexpr int kHelpPanelH  = 108;

    int measureContentHeight (int rowH, int gap) const
    {
        int h = rowH;

        if (! isPrimaryRole() && ! isBackupRole())
            return rowH + gap + rowH;

        h += gap + rowH;
        h += gap + rowH;

        if (scanResultsVisible && scanResultsList.isVisible())
        {
            h += gap + juce::jlimit (54, 132, scanResults.size() * 26 + 4);

            if (connectBtn.isVisible())
                h += gap + rowH;
        }

        if (hasConfiguredPeers() && activePeersList.isVisible())
        {
            h += gap + 20;
            h += gap + juce::jlimit (28, 110, configuredPeerEntries.size() * 26 + 4);

            if (reconnectBtn.isVisible())
                h += gap + rowH;
        }

        if (followerLockToggle.isVisible())
            h += gap + rowH;

        if (syncConfigBtn.isVisible())
            h += gap + rowH;

        return h;
    }

    void layoutContentStack (juce::Rectangle<int> block, int rowH, int gap)
    {
        auto takeRow = [&] (int h) -> juce::Rectangle<int>
        {
            auto row = block.removeFromTop (h);
            block.removeFromTop (gap);
            return row;
        };

        auto roleRow = takeRow (rowH);
        roleLabel.setBounds (roleRow.removeFromLeft (96));
        roleRow.removeFromLeft (8);
        roleCombo.setBounds (roleRow);

        if (! isPrimaryRole() && ! isBackupRole())
        {
            layoutActionToggles (takeRow, rowH);
            return;
        }

        machineInfoLabel.setBounds (takeRow (rowH));

        auto scanRow = takeRow (rowH);
        const int colW = scanRow.getWidth() / 3;
        scanSectionLabel.setBounds (scanRow.removeFromLeft (colW).reduced (0, 4));

        auto btnCol = scanRow.removeFromRight (colW);
        if (scanPeerBtn.isVisible())
            scanPeerBtn.setBounds (btnCol.reduced (2, 2));

        auto portCol = scanRow.reduced (2, 0);
        const int editW = 48;
        const int lblW  = juce::jmin (portCol.getWidth() - editW - 4,
                                    juce::GlyphArrangement::getStringWidthInt (portLabel.getFont(),
                                                                               portLabel.getText()) + 6);
        auto pair = portCol.withSizeKeepingCentre (lblW + 4 + editW, rowH);
        portLabel.setBounds (pair.removeFromLeft (lblW));
        pair.removeFromLeft (4);
        portEditor.setBounds (pair);

        if (scanResultsVisible && scanResultsList.isVisible())
        {
            const int scanListH = juce::jlimit (54, 132, scanResults.size() * 26 + 4);
            scanResultsList.setBounds (takeRow (scanListH));

            if (connectBtn.isVisible())
                connectBtn.setBounds (takeRow (rowH).withSizeKeepingCentre (kActionBtnW, rowH - 2));
        }

        if (hasConfiguredPeers() && activePeersList.isVisible())
        {
            auto headerRow = takeRow (20);
            const bool showRemove = removePeerBtn.isVisible();

            if (showRemove)
            {
                removePeerBtn.setBounds (headerRow.removeFromRight (kActionBtnW));
                headerRow.removeFromRight (8);
            }

            activePeersLabel.setBounds (headerRow);
            activePeersLabel.setJustificationType (showRemove ? juce::Justification::centredLeft
                                                              : juce::Justification::centred);

            const int peerListH = juce::jlimit (28, 110, configuredPeerEntries.size() * 26 + 4);
            activePeersList.setBounds (takeRow (peerListH));

            if (reconnectBtn.isVisible())
                reconnectBtn.setBounds (takeRow (rowH).withSizeKeepingCentre (kActionBtnW, rowH - 2));
        }

        if (syncConfigBtn.isVisible())
            syncConfigBtn.setBounds (takeRow (rowH));

        layoutActionToggles (takeRow, rowH);
    }

    void layoutActionToggles (const std::function<juce::Rectangle<int> (int)>& takeRow, int rowH)
    {
        if (oscEnableToggle.isVisible())
        {
            auto row = takeRow (rowH);
            const int toggleW = juce::jmin (320, row.getWidth());
            oscEnableToggle.setBounds (row.withSizeKeepingCentre (toggleW, rowH));
        }

        if (! followerLockToggle.isVisible())
            return;

        auto row = takeRow (rowH);

        if (takeoverBtn.isVisible())
        {
            const int slotW = row.getWidth() / 2;
            followerLockToggle.setBounds (row.removeFromLeft (slotW).reduced (2, 0));
            takeoverBtn.setBounds (row.reduced (2, 0));
        }
        else
        {
            followerLockToggle.setBounds (row);
        }
    }

    static constexpr int kActionBtnW = 128;

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

    static void paintCompactPeerColumns (juce::Graphics& g,
                                         int width,
                                         int height,
                                         int nameX,
                                         const juce::String& displayName,
                                         const juce::String& ipText,
                                         const juce::String& latencyText,
                                         juce::Colour col)
    {
        const int ipX  = juce::jmax (nameX + 80, (int) (width * 0.40f));
        const int latX = juce::jmax (ipX + 100, (int) (width * 0.68f));
        const int nameW = juce::jmax (48, ipX - nameX - 8);

        g.setFont (ShowTheme::fontBold (12.0f));
        g.setColour (col);
        g.drawText (displayName, nameX, 0, nameW, height, juce::Justification::centredLeft, true);

        g.setFont (ShowTheme::font (11.5f));
        g.setColour (col.withAlpha (0.85f));
        g.drawText (ipText, ipX, 0, latX - ipX - 4, height, juce::Justification::centredLeft, true);

        g.setColour (col.withAlpha (0.70f));
        g.drawText (latencyText, latX, 0, width - latX - 4, height, juce::Justification::centredLeft, true);
    }

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

            juce::String latency = "—";
            if (entry.discoveryMs > 0)
                latency = juce::String (entry.discoveryMs) + " ms";

            const auto displayName = entry.hostName.isNotEmpty() ? entry.hostName : entry.address;
            paintCompactPeerColumns (g, width, height, 30, displayName, entry.address, latency, col);
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

            juce::String latency = "—";
            if (status.latencyMs > 0)
                latency = juce::String (status.latencyMs) + " ms";
            else if (entry.ip.isNotEmpty() && status.quality == showcontrol::backup::LinkQuality::offline)
                latency = showcontrol::localization::tr (u8"Mất");

            const auto displayName = entry.hostName.isNotEmpty() ? entry.hostName
                                 : (status.hostName.isNotEmpty() ? status.hostName : entry.ip);
            paintCompactPeerColumns (g, width, height, 24, displayName, entry.ip, latency, col);
        }

        void selectedRowsChanged (int lastRowSelected) override
        {
            owner.selectedActivePeerIndex = lastRowSelected;
            owner.applyVisibility();
        }

    private:
        BackupSyncPreferencesPanel& owner;
    };

    friend class ScanResultsListModel;
    friend class ActivePeersListModel;

    bool isPrimaryRole() const noexcept { return roleCombo.getSelectedId() == 2; }
    bool isBackupRole() const noexcept { return roleCombo.getSelectedId() == 3; }
    bool hasConfiguredPeers() const noexcept { return configuredPeerEntries.size() > 0; }

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

    void refreshHelpText()
    {
        if (isPrimaryRole())
        {
            helpLabel.setText (showcontrol::localization::tr (
                u8"Quét LAN → chọn máy phụ → Kết nối. Máy chính gửi GO / Stop / Panic.\n"
                u8"Thêm cùng file nhạc trên máy phụ (đường dẫn có thể khác Mac/Win).\n"
                u8"Dùng 「Đồng bộ cấu hình sang máy phụ」 để đồng bộ thứ tự và cài đặt cue.\n"
                u8"Trạng thái link hiển thị trên màn hình chính (xanh / vàng / đỏ)."),
                juce::dontSendNotification);
        }
        else if (isBackupRole())
        {
            helpLabel.setText (showcontrol::localization::tr (
                u8"Quét LAN → chọn máy chính → Kết nối. Máy phụ nhận lệnh GO / Stop / Panic.\n"
                u8"Thêm cùng file nhạc trên máy này — đường dẫn Mac/Win có thể khác máy chính.\n"
                u8"Dùng 「Kết nối lại」 khi mất link. macOS: bật Local Network."),
                juce::dontSendNotification);
        }
        else
        {
            helpLabel.setText (showcontrol::localization::tr (
                u8"Chế độ độc lập — không đồng bộ với máy khác.\n"
                u8"Bật tùy chọn phía trên nếu cần điều khiển GO / Stop / Panic từ bên ngoài (OSC hoặc LAN)."),
                juce::dontSendNotification);
        }
    }

    void refreshLocalMachineDisplay()
    {
        const auto info = showcontrol::backup::getPrimaryLocalLanNetworkInfo();
        const auto col  = getLookAndFeel().findColour (juce::Label::textColourId);
        const int port  = portEditor.getText().getIntValue() > 0
                        ? portEditor.getText().getIntValue()
                        : (int) showcontrol::backup::kDefaultSyncPort;

        const auto prefix = showcontrol::localization::tr (u8"Thông tin máy:");

        if (info.ip.isEmpty())
        {
            machineInfoLabel.setText (prefix + " "
                                      + showcontrol::localization::tr (u8"Không phát hiện giao diện mạng LAN"),
                                      juce::dontSendNotification);
            machineInfoLabel.setColour (juce::Label::textColourId, col.withAlpha (0.45f));
            return;
        }

        const auto subnet = info.subnetCidr.isNotEmpty() ? info.subnetCidr : "—";
        machineInfoLabel.setText (prefix + " " + info.ip + " · " + subnet + " · UDP " + juce::String (port),
                                  juce::dontSendNotification);
        machineInfoLabel.setColour (juce::Label::textColourId, col);
    }

    void applyVisibility()
    {
        const bool primary   = isPrimaryRole();
        const bool backup    = isBackupRole();
        const bool networked = primary || backup;
        const bool hasPeers  = hasConfiguredPeers();

        machineInfoLabel.setVisible (networked);
        scanSectionLabel.setVisible (networked);
        scanPeerBtn.setVisible (networked);
        portLabel.setVisible (networked);
        portEditor.setVisible (networked);

        scanResultsList.setVisible (networked && scanResultsVisible);
        connectBtn.setVisible (networked && scanResultsVisible);

        activePeersLabel.setVisible (networked && hasPeers);
        activePeersList.setVisible (networked && hasPeers);
        removePeerBtn.setVisible (primary && hasPeers && selectedActivePeerIndex >= 0);
        reconnectBtn.setVisible (backup && hasPeers);

        syncConfigBtn.setVisible (primary && hasPeers);

        followerLockToggle.setVisible (backup);
        takeoverBtn.setVisible (backup);
        oscEnableToggle.setVisible (! networked);

        if (! networked)
            clearScanResults (false);
    }

    void refreshRoleUi()
    {
        if (isBackupRole() && configuredPeerEntries.size() > 0)
            peerEditor.setText (configuredPeerEntries.getReference (0).ip, juce::dontSendNotification);

        if (isPrimaryRole())
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy dự phòng đã kết nối"), juce::dontSendNotification);
        else if (isBackupRole())
            activePeersLabel.setText (showcontrol::localization::tr (u8"Máy chính đã kết nối"), juce::dontSendNotification);

        refreshHelpText();
        refreshLocalMachineDisplay();
        refreshActivePeerList();
        refreshReconnectButton();
        applyVisibility();
        resized();

        if (onPreferredHeightChanged != nullptr)
            onPreferredHeightChanged();
    }

    void notifyChanged()
    {
        saveToPreferences();

        if (onSettingsChanged != nullptr)
            onSettingsChanged();
    }

    void refreshReconnectButton()
    {
        if (! isBackupRole() || ! hasConfiguredPeers())
            return;

        auto quality = showcontrol::backup::LinkQuality::unknown;

        if (configuredPeerEntries.size() > 0)
            quality = lookupRuntimeStatus (configuredPeerEntries.getReference (0).ip).quality;
        else if (peerEditor.getText().trim().isNotEmpty())
            quality = lookupRuntimeStatus (peerEditor.getText().trim()).quality;

        reconnectBtn.setEnabled (quality != showcontrol::backup::LinkQuality::good);
    }

    void refreshTakeoverButton()
    {
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
        applyVisibility();
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
        applyVisibility();
        notifyChanged();
        resized();
    }

    void clearScanResults (bool relayout = true)
    {
        scanResults.clear();
        scanResultsVisible = false;
        scanResultsList.setVisible (false);
        connectBtn.setVisible (false);
        scanResultsList.updateContent();

        if (relayout)
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
        applyVisibility();
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
            u8"• Bật 「Nhận OSC / đồng bộ LAN」\n"
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
        {
            refreshActivePeerList();
            refreshReconnectButton();
        }
    }

    juce::String panelTitleText, helpTitleText;
    juce::Label roleLabel, portLabel, helpLabel, machineInfoLabel;
    juce::Label scanSectionLabel, activePeersLabel;
    juce::ComboBox roleCombo;
    juce::TextEditor peerEditor, portEditor;
    juce::TextButton scanPeerBtn, connectBtn, removePeerBtn, reconnectBtn, syncConfigBtn, takeoverBtn;
    juce::ToggleButton followerLockToggle, oscEnableToggle;
    juce::ListBox scanResultsList, activePeersList;
    ScanResultsListModel scanResultsModel;
    ActivePeersListModel activePeersModel;
    juce::Array<ScanResultEntry> scanResults;
    juce::Array<ConfiguredPeerEntry> configuredPeerEntries;
    juce::Rectangle<int> mainPanelBounds, helpPanelBounds, stackAreaBounds;
    int selectedActivePeerIndex = -1;
    bool scanResultsVisible = false;
    bool takeoverActive = false;
};
