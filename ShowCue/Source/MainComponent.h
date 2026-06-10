#pragma once
#include <atomic>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h> 
#include <juce_audio_utils/juce_audio_utils.h>     
#include "ShowTheme.h"
#include "ShowControlLookAndFeel.h"
#include "ShowAudioFormats.h"
#include "SidebarPanel.h"
#include "SoundPad.h"
#include "InspectorPanel.h"
#include "MasterDeckPanel.h"
#include "CueListPanel.h"
#include "AudioEngine.h"
#include "HotkeyManager.h"
#include "HotkeyAssignDialog.h"
#include "VideoAudioExtractor.h"
#include "FfmpegSetupDialog.h"
#include "AudioDeviceSettingsPanel.h"
#include "BusMixerPanel.h"
#include "ErrorHandler.h"
#include "SetSecondaryWindow.h"
#include "StageMonitorComponent.h"

namespace ShowControlCommandIDs
{
    enum
    {
        showAboutDialog  = 0x53430001,
        checkForUpdates  = 0x53430002,
        openPreferences  = 0x53430003
    };
}

class MainComponent  : public juce::Component,
                       public juce::DragAndDropContainer,
                       public juce::Timer,
                       public juce::FileDragAndDropTarget,
                       public juce::KeyListener,
                       public juce::ApplicationCommandTarget,
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
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyPressed (const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged (bool isKeyDown) override;
    void timerCallback() override; 
    void visibilityChanged() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void loadList (int listIndex, int trackCount, bool isGrid);
    void saveProject(); 
    void loadProject(); 
    juce::File getProjectFile();
    void triggerManualMusicIngestion();
    void applyThemePreference (int themeId);
    void lookAndFeelChanged() override;
    void refreshSidebarPlayingStatus();
    void showAudioSettingsDialog();
    void showPreferencesDialog (int initialTabIndex = 0);
    void showAboutDialog();
    void checkForUpdates();
    void showHotkeyAssignDialogForPad (SoundPad* pad);
    void crossfadeOtherPadsOnSameBus (SoundPad* starter, int listIndex);
    void ingestVideoFileToPad (const juce::File& videoFile, SoundPad* targetPad);
    void offerFfmpegSetupThenIngestVideo (const juce::File& videoFile, SoundPad* targetPad);
    void normalizeActiveList (bool useLufs);
    void applyProjectDefaultsToPad (SoundPad* pad) const;
    void triggerGlobalPanicFadeAll();

    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform (const juce::ApplicationCommandTarget::InvocationInfo& info) override;

private:
    juce::Component* topLevelKeyListenerHost = nullptr;
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
    };

    SoundPad* ensurePadSlotAtIndex (ListData& list, int index);
    void syncCueMetadataFromPads (ListData& list);
    void refreshCueListPanel();
    bool triggerCueGo (int padIndex);
    bool triggerCueListPlay (int padIndex);
    bool triggerCueListPause (int padIndex);
    bool triggerCueListStop (int padIndex);
    bool isCueListViewActive() const noexcept;
    void armPad (SoundPad* pad);
    void setSoloPad (SoundPad* pad, bool enable);
    void updateCuePlaybackIndicators();
    /** Đồng bộ selection + inspector + master deck theo pad đang phát (single source of truth). */
    void syncUiToPlayingPad (SoundPad* pad, bool scrollIntoView);
    SoundPad* findPlayingPadInActiveBgmList() const;
    bool triggerPadFromHotkey (const HotkeyBinding& binding);
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
    void deletePadFromList (SoundPad* pad);
    void deletePadsFromList (int listIdx, const juce::Array<int>& padIndices);
    void deleteSelectedPadsFromActiveList();
    void compactCueListPads (ListData& list);
    void updateCueGridUIFromData (ListData& list);
    bool tryHandleDeleteOrBackspaceKey (const juce::KeyPress& key);
    bool handleDeleteKeyForActiveSelection();
    void promptDeleteSelectedPadsConfirmation();
    juce::Array<int> collectActiveListDeletionIndices() const;
    void safelyPreparePadForDeletion (SoundPad* pad);
    void moveSelectedPadsInActiveListUp();
    void moveSelectedPadsInActiveListDown();
    void moveSelectedPadsInActiveListToTop();
    void moveSelectedPadsInActiveListToBottom();
    void movePadsBlockInList (int listIdx, const juce::Array<int>& sourceIndices, int insertBeforeIndex);
    void applySelectionForPadClick (int clickedIndex, const juce::ModifierKeys& mods);
    void applyPadSelectionVisualState();
    bool isPadSelectedInActiveList (int padIndex) const;
    void beginMarqueeSelection (juce::Point<int> posInScrollContent, const juce::ModifierKeys& mods);
    void updateMarqueeSelection (juce::Point<int> posInScrollContent);
    void endMarqueeSelection();
    void applyMarqueeSelectionToPads();
    juce::Rectangle<int> getMarqueeRectInScrollContent() const;
    void showTrackContextMenu (SoundPad* pad);
    void handleTrackMenuResult (SoundPad* pad, int result);
    void resetFadeForSelectedPads();
    void syncContextMenuTargetSelection (int listIdx, int padIdx);
    void promptReplaceTrackAudioFile (SoundPad* pad);
    void handleAudioFileReplacement (SoundPad* pad, const juce::File& file);
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
    void cancelPadReorder();
    int hitTestPadInsertIndex (juce::Point<int> local) const;
    PadGridLayout getPadGridLayout (int mainViewWidth, int mainViewHeight, int padCount) const;
    void layoutActiveListPads();
    static bool listHasLoadedAudio (const ListData& list) noexcept;
    void autoScrollViewportForPadReorder (juce::Point<int> posInScrollContent);
    juce::Rectangle<int> getListInsertLineBounds() const;
    juce::Rectangle<int> getGridGapCellBounds() const;
    void paintPadReorderOverlay (juce::Graphics& g) const;
    void updatePadReorderOverlayBounds();
    void setSidebarVisible (bool shouldShow);
    void setInspectorVisible (bool shouldShow);
    /** Chuyển PAD grid ↔ Cue List trong bộ CUE đang active (chỉ message thread). */
    void setPlayoutMode (bool isPadMode);
    void refreshAllPanelThemes (bool shouldBeDark);
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
     * RT-safe: CriticalSection bảo vệ danh sách pad cho add/remove từ message thread.
     * Lock này hiếm khi bị tranh chấp (chỉ khi user thêm/xóa pad) nên tác động thực tế
     * đến luồng audio là negligible — cùng pattern với juce::MixerAudioSource.
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
        float getLevelLeft()  const noexcept      { return buses[0].peakL.load (std::memory_order_relaxed); }
        float getLevelRight() const noexcept      { return buses[0].peakR.load (std::memory_order_relaxed); }

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
        std::array<PadRealtimeSource*, kMaxPadSlots> slots {};

        // CriticalSection: bảo vệ slots[] — cùng pattern juce::MixerAudioSource.
        // Audio callback giữ lock trong thời gian xử lý block; message thread giữ
        // khi add/remove. Contention xảy ra cực hiếm → tác động RT không đáng kể.
        juce::CriticalSection padListCS;

        // Pre-allocated render buffer (2ch × maxBlockSize) — resize trong audioDeviceAboutToStart
        juce::AudioBuffer<float> tempBuffer;

        std::atomic<bool> isPrepared    { false };
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

    juce::TimeSliceThread timeSliceThread { "ShowControl ReadAhead" };
    juce::AudioDeviceManager deviceManager;
    MultiOutputAudioCallback multiOutputCallback;

    ShowControlLookAndFeel appLookAndFeel;
    juce::TooltipWindow tooltipWindow { this };
    int activeListIndex = -1; int selectedBgmIndex = 0; bool isDarkMode = true;
    /** 0 = BÀN PAD, 1 = DANH SÁCH CUE QLab — định tuyến Space/P/S (không đè GO/Panic). */
    int activeList = 0;
    SoundPad* lastUiSyncedPlayingPad = nullptr;
    int themePreferenceId = 1;
    bool projectDefaultNormalizeLufs = false;
    HotkeyScopeMode hotkeyScopeMode = HotkeyScopeMode::activeList;
    int lastHotkeyKeyCode = 0;
    juce::uint32 lastHotkeyTriggerMs = 0;
    bool inExclusiveKeyHandler = false;
    TransportCommandKind lastBgmTransportKind  = TransportCommandKind::none;
    juce::uint32         lastBgmTransportCommandMs = 0;
    TransportCommandKind lastCueGoTransportKind  = TransportCommandKind::none;
    juce::uint32         lastCueGoCommandMs = 0;
    bool isCurrentlyDragging = false;
    SoundPad* padReorderSource = nullptr;
    int padReorderFromIndex = -1;
    int padReorderInsertIndex = -1;
    bool padReorderActive = false;
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
    juce::Slider gridSizeSlider;
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
    /** Chặn userPlayToggle / GO khi khôi phục selection lúc mở app (chỉ message thread). */
    std::atomic<bool> isInitialLoading { false };
    juce::uint32 startupInputGuardUntilMs = 0;
    bool isPlaybackCommandBlocked() const noexcept
    {
        return isInitialLoading.load (std::memory_order_relaxed);
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};