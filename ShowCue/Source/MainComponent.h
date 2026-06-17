#pragma once
#include <array>
#include <atomic>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>     
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowAudioFormats.h"
#include "SidebarPanel.h"
#include "SoundPad.h"
#include "InspectorPanel.h"
#include "PadPanel.h"
#include "ShowPadGridMatrix.h"
#include "MasterDeckPanel.h"
#include "CueListPanel.h"
#include "AudioEngine.h"
#include "HotkeyManager.h"
#include "HotkeyAssignDialog.h"
#include "VideoAudioExtractor.h"
#include "ShowKeyboardInput.h"
#include "ShowCrossComponentDrag.h"
#include "FfmpegSetupDialog.h"
#include "AudioDeviceSettingsPanel.h"
#include "BusMixerPanel.h"
#include "ErrorHandler.h"
#include "SetSecondaryWindow.h"
#include "StageMonitorComponent.h"
#include "ShowUndoActions.h"
#include "ShowBackupLanDiscovery.h"

namespace showcontrol::update { class ShowUpdateChecker; }
namespace showcontrol::osc { class ShowOscListener; }
namespace showcontrol::backup { class ShowBackupSyncBroadcaster; }

namespace ShowControlCommandIDs
{
    enum
    {
        showAboutDialog       = 0x53430001,
        checkForUpdates       = 0x53430002,
        openPreferences       = 0x53430003,
        exportShowcuePackage  = 0x53430004,
        importShowcuePackage  = 0x53430005
    };
}

class OneShotApplicationTimer;

class MainComponent  : public juce::Component,
                       public juce::DragAndDropContainer,
                       public juce::DragAndDropTarget,
                       public juce::Timer,
                       public juce::FileDragAndDropTarget,
                       public juce::ApplicationCommandTarget,
                       public juce::AsyncUpdater,
                       private juce::DarkModeSettingListener
{
public:
    enum class HotkeyScopeMode : int
    {
        activeList = 1,
        global = 2
    };

    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void timerCallback() override; 
    void visibilityChanged() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDragEnter (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDragMove (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDragExit (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails) override;

    void loadList (int listIndex, int trackCount, bool isGrid);
    void saveProject();
    void triggerSave();
    void persistApplicationStateNow();
    void rebuildHotkeyBindings();
    void movePadsToGridCell (int listIdx,
                             const juce::Array<int>& padIndices,
                             int anchorIndex,
                             int targetRow,
                             int targetCol);
    void copyTracksToPadGrid (int targetRow,
                              int targetCol,
                              const juce::Array<showcontrol::crossdrag::TrackCopyRecord>& tracks);
    void copyLegacySidebarItemsToPadGrid (int targetRow,
                                          int targetCol,
                                          const juce::Array<int>& itemIds,
                                          const juce::String& sourceListName);
    void appendPadsFromGridToList (int targetListIdx,
                                   const juce::Array<int>& padIndices);
    void appendCopyTracksToPlaylist (int targetListIdx,
                                     const juce::Array<showcontrol::crossdrag::TrackCopyRecord>& tracks);
    void consumeInternalJucePadDrop() noexcept;
    int getActiveListIndex() const noexcept { return activeListIndex; }
    bool isPadGridReorderActive() const noexcept { return padReorderActive && padReorderIsGridMode; }
    void saveApplicationState();
    void loadApplicationState();
    bool isOperatingState() const noexcept { return isPerformingStateOperation.load (std::memory_order_acquire); }
    void prepareForApplicationShutdown();
    void loadProject();
    juce::File getProjectFile();
    void triggerManualMusicIngestion();
    void applyThemePreference (int themeId);
    void setAppLanguage (int languageIndex);
    void lookAndFeelChanged() override;
    void refreshSidebarPlayingStatus();
    /** Quét ưu tiên toàn cục PAD → CUE → BGM và cập nhật MasterDeck — độc lập luồng chuyển playlist Sidebar. */
    void updateMainDeskDisplay();
    void showAudioSettingsDialog();
    void showPreferencesDialog (int initialTabIndex = 0);
    void showAboutDialog();
    void checkForUpdates();
    void showHotkeyAssignDialogForPad (SoundPad* pad);
    void crossfadeOtherPadsOnSameBus (SoundPad* starter, int listIndex);
    void ingestVideoFileToPad (const juce::File& videoFile, SoundPad* targetPad);
    void offerFfmpegSetupThenIngestVideo (const juce::File& videoFile, SoundPad* targetPad);
    void normalizeActiveListWithSettings (const showcontrol::loudness::LoudnessSettings& settings);
    juce::Array<showcontrol::loudness::ListPreviewRow> buildLoudnessPreviewForActiveList (
        const showcontrol::loudness::LoudnessSettings& settings) const;
    void applyProjectDefaultsToPad (SoundPad* pad) const;
    void triggerGlobalPanicFadeAll();
    void executePanicFadeAllLocked();
    void applyPanicFadeUiAftermath (int fadedCount);
    bool triggerExternalGo (int listIndex, int padIndex);
    bool triggerExternalSyncGo (int listIndex, int padIndex, float preWaitMs = 0.0f);
    void triggerGlobalStopAll();
    void triggerGlobalPauseAll();
    void exportProjectShowcuePackage();
    void importProjectShowcuePackage();
    void restartBackupSync();
    void restartOscListener() { restartBackupSync(); }
    void setBackupTakeoverActive (bool active);
    bool isBackupTakeoverActive() const noexcept { return backupTakeoverActive; }
    void finalizeStartupPlaylistUi();
    juce::var buildPlaylistJson() const;
    bool loadPlaylistFromJson (const juce::var& playlistVar);
    void reloadPadWaveformFromConfig (SoundPad* pad);
    void reloadAllPadWaveformsFromConfig();
    void reloadAllPadWaveformsStaggered (int batchSize = 6, int batchDelayMs = 16);
    void refreshStartupPlaylistDisplay();

    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    bool performApplicationUndo();
    bool performApplicationRedo();
    void forceStopActiveAudioForSafety();
    void applyPadGridDropAt (int listIdx, SoundPad* sourcePad, int targetRow, int targetCol);

    struct AudioFormatMigrationEntry
    {
        int listIndex = -1;
        int padIndex  = -1;
        juce::String filePath;
    };

    struct AudioFormatMigrationResult
    {
        int listIndex = -1;
        int padIndex  = -1;
        juce::String formatString;
        int sampleRate   = 0;
        int bitDepth     = 0;
        int numChannels  = 0;
    };

    bool collectPadsNeedingFormatMigration (juce::Array<AudioFormatMigrationEntry>& out) const;
    void startBackgroundAudioFormatMigration (const juce::Array<AudioFormatMigrationEntry>& entries);
    void applyAudioFormatMigrationResults (const juce::Array<AudioFormatMigrationResult>& results);
    void refreshUiAfterFormatMigration();

    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform (const juce::ApplicationCommandTarget::InvocationInfo& info) override;

private:
    void finishDeferredStartup();
    bool shouldBlockLocalPlaybackCommand() const noexcept;
    void broadcastSyncIfPrimary (const std::function<void (showcontrol::backup::ShowBackupSyncBroadcaster&)>& action);
    void handleSyncPanic();
    void handleSyncGo (int listIndex, int padIndex, float preWaitMs);
    void handleSyncStopAll();
    void handleSyncPauseAll();
    void handleSyncStopCue (int listIndex, int padIndex);
    void handleSyncPauseCue (int listIndex, int padIndex);
    void handleSyncHeartbeat (juce::uint32 sequence);
    void handleSyncTakeover (bool active);
    void handleSyncSelection (int listIndex, int padIndex, int viewMode, const juce::Array<int>& multiIndices);
    void applySyncedSelection (int listIndex, int padIndex, int viewMode, const juce::Array<int>& multiIndices);
    void broadcastSelectionSyncIfPrimary();
    int currentSyncViewMode() const noexcept;
    void setPlayoutModeInternal (bool isPadMode, bool persistToDisk);
    void updateBackupStatusLabel();
    void tickBackupHeartbeat();
    void pollBackupDiscoverySocket();
    void startBackupDiscoveryResponder();
    void stopBackupDiscoveryResponder();
    void scanLanPeersAsync (int wantRole,
                            std::function<void (const juce::Array<showcontrol::backup::LanPeerInfo>&)> onDone);
    void shutdownActiveTimers() noexcept;
    void applyFactoryDefaultApplicationState();
    void handleAsyncUpdate() override;
    void executeActualDiskWriteJSON();
    void saveApplicationStateInternal();
    std::unique_ptr<juce::XmlElement> buildProjectXml();

    ShowControlLookAndFeel appLookAndFeel;
    SidebarPanel sidebarPanel;
    InspectorPanel inspectorPanel; 

    struct ListData
    {
        juce::OwnedArray<SoundPad> pads;
        juce::Array<CueItem> cueMeta;
        bool isGrid = true;
        bool isLooping = false;
        bool useCueListPanel = false;
        bool clickPadToTrigger = false;
        bool autoArmOnSelect = true;
        bool isLocked = false;
        juce::Colour themeColour = showcontrol::colours::defaultTagColour();
        juce::Array<int> savedPadSelection;
        int savedPrimaryPadIndex = -1;
    };

    SoundPad* ensurePadSlotAtIndex (ListData& list, int index);
    void syncCueMetadataFromPads (ListData& list);
    void applyTagColourToPadAndCue (ListData& list, int index, juce::Colour colour);
    void applyTagColourWithUndo (int listIdx, int index, juce::Colour colour);
    void applyTrackRenameAtIndex (int listIdx, int cueIndex, const juce::String& newName, bool saveNow = true);
    void applyTrackRenameWithUndo (int listIdx, int cueIndex, const juce::String& newName, const juce::String& oldName);
    void performActiveListIngestWithUndo (std::function<void (ListData&)> ingestFn);
    void beginInspectorPadUndoSession (SoundPad* pad);
    void commitInspectorPadUndoSession (SoundPad* pad);
    void performPadAudioCutWithUndo (SoundPad* pad, double cutStart, double cutEnd,
                                     std::function<void (bool success)> onDone);
    void refreshTagColourLiveUi (ListData& list, int changedIndex);
    void updateListThemeColour (int listIndex, juce::Colour colour);
    void refreshGlobalTrackAccent();
    void syncPadTagColourFromCueMeta (ListData& list, SoundPad* pad) noexcept;
    void presentPadInInspector (SoundPad* pad);
    SoundPad* getActiveSoundPad() const noexcept { return inspectorPanel.getCurrentPad(); }
    void syncBgmListHeaderScrollbar();
    int getPlaylistViewportContentWidth() noexcept;
    void refreshCueListPanel (bool resetScrollToTop = true);
    bool triggerCueGo (int padIndex, bool fromSync = false);
    bool triggerCueListPlay (int padIndex);
    bool triggerCueListPause (int padIndex);
    bool triggerCueListStop (int padIndex);
    bool isCueListViewActive() const noexcept;
    void armPad (SoundPad* pad);
    void setSoloPad (SoundPad* pad, bool enable);
    void updateCuePlaybackIndicators();
    /** Ép highlight/selection sang pad mới ngay lập tức — tách khỏi fade-out pad cũ. */
    void forwardUiSelectionToPad (SoundPad* pad, bool scrollIntoView = false);
    /** Chặn timer/callback của pad fade-out cướp con trỏ UI khỏi pad đang focus. */
    bool shouldAcceptPlaybackUiEventFromPad (SoundPad* pad) const noexcept;
    SoundPad* findPrimaryPlaybackPadForActiveList() const noexcept;
    SoundPad* findGloballyPrioritizedPlayingPad() const noexcept;
    SoundPad* getAllActivePlayingPadTrackGlobal() const noexcept;
    SoundPad* getAllActivePlayingCueTrackGlobal() const noexcept;
    SoundPad* getAllActivePlayingBGMTrackGlobal() const noexcept;
    void updateTrackPlayingInfo (SoundPad* pad);
    void showNoTrackPlayingState();
    void refreshMasterDeckBgmTransportState();
    void resetPlaybackDisplayCaches() noexcept;
    /** Đồng bộ selection + inspector + master deck theo pad đang phát (single source of truth). */
    void syncUiToPlayingPad (SoundPad* pad, bool scrollIntoView);
    /** Sau toggle sang PAD grid — đồng bộ armed/playing + mở khóa tương tác. */
    void synchronizePadGridWithEngineState();
    void finishPlayoutViewHeavySync (bool isPadMode);
    void applyPlayoutViewFocus (bool isPadMode);
    SoundPad* findPlayingPadInActiveBgmList() const;
    bool triggerPadFromHotkey (const HotkeyBinding& binding, bool fromSync = false);
    void rebuildDefaultHotkeysForList (int listIndex);
    void ensureDefaultHotkeysForList (int listIndex);
    juce::OwnedArray<ListData> allLists;

    void handleTrackFinished (SoundPad* finishedPad);
    void advanceBgmPlaylistOnNaturalEnd (SoundPad* finishedPad);
    static int findListIndexForPad (const juce::OwnedArray<ListData>& lists, SoundPad* pad);
    static int findNextBgmTrackIndex (const ListData& list, int afterIndex);
    static int findPrevBgmTrackIndex (const ListData& list, int beforeIndex);
    void triggerBgmPlayPause();
    void triggerBgmNext();
    void triggerBgmPrev();
    /** Preload reader + waveform cho dòng BGM đang chọn (và hàng lân cận). */
    void prefetchBgmPadAtIndex (int index);

    enum class TransportCommandKind : uint8_t { none = 0, stop, play };
    static constexpr juce::uint32 kTransportCommandGuardMs = 400;
    bool allowTransportCommand (TransportCommandKind next,
                                TransportCommandKind& lastKind,
                                juce::uint32& lastCommandMs) noexcept;

    void rebuildSidebarFromAllLists();
    void importListsFromDroppedFolders (const juce::StringArray& folderPaths, bool targetIsBgm);
    juce::Rectangle<int> getCenterContentDropBounds() const;
    void ingestDroppedFilesToActiveBgmList (ListData& list, const juce::StringArray& validAudioFiles,
                                            const juce::StringArray& validVideoFiles, int dropLocalX, int dropLocalY);
    void ingestDroppedFilesToActiveCuePads (ListData& list, const juce::StringArray& validAudioFiles,
                                            const juce::StringArray& validVideoFiles);
    void finalizeAfterFileDropIngest();
    bool trySwitchListByShortcut (const juce::KeyPress& key);
    bool isShowControlManagedHotkey (const juce::KeyPress& key) const noexcept;
    bool matchesHotkeyBindingForKey (const juce::KeyPress& key) const noexcept;
    bool handleArrowNavigationKey (const juce::KeyPress& key);
    ListData* getActiveListSafe() noexcept;
    const ListData* getActiveListSafe() const noexcept;
    void detachInspectorFromPadsInList (ListData* list) noexcept;
    void releaseAllPadResources() noexcept;
    void shutdownAudioAndPads() noexcept;
    void releaseAllLookAndFeelAttachments() noexcept;
    void enterEmptyProjectState();
    void syncSidebarFromAllLists (const juce::Array<juce::String>& names);
    void syncPlayoutModeBarFromActiveList();
    void moveListInProject (int fromIdx, int toIdx);
    void openSetInSecondaryWindow (int listIndex);
    void duplicateListAtIndex (int listIndex);
    void addSoundsToSet (int listIndex);
    void morphSetStructure (int listIndex);
    void toggleListLock (int listIndex);
    void exportSetAtIndex (int listIndex);
    bool isActiveListLocked() const noexcept;
    void movePadInList (int listIdx, int fromPadIdx, int toPadIdx);
    void movePadToGridCellImpl (int listIdx, SoundPad* pad, int row, int col);
    void movePadsToGridCellImpl (int listIdx,
                                 const juce::Array<int>& padIndices,
                                 int anchorIndex,
                                 int targetRow,
                                 int targetCol);
    void performPadGridMutationWithUndo (int listIdx,
                                         const juce::String& actionName,
                                         std::function<void()> mutation);
    void sortListTracksAscending (int listIdx);
    void applyListSortAscending (int listIdx);
    void deletePadFromList (SoundPad* pad);
    void deletePadsFromList (int listIdx, const juce::Array<int>& padIndices);
    void deletePadsFromListImpl (int listIdx, const juce::Array<int>& padIndices);
    static juce::Array<int> buildBulkDeleteIndicesDescending (const juce::Array<int>& indices, int listSize);
    void deleteSelectedPadsFromActiveList();
    void compactCueListPads (ListData& list);
    void updateCueGridUIFromData (ListData& list);
    bool tryHandleDeleteOrBackspaceKey (const juce::KeyPress& key);
    static bool isSpacebarKey (const juce::KeyPress& key) noexcept;
    bool executeSpacebarTransportKey (const juce::KeyPress& key);
    bool handleApplicationHotkey (const juce::KeyPress& key);
    bool triggerPadByKeyCode (int keyCode, juce::ModifierKeys modifiers);
    bool triggerPadByKeyCode (const juce::KeyPress& key);
    void routePhysicalHotkeyFromKeyCode (int keyCode);
    bool tryTriggerPadByPhysicalKeyCode (int keyCode, juce::uint32 nowMs);
    bool tryTriggerPadByTelexAwareKeyPress (const juce::KeyPress& key, juce::uint32 nowMs);
    bool handleDeleteKeyForActiveSelection();
    void promptDeleteSelectedPadsConfirmation();
    juce::Array<int> collectActiveListDeletionIndices() const;
    void safelyPreparePadForDeletion (SoundPad* pad);
    void surgicalStopPadIfTransportActive (SoundPad* pad) noexcept;
    void surgicalStopTransportActivePadsInList (const ListData& list) noexcept;
    void detachDeckUiReferencesIfPadInList (const ListData& list) noexcept;
    void moveSelectedPadsInActiveListUp();
    void moveSelectedPadsInActiveListDown();
    void moveSelectedPadsInActiveListToTop();
    void moveSelectedPadsInActiveListToBottom();
    void movePadsBlockInList (int listIdx, const juce::Array<int>& sourceIndices, int insertBeforeIndex);
    void applySelectionForPadClick (int clickedIndex, const juce::ModifierKeys& mods);
    void applyPadSelectionVisualState();
    void saveActiveListSelection();
    void restoreListSelection (ListData& list);
    bool isPadSelectedInActiveList (int padIndex) const;
    void beginMarqueeSelection (juce::Point<int> posInScrollContent, const juce::ModifierKeys& mods);
    void updateMarqueeSelection (juce::Point<int> posInScrollContent);
    void endMarqueeSelection();
    void applyMarqueeSelectionToPads();
    juce::Rectangle<int> getMarqueeRectInScrollContent() const;
    void showTrackContextMenu (SoundPad* pad);
    void showBgmListBackgroundSortMenu (const juce::MouseEvent& e);
    void handleTrackMenuResult (SoundPad* pad, int result);
    void resetFadeForSelectedPads();
    void syncContextMenuTargetSelection (int listIdx, int padIdx);
    void promptReplaceTrackAudioFile (SoundPad* pad);
    void handleAudioFileReplacement (SoundPad* pad, const juce::File& file);
    void duplicatePadImpl (SoundPad* sourcePad);
    void duplicateSelectedPadsImpl();
    void duplicatePad (SoundPad* sourcePad);
    void duplicateSelectedPads();
    void duplicatePadAtIndex (int listIdx, int padIdx);
    void revealPadFileInOS (SoundPad* pad);

    struct PadGridLayout
    {
        int padW = 120, padH = 92, gap = 15, cols = 1, startX = 0, viewWidth = 0;
        juce::Rectangle<int> cellBounds (int slot) const noexcept;
    };

    void beginPadReorder (SoundPad* source);
    void updatePadReorder (juce::Point<int> posInScrollContent);
    void endPadReorder();
    void cancelPadReorder (bool keepPadGridDragVisual = false);
    int hitTestPadInsertIndex (juce::Point<int> local) const;
    PadGridLayout getPadGridLayout (int mainViewWidth, int mainViewHeight, int padCount) const;
    void movePadToGridCell (int listIdx, SoundPad* pad, int row, int col);
    bool assignNextFreeGridCell (ListData& list, SoundPad* pad);
    static juce::Point<int> findSmartDuplicateGridSlot (const ListData& list,
                                                        int sourceRow,
                                                        int sourceCol) noexcept;
    using GridOccupancy = std::array<std::array<bool, showcontrol::padgrid::kCols>,
                                     showcontrol::padgrid::kRows>;
    static GridOccupancy buildGridOccupancyFromList (const ListData& list) noexcept;
    static juce::Point<int> findProximitySlotInGrid (const GridOccupancy& grid,
                                                     int sourceRow,
                                                     int sourceCol) noexcept;
    static void markGridCellOccupied (GridOccupancy& grid, int row, int col) noexcept;
    SoundPad* createDuplicatePadFromSource (const ListData& list, SoundPad* src,
                                            bool makeVisible, bool fastRamClone = false);
    void finishBatchDuplicatedPads (const juce::Array<SoundPad*>& createdPads) noexcept;
    PadPanel* getPadPanel() const noexcept;
    void layoutActiveListPads();
    int findListIndexByName (const juce::String& name) const;
    juce::var buildSidebarListDragPayload (const juce::Array<int>& itemIds) const;
    juce::var buildPadPanelDragPayload (const juce::Array<int>& padIndices, int anchorIndex) const;
    static bool listHasLoadedAudio (const ListData& list) noexcept;
    void autoScrollViewportForPadReorder (juce::Point<int> posInScrollContent);
    juce::Rectangle<int> getListInsertLineBounds() const;
    juce::Rectangle<int> getGridGapCellBounds() const;
    void repaintPadReorderInsertStrip (int insertIndex) const;
    void repaintPadReorderCapsuleStrip (juce::Point<int> pointerInScrollContent) const;
    void paintPadReorderOverlay (juce::Graphics& g) const;
    void updatePadReorderOverlayBounds();
    bool canAcceptCrossCopyToPadGrid() const noexcept;
    juce::Point<int> mapDropPointToPadGridCell (juce::Point<int> localInMain) const noexcept;
    bool handleCrossCopyDropOnPadGrid (const juce::DragAndDropTarget::SourceDetails& dragSourceDetails);
    void setCrossCopyDropHighlightActive (bool active);
    void setPadPanelChildrenMousePassthrough (bool passthrough) noexcept;
    void setSidebarVisible (bool shouldShow);
    void setInspectorVisible (bool shouldShow);
    /** Chuyển PAD grid ↔ Cue List trong bộ CUE đang active (chỉ message thread). */
    void setPlayoutMode (bool isPadMode);
    void releaseUiFocusForViewSwitch();
    /** Ép resized + nạp lại nội dung vùng trung tâm sau toggle view (0ms, message thread). */
    void flushPlayoutViewGraphics (bool isPadMode);
    void refreshAllPanelThemes (bool shouldBeDark);
    void refreshLocalizedUi();
    void refreshLocalizedBusNames();
    void darkModeSettingChanged() override;
    void toggleStageMonitorWindow();
    void pushStageMonitorUpdate();
    StageMonitorSnapshot buildStageMonitorSnapshot (SoundPad* pad) const;
    SoundPad* findAnyActivePlayingPad() const;

    /**
     * MultiOutputAudioCallback — thay thế AudioSourcePlayer + MasterMixerSource.
     *
     * Kiến trúc routing:
     *   Mỗi PadRealtimeSource giữ atomic<int> outputBusIndex (0..kMaxBuses-1).
     *   Audio callback render từng pad vào tempBuffer (2ch, pre-allocated),
     *   rồi mix vào cặp kênh (bus*2, bus*2+1) của device output buffer.
     *
     * RT-safe: slots[] là std::atomic<PadRealtimeSource*> — audio callback snapshot
     * không khóa; unregister đợi PadRealtimeSource::waitUntilAudioIdle().
     */
    class MultiOutputAudioCallback : public juce::AudioIODeviceCallback
    {
    public:
        static constexpr int kMaxPadSlots = 256;   // 48 pads/list × tối đa lists
        static constexpr int kMaxBuses    = 8;     // 8 stereo bus = 16 output channels

        struct BusConfig
        {
            std::atomic<float> gain  { 1.0f };
            std::atomic<float> peakL { 0.0f };
            std::atomic<float> peakR { 0.0f };
            juce::String name;  // chỉ đọc/ghi từ message thread
        };

        std::array<BusConfig, kMaxBuses> buses;
        MasterOutputDynamics masterDynamics;

        void setMasterLimiterEnabled (bool on) noexcept;
        void setMasterLimiterThresholdDb (float db) noexcept;
        void setMasterLimiterReleaseMs (float ms) noexcept;
        bool getMasterLimiterEnabled() const noexcept;
        float getMasterLimiterThresholdDb() const noexcept;
        float getMasterLimiterReleaseMs() const noexcept;

        // Message thread: đăng ký/hủy pad source
        void registerSource   (PadRealtimeSource* src);
        void unregisterSource (PadRealtimeSource* src);
        void removeAllSources ();

        // Message thread: đặt tên bus (UI)
        void         setBusName (int bus, const juce::String& name);
        juce::String getBusName (int bus) const;
        juce::StringArray getAllBusNames() const;

        // Cross-thread atomics: master gain = bus 0 gain; VU = bus 0 peak
        void  setMasterGain (float gain) noexcept { buses[0].gain.store (gain, std::memory_order_relaxed); }
        float getMasterGain() const noexcept      { return buses[0].gain.load  (std::memory_order_relaxed); }
        float getLevelLeft()  const noexcept      { return masterMeterL.load (std::memory_order_relaxed); }
        float getLevelRight() const noexcept      { return masterMeterR.load (std::memory_order_relaxed); }

        void  setBusGain (int bus, float gain) noexcept
        {
            if (bus >= 0 && bus < kMaxBuses)
                buses[bus].gain.store (gain, std::memory_order_relaxed);
        }
        float getBusGain (int bus) const noexcept
        {
            return (bus >= 0 && bus < kMaxBuses) ? buses[bus].gain.load (std::memory_order_relaxed) : 1.0f;
        }
        float getBusPeakL (int bus) const noexcept
        {
            return (bus >= 0 && bus < kMaxBuses) ? buses[bus].peakL.load (std::memory_order_relaxed) : 0.0f;
        }
        float getBusPeakR (int bus) const noexcept
        {
            return (bus >= 0 && bus < kMaxBuses) ? buses[bus].peakR.load (std::memory_order_relaxed) : 0.0f;
        }

        // AudioIODeviceCallback
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
        void audioDeviceStopped()                                   override;
        void audioDeviceIOCallbackWithContext (
            const float* const* inputChannelData,  int numInputChannels,
            float* const*       outputChannelData, int numOutputChannels,
            int numSamples,
            const juce::AudioIODeviceCallbackContext& context)      override;

    private:
        std::array<std::atomic<PadRealtimeSource*>, kMaxPadSlots> slots {};

        // Pre-allocated render buffer (2ch × maxBlockSize) — resize trong audioDeviceAboutToStart
        juce::AudioBuffer<float> tempBuffer;

        std::atomic<bool> isPrepared    { false };
        std::atomic<float> masterMeterL { 0.0f };
        std::atomic<float> masterMeterR { 0.0f };
        int    currentBlockSize  = 512;
        double currentSampleRate = 44100.0;

        int findEmptySlot()                       const noexcept;
        int findSlotOf (const PadRealtimeSource*) const noexcept;
    };

    SoundPad* createSoundPad();
    void wireSoundPad (SoundPad* pad);
    void attachReadAheadToAllPads();
    void registerPadWithMixer (SoundPad* pad);
    void unregisterPadFromMixer (SoundPad* pad);
    void registerAllPadsWithMixer();
    void forceAllPadsIdleAtStartup();

    struct ListOrderUndoState
    {
        juce::Array<SoundPad*> padOrder;
        juce::Array<CueItem> cueMeta;
        juce::Array<int> padSelection;
        int primaryPadIndex = -1;
    };

    struct GridPositionsUndoState
    {
        struct Entry { SoundPad* pad = nullptr; int row = 0; int col = 0; };
        juce::Array<Entry> entries;
        juce::Array<int> padSelection;
        int primaryPadIndex = -1;
    };

    struct DeletedPadUndoEntry
    {
        int index = -1;
        std::unique_ptr<juce::XmlElement> padXml;
    };

    struct ListDeletionUndoState
    {
        int listIdx = -1;
        juce::Array<DeletedPadUndoEntry> removedPads;
        juce::Array<int> padSelection;
        int primaryPadIndex = -1;
    };

    struct PlaylistSnapshotUndoState
    {
        int listIdx = -1;
        int activeListIndexAtCapture = -1;
        juce::Array<int> activePadSelection;
        int activePrimaryPadIndex = -1;
        juce::String sidebarName;
        bool isGrid = true;
        bool isLooping = false;
        bool useCueListPanel = false;
        bool clickPadToTrigger = false;
        bool autoArmOnSelect = true;
        bool isLocked = false;
        juce::Colour themeColour = showcontrol::colours::defaultTagColour();
        juce::Array<CueItem> cueMeta;
        juce::OwnedArray<juce::XmlElement> padXmls;
    };

    ListOrderUndoState captureListOrderSnapshot (int listIdx) const;
    void restoreListOrderSnapshot (int listIdx, const ListOrderUndoState& state);
    GridPositionsUndoState captureGridPositionsSnapshot (int listIdx) const;
    void restoreGridPositionsSnapshot (int listIdx, const GridPositionsUndoState& state);
    ListDeletionUndoState captureListDeletionSnapshot (int listIdx,
                                                       const juce::Array<int>& padIndices) const;
    void restoreListDeletionSnapshot (const ListDeletionUndoState& state);
    PlaylistSnapshotUndoState capturePlaylistSnapshot (int listIdx) const;
    void restorePlaylistSnapshot (const PlaylistSnapshotUndoState& state);
    void deleteListAtIndexImpl (int idx);
    void movePadInListImpl (int listIdx, int fromPadIdx, int toPadIdx);
    void movePadsBlockInListImpl (int listIdx,
                                  const juce::Array<int>& sourceIndices,
                                  int insertBeforeIndex);
    void applyUndoXmlToPad (ListData& list, SoundPad* pad, const juce::XmlElement& padElem);
    SoundPad* insertPadFromUndoXml (ListData& list, int index, const juce::XmlElement& padElem);
    SoundPad* restorePadFromUndoXml (ListData& list, int index, const juce::XmlElement& padElem);
    void performUndoableMutation (const juce::String& transactionName,
                                  std::function<void()> performMutation,
                                  std::function<void()> undoMutation);
    void captureSelectionForUndoSnapshot (int listIdx,
                                          juce::Array<int>& outSelection,
                                          int& outPrimary) const;
    void applySelectionFromUndoSnapshot (int listIdx,
                                         const juce::Array<int>& selection,
                                         int primary);
    void hidePadsForAllListsExcept (int visibleListIdx);
    std::unique_ptr<juce::XmlElement> capturePadUndoSnapshot (const ListData& list, int padIdx) const;
    void applyPadUndoSnapshot (int listIdx, int padIdx, const juce::XmlElement& xml);
    void restoreListOrderAndDeleteOrphanPads (int listIdx, const ListOrderUndoState& before);
    void clearAllPanelsSelectionLive();
    void refreshAllPanelsAfterDataMutation (int listIdx);
    void refreshGridLayoutAfterMutation (int listIdx);
    void refreshPadGridLayoutFast (int listIdx);
    void refreshListOrderAfterMutation (int listIdx);
    static bool isSearchWindowFocused() noexcept;

    juce::UndoManager undoManager { 50 };
    std::atomic<bool> isPerformingUndoRedo { false };
    juce::HashMap<juce::uint64, juce::String> pendingPadRenameOldNames;
    struct InspectorPadUndoSession
    {
        int listIdx = -1;
        int padIdx = -1;
        std::unique_ptr<juce::XmlElement> beforeXml;
    };
    InspectorPadUndoSession inspectorPadUndoSession;
    juce::String inspectorNameEditOldValue;

    juce::TimeSliceThread timeSliceThread { "ShowCue ReadAhead" };
    juce::AudioDeviceManager deviceManager;
    MultiOutputAudioCallback multiOutputCallback;

    juce::TooltipWindow tooltipWindow { this };
    std::atomic<bool> isPerformingStateOperation { false };
    juce::InterProcessLock stateIoLock { "ShowCue_StateIO_Lock" };
    bool deferredStartupComplete = false;
    int activeListIndex = -1; int selectedBgmIndex = 0; bool isDarkMode = true;
    /** 0 = BÀN PAD, 1 = DANH SÁCH CUE QLab — định tuyến Space/P/S (không đè GO/Panic). */
    int activeList = 0;
    SoundPad* lastUiSyncedPlayingPad = nullptr;
    /** Cache updateMainDeskDisplay — tránh cập nhật MasterDeck mỗi 100 ms khi pad không đổi. */
    SoundPad* lastDeskDisplayPad = nullptr;
    juce::Array<bool> cachedSidebarListPlayingActive;
    int lastCuePlaybackListIndex = -1;
    int lastCuePlaybackPlayingIdx = -1;
    int lastCuePlaybackArmedIdx = -1;
    /** Token UI playback — pad user vừa chọn/phát; fade-out pad khác không được bẻ selection. */
    SoundPad* uiPlaybackFocusPad = nullptr;
    /** 1 = Dark, 2 = Light, 3 = Match System */
    int themePreferenceId = 1;
    /** 0 = Match System, 1 = Tiếng Việt, 2 = English */
    int languagePreferenceIndex = 1;
    bool projectDefaultNormalizeLufs = false;
    HotkeyScopeMode hotkeyScopeMode = HotkeyScopeMode::activeList;
    int lastHotkeyKeyCode = 0;
    juce::uint32 lastHotkeyTriggerMs = 0;
    TransportCommandKind lastBgmTransportKind  = TransportCommandKind::none;
    juce::uint32         lastBgmTransportCommandMs = 0;
    TransportCommandKind lastCueGoTransportKind  = TransportCommandKind::none;
    juce::uint32         lastCueGoCommandMs = 0;
    bool isCurrentlyDragging = false;
    SoundPad* padReorderSource = nullptr;
    int padReorderFromIndex = -1;
    int padReorderInsertIndex = -1;
    bool padReorderActive = false;
    bool crossComponentDragConsumed = false;
    bool crossCopyDropHighlightActive = false;
    juce::Rectangle<int> crossCopyDropHighlightBounds;
    juce::Point<int> padReorderPointerPos { 0, 0 };
    juce::Point<int> padReorderDragOffset { 0, 0 };
    juce::Image padReorderGhostImage;
    juce::int64 padReorderLastAutoScrollMs = 0;
    bool padReorderIsGridMode = false;
    juce::uint32 padReorderStackAnimStartMs = 0;
    bool padReorderStackAnimActive = false;
    void resetPadReorderVisualState();
    class PadReorderOverlay;
    class PlayoutModeBar;
    class PlayoutModeButtonLookAndFeel;
    class SplitterHandle;
    class SplitterButtonLookAndFeel;
    std::unique_ptr<PadReorderOverlay> padReorderOverlay;
    std::unique_ptr<SplitterHandle> leftSplitter;
    std::unique_ptr<SplitterHandle> rightSplitter;
    juce::Viewport viewScroller;
    std::unique_ptr<juce::Component> scrollContent;
    std::unique_ptr<CueListPanel> cueListPanel;
    juce::TextButton addMusicFloatingBtn;
    juce::TextButton showSidebarBtn;
    juce::TextButton showInspectorBtn;
    std::unique_ptr<StageMonitorWindow> stageMonitorWindow;
    std::unique_ptr<PlayoutModeBar> playoutModeBar;
    std::unique_ptr<PlayoutModeButtonLookAndFeel> playoutModeButtonLaf;
    std::unique_ptr<juce::FileChooser> mainFileChooser;
    std::unique_ptr<juce::FileChooser> exportFileChooser;
    std::unique_ptr<juce::FileChooser> trackReplaceFileChooser;
    juce::OwnedArray<SetSecondaryWindow> secondarySetWindows;
    std::unique_ptr<juce::Component> listHeaderComponent;
    class EmptyProjectPlaceholder;
    std::unique_ptr<EmptyProjectPlaceholder> emptyStatePanel;
    AudioEngine audioEngine;
    HotkeyManager hotkeyManager;
    std::unique_ptr<juce::Timer> pendingGoTimer;
    std::unique_ptr<OneShotApplicationTimer> startupReassertTimer;
    std::unique_ptr<OneShotApplicationTimer> startupGuardTimer;
    std::unique_ptr<OneShotApplicationTimer> deferredIdlePadsTimer;
    /** Chặn userPlayToggle / GO khi khôi phục selection lúc mở app (chỉ message thread). */
    std::atomic<bool> isInitialLoading { false };
    std::atomic<bool> audioFormatMigrationRunning { false };
    juce::uint32 startupInputGuardUntilMs = 0;
    juce::uint32 lastPanicFadeRequestMs = 0;
    std::atomic<bool> panicFadeDispatchScheduled { false };
    bool isPlaybackCommandBlocked() const noexcept
    {
        return ! deferredStartupComplete
            || isInitialLoading.load (std::memory_order_relaxed);
    }
    int pendingGoPadIndex = -1;
    int sidebarWidth = 250;
    int inspectorWidth = 360;
    bool sidebarVisible = true;
    bool inspectorVisible = true;
    juce::Array<int> selectedPadIndices;
    juce::Array<int> marqueeBaseSelection;
    juce::Point<int> marqueeStartPos { 0, 0 };
    juce::Point<int> marqueeEndPos { 0, 0 };
    bool marqueeSelectionPrimed = false;
    bool marqueeSelectionActive = false;
    bool marqueeSelectionAdditive = false;
    bool marqueeSelectionRangeSmart = false;
    int marqueeAnchorIndex = -1;
    SoundPad* soloPad = nullptr;
    struct SoloGainBackup { SoundPad* pad = nullptr; float gain = 1.0f; };
    juce::Array<SoloGainBackup> soloGainBackups;

    juce::ComponentDragger windowDragger;
    bool windowDragActive = false;

    MasterDeckPanel masterDeckPanel;
    BusMixerPanel busMixerPanel;
    juce::ComponentAnimator layoutAnimator;
    std::unique_ptr<SplitterButtonLookAndFeel> splitterButtonLaf;

    std::unique_ptr<showcontrol::update::ShowUpdateChecker> updateChecker;
    std::unique_ptr<showcontrol::osc::ShowOscListener> oscListener;
    std::unique_ptr<showcontrol::backup::ShowBackupSyncBroadcaster> backupBroadcaster;
    std::atomic<bool> syncApplying { false };
    bool backupTakeoverActive = false;
    juce::uint32 lastPrimaryHeartbeatRxMs = 0;
    int lastBroadcastSelectionList = -2;
    int lastBroadcastSelectionPad  = -2;
    int lastBroadcastSelectionView = -2;
    juce::Array<int> lastBroadcastSelectionMulti;
    juce::uint32 heartbeatSendSeq = 0;
    juce::uint32 lastHeartbeatTickMs = 0;
    std::unique_ptr<juce::DatagramSocket> backupDiscoverySocket;
    juce::uint32 lastAutosaveAtMs = 0;
    void maybeRunAutosave();

    /** Khai báo cuối — hủy sau cùng (sau pads/audio) để tránh leak AudioFormat. */
    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};