#pragma once
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include "AudioAnalyzer.h"
#include "AudioMetadataReader.h"
#include "PadCueState.h"
#include "PadRealtimeSource.h"
#include "ShowGraphicsSafe.h"
#include "ShowFlatIcons.h"
#include "ShowTheme.h"
#include "ShowTagColors.h"
#include "ShowPadGridMatrix.h"
#include "ShowCrossComponentDrag.h"
#include "ShowControlLookAndFeel.h"
#include "ShowLocalization.h"
#include "ShowWaveformCache.h"
#include "ShowAudioPreloadCache.h"
#include "VideoAudioExtractor.h"
#include "ShowLoudnessNormalize.h"

namespace showcontrol::audio
{
inline juce::AudioFormatManager*& activeFormatManagerPtr() noexcept
{
    static juce::AudioFormatManager* ptr = nullptr;
    return ptr;
}

inline void bindActiveFormatManager (juce::AudioFormatManager& manager) noexcept
{
    activeFormatManagerPtr() = &manager;
}

inline void unbindActiveFormatManager() noexcept
{
    activeFormatManagerPtr() = nullptr;
}

inline juce::AudioFormatManager& sharedFormatManager()
{
    if (activeFormatManagerPtr() != nullptr)
        return *activeFormatManagerPtr();

    static juce::AudioFormatManager fallbackManager;
    static bool formatsRegistered = false;

    if (! formatsRegistered)
    {
        fallbackManager.registerBasicFormats();
        formatsRegistered = true;
    }

    return fallbackManager;
}
} // namespace showcontrol::audio

namespace showcontrol::background
{
inline juce::ThreadPool& pool()
{
    // Song song I/O: preload BGM + waveform + normalize không chặn message thread.
    static juce::ThreadPool tp { 5 };
    return tp;
}

class LambdaJob final : public juce::ThreadPoolJob
{
public:
    explicit LambdaJob (std::function<void()> fnIn)
        : juce::ThreadPoolJob ("sc-bg"), fn (std::move (fnIn)) {}

    JobStatus runJob() override
    {
        if (fn) fn();
        return jobHasFinished;
    }

private:
    std::function<void()> fn;
};

inline void enqueue (std::function<void()> fn)
{
    pool().addJob (new LambdaJob (std::move (fn)), true);
}

inline void shutdownPool() noexcept
{
    pool().removeAllJobs (true, 5000);
}
} // namespace showcontrol::background

//==============================================================================
class VolumeNormalizer
{
public:
    /** Đo đầy đủ trên thread pool — callback luôn trên message thread. */
    void analyzeAudioFile (const juce::File& file,
                           std::function<void (AudioAnalyzer::FileLoudnessAnalysis)> onComplete)
    {
        if (! file.existsAsFile() || ! onComplete)
            return;

        isAnalyzing.store (true, std::memory_order_release);

        showcontrol::background::enqueue ([file, cb = std::move (onComplete)]() mutable
        {
            auto& localFormatManager = showcontrol::audio::sharedFormatManager();
            const auto analysis = AudioAnalyzer::analyzeFile (file, localFormatManager);

            juce::MessageManager::callAsync ([cb = std::move (cb), analysis]() mutable
            {
                cb (analysis);
            });
        });
    }

    void markFinished() noexcept { isAnalyzing.store (false, std::memory_order_release); }
    bool isBusy() const noexcept { return isAnalyzing.load (std::memory_order_acquire); }

private:
    std::atomic<bool> isAnalyzing { false };
};

//==============================================================================
class SoundPad : public juce::Component,
                 public juce::Timer,
                 private juce::ChangeListener,
                 private juce::Label::Listener
{
public:
    struct LoadedAudioPayload
    {
        juce::File file;
        std::unique_ptr<juce::AudioFormatReader> reader;
        std::unique_ptr<juce::MemoryMappedFile> mappedFile;
        juce::AudioBuffer<float> slicePreloadBuffer;
        juce::AudioBuffer<float> fullRamBuffer;
        double sampleRate = 44100.0;
        juce::String displayName;
        AudioMetadata meta;
        bool usesFullRam = false;
    };

    /** Slice head + mmap theo PRELOAD_SECONDS — đọc đĩa trên TimeSliceThread, không trong audio callback. */
    static int defaultReadAheadSamples (double sampleRate) noexcept
    {
        return showcontrol::preload::readAheadSamplesForRate (sampleRate);
    }

    explicit SoundPad (juce::AudioFormatManager& formatManagerIn)
        : formatManager (formatManagerIn),
          thumbnail (384, formatManager, showcontrol::waveform::sharedCache()),
          realtimeSource (transportSource)
    {
        thumbnail.addChangeListener (this);
        setWantsKeyboardFocus (false);
        rebuildPaintResources();

        addChildComponent (trackNameLabel);
        trackNameLabel.setVisible (false);
        trackNameLabel.addListener (this);
    }
    
    ~SoundPad() override
    {
        trackNameLabel.removeListener (this);
        stopTimer();
        cancelPendingAsyncWork();
        releaseThumbnailResources();
        realtimeSource.postStop();
        transportSource.setSource (nullptr);
        readerSource.reset();
        memorySource.reset();
        ownedFullRamBuffer.setSize (0, 0);
    }

    /** Message thread: vô hiệu callAsync/enqueue đang treo + dừng normalize — gọi trước remove/compact. */
    void cancelPendingAsyncWork() noexcept
    {
        audioLoadGeneration.fetch_add (1, std::memory_order_acq_rel);
        playbackPreloadRequested.store (false, std::memory_order_relaxed);
        hasPendingNormalization = false;
        pendingNormalizationFile = juce::File();
        normalizer.markFinished();
    }

    /** UI shutdown: gỡ listener/cache trước khi hủy SoundPad — tránh dangling + leak cache. */
    void releaseThumbnailResources() noexcept
    {
        thumbnail.removeChangeListener (this);
        thumbnail.setSource (nullptr);
        thumbnail.clear();

        if (thumbnailLoaded && musicFile.existsAsFile())
        {
            showcontrol::waveform::sharedCache().removeThumb (
                showcontrol::waveform::hashForAudioFile (musicFile));
        }

        thumbnailLoaded = false;
    }

    void setSharedTimeSliceThread (juce::TimeSliceThread* thread)
    {
        sharedTimeSliceThread = thread;
        ensureReadAheadBuffer();
    }

    std::function<void(SoundPad*, const juce::ModifierKeys&)> onSelected;
    std::function<void()> onPlaybackStateChanged;
    /** BGM: dừng pad khác trong list trước khi phát (Foobar playlist). */
    std::function<void(SoundPad*)> onWillStartPlay;
    std::function<void(SoundPad*)> onContextMenuRequested;
    std::function<void(SoundPad*, juce::Point<int> pointerInPanel)> onPadReorderBegin;
    std::function<void(SoundPad*, juce::Point<int>)> onPadReorderMove;
    std::function<void(SoundPad*)> onPadReorderEnd;
    /** Grid DnD: mô tả multi-pad cho DragAndDropContainer::startDragging. */
    std::function<juce::var()> onBuildPadDragDescription;
    /** List DnD: payload SIDEBAR_LIST khi kéo từ BGM/CUE list view. */
    std::function<juce::var()> onBuildSidebarListDragDescription;
    std::function<juce::Image (int itemCount)> onCreateMultiItemDragImage;
    /** List reorder: số dòng đang chọn (badge capsule drag). */
    std::function<int()> onGetRowReorderDragCount;
    std::function<void(SoundPad*)> onTrackFinished; // Cổng callback báo tử chuyển bài liên tục Foobar2000 Mode
    std::function<void(SoundPad*)> onAudioFileLoaded;
    /** Background normalize xong — Inspector cập nhật nhãn LUFS/RMS. */
    std::function<void(SoundPad*)> onNormalizationComplete;
    /** Farrago/QLab: click pad hoặc phím GO — MainComponent xử lý pre-wait. */
    std::function<void(SoundPad*)> onRequestGo;
    /** true trong lúc startup reassert — chặn triggerPlay/Stop ảo từ UI. */
    std::function<bool()> isPlaybackCommandBlocked;
    /** Token UI playback — pad fade-out cũ return im lặng khi không còn là focus. */
    std::function<bool()> isActivePlaybackUiOwner;

    /** wireSoundPad chỉ gán callback một lần — tránh đăng ký listener trùng khi loadList. */
    bool isUiCallbacksWired = false;

    /** Chặn trigger hotkey/GO trùng trên cùng pad (message thread, 300ms). */
    bool tryClaimPadHotkeyTrigger (juce::uint32 holdoffMs = 100) noexcept
    {
        const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
        const juce::uint32 untilMs = hotkeyTriggerGuardUntilMs.load (std::memory_order_acquire);

        if (nowMs < untilMs)
            return false;

        hotkeyTriggerGuardUntilMs.store (nowMs + holdoffMs, std::memory_order_relaxed);
        return true;
    }

    void clearHotkeyTriggerGuard() noexcept
    {
        hotkeyTriggerGuardUntilMs.store (0, std::memory_order_relaxed);
    }

    void setClickToTrigger (bool shouldTriggerOnClick) noexcept { clickToTriggerOnClick = shouldTriggerOnClick; }
    bool getClickToTrigger() const noexcept { return clickToTriggerOnClick; }

    void setArmed (bool armed) noexcept
    {
        if (isArmedState != armed)
        {
            isArmedState = armed;
            repaint();
        }
    }

    bool isArmed() const noexcept { return isArmedState; }

    void setIsCurrentlyDragged (bool dragged) noexcept
    {
        if (isCurrentlyDragged == dragged)
            return;

        isCurrentlyDragged = dragged;
        setAlpha (dragged ? 0.0f : 1.0f);
        resized();
        repaint();
    }

    bool isCurrentlyDraggedState() const noexcept { return isCurrentlyDragged; }
    bool hasActiveJuceSystemDrag() const noexcept { return juceSystemDragStarted; }

    /** Đọc trước buffer trên message thread — gọi trước GO để giảm trễ. */
    void prepareForInstantPlay()
    {
        warmReadAheadPipeline();
    }

    /**
     * Predictive preload: đảm bảo reader + read-ahead sẵn sàng khi user chọn dòng BGM.
     * Chỉ message thread gọi; I/O nặng đã ở ThreadPool.
     */
    void requestPreloadForPlayback() noexcept
    {
        playbackPreloadRequested.store (true, std::memory_order_relaxed);

        if (musicFile.existsAsFile())
            showcontrol::preload::sharedPool().requestPreload (musicFile);

        if (! hasFile && ! isLoading() && musicFile.existsAsFile())
            loadAudioFileInternal (musicFile);
        else if (hasFile)
            prepareForInstantPlay();

        if (thumbnailLoadAllowedNow)
            ensureThumbnailLoaded();
    }

    PadCueState getCueState() const noexcept { return cueState.load (std::memory_order_relaxed); }
    bool isLoading() const noexcept { return getCueState() == PadCueState::loading; }

    juce::AudioTransportSource& getTransportSource() { return transportSource; }
    juce::AudioSource& getMixerAudioSource() { return realtimeSource; }
    juce::AudioThumbnail& getThumbnail() { return thumbnail; }

    // Lazy resources for faster startup:
    // - Thumbnail (waveform) chỉ setSource khi được phép.
    // - Auto-normalize (RMS) có thể trì hoãn cho PAD ngoài viewport.
    void setThumbnailLoadAllowed (bool allow, bool loadImmediately = true) noexcept
    {
        thumbnailLoadAllowedNow = allow;

        if (allow && loadImmediately)
            ensureThumbnailLoaded();
    }

    void setNormalizationLoadAllowed (bool allow, bool analyzeImmediately = true)
    {
        normalizationAllowedNow = allow;

        if (allow && analyzeImmediately)
            maybeStartNormalization();
    }

    bool isThumbnailLoaded() const noexcept { return thumbnailLoaded; }

    /** Ép nạp lại FileInputSource vào AudioThumbnail (sau load config / drop file). */
    void reloadWaveformThumbnail (bool force = false)
    {
        const juce::File file = musicFile.existsAsFile() ? musicFile : thumbnailPendingFile;

        if (! file.existsAsFile())
            return;

        if (! force && thumbnailLoaded && thumbnailPendingFile == file)
            return;

        thumbnailPendingFile = file;

        if (thumbnailLoaded)
        {
            thumbnail.setSource (nullptr);
            thumbnail.clear();
            thumbnailLoaded = false;
            invalidateWaveformBaseCache();
        }

        ensureThumbnailLoaded();
    }

    /** Chỉ queue load nếu chưa có — tránh xóa cache khi layout lại grid/list. */
    void requestWaveformThumbnailLoad() noexcept
    {
        if (thumbnailLoaded)
            return;

        ensureThumbnailLoaded();
    }

    /** UI đọc cueState (cập nhật ngay khi bấm); audio thread đồng bộ sau. */
    bool isPlaying() const { return getCueState() == PadCueState::playing; }
    bool isPaused() const { return getCueState() == PadCueState::paused; }
    bool isStopping() const { return getCueState() == PadCueState::stopping; }
    bool isTransportActive() const
    {
        return isPlaying() || isPaused() || isFading() || isStopping();
    }

    /** Chỉ playing/paused — không tính fade-out stop (UI/waveform/highlight). */
    bool isPlaybackPositionLive() const noexcept
    {
        return isPlaying() || isPaused();
    }
    bool usesCuePauseResume() const noexcept { return isCueListPlayback; }

    void setCueListPlayback (bool isCueList) noexcept { isCueListPlayback = isCueList; }

    bool isRegisteredWithMasterMixer() const noexcept { return mixerRegisteredWithMaster; }
    void markRegisteredWithMasterMixer() noexcept { mixerRegisteredWithMaster = true; }
    void markUnregisteredFromMasterMixer() noexcept { mixerRegisteredWithMaster = false; }
    double getPlaybackPosition() const
    {
        if (! isPlaybackPositionLive())
            return getTrimStart();

        return realtimeSource.getPublishedPosition();
    }
    double getPlaybackLength() const { return realtimeSource.getPublishedLength(); }
    void seekTo (double seconds) { realtimeSource.postSeek (seconds); }
    void setOutputGain (float gain) { realtimeSource.postSetGain (gain); }
    float getOutputGain() const { return realtimeSource.getPublishedGain(); }

    /** Thời gian đã phát trong vùng trim (giây, native transport). */
    double getElapsedSeconds() const
    {
        double tStart = 0.0, tEnd = 0.0;
        getTrimmedDisplayRange (tStart, tEnd);
        juce::ignoreUnused (tEnd);
        return juce::jmax (0.0, getPlaybackPosition() - tStart);
    }

    /** Thời gian còn lại trong vùng trim (giây, native transport). */
    double getRemainingSeconds() const
    {
        if (! hasFile)
            return 0.0;

        double tStart = 0.0, tEnd = 0.0;
        getTrimmedDisplayRange (tStart, tEnd);
        juce::ignoreUnused (tStart);
        return juce::jmax (0.0, tEnd - getPlaybackPosition());
    }

    /** Output bus routing cho MultiOutputAudioCallback — RT-safe atomic. */
    void setOutputBus (int bus) noexcept { realtimeSource.setOutputBus (bus); }
    int  getOutputBus() const noexcept   { return realtimeSource.getOutputBus(); }

    /** Expose RT source trực tiếp để MultiOutputAudioCallback lưu con trỏ. */
    PadRealtimeSource& getRealtimeSource() noexcept { return realtimeSource; }
    bool isLooping() const { return isLoopingState; }
    /** Cue: loop ô. BGM: mặc định auto-next; bật loop = giữ lại bài này (playlist vẫn loop list khi hết chuỗi). */
    void setLooping (bool s)
    {
        if (isLoopingState == s)
            return;

        isLoopingState = s;
        // Loop theo trim do PadRealtimeSource xử lý — file-loop của reader gây nhảy position khi bật giữa chừng.
        if (readerSource)
            readerSource->setLooping (false);
        realtimeSource.setLooping (s);

        if (! isRenderAsGridMode)
            rebuildPaintResources();

        repaint();
    }
    juce::String getFilePath() const { return hasFile ? musicFile.getFullPathName() : ""; }

    /** Đường dẫn đã gán (kể cả khi file đang load async) — dùng migration / save JSON. */
    juce::String getConfiguredFilePath() const noexcept { return musicFile.getFullPathName(); }

    juce::String getPadName() const
    {
        if (customName.isNotEmpty())
            return customName;

        if (cachedMeta.title.isNotEmpty())
            return cachedMeta.title;

        return hasFile ? cachedFileName : juce::String::fromUTF8 (u8"Trống");
    }

    void setCustomName (const juce::String& name)
    {
        customName = name.trim();
        repaint();
    }

    juce::Colour getTagColour() const noexcept { return tagColour; }

    juce::Colour& getTagColourRef() noexcept { return tagColour; }

    /** Live 0ms — Inspector / palette gọi trực tiếp; ép waveform cache đổi màu ngay. */
    void setPadThemeColour (juce::Colour newColour)
    {
        const auto snapped = showcontrol::colours::snapToPalette (newColour);
        tagColour = snapped;
        invalidateWaveformBaseCache();
        repaint();
    }

    void setTagColour (juce::Colour c)
    {
        const auto snapped = showcontrol::colours::snapToPalette (c);

        if (tagColour.getARGB() == snapped.getARGB())
        {
            invalidateWaveformBaseCache();
            repaint();
            return;
        }

        setPadThemeColour (snapped);
    }

    /** Nền thẻ PAD — trắng (sáng) / xám đen flat (tối). */
    juce::Colour getPadSurfaceColour() const noexcept
    {
        return showcontrol::colours::padSurfaceColour (isDarkMode);
    }

    /** Màu mực chủ đề — tiêu đề, hotkey, waveform trên thẻ PAD Farrago. */
    juce::Colour getActiveInkColour() const noexcept
    {
        const auto& pal = ShowTheme::get (isDarkMode);

        if (showcontrol::colours::isDefaultTagColour (tagColour))
            return pal.textPrimary;

        return tagColour;
    }

    juce::Colour getPadThemeColour() const noexcept { return getActiveInkColour(); }

    juce::Colour getPadTitleColour() const noexcept
    {
        return getActiveInkColour();
    }

    juce::Colour getWaveformFillColour() const noexcept
    {
        const auto& pal = ShowTheme::get (isDarkMode);

        if (showcontrol::colours::isDefaultTagColour (tagColour))
            return pal.waveformFill.withAlpha (showcontrol::colours::kPadWaveformInkAlpha);

        return tagColour.withAlpha (showcontrol::colours::kPadWaveformInkAlpha);
    }

    juce::Colour getPadSelectionBorderColour() const noexcept
    {
        if (showcontrol::colours::isDefaultTagColour (tagColour))
            return ShowTheme::get (isDarkMode).accent;

        return tagColour;
    }

    /** List row hoặc ô PAD grid — double-click tên / menu chuột phải. Message thread only. */
    void beginTrackNameEdit()
    {
        if (isLoading())
            return;

        trackNameEditing = true;
        ShowControlLookAndFeel::applyTrackNameLabelStyle (trackNameLabel, isDarkMode, isSelectedRowState);
        trackNameLabel.setText (getPadName(), juce::dontSendNotification);
        layoutTrackNameLabel();
        trackNameLabel.setVisible (true);
        trackNameLabel.toFront (false);
        repaint();

        juce::Component::SafePointer<SoundPad> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr || ! safe->trackNameLabel.isVisible())
                return;

            safe->trackNameLabel.showEditor();
        });
    }

    bool isPointInTrackNameArea (int localX, int localY) const noexcept
    {
        if (isRenderAsGridMode)
        {
            constexpr int kTitlePadLeft  = 12;
            constexpr int kTitlePadRight = 10;
            const int titleWidth = juce::jmax (0, getWidth() - kTitlePadLeft - kTitlePadRight);
            return localX >= kTitlePadLeft && localX < kTitlePadLeft + titleWidth
                && localY >= 6 && localY < 28;
        }

        int textX = showcontrol::bgmList::kNameStartDefault;

        if (isPlaying() || (isCueListPlayback && isPaused()))
            textX = showcontrol::bgmList::kNameStartWithStatusIcon;

        const bool reserveLoopSlot = ! isCueListPlayback && hasFile;
        const auto nameLayout = showcontrol::bgmList::layoutListNameRow (getWidth(), getHeight(), textX,
                                                                         reserveLoopSlot);
        return nameLayout.nameArea.contains (localX, localY);
    }

    std::function<void (SoundPad*)> onTrackNameChanged;
    std::function<void (SoundPad*)> onTrackNameEditBegan;

    juce::String getSearchableTokens() const
    {
        return shortcutLabel + " " + cachedFileName + " " + customName + " "
             + cachedMeta.title + " " + cachedMeta.artist + " " + cachedMeta.album
             + " " + getFilePath();
    }
    juce::String getShortcutLabel() const { return shortcutLabel; }
    bool hasAudioFile() const { return hasFile && ! isLoading(); }
    /** CUE grid động: giữ ô khi đang load, đã có file, hoặc còn đường dẫn cấu hình (kể cả file tạm thiếu trên đĩa). */
    bool occupiesCueGridSlot() const noexcept
    {
        return isLoading() || hasFile || musicFile.getFullPathName().isNotEmpty();
    }
    int getPadIndex() const { return myIndex; }
    juce::String getPadDragIdentityToken() const noexcept { return juce::String (myIndex); }
    int getGridRow() const noexcept { return gridRow; }
    int getGridCol() const noexcept { return gridCol; }

    void updateGridPosition (int row, int col) { setGridPosition (row, col, true); }

    void refreshHotkeyLabel()
    {
        refreshShortcutLabelFromGrid();
        repaint();
    }

    void setTrimStart (double seconds)
    {
        trimStart = std::max (0.0, seconds);
        realtimeSource.setTrimRange (trimStart, trimEnd);
    }

    void setTrimEnd (double seconds)
    {
        trimEnd = std::max (0.0, seconds);
        realtimeSource.setTrimRange (trimStart, trimEnd);
    }
    double getTrimStart() const { return trimStart; }
    double getTrimEnd()   const { return trimEnd; }

    /** Khoảng thời gian hiển thị waveform (đã áp trim). */
    void getTrimmedDisplayRange (double& outStart, double& outEnd) const noexcept
    {
        const double total = getPlaybackLength();
        outStart = trimStart;
        outEnd   = (trimEnd > 0.0) ? std::min (trimEnd, total) : total;
    }

    bool hasTrimApplied() const noexcept
    {
        const double total = getPlaybackLength();
        if (total <= 0.0)
            return false;

        double t0 = 0.0, t1 = 0.0;
        getTrimmedDisplayRange (t0, t1);
        return t0 > 0.01 || t1 < total - 0.01;
    }

    void triggerTrimUpdateLive()
    {
        repaint();
        if (onPlaybackStateChanged) 
            onPlaybackStateChanged();
    }

    /** Sau khi kéo trim xong: đưa playhead về trong vùng IN/OUT nếu đang phát. */
    void syncPlaybackPositionToTrimRange()
    {
        if (! isTransportActive())
            return;

        const double pos = getPlaybackPosition();
        const double len = getPlaybackLength();
        const double effectiveEnd = (trimEnd > 0.0) ? std::min (trimEnd, len) : len;

        if (pos < trimStart)
            seekTo (trimStart);
        else if (trimEnd > 0.0 && pos > trimEnd)
            seekTo (trimEnd);
        else if (trimEnd <= 0.0 && pos > len)
            seekTo (len);
    }

    double getEffectiveLength() const
    {
        const double total = getPlaybackLength();
        const double end   = (trimEnd > 0.0) ? std::min (trimEnd, total) : total;
        return std::max (0.0, end - trimStart);
    }

    void updateTheme (bool isDark)
    {
        isDarkMode = isDark;
        setOpaque (true);
        invalidateWaveformBaseCache();
        rebuildPaintResources();
        repaint();
    }

    void setIsSelectedRow (bool select) { isSelectedRowState = select; repaint(); }
    bool isRowSelected() const noexcept { return isSelectedRowState; }

    void setRenderMode (bool asGrid)
    {
        isRenderAsGridMode = asGrid;
        setWantsKeyboardFocus (asGrid);
        rebuildPaintResources();
        repaint();
    }

    void configurePad (const juce::String& path, float gain, bool loop)
    {
        pendingLoadGain = gain;
        setLooping (loop);
        if (path.isNotEmpty())
            loadAudioFileInternal (juce::File (path));
        else
            unloadAudioFile();
        repaint();
    }

    /** Message thread: thay file — dừng cứng nếu đang phát/fade, giữ tên/gain/hotkey/loop. */
    void replaceAudioFileKeepingPadSettings (const juce::File& f)
    {
        if (! f.existsAsFile())
            return;

        pendingLoadGain = getOutputGain();

        stopTimer();
        playState.store (PlayState::Stopped, std::memory_order_release);
        realtimeSource.clearStaleFadeOutArmOnMessageThread();
        realtimeSource.postResetOutputIdle();
        transportSource.stop();
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        lastDeferredStopGeneration = realtimeSource.getDeferredStopGeneration();

        loadAudioFileInternal (f);
        notifyPlaybackStateChanged();
    }

    /** Đọc payload trên background thread — dùng sau cắt nhạc để gắn file ngay, không queue load lần 2. */
    static std::unique_ptr<LoadedAudioPayload> readPayloadFromFile (const juce::File& f)
    {
        if (! f.existsAsFile())
            return {};

        juce::AudioFormatManager localFormatManager;
        localFormatManager.registerBasicFormats();

        auto payload = std::make_unique<LoadedAudioPayload>();
        payload->file = f;
        payload->reader.reset (localFormatManager.createReaderFor (f));

        if (payload->reader == nullptr)
            return {};

        payload->sampleRate = payload->reader->sampleRate;
        payload->displayName = VideoAudioExtractor::displayNameFromAudioPath (f);
        payload->meta = AudioMetadataReader::readFromReader (payload->reader.get(), f);
        return payload;
    }

    static std::unique_ptr<LoadedAudioPayload> payloadFromPreloadedCue (
        std::unique_ptr<showcontrol::preload::PreloadedAudioCue> cue)
    {
        if (cue == nullptr)
            return nullptr;

        auto payload = std::make_unique<LoadedAudioPayload>();
        payload->file = cue->file;
        payload->sampleRate = cue->sampleRate;
        payload->displayName = cue->displayName;
        payload->meta = cue->meta;
        payload->usesFullRam = cue->usesFullRam;
        payload->fullRamBuffer = std::move (cue->fullRamBuffer);
        payload->slicePreloadBuffer = std::move (cue->slicePreloadBuffer);
        payload->mappedFile = std::move (cue->mappedFile);

        if (payload->usesFullRam)
            return payload;

        payload->reader = std::move (cue->reader);

        if (payload->reader == nullptr)
        {
            juce::AudioFormatManager localFormatManager;
            localFormatManager.registerBasicFormats();
            payload->reader.reset (localFormatManager.createReaderFor (payload->file));
        }

        return payload;
    }

    /** Message thread: gắn file đã đọc sẵn — hủy load async đang chờ, refresh waveform ngay. */
    void adoptPreloadedAudioPayload (std::unique_ptr<LoadedAudioPayload> payload)
    {
        audioLoadGeneration.fetch_add (1, std::memory_order_acq_rel);
        commitLoadedAudioPayload (std::move (payload));
        reloadThumbnailImmediately();
    }

    /** Buộc thumbnail đọc lại file hiện tại — Inspector / Trim Editor cập nhật waveform tức thì. */
    void reloadThumbnailImmediately()
    {
        invalidateWaveformBaseCache();

        if (! musicFile.existsAsFile())
            return;

        thumbnail.setSource (nullptr);
        thumbnail.clear();
        thumbnailLoaded = false;
        thumbnailPendingFile = musicFile;

        if (thumbnailLoadAllowedNow)
            ensureThumbnailLoaded();

        repaint();
    }

    void setPadIndex (int index)
    {
        myIndex = index;

        if (! gridPositionAssigned)
        {
            const auto cell = showcontrol::padgrid::gridFromLinearSlot (index);
            setGridPosition (cell.y, cell.x, true);
        }
        else
        {
            refreshShortcutLabelFromGrid();
        }

        repaint();
    }

    void setGridPosition (int row, int col, bool refreshHotkeyLabel = true)
    {
        gridRow = juce::jlimit (0, showcontrol::padgrid::kRows - 1, row);
        gridCol = juce::jlimit (0, showcontrol::padgrid::kCols - 1, col);
        gridPositionAssigned = true;

        if (refreshHotkeyLabel)
            refreshShortcutLabelFromGrid();

        repaint();
    }

    /** Batch duplicate — cập nhật ô lưới + hotkey, không repaint trong vòng lặp. */
    void assignGridCellSilent (int row, int col) noexcept
    {
        gridRow = juce::jlimit (0, showcontrol::padgrid::kRows - 1, row);
        gridCol = juce::jlimit (0, showcontrol::padgrid::kCols - 1, col);
        gridPositionAssigned = true;
        refreshShortcutLabelFromGrid();
    }

    /**
     * Nhân bản RAM-first từ PAD đã sẵn sàng — chỉ sao chép metadata trong vòng lặp batch,
     * không đọc đĩa / không setSource waveform. Gọi finalizeClonedAudioAttach() sau batch.
     */
    bool cloneReadyAudioFrom (const SoundPad& src) noexcept
    {
        if (&src == this)
            return false;

        cancelPendingAsyncWork();
        thumbnailLoadAllowedNow = false;
        normalizationAllowedNow = false;
        hasPendingNormalization = false;
        pendingNormalizationFile = juce::File();

        musicFile = src.musicFile;
        cachedFileName = src.cachedFileName;
        cachedMeta = src.cachedMeta;
        measuredLoudness = src.measuredLoudness;
        thumbnailPendingFile = musicFile;
        pendingLoadGain = src.getOutputGain();

        if (! musicFile.existsAsFile())
        {
            hasFile = false;
            thumbnailLoaded = false;
            setCueState (PadCueState::empty, CueTransitionReason::fileUnload, true);
            return false;
        }

        hasFile = false;
        thumbnailLoaded = false;
        setCueState (PadCueState::loading, CueTransitionReason::fileLoadStart, true);
        return true;
    }

    /** Sau batch duplicate — gắn transport + waveform qua AudioThumbnailCache dùng chung. */
    bool finalizeClonedAudioAttach() noexcept
    {
        if (! musicFile.existsAsFile())
        {
            setCueState (PadCueState::empty, CueTransitionReason::fileLoadFail, true);
            return false;
        }

        const double savedTrimStart = trimStart;
        const double savedTrimEnd   = trimEnd;
        const float savedGain       = getOutputGain();

        auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (musicFile));

        if (reader == nullptr)
        {
            setCueState (PadCueState::empty, CueTransitionReason::fileLoadFail, true);
            return false;
        }

        realtimeSource.postStop();
        transportSource.setSource (nullptr);
        readerSource.reset();
        memorySource.reset();

        sourceSampleRate = reader->sampleRate;
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
        readerSource->setLooping (false);
        realtimeSource.setLooping (isLoopingState);

        const int readAhead = sharedTimeSliceThread != nullptr ? defaultReadAheadSamples (sourceSampleRate) : 0;
        transportUsesReadAhead = readAhead > 0;
        transportSource.setSource (readerSource.get(), readAhead, sharedTimeSliceThread, sourceSampleRate);

        trimStart = savedTrimStart;
        trimEnd   = savedTrimEnd;
        realtimeSource.setTrimRange (trimStart, trimEnd);
        setOutputGain (savedGain);

        hasFile = true;
        setCueState (PadCueState::ready, CueTransitionReason::fileLoadedReady, true);
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();

        thumbnailLoadAllowedNow = true;
        ensureThumbnailLoaded();
        warmReadAheadPipeline();

        return true;
    }

    void refreshShortcutLabelFromGrid()
    {
        shortcutLabel = showcontrol::padgrid::hotkeyForCell (gridRow, gridCol).label;
    }

    void triggerPlay()
    {
        if (isPlaybackCommandBlocked && isPlaybackCommandBlocked())
            return;

        if (! hasFile || isLoading())
            return;

        if (isStopping())
        {
            if (isFading())
                return;

            realtimeSource.clearStaleFadeOutArmOnMessageThread();
            setCueState (PadCueState::stopped, CueTransitionReason::userStop, true);
            playState.store (PlayState::Stopped, std::memory_order_release);
        }

        if (! isCueListPlayback)
        {
            if (isPlaying() || isFading() || isStopping())
            {
                if (isStopping())
                    return;

                triggerStopImmediate();
                return;
            }

            acknowledgeDeferredStopGeneration();
            // Ack stale finishedGen từ chu kỳ phát trước để tránh timer callback
            // nhầm lẫn natural-end cũ với natural-end của play mới.
            lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
            // Set playing trước onWillStartPlay để syncUiToPlayingPad thấy isPlaying=true ngay.
            setCueState (PadCueState::playing, CueTransitionReason::userPlayToggle);
            playState.store (PlayState::Playing, std::memory_order_release);

            if (onWillStartPlay)
                onWillStartPlay (this);

            ensureLufsSyncBeforePlay();
            postPlayOrFadeIn (CueTransitionReason::userPlayToggle);
            startLiveUiTimer();
            notifyPlaybackStateChanged();
            return;
        }

        if (isPaused())
        {
            acknowledgeDeferredStopGeneration();
            lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
            setCueState (PadCueState::playing, CueTransitionReason::userResume);
            if (! realtimeSource.postResume())
                setCueState (PadCueState::paused, CueTransitionReason::userResume, true);
        }
        else if (isPlaying() || isFading())
        {
            setCueState (PadCueState::paused, CueTransitionReason::userPlayToggle);
            realtimeSource.postPause();
            transportSource.stop();
        }
        else
        {
            acknowledgeDeferredStopGeneration();
            lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
            setCueState (PadCueState::playing, CueTransitionReason::userPlayToggle);
            playState.store (PlayState::Playing, std::memory_order_release);
            ensureLufsSyncBeforePlay();
            postPlayOrFadeIn (CueTransitionReason::userPlayToggle);
        }

        startLiveUiTimer();
        notifyPlaybackStateChanged();
    }

    void triggerPause()
    {
        if (isPlaybackCommandBlocked && isPlaybackCommandBlocked())
            return;

        if (! isCueListPlayback)
            return;

        if (! hasFile || isLoading())
            return;

        if (! isPlaying() && ! isFading())
            return;

        setCueState (PadCueState::paused, CueTransitionReason::userPause);
        realtimeSource.postPause();
        transportSource.stop();
        startLiveUiTimer();
        notifyPlaybackStateChanged();
    }

    void triggerResume()
    {
        if (isPlaybackCommandBlocked && isPlaybackCommandBlocked())
            return;

        if (! isCueListPlayback)
            return;

        if (! hasFile || isLoading() || ! isPaused())
            return;

        acknowledgeDeferredStopGeneration();
        setCueState (PadCueState::playing, CueTransitionReason::userResume);
        if (! realtimeSource.postResume())
            setCueState (PadCueState::paused, CueTransitionReason::userResume, true);
        startLiveUiTimer();
        notifyPlaybackStateChanged();
    }

    /** Im lặng khi mở app / sau load project — không fade, không notify chain. */
    void forceIdleAtStartup() noexcept
    {
        stopTimer();
        setCueState (PadCueState::ready, CueTransitionReason::userStop, true);
        playState.store (PlayState::Stopped, std::memory_order_release);
        realtimeSource.postResetOutputIdle();
        transportSource.stop();
        transportSource.setPosition (trimStart);
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        lastDeferredStopGeneration = realtimeSource.getDeferredStopGeneration();
    }

    /** Dừng cứng 0ms — chém đứt vách âm thanh, không fade ngầm. */
    void triggerStopImmediate() noexcept
    {
        if (isPlaybackCommandBlocked && isPlaybackCommandBlocked())
            return;

        playState.store (PlayState::Stopped, std::memory_order_release);
        setCueState (PadCueState::stopped, CueTransitionReason::userStop, true);
        realtimeSource.clearVisualFadeFlagsOnMessageThread();
        realtimeSource.postStop();
        transportSource.stop();
        transportSource.setPosition (trimStart);
        realtimeSource.postSeek (trimStart);
        realtimeSource.snapPublishedUiToTrimOnMessageThread();
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        lastDeferredStopGeneration = realtimeSource.getDeferredStopGeneration();
        shortCircuitPlaybackVisuals();
        notifyPlaybackStateChanged();
    }

    void triggerStop()
    {
        if (isPlaybackCommandBlocked && isPlaybackCommandBlocked())
            return;

        if (playState.load (std::memory_order_acquire) == PlayState::FadingOut
            || isFadeOutArmed()
            || isStopping())
            return;

        triggerStopImmediate();
    }

    /** Stop theo cấu hình fadeOutMs — 0ms = hard stop, >0 = fade out. */
    void stopTransportWithConfiguredFade() noexcept
    {
        if (fadeOutMs < 5.0)
            triggerStopImmediate();
        else
            startFadeOut();
    }
    void loadExternalFile (const juce::File& file) { loadAudioFileInternal (file); }

    void startFadeIn (double durationMs = 500.0)
    {
        realtimeSource.postFadeIn ((float) durationMs, transportSource.getGain());
        startLiveUiTimer();
    }

    /** durationMsOverride < 0 → dùng fadeOutMs của pad; ngược lại dùng giá trị truyền vào. */
    void startFadeOut (double durationMsOverride = -1.0, bool ignorePlaybackCommandBlock = false)
    {
        if (! ignorePlaybackCommandBlock && isPlaybackCommandBlocked && isPlaybackCommandBlocked())
            return;

        if (! hasFile || isLoading())
            return;

        if (isFading() || isFadeOutArmed())
            return;

        acknowledgeDeferredStopGeneration();
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        setCueState (PadCueState::stopping, CueTransitionReason::userStop, true);
        playState.store (PlayState::FadingOut, std::memory_order_release);

        const double dur = durationMsOverride >= 0.0 ? durationMsOverride : fadeOutMs;

        if (dur < 5.0)
        {
            triggerStopImmediate();
            return;
        }

        if (! realtimeSource.postFadeOut ((float) dur))
        {
            playState.store (isPlaying() ? PlayState::Playing : PlayState::Stopped,
                             std::memory_order_release);
            setCueState (PadCueState::ready, CueTransitionReason::userStop, true);
            return;
        }

        startLiveUiTimer();
        notifyPlaybackStateChanged();
    }

    bool isFading() const { return realtimeSource.isFadeActive(); }
    bool isFadeOutArmed() const noexcept { return realtimeSource.isFadeOutArmed(); }

    /** BGM/PAD: đang fade-out stop — message thread guard, không đụng audio lock. */
    bool isFadeOutInProgress() const noexcept
    {
        if (playState.load (std::memory_order_acquire) == PlayState::FadingOut)
            return true;

        if (isFadeOutArmed())
            return true;

        return isStopping() && isFading();
    }
    void setAutoNormalize (bool enable)
    {
        autoNormalizeEnabled = enable;
        loudnessSettings.enabled = enable;
        refreshLufsSyncGain();
    }
    bool getAutoNormalize() const { return autoNormalizeEnabled; }

    void setNormalizeUseLufs (bool useLufs) noexcept
    {
        normalizeUseLufs = useLufs;
        loudnessSettings.mode = useLufs ? showcontrol::loudness::MeasureMode::lufs
                                        : showcontrol::loudness::MeasureMode::rms;
        refreshLufsSyncGain();
    }
    bool getNormalizeUseLufs() const noexcept { return normalizeUseLufs; }

    void setLoudnessSettings (const showcontrol::loudness::LoudnessSettings& settings) noexcept
    {
        loudnessSettings = settings;
        autoNormalizeEnabled = settings.enabled;
        normalizeUseLufs = settings.mode == showcontrol::loudness::MeasureMode::lufs;
        abCompareBypass = settings.abCompareOriginal;
        refreshLufsSyncGain();
    }

    const showcontrol::loudness::LoudnessSettings& getLoudnessSettings() const noexcept
    {
        return loudnessSettings;
    }

    const AudioAnalyzer::FileLoudnessAnalysis& getFileLoudnessAnalysis() const noexcept
    {
        return fileLoudnessAnalysis;
    }

    void setAbCompareBypass (bool bypass) noexcept
    {
        abCompareBypass = bypass;
        loudnessSettings.abCompareOriginal = bypass;
        refreshLufsSyncGain();
    }

    bool isAbCompareBypass() const noexcept { return abCompareBypass; }

    // ── DSP: EQ 6-band (JUCE) + LUFS sync — UI/message thread cập nhật coeffs; audio chỉ process
    void setDspEqEnabled (bool on) noexcept
    {
        realtimeSource.getDsp().getEq().setEnabled (on);
    }

    /** Bật EQ không tăng revision (kéo band ngay sau đó chỉ bump một lần). */
    void setDspEqEnabledNoCoeffBump (bool on) noexcept
    {
        realtimeSource.getDsp().getEq().setEnabledNoCoeffBump (on);
    }
    bool getDspEqEnabled() const noexcept    { return realtimeSource.getDsp().getEq().isEnabled(); }

    void setDspEqBandGainDb (int band, float gainDb) noexcept
    {
        realtimeSource.getDsp().getEq().setBandGainDb (band, gainDb);
    }

    void resetDspEqToDefaults() noexcept
    {
        realtimeSource.getDsp().getEq().resetToDefaults();
    }

    float getDspEqBandGainDb (int band) const noexcept
    {
        return realtimeSource.getDsp().getEq().getBandGainDb (band);
    }

    void setDspEqGainsDb (float lowDb, float midDb, float highDb) noexcept
    {
        realtimeSource.getDsp().getEq().setLegacyGainsDb (lowDb, midDb, highDb);
    }

    float getDspEqLowDb()  const noexcept { return realtimeSource.getDsp().getEq().getLegacyLowDb(); }
    float getDspEqMidDb()  const noexcept { return realtimeSource.getDsp().getEq().getLegacyMidDb(); }
    float getDspEqHighDb() const noexcept { return realtimeSource.getDsp().getEq().getLegacyHighDb(); }

    float getDspLufsSyncGain() const noexcept { return realtimeSource.getDsp().getLoudness().getGain(); }
    double getMeasuredIntegratedLufs() const noexcept { return measuredLoudness; }

    /** Quét LUFS tích hợp (background) trước GO nếu chưa có số đo. */
    void ensureLufsSyncBeforePlay() noexcept
    {
        if (! autoNormalizeEnabled || ! hasFile)
            return;

        if (hasValidLoudnessMeasurement())
            applyVolumeSyncGainIfReady();
        else
            requestNormalization();
    }

    /** Đo lại gain trên background thread (sau đổi RMS/LUFS hoặc đồng bộ list). */
    void requestNormalization()
    {
        if (! hasFile || ! autoNormalizeEnabled)
            return;

        pendingNormalizationFile = musicFile;
        hasPendingNormalization = true;

        if (normalizationAllowedNow)
            maybeStartNormalization();
    }

    /** Trạng thái pad lưu trong project v2 — áp dụng sau khi load file xong (message thread). */
    struct PadProjectState
    {
        juce::String customName;
        double trimStart = 0.0;
        double trimEnd   = 0.0;
        float  outputGain = 1.0f;
        bool   autoNormalize = true;
        bool   normalizeUseLufs = false;
        int    normalizePreset = (int) showcontrol::loudness::Preset::liveShow;
        int    normalizeProfile = (int) showcontrol::loudness::ContentProfile::general;
        bool   normalizeSafeMode = true;
        double normalizeCustomTargetLufs = -16.0;
        int    outputBus = 0;   // Bus routing index cho MultiOutputAudioCallback
        AudioMetadata cachedMeta;   // Restore nhanh không cần đọc lại file
        double fadeInMs  = 0.0;     // 0 = không fade in khi phát
        double fadeOutMs = 0.0;     // 0 = không thiết lập fade out mặc định
        bool   dspEqEnabled = false;
        float  dspEqLowDb   = 0.0f;
        float  dspEqMidDb   = 0.0f;
        float  dspEqHighDb  = 0.0f;
        std::array<float, PadParametricEq6::kNumBands> dspEqBandDb {};
    };

    const AudioMetadata& getMetadata() const noexcept { return cachedMeta; }

    /** Đọc TBPM / vorbis BPM nếu pad đã load file nhưng cachedMeta chưa có BPM. */
    void supplementBpmFromFileIfMissing()
    {
        if (! hasFile || cachedMeta.bpm > 0.0)
            return;

        AudioMetadataReader::supplementBpm (cachedMeta, musicFile);
        repaint();
    }

    // Per-pad fade duration (UI thread only)
    void   setFadeInMs  (double ms) noexcept { fadeInMs  = std::max (0.0, ms); }
    void   setFadeOutMs (double ms) noexcept { fadeOutMs = std::max (0.0, ms); }
    double getFadeInMs()  const noexcept { return fadeInMs; }
    double getFadeOutMs() const noexcept { return fadeOutMs; }


    void setPendingProjectState (const PadProjectState& state)
    {
        pendingProjectState = state;
        hasPendingProjectState = true;
        applyStartupDisplayFromProjectState (state);
    }

    /** Gán title hiển thị tức thì khi load JSON — không đọc file, không I/O. */
    void applyInstantDisplayTitle (const juce::String& title)
    {
        const auto trimmed = title.trim();

        if (trimmed.isEmpty())
            return;

        cachedMeta.title = trimmed;
        repaint();
    }

    /** Gán chuỗi định dạng âm thanh cache — Inspector đọc RAM, không mở file. */
    void applyInstantDisplayFormat (const juce::String& formatInfo)
    {
        const auto trimmed = formatInfo.trim();

        if (trimmed.isEmpty())
            return;

        cachedMeta.formatInfoString = trimmed;
        repaint();
    }

    /** Kết quả migration ngầm — cập nhật RAM + pending project state. */
    void applyMigratedAudioFormat (const juce::String& formatInfo,
                                   int sampleRate,
                                   int bitDepth,
                                   int numChannels)
    {
        cachedMeta.formatInfoString = formatInfo;

        if (sampleRate > 0)
            cachedMeta.sampleRate = sampleRate;

        if (bitDepth > 0)
            cachedMeta.bitDepth = bitDepth;

        if (numChannels > 0)
            cachedMeta.numChannels = numChannels;

        if (hasPendingProjectState)
        {
            pendingProjectState.cachedMeta.formatInfoString = cachedMeta.formatInfoString;
            pendingProjectState.cachedMeta.sampleRate       = cachedMeta.sampleRate;
            pendingProjectState.cachedMeta.bitDepth         = cachedMeta.bitDepth;
            pendingProjectState.cachedMeta.numChannels      = cachedMeta.numChannels;
        }

        repaint();
    }

    juce::String getCachedFormatInfoString() const
    {
        return cachedMeta.getFormatInfo();
    }

    void rebuildCachedFormatInfoFromMetadata() noexcept
    {
        if (cachedMeta.sampleRate > 0 || cachedMeta.bitDepth > 0 || cachedMeta.numChannels > 0)
            cachedMeta.formatInfoString = cachedMeta.buildFormatInfoUncached();
    }

    /** Waveform + normalize — message thread kế tiếp, không chặn click chọn. */
    void scheduleDeferredInspectorLoads()
    {
        auto loadNow = [this]
        {
            setThumbnailLoadAllowed (true, false);
            setNormalizationLoadAllowed (true, false);
            ensureThumbnailLoaded();
            maybeStartNormalization();
        };

        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            loadNow();
            return;
        }

        juce::Component::SafePointer<SoundPad> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
            {
                safe->setThumbnailLoadAllowed (true, false);
                safe->setNormalizationLoadAllowed (true, false);
                safe->ensureThumbnailLoaded();
                safe->maybeStartNormalization();
            }
        });
    }

    void applyStartupDisplayFromProjectState (const PadProjectState& state)
    {
        if (state.customName.isNotEmpty())
            customName = state.customName;

        if (state.cachedMeta.title.isNotEmpty())
            cachedMeta.title = state.cachedMeta.title;

        if (state.cachedMeta.artist.isNotEmpty())
            cachedMeta.artist = state.cachedMeta.artist;

        if (state.cachedMeta.album.isNotEmpty())
            cachedMeta.album = state.cachedMeta.album;

        if (state.cachedMeta.bpm > 0.0)
            cachedMeta.bpm = state.cachedMeta.bpm;

        if (state.cachedMeta.sampleRate > 0)
            cachedMeta.sampleRate = state.cachedMeta.sampleRate;

        if (state.cachedMeta.bitDepth > 0)
            cachedMeta.bitDepth = state.cachedMeta.bitDepth;

        if (state.cachedMeta.numChannels > 0)
            cachedMeta.numChannels = state.cachedMeta.numChannels;

        if (state.cachedMeta.formatInfoString.isNotEmpty())
            cachedMeta.formatInfoString = state.cachedMeta.formatInfoString;
        else
            rebuildCachedFormatInfoFromMetadata();

        repaint();
    }
    double getDetectedRMS() const { return measuredLoudness; }

    float getNormalizedGain() const
    {
        if (abCompareBypass)
            return 1.0f;

        if (! hasValidLoudnessMeasurement())
            return 1.0f;

        return showcontrol::loudness::computeSafeGain (fileLoudnessAnalysis, loudnessSettings);
    }

    bool hasValidLoudnessMeasurement() const noexcept
    {
        return fileLoudnessAnalysis.valid;
    }

    /** Ghi gain chuẩn hoá vào output fader + mixer (RT ramp 20 ms, không pop). */
    bool applyVolumeSyncGainIfReady() noexcept
    {
        if (! autoNormalizeEnabled || ! hasValidLoudnessMeasurement())
            return false;

        const float calculatedGain = getNormalizedGain();
        setOutputGain (calculatedGain);
        realtimeSource.getDsp().getLoudness().setGain (1.0f);
        return true;
    }

    void resetFadeDurations() noexcept
    {
        setFadeInMs (0.0);
        setFadeOutMs (0.0);
    }

    bool isNormalizationInProgress() const noexcept { return normalizer.isBusy(); }

    juce::String getLoudnessDisplayString() const
    {
        if (! autoNormalizeEnabled || ! hasFile)
            return {};

        if (! hasValidLoudnessMeasurement())
            return showcontrol::localization::tr (u8"Chưa đo");

        const auto gainStr = juce::String (getNormalizedGain(), 2);
        juce::String text;

        if (loudnessSettings.mode == showcontrol::loudness::MeasureMode::lufs)
            text = AudioAnalyzer::formatLUFSValue (fileLoudnessAnalysis.integratedLufs);
        else
            text = juce::String::fromUTF8 (u8"RMS ") + juce::String (fileLoudnessAnalysis.rms, 4);

        text += showcontrol::localization::tr (u8" · Peak ")
              + juce::String (fileLoudnessAnalysis.truePeakDbfs, 1) + " dBFS";
        text += showcontrol::localization::tr (u8" · Gain ") + gainStr + "x";
        return text;
    }

    juce::String getTimeRemainingString()
    {
        if (! hasFile)
            return "00:00.0";

        const double total = getPlaybackLength();
        const double end   = (trimEnd > 0.0) ? std::min (trimEnd, total) : total;
        return formatTimeString (getRemainingSeconds());
    }

    juce::String formatTimeString (double timeInSeconds) {
        return showcontrol::bgmList::formatPlaylistTime (timeInSeconds);
    }

    bool ownsPlaybackUiUpdates() const noexcept
    {
        return ! isActivePlaybackUiOwner || isActivePlaybackUiOwner();
    }

    void timerCallback() override
    {
        consumeDeferredTransportStopRequests();
        consumeRealtimeDiagnostics();

        if (! isPlaybackPositionLive() && ! isLoading())
        {
            shortCircuitPlaybackVisuals();
            stopTimer();
            return;
        }

        if (isStopping() && ! isFading())
        {
            finalizeStoppingAfterFadeOut();
            stopTimer();
            return;
        }

        const uint32_t finishedGen = realtimeSource.getTrackFinishedGeneration();
        if (finishedGen != lastTrackFinishedGen)
        {
            lastTrackFinishedGen = finishedGen;
            setCueState (PadCueState::ready, CueTransitionReason::naturalEnd, true);
            transportSource.stop();
            stopTimer();

            if (ownsPlaybackUiUpdates())
            {
                repaint();

                if (onPlaybackStateChanged)
                    onPlaybackStateChanged();
            }

            if (! isCueListPlayback && onTrackFinished)
                onTrackFinished (this);

            return;
        }

        const auto prevCueState = getCueState();
        syncCueStateFromPlayback();

        if (isPlaybackPositionLive()
            && (prevCueState != getCueState() || isLoading()))
            repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { isMouseHovering = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { isMouseHovering = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenuRequested)
                onContextMenuRequested (this);
            return;
        }

        reorderDragActive = false;
        juceSystemDragStarted = false;

        if (onSelected)
            onSelected (this, e.mods);

        if (isRenderAsGridMode)
            grabKeyboardFocus();

        if (clickToTriggerOnClick && hasAudioFile() && onRequestGo != nullptr)
            onRequestGo (this);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (isPointInTrackNameArea (e.x, e.y))
        {
            beginTrackNameEdit();
            return;
        }

        juce::Component::mouseDoubleClick (e);

        if (! hasAudioFile())
            return;

        if (onRequestGo != nullptr)
            onRequestGo (this);
        else
            triggerPlay();

        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        if (! reorderDragActive)
        {
            if (e.getDistanceFromDragStart() < 8)
                return;

            reorderDragActive = true;

            const auto panelPos = pointerInPadPanel (e);

            if (onPadReorderBegin)
                onPadReorderBegin (this, panelPos);

            tryStartJuceCrossDrag (e);
        }

        if (onPadReorderMove)
            onPadReorderMove (this, pointerInPadPanel (e));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);

        if (reorderDragActive)
        {
            if (onPadReorderEnd)
                onPadReorderEnd (this);
        }

        reorderDragActive = false;
        juceSystemDragStarted = false;

        if (isCurrentlyDragged)
            setIsCurrentlyDragged (false);
    }

    void paintGridMode (juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        paintGridBackground (g, bounds);
        paintGridHeader (g);
        if (hasFile && showGridWaveform)
            paintGridWaveform (g, bounds);
        paintGridFooter (g);
        if (showGridHotkeyBadge)
            paintGridBadge (g, bounds);
    }

    juce::Image makeFarragoDragSnapshot (float scaleFactor = 1.0f)
    {
        const float scale = juce::jmax (1.0f, scaleFactor);
        auto image = createComponentSnapshot (getLocalBounds(), true, scale);

        if (! image.isValid())
            return image;

        juce::Image faded = image.createCopy();

        if (faded.getFormat() != juce::Image::PixelFormat::ARGB)
            faded = faded.convertedToFormat (juce::Image::PixelFormat::ARGB);

        juce::Image::BitmapData data (faded, juce::Image::BitmapData::readWrite);
        constexpr float kFarragoGhostAlpha = 0.82f;

        for (int y = 0; y < faded.getHeight(); ++y)
        {
            auto* line = data.getLinePointer (y);

            for (int x = 0; x < faded.getWidth(); ++x)
            {
                auto* px = line + x * 4;
                px[3] = (juce::uint8) juce::jlimit (0, 255,
                                                    (int) std::lround ((float) px[3] * kFarragoGhostAlpha));
            }
        }

        return faded;
    }

    juce::Point<int> pointerInPadPanel (const juce::MouseEvent& e) const noexcept
    {
        if (auto* scrollParent = findParentComponentOfClass<juce::Viewport>())
        {
            if (auto* viewed = scrollParent->getViewedComponent())
                return e.getEventRelativeTo (viewed).getPosition();
        }

        return e.getPosition();
    }

    void tryStartJuceCrossDrag (const juce::MouseEvent& e)
    {
        if (juceSystemDragStarted)
            return;

        if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            juce::var description;

            if (isRenderAsGridMode)
            {
                if (onBuildPadDragDescription == nullptr)
                    return;

                const auto payload = onBuildPadDragDescription();

                juce::Array<int> padIndices;
                int anchorIndex = -1;

                if (showcontrol::crossdrag::decodePadPanelPayload (payload, padIndices, anchorIndex))
                {
                    description = showcontrol::crossdrag::buildLocalPadMoveDragToken (padIndices, anchorIndex);
                }
                else
                {
                    description = showcontrol::crossdrag::buildLocalPadMoveDragToken (getPadDragIdentityToken().getIntValue());
                }
            }
            else
            {
                description = showcontrol::crossdrag::buildLocalRowReorderDragToken (getPadDragIdentityToken().getIntValue());
            }

            if (isRenderAsGridMode)
            {
                if (! description.isString())
                    return;
            }
            else if (! description.isString())
            {
                return;
            }

            setIsCurrentlyDragged (true);

            int itemCount = showcontrol::crossdrag::dragPayloadItemCount (description);

            if (! isRenderAsGridMode && onGetRowReorderDragCount != nullptr)
                itemCount = juce::jmax (1, onGetRowReorderDragCount());

            juce::Image dragImage;

            const float dragScale = juce::Component::getApproximateScaleFactorForComponent (this);

            if (isRenderAsGridMode)
                dragImage = makeFarragoDragSnapshot (dragScale);
            else if (onCreateMultiItemDragImage != nullptr)
                dragImage = onCreateMultiItemDragImage (itemCount);
            else
                dragImage = showcontrol::crossdrag::createPremiumDragImage (getPadName(), itemCount);

            if (! dragImage.isValid())
                return;

            const auto imageBounds = juce::Rectangle<int> (dragImage.getWidth(), dragImage.getHeight());
            juce::Point<int> imageOffset = showcontrol::crossdrag::dragImageAnchorFromMouseDown (
                e.getMouseDownPosition(), imageBounds);

            // Kéo nội bộ MainComponent — tránh ghost desktop HiDPI văng lệch góc màn hình (Windows).
            dragContainer->startDragging (description,
                                          this,
                                          juce::ScaledImage (dragImage, dragScale),
                                          false,
                                          &imageOffset);
            juceSystemDragStarted = true;
        }
    }

    void paintListMode (juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        const auto pal = ShowTheme::get (isDarkMode);

        if (! isSelectedRowState && isMouseHovering)
        {
            g.setColour (pal.rowHover);
            g.fillAll();
        }
        else
        {
            showcontrol::bgmList::paintPlaylistRowBackground (g, getLocalBounds(), isSelectedRowState, pal);
        }

        if (! showcontrol::colours::isDefaultTagColour (tagColour))
        {
            g.setColour (tagColour);
            showcontrol::gfx::safeFillRect (g, 0, 0, showcontrol::bgmList::kLeftRailWidth, getHeight());
        }
        else
        {
            showcontrol::bgmList::paintPlaylistRowLeftRail (g, getHeight(), pal);
        }

        g.setColour (pal.borderSubtle);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, bounds.getWidth());

        g.setColour (pal.textMuted);
        g.setFont (paintResources.listIndex);
        g.drawText (juce::String (myIndex + 1),
                    showcontrol::bgmList::kIndexX, 0,
                    showcontrol::bgmList::kIndexWidth, getHeight(),
                    juce::Justification::centred);

        const bool highlightRow = isPlaying() || isPaused();
        if (isPlaying())
        {
            showcontrol::icons::paintSpeakerIcon (g, paintResources.listStatusIconBounds,
                                                  showcontrol::icons::speakerPlayingColour (isSelectedRowState),
                                                  isSelectedRowState);
        }
        else if (isCueListPlayback && isPaused())
        {
            const auto iconCol = showcontrol::icons::iconColourForListState (isSelectedRowState, isDarkMode);
            showcontrol::icons::paintPauseIcon (g, paintResources.listStatusIconBounds, iconCol);
        }

        juce::Colour trackNameColour = pal.textPrimary;

        if (highlightRow)
            trackNameColour = (isCueListPlayback && isPaused()) ? pal.warning : pal.success;

        g.setColour (trackNameColour);
        g.setFont (paintResources.listTitle);

        if (isLoading())
        {
            g.setColour (pal.warning);
            g.setFont (paintResources.listTitle);
            g.drawText (juce::String::fromUTF8 (u8"Đang nạp..."), paintResources.listNameArea,
                        juce::Justification::centredLeft, true);
            return;
        }

        if (! trackNameEditing)
            g.drawText (getPadName(), paintResources.listNameArea,
                        juce::Justification::centredLeft, true);

        if (paintResources.listShowArtist)
        {
            g.setColour (pal.textMuted);
            g.setFont (paintResources.listArtist);
            g.drawText (cachedMeta.artist,
                        paintResources.listNameTextX + paintResources.listNameWidth + 4, 0,
                        paintResources.listArtistWidth, getHeight(),
                        juce::Justification::centredLeft, true);
        }

        if (! isCueListPlayback && isLooping() && hasFile)
            showcontrol::icons::paintLoopIcon (g, paintResources.listLoopIconBounds, pal.accent, true);

        if (hasFile)
        {
            const double remainingTime = isPlaybackPositionLive() ? getRemainingSeconds() : 0.0;
            if (isPlaybackPositionLive() && remainingTime <= 5.0 && isPlaying())
            {
                if ((juce::Time::getMillisecondCounter() % 400) < 200) g.setColour (pal.danger);
                else g.setColour (pal.textSecondary);
            }
            else
            {
                g.setColour (pal.textSecondary);
            }

            g.setFont (paintResources.listTimer);
            showcontrol::bgmList::drawPlaylistTimeCell (g, formatTimeString (remainingTime),
                                                        paintResources.listRemainingRect);

            g.setColour (pal.textMuted);
            g.setFont (paintResources.listTimer);
            showcontrol::bgmList::drawPlaylistTimeCell (g, formatTimeString (getEffectiveLength()),
                                                        paintResources.listTotalRect);
        }
    }

    void resized() override
    {
        const int w = getWidth();
        const int h = getHeight();
        const bool isTinyCell = h < 40 || w < 48;
        const bool isCompactFooter = h < 65 || w < 85;

        showGridWaveform    = ! isTinyCell;
        showGridDuration    = isRenderAsGridMode && ! isCompactFooter;
        showGridHotkeyBadge = isRenderAsGridMode && w > 20;

        invalidateWaveformBaseCache();
        rebuildPaintResources();

        if (trackNameEditing)
            layoutTrackNameLabel();
    }

    void paint (juce::Graphics& g) override
    {
        const auto pal = ShowTheme::get (isDarkMode);
        g.fillAll (isRenderAsGridMode ? pal.padGradientTop : pal.listRowBg);

        const auto bounds = getLocalBounds().toFloat();
        if (isRenderAsGridMode)
            paintGridMode (g, bounds);
        else
            paintListMode (g, bounds);
    }

    /** Message thread: dập tắt waveform/highlight ngay khi stop — 0ms hard-kill. */
    void shortCircuitPlaybackVisuals() noexcept
    {
        if (! isPlaybackPositionLive() && ! isLoading())
            realtimeSource.snapPublishedUiToTrimOnMessageThread();

        invalidateWaveformBaseCache();
        stopTimer();
        setAlpha (1.0f);
        juce::Desktop::getInstance().getAnimator().cancelAnimation (this, true);
        repaint();
    }

private:
    enum class CueTransitionReason
    {
        userPlayToggle,
        userPause,
        userResume,
        userStop,
        timerSync,
        fileLoadStart,
        fileLoadFail,
        fileLoadedReady,
        fileUnload,
        naturalEnd
    };

    enum class PlayState : uint8_t
    {
        Stopped  = 0,
        Playing  = 1,
        FadingOut = 2
    };

    juce::File musicFile; bool hasFile = false, isLoopingState = false, isDarkMode = true; bool isRenderAsGridMode = true, isSelectedRowState = false, isMouseHovering = false; int myIndex = 0; int gridRow = 0, gridCol = 0; bool gridPositionAssigned = false; juce::String shortcutLabel, cachedFileName, customName; juce::Colour tagColour = showcontrol::colours::defaultTagColour(); double trimStart = 0.0, trimEnd = 0.0;
    AudioMetadata cachedMeta;
    double fadeInMs  = 0.0;
    double fadeOutMs = 0.0;

    juce::AudioFormatManager& formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::MemoryAudioSource> memorySource;
    juce::AudioBuffer<float> ownedFullRamBuffer;
    juce::AudioTransportSource transportSource;
    PadRealtimeSource realtimeSource;
    juce::AudioThumbnail thumbnail;
    VolumeNormalizer normalizer;
    double measuredLoudness = 0.0;
    AudioAnalyzer::FileLoudnessAnalysis fileLoudnessAnalysis;
    showcontrol::loudness::LoudnessSettings loudnessSettings;
    bool abCompareBypass = false;
    bool autoNormalizeEnabled = true;
    bool normalizeUseLufs = false;
    uint32_t lastTrackFinishedGen = 0;
    float pendingLoadGain = 1.0f;
    bool thumbnailLoadAllowedNow = false;
    bool thumbnailLoaded = false;
    juce::File thumbnailPendingFile;
    juce::Image waveformBaseCache;
    bool waveformBaseCacheValid = false;

    bool normalizationAllowedNow = false;
    bool hasPendingNormalization = false;
    juce::File pendingNormalizationFile;
    std::atomic<PadCueState> cueState { PadCueState::empty };
    std::atomic<PlayState> playState { PlayState::Stopped };
    uint32_t lastDeferredStopGeneration = 0;
    bool isCueListPlayback = true;
    bool clickToTriggerOnClick = false;
    bool isArmedState = false;
    bool reorderDragActive = false;
    bool juceSystemDragStarted = false;
    bool isCurrentlyDragged = false;
    bool trackNameEditing = false;
    bool showGridWaveform = true;
    bool showGridDuration = true;
    bool showGridHotkeyBadge = true;
    TrackNameEditLabel trackNameLabel;
    bool mixerRegisteredWithMaster = false;
    juce::TimeSliceThread* sharedTimeSliceThread = nullptr;
    double sourceSampleRate = 44100.0;
    bool transportUsesReadAhead = false;
    PadProjectState pendingProjectState;
    bool hasPendingProjectState = false;
    uint32_t droppedCommandSinceLastLog = 0;
    juce::uint32 lastDroppedCommandLogMs = 0;
    std::atomic<juce::uint32> hotkeyTriggerGuardUntilMs { 0 };
    std::atomic<uint32_t> audioLoadGeneration { 0 };
    std::atomic<bool> playbackPreloadRequested { false };

    struct PaintResources
    {
        juce::Font gridTitle { juce::FontOptions() };
        juce::Font gridArtist { juce::FontOptions() };
        juce::Font gridTimerBold { juce::FontOptions() };
        juce::Font gridTimer { juce::FontOptions() };
        juce::Font gridBadge { juce::FontOptions() };
        juce::Font listIndex { juce::FontOptions() };
        juce::Font listTitle { juce::FontOptions() };
        juce::Font listTitleBold { juce::FontOptions() };
        juce::Font listArtist { juce::FontOptions() };
        juce::Font listTimerBold { juce::FontOptions() };
        juce::Font listTimer { juce::FontOptions() };
        juce::Rectangle<int> waveformBounds;
        juce::Rectangle<int> gridTitleRect;
        juce::Rectangle<int> gridArtistRect;
        juce::Rectangle<int> gridRemainingRect;
        juce::Rectangle<int> gridTotalRect;
        juce::Rectangle<int> listRemainingRect;
        juce::Rectangle<int> listTotalRect;
        juce::Rectangle<float> listLoopIconBounds;
        juce::Rectangle<float> listStatusIconBounds;
        juce::Rectangle<float> gridBadgeRect;
        juce::Rectangle<int> listNameArea;
        int listNameTextX = showcontrol::bgmList::kNameStartDefault;
        int listNameWidth = 0;
        int listArtistWidth = 0;
        bool listShowArtist = false;
    };

    PaintResources paintResources;

    void rebuildPaintResources()
    {
        paintResources.gridTitle     = ShowTheme::fontBold (13.0f);
        paintResources.gridArtist    = ShowTheme::font (10.5f);
        paintResources.gridTimerBold = ShowTheme::timerFont (11.5f, true);
        paintResources.gridTimer     = ShowTheme::timerFont (11.0f);
        paintResources.gridBadge     = ShowTheme::fontBold (10.0f);
        const auto listTypography    = showcontrol::bgmList::makePlaylistRowTypography();
        paintResources.listIndex     = listTypography.index;
        paintResources.listTitle     = listTypography.cellPlain;
        paintResources.listTitleBold = listTypography.cellPlain;
        paintResources.listArtist    = ShowTheme::font (11.5f);
        paintResources.listTimerBold = showcontrol::bgmList::playlistTimerFont (true);
        paintResources.listTimer     = showcontrol::bgmList::playlistTimerFont (false);

        if (isRenderAsGridMode)
        {
            const int w = getWidth();
            const int h = getHeight();
            const float aspectWH = (h > 0) ? (float) w / (float) h : 1.0f;
            const bool isWideFlat   = aspectWH > 1.75f;
            const bool isTallNarrow = aspectWH < 0.8f;
            const float layoutScale = juce::jlimit (0.62f, 1.0f,
                                                    juce::jmin ((float) w / 96.0f, (float) h / 44.0f));

            paintResources.gridTitle     = ShowTheme::fontBold (13.0f * layoutScale);
            paintResources.gridArtist    = ShowTheme::font (10.5f * layoutScale);
            paintResources.gridTimerBold = ShowTheme::timerFont (11.5f * layoutScale, true);
            paintResources.gridTimer     = ShowTheme::timerFont (11.0f * layoutScale);
            paintResources.gridBadge     = ShowTheme::fontBold (juce::jmax (8.0f, 10.0f * layoutScale));

            const int kTitleTopPad = juce::jmax (2, h / (isWideFlat ? 18 : 14));
            const int kTitleLineH  = juce::jmax (10, juce::jmin (18, h / (isTallNarrow ? 6 : 5)));
            const bool showArtist  = showGridDuration && cachedMeta.artist.isNotEmpty()
                                     && h > 52 && ! isWideFlat;
            const int artistLineH  = showArtist ? juce::jmax (9, kTitleLineH - 2) : 0;
            const int headerBottom = kTitleTopPad + kTitleLineH
                                   + (artistLineH > 0 ? artistLineH + 2 : juce::jmax (2, h / 18));

            const int bottomBarH = showGridDuration
                                       ? juce::jmax (26, juce::jmax (12, h / (isWideFlat ? 9 : 7)))
                                       : (showGridHotkeyBadge ? 20 : 4);

            paintResources.gridTitleRect = { 6, kTitleTopPad, juce::jmax (0, w - 12), kTitleLineH };

            if (artistLineH > 0)
                paintResources.gridArtistRect = { 6, kTitleTopPad + kTitleLineH + 1, juce::jmax (0, w - 12), artistLineH };
            else
                paintResources.gridArtistRect = {};

            paintResources.waveformBounds = showcontrol::gfx::sanitise (
                getLocalBounds().withTrimmedTop (headerBottom)
                                 .withTrimmedBottom (bottomBarH)
                                 .reduced (juce::jmax (2, w / (isTallNarrow ? 18 : 24)),
                                           juce::jmax (1, h / (isWideFlat ? 32 : 28))));

            const int labelH = 16;

            if (showGridDuration)
            {
                const int yPos    = h - labelH - 4;
                const int centerW = juce::jlimit (30, 50, w / 4);
                const int sideW   = juce::jmax (0, (w - centerW) / 2 - 6);

                paintResources.gridRemainingRect = { 4, yPos, sideW, labelH };
                paintResources.gridBadgeRect     = { (float) ((w - centerW) / 2),
                                                     (float) (yPos - 2),
                                                     (float) centerW,
                                                     (float) (labelH + 2) };
                paintResources.gridTotalRect     = { w - (w - centerW) / 2 + 2, yPos, sideW, labelH };
            }
            else if (showGridHotkeyBadge)
            {
                paintResources.gridRemainingRect = {};
                paintResources.gridTotalRect     = {};
                paintResources.gridBadgeRect     = { 0.0f,
                                                     (float) (h - 20),
                                                     (float) w,
                                                     18.0f };
            }
            else
            {
                paintResources.gridRemainingRect = {};
                paintResources.gridTotalRect     = {};
                paintResources.gridBadgeRect     = {};
            }
        }
        else
        {
            paintResources.waveformBounds = showcontrol::gfx::sanitise (
                getLocalBounds().withTrimmedTop (32).withTrimmedBottom (28).reduced (8, 0));
        }

        paintResources.listRemainingRect = showcontrol::bgmList::timeRemainingBounds (getWidth(), getHeight());
        paintResources.listTotalRect     = showcontrol::bgmList::totalDurationBounds (getWidth(), getHeight());
        paintResources.listStatusIconBounds = showcontrol::bgmList::statusIconBounds (getHeight());

        const bool highlightRow = isPlaying() || isPaused();
        paintResources.listNameTextX = (isPlaying() || (isCueListPlayback && isPaused()))
            ? showcontrol::bgmList::kNameStartWithStatusIcon
            : showcontrol::bgmList::kNameStartDefault;

        const bool reserveLoopSlot = ! isCueListPlayback && hasFile;
        const auto nameLayout = showcontrol::bgmList::layoutListNameRow (getWidth(), getHeight(),
                                                                         paintResources.listNameTextX,
                                                                         reserveLoopSlot);
        paintResources.listNameArea = nameLayout.nameArea;
        paintResources.listLoopIconBounds = nameLayout.loopIconArea;
        paintResources.listNameWidth = nameLayout.nameArea.getWidth();

        const int availW = paintResources.listNameWidth;
        paintResources.listShowArtist = cachedMeta.artist.isNotEmpty() && getHeight() >= 30;
        paintResources.listNameWidth  = paintResources.listShowArtist ? juce::jmax (0, availW / 2 - 4) : availW;
        paintResources.listArtistWidth = juce::jmax (0, availW - paintResources.listNameWidth - 4);

        if (! isRenderAsGridMode)
        {
            const float badgeWidth = 24.0f, badgeHeight = 15.0f;
            paintResources.gridBadgeRect = juce::Rectangle<float> ((getWidth() - badgeWidth) * 0.5f,
                                                                 (float) getHeight() - badgeHeight - 4.0f,
                                                                 badgeWidth, badgeHeight);
        }
    }

    /** fadeInMs > 0 → fade-in; ngược lại postPlay() tức thì (message thread). */
    void postPlayOrFadeIn (CueTransitionReason reason)
    {
        warmReadAheadPipeline();

        if (fadeInMs > 0.0)
        {
            startFadeIn (fadeInMs);
            return;
        }

        if (! realtimeSource.postPlay())
            setCueState (PadCueState::ready, reason, true);
    }

    void applyPendingProjectState()
    {
        if (pendingProjectState.customName.isNotEmpty())
            customName = pendingProjectState.customName;

        setTrimStart (pendingProjectState.trimStart);
        setTrimEnd (pendingProjectState.trimEnd);
        setAutoNormalize (pendingProjectState.autoNormalize);
        setNormalizeUseLufs (pendingProjectState.normalizeUseLufs);
        loudnessSettings.preset = (showcontrol::loudness::Preset) juce::jlimit (0, 3, pendingProjectState.normalizePreset);
        loudnessSettings.profile = (showcontrol::loudness::ContentProfile) juce::jlimit (0, 4, pendingProjectState.normalizeProfile);
        loudnessSettings.safeMode = pendingProjectState.normalizeSafeMode;
        loudnessSettings.customTargetLufs = pendingProjectState.normalizeCustomTargetLufs;
        loudnessSettings.enabled = pendingProjectState.autoNormalize;
        loudnessSettings.mode = pendingProjectState.normalizeUseLufs
                                    ? showcontrol::loudness::MeasureMode::lufs
                                    : showcontrol::loudness::MeasureMode::rms;

        if (! pendingProjectState.autoNormalize)
            setOutputGain (pendingProjectState.outputGain);
        else
            pendingLoadGain = pendingProjectState.outputGain;

        setOutputBus (pendingProjectState.outputBus);

        // Chỉ restore metadata từ XML khi chưa đọc được từ file (fallback)
        if (cachedMeta.title.isEmpty() && pendingProjectState.cachedMeta.title.isNotEmpty())
            cachedMeta = pendingProjectState.cachedMeta;

        fadeInMs  = pendingProjectState.fadeInMs;
        fadeOutMs = pendingProjectState.fadeOutMs;

        setDspEqEnabled (pendingProjectState.dspEqEnabled);
        bool anyBand = false;
        for (int b = 0; b < PadParametricEq6::kNumBands; ++b)
        {
            if (std::abs (pendingProjectState.dspEqBandDb[(size_t) b]) > 0.05f)
            {
                anyBand = true;
                setDspEqBandGainDb (b, pendingProjectState.dspEqBandDb[(size_t) b]);
            }
        }
        if (! anyBand)
            setDspEqGainsDb (pendingProjectState.dspEqLowDb,
                             pendingProjectState.dspEqMidDb,
                             pendingProjectState.dspEqHighDb);
        refreshLufsSyncGain();

        hasPendingProjectState = false;
        repaint();
    }

    void refreshLufsSyncGain() noexcept
    {
        if (! autoNormalizeEnabled)
        {
            realtimeSource.getDsp().getLoudness().setGain (1.0f);
            return;
        }

        applyVolumeSyncGainIfReady();
    }

    void ensureReadAheadBuffer()
    {
        if (! hasFile || memorySource != nullptr || readerSource == nullptr
            || sharedTimeSliceThread == nullptr || transportUsesReadAhead)
            return;

        const float g = transportSource.getGain();
        transportSource.setSource (readerSource.get(),
                                   defaultReadAheadSamples (sourceSampleRate),
                                   sharedTimeSliceThread,
                                   sourceSampleRate);
        transportSource.setGain (g);
        transportUsesReadAhead = true;
    }

    /** Prime BufferingAudioSource + slice cache — message thread, trước GO/PAD. */
    void warmReadAheadPipeline() noexcept
    {
        if (memorySource != nullptr)
            return;

        ensureReadAheadBuffer();

        if (sharedTimeSliceThread != nullptr)
            sharedTimeSliceThread->notify();
    }

    bool isAllowedCueTransition (PadCueState from, PadCueState to) const noexcept
    {
        if (from == to)
            return true;

        switch (from)
        {
            case PadCueState::empty:   return to == PadCueState::loading || to == PadCueState::ready;
            case PadCueState::loading: return to == PadCueState::ready || to == PadCueState::empty || to == PadCueState::stopped;
            case PadCueState::ready:   return to == PadCueState::playing || to == PadCueState::stopped || to == PadCueState::empty;
            case PadCueState::playing: return to == PadCueState::paused || to == PadCueState::stopped
                                            || to == PadCueState::stopping || to == PadCueState::ready;
            case PadCueState::paused:  return to == PadCueState::playing || to == PadCueState::stopped
                                            || to == PadCueState::stopping || to == PadCueState::ready;
            case PadCueState::stopping: return to == PadCueState::stopped || to == PadCueState::ready
                                             || to == PadCueState::playing;
            case PadCueState::stopped: return to == PadCueState::playing || to == PadCueState::ready || to == PadCueState::empty || to == PadCueState::loading;
            default:                   return true;
        }
    }

    const char* cueReasonToString (CueTransitionReason reason) const noexcept
    {
        switch (reason)
        {
            case CueTransitionReason::userPlayToggle: return "userPlayToggle";
            case CueTransitionReason::userPause:      return "userPause";
            case CueTransitionReason::userResume:     return "userResume";
            case CueTransitionReason::userStop:       return "userStop";
            case CueTransitionReason::timerSync:      return "timerSync";
            case CueTransitionReason::fileLoadStart:  return "fileLoadStart";
            case CueTransitionReason::fileLoadFail:   return "fileLoadFail";
            case CueTransitionReason::fileLoadedReady:return "fileLoadedReady";
            case CueTransitionReason::fileUnload:     return "fileUnload";
            case CueTransitionReason::naturalEnd:     return "naturalEnd";
            default:                                  return "unknown";
        }
    }

    bool setCueState (PadCueState state, CueTransitionReason reason, bool force = false) noexcept
    {
        const auto current = cueState.load (std::memory_order_relaxed);
        if (! force && ! isAllowedCueTransition (current, state))
        {
            juce::Logger::writeToLog ("[CUE-SM] warn transition "
                                      + juce::String ((int) current) + " -> " + juce::String ((int) state)
                                      + " reason=" + juce::String (cueReasonToString (reason)));
        }

        cueState.store (state, std::memory_order_relaxed);
        return true;
    }

    void changeListenerCallback (juce::ChangeBroadcaster* source) override
    {
        if (source == &thumbnail)
        {
            invalidateWaveformBaseCache();
            rebuildWaveformBaseCacheIfNeeded();
            repaint();
        }
    }

    void invalidateWaveformBaseCache() noexcept
    {
        waveformBaseCacheValid = false;
        waveformBaseCache = {};
    }

    void rebuildWaveformBaseCacheIfNeeded()
    {
        if (waveformBaseCacheValid || ! thumbnailLoaded)
            return;

        const auto& wb = paintResources.waveformBounds;

        if (wb.getWidth() <= 0 || wb.getHeight() <= 0)
            return;

        if (thumbnail.getTotalLength() <= 0.0)
            return;

        double tStart = 0.0, tEnd = 0.0;
        getTrimmedDisplayRange (tStart, tEnd);

        waveformBaseCache = juce::Image (juce::Image::ARGB, wb.getWidth(), wb.getHeight(), true);
        juce::Graphics cacheG (waveformBaseCache);
        const auto surface = getPadSurfaceColour();
        cacheG.fillAll (surface);

        const auto dimInk = showcontrol::colours::opaqueWaveformInk (
            surface, getWaveformFillColour().withAlpha (1.0f),
            showcontrol::colours::kPadWaveformInkAlpha);
        cacheG.setColour (dimInk);
        thumbnail.drawChannel (cacheG, wb.withPosition (0, 0), tStart, tEnd, 0, 1.0f);
        waveformBaseCacheValid = true;
    }

    void labelTextChanged (juce::Label* labelThatHasChanged) override
    {
        if (labelThatHasChanged != &trackNameLabel)
            return;

        const juce::String newName = trackNameLabel.getText().trim();

        if (newName.isNotEmpty())
        {
            setCustomName (newName);

            if (onTrackNameChanged)
                onTrackNameChanged (this);
        }
    }

    void editorShown (juce::Label* label, juce::TextEditor& editor) override
    {
        if (label != &trackNameLabel)
            return;

        if (onTrackNameEditBegan)
            onTrackNameEditBegan (this);

        ShowControlLookAndFeel::applyInlineListNameEditorStyle (editor, isDarkMode, isSelectedRowState);

        if (isRenderAsGridMode)
        {
            const auto pal = ShowTheme::get (isDarkMode);
            editor.setColour (juce::TextEditor::backgroundColourId, pal.panelElevated);
        }

        const auto pal = ShowTheme::get (isDarkMode);
        editor.setTextToShowWhenEmpty (showcontrol::localization::tr (u8"Nhập tên bài hát mới"),
                                       pal.textMuted);
    }

    void editorHidden (juce::Label* label, juce::TextEditor& editor) override
    {
        juce::ignoreUnused (editor);

        if (label != &trackNameLabel)
            return;

        trackNameEditing = false;
        trackNameLabel.setVisible (false);
        repaint();
    }

    void layoutTrackNameLabel()
    {
        if (isRenderAsGridMode)
        {
            constexpr int kTitlePadLeft  = 12;
            constexpr int kTitlePadRight = 10;
            const int titleWidth = juce::jmax (0, getWidth() - kTitlePadLeft - kTitlePadRight);
            trackNameLabel.setBounds (kTitlePadLeft, 6, titleWidth, 22);
            return;
        }

        rebuildPaintResources();
        trackNameLabel.setBounds (paintResources.listNameArea);
        trackNameLabel.setJustificationType (juce::Justification::centredLeft);
    }

    void ensureThumbnailLoaded()
    {
        if (! thumbnailLoadAllowedNow || thumbnailLoaded)
            return;

        if (! thumbnailPendingFile.existsAsFile())
            return;

        // Disk cache (.wfc) + RAM cache 500 slot — loadNewThumb trên TimeSliceThread dùng chung.
        thumbnail.setSource (new juce::FileInputSource (thumbnailPendingFile));
        thumbnailLoaded = true;
    }

    void maybeStartNormalization()
    {
        if (! normalizationAllowedNow || ! autoNormalizeEnabled || ! hasPendingNormalization)
            return;

        if (! pendingNormalizationFile.existsAsFile())
        {
            hasPendingNormalization = false;
            return;
        }

        if (normalizer.isBusy())
        {
            hasPendingNormalization = true;
            return;
        }

        hasPendingNormalization = false;

        const auto fileToAnalyze = pendingNormalizationFile;
        juce::Component::SafePointer<SoundPad> safePad (this);

        normalizer.analyzeAudioFile (fileToAnalyze,
            [safePad, fileToAnalyze] (AudioAnalyzer::FileLoudnessAnalysis analysis)
            {
                dispatchNormalizationComplete (safePad, fileToAnalyze, analysis);
            });
    }

    void notifyPlaybackStateChanged()
    {
        rebuildPaintResources();

        if (! isPlaybackPositionLive() && ! isLoading())
            shortCircuitPlaybackVisuals();

        repaint();

        if (ownsPlaybackUiUpdates() && onPlaybackStateChanged)
            onPlaybackStateChanged();
    }

    void startLiveUiTimer() noexcept
    {
        startTimerHz (60);
    }

    /** Kết thúc fade-out stop im lặng — không kích hoạt onTrackFinished / GO. */
    void finalizeStoppingAfterFadeOut()
    {
        if (getCueState() != PadCueState::stopping)
            return;

        playState.store (PlayState::Stopped, std::memory_order_release);
        realtimeSource.clearVisualFadeFlagsOnMessageThread();
        realtimeSource.clearStaleFadeOutArmOnMessageThread();
        setCueState (PadCueState::stopped, CueTransitionReason::userStop, true);
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        lastDeferredStopGeneration = realtimeSource.getDeferredStopGeneration();

        // Message thread: dừng transport sau khi audio thread đã ramp gain về 0.
        transportSource.setGain (getOutputGain());
        transportSource.stop();
        transportSource.setPosition (trimStart);
        realtimeSource.postSeek (trimStart);
        realtimeSource.postSetGain (getOutputGain());
        realtimeSource.snapPublishedUiToTrimOnMessageThread();
        shortCircuitPlaybackVisuals();
        notifyPlaybackStateChanged();
    }

    void consumeDeferredTransportStopRequests()
    {
        const uint32_t generation = realtimeSource.getDeferredStopGeneration();
        if (generation == lastDeferredStopGeneration)
            return;

        lastDeferredStopGeneration = generation;

        // Playing / Paused: không reset vị trí — pause cần giữ con trỏ để Resume (phím P).
        if (getCueState() == PadCueState::playing || getCueState() == PadCueState::paused)
            return;

        playState.store (PlayState::Stopped, std::memory_order_release);
        transportSource.setGain (getOutputGain());
        transportSource.stop();
        transportSource.setPosition (trimStart);
        realtimeSource.postSeek (trimStart);
    }

    void acknowledgeDeferredStopGeneration() noexcept
    {
        // Bỏ qua stop request cũ trước khi phát/resume để tránh race "vừa play đã bị stop lại".
        lastDeferredStopGeneration = realtimeSource.getDeferredStopGeneration();
    }

    void consumeRealtimeDiagnostics()
    {
        droppedCommandSinceLastLog += realtimeSource.consumeDroppedCommandCount();

        if (droppedCommandSinceLastLog == 0)
            return;

        const auto nowMs = juce::Time::getMillisecondCounter();
        if (nowMs - lastDroppedCommandLogMs < 1000)
            return;

        juce::Logger::writeToLog ("[RT-DIAG] Pad command queue overflow, dropped="
                                  + juce::String ((int) droppedCommandSinceLastLog));
        droppedCommandSinceLastLog = 0;
        lastDroppedCommandLogMs = nowMs;
    }

    bool shouldApplyPublishedCueState (PadCueState local, PadCueState published) const noexcept
    {
        if (local == PadCueState::paused && published != PadCueState::paused)
            return false;

        if (local == PadCueState::stopped && published == PadCueState::playing)
            return false;

        if (local == PadCueState::stopping && published == PadCueState::playing)
            return false;

        if (local == PadCueState::stopping && (published == PadCueState::stopped || published == PadCueState::ready))
            return true;

        if (local == PadCueState::playing && published == PadCueState::paused)
            return false;

        // Không revert playing → stopped/ready: lệnh play có thể chưa được audio thread xử lý,
        // hoặc publishedCueState momentarily stale. Natural end luôn được phát hiện qua
        // trackFinishedGeneration trước khi syncCueStateFromPlayback() có thể thấy ready.
        if (local == PadCueState::playing &&
            (published == PadCueState::stopped || published == PadCueState::ready))
            return false;

        return true;
    }

    void syncCueStateFromPlayback() noexcept
    {
        if (getCueState() == PadCueState::loading)
            return;

        if (! hasFile)
        {
            setCueState (PadCueState::empty, CueTransitionReason::timerSync, true);
            return;
        }

        if (isFading())
        {
            if (getCueState() != PadCueState::stopping && getCueState() != PadCueState::stopped)
                setCueState (PadCueState::playing, CueTransitionReason::timerSync);
            return;
        }

        const auto local = getCueState();
        const auto published = realtimeSource.getPublishedCueState();

        if (! shouldApplyPublishedCueState (local, published))
            return;

        if (published == PadCueState::empty)
            setCueState (PadCueState::ready, CueTransitionReason::timerSync);
        else if (published != local)
            setCueState (published, CueTransitionReason::timerSync, true);
    }

    static void dispatchAsyncLoadFailed (juce::Component::SafePointer<SoundPad> safePad,
                                         uint32_t generation) noexcept
    {
        if (safePad == nullptr)
            return;

        if (generation != safePad->audioLoadGeneration.load (std::memory_order_acquire))
            return;

        safePad->hasFile = false;
        safePad->setCueState (PadCueState::empty, CueTransitionReason::fileLoadFail, true);
        safePad->repaint();
    }

    static void dispatchAsyncLoadComplete (juce::Component::SafePointer<SoundPad> safePad,
                                           uint32_t generation,
                                           std::unique_ptr<LoadedAudioPayload> payload)
    {
        if (safePad == nullptr)
            return;

        if (generation != safePad->audioLoadGeneration.load (std::memory_order_acquire))
            return;

        safePad->commitLoadedAudioPayload (std::move (payload));
    }

    static void dispatchNormalizationComplete (juce::Component::SafePointer<SoundPad> safePad,
                                               const juce::File& fileToAnalyze,
                                               const AudioAnalyzer::FileLoudnessAnalysis& analysis) noexcept
    {
        if (safePad == nullptr)
            return;

        safePad->normalizer.markFinished();

        if (fileToAnalyze != safePad->musicFile)
            return;

        safePad->fileLoudnessAnalysis = analysis;
        safePad->measuredLoudness = analysis.integratedLufs < -0.01
                                        ? analysis.integratedLufs
                                        : analysis.rms;

        if (safePad->autoNormalizeEnabled)
        {
            if (! safePad->applyVolumeSyncGainIfReady() && safePad->pendingLoadGain > 0.0f)
                safePad->setOutputGain (safePad->pendingLoadGain);
        }
        else if (safePad->pendingLoadGain > 0.0f)
        {
            safePad->setOutputGain (safePad->pendingLoadGain);
        }

        if (safePad->onNormalizationComplete)
            safePad->onNormalizationComplete (safePad.getComponent());

        if (safePad->hasPendingNormalization && safePad->normalizationAllowedNow)
            safePad->maybeStartNormalization();
    }

    void loadAudioFileInternal (const juce::File& f)
    {
        musicFile = f;

        if (! f.existsAsFile())
        {
            hasFile = false;
            cachedFileName = f.getFileNameWithoutExtension();
            setCueState (PadCueState::empty, CueTransitionReason::fileLoadFail, true);
            repaint();
            return;
        }

        showcontrol::preload::sharedPool().requestPreload (f);

        if (auto cached = showcontrol::preload::sharedPool().tryTake (f))
        {
            auto payload = payloadFromPreloadedCue (std::move (cached));

            if (payload != nullptr)
            {
                commitLoadedAudioPayload (std::move (payload));
                return;
            }
        }

        const uint32_t generation = audioLoadGeneration.fetch_add (1, std::memory_order_acq_rel) + 1;
        hasFile = false;
        setCueState (PadCueState::loading, CueTransitionReason::fileLoadStart, true);
        repaint();

        juce::Component::SafePointer<SoundPad> safePad (this);

        showcontrol::background::enqueue ([safePad, f, generation]()
        {
            juce::AudioFormatManager localFormatManager;
            localFormatManager.registerBasicFormats();

            if (auto warmReader = std::unique_ptr<juce::AudioFormatReader> (localFormatManager.createReaderFor (f)))
            {
                const int warmSamples = juce::jmin (showcontrol::preload::readAheadSamplesForRate (warmReader->sampleRate),
                                                    (int) warmReader->lengthInSamples);
                if (warmSamples > 0)
                {
                    juce::AudioBuffer<float> warmBuf (juce::jmax (1, (int) warmReader->numChannels),
                                                      warmSamples);
                    warmBuf.clear();
                    warmReader->read (&warmBuf, 0, warmSamples, 0, true, true);
                }
            }

            auto payload = std::make_unique<LoadedAudioPayload>();
            payload->file = f;
            payload->reader.reset (localFormatManager.createReaderFor (f));

            if (payload->reader == nullptr)
            {
                juce::MessageManager::callAsync ([safePad, generation]()
                {
                    dispatchAsyncLoadFailed (safePad, generation);
                });
                return;
            }

            payload->sampleRate = payload->reader->sampleRate;
            payload->displayName = VideoAudioExtractor::displayNameFromAudioPath (f);
            payload->meta = AudioMetadataReader::readFromReader (payload->reader.get(), f);

            juce::MessageManager::callAsync ([safePad, generation, payload = std::move (payload)]() mutable
            {
                dispatchAsyncLoadComplete (safePad, generation, std::move (payload));
            });
        });
    }

    /** Message thread: hoán đổi reader + transport — không đọc đĩa, RT-safe cho play ngay sau. */
    void commitLoadedAudioPayload (std::unique_ptr<LoadedAudioPayload> payload)
    {
        if (payload == nullptr || (! payload->usesFullRam && payload->reader == nullptr))
        {
            setCueState (PadCueState::empty, CueTransitionReason::fileLoadFail, true);
            repaint();
            return;
        }

        realtimeSource.postStop();
        transportSource.setSource (nullptr);
        readerSource.reset();
        memorySource.reset();
        ownedFullRamBuffer.setSize (0, 0);

        const auto& f = payload->file;
        musicFile = f;
        cachedFileName = payload->displayName;
        const double previousBpm = cachedMeta.bpm;
        cachedMeta = payload->meta;
        if (cachedMeta.bpm <= 0.0 && previousBpm > 0.0)
            cachedMeta.bpm = previousBpm;

        if (cachedMeta.formatInfoString.isEmpty())
            cachedMeta.formatInfoString = cachedMeta.buildFormatInfoUncached();
        sourceSampleRate = payload->sampleRate;

        if (! isCueListPlayback && isLoopingState)
            isLoopingState = false;

        if (payload->usesFullRam && payload->fullRamBuffer.getNumSamples() > 0)
        {
            ownedFullRamBuffer = std::move (payload->fullRamBuffer);
            memorySource = std::make_unique<juce::MemoryAudioSource> (ownedFullRamBuffer, false);
            transportUsesReadAhead = false;
            transportSource.setSource (memorySource.get(), 0, nullptr, sourceSampleRate);
        }
        else
        {
            readerSource = std::make_unique<juce::AudioFormatReaderSource> (payload->reader.release(), true);
            readerSource->setLooping (false);
            const int readAhead = sharedTimeSliceThread != nullptr ? defaultReadAheadSamples (sourceSampleRate) : 0;
            transportUsesReadAhead = readAhead > 0;
            transportSource.setSource (readerSource.get(), readAhead, sharedTimeSliceThread, sourceSampleRate);
        }

        realtimeSource.setLooping (isLoopingState);
        setOutputGain (pendingLoadGain);

        thumbnailPendingFile = f;
        thumbnailLoaded = false;
        if (thumbnailLoadAllowedNow)
            ensureThumbnailLoaded();

        hasFile = true;
        trimStart = 0.0;
        trimEnd = 0.0;
        realtimeSource.setTrimRange (trimStart, trimEnd);
        setCueState (PadCueState::ready, CueTransitionReason::fileLoadedReady, true);
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();

        warmReadAheadPipeline();

        if (hasPendingProjectState)
            applyPendingProjectState();
        else
        {
            fadeInMs  = 0.0;
            fadeOutMs = 0.0;
        }

        if (autoNormalizeEnabled)
        {
            pendingNormalizationFile = f;
            hasPendingNormalization = true;
            if (normalizationAllowedNow)
                maybeStartNormalization();
        }

        repaint();

        if (onAudioFileLoaded)
            onAudioFileLoaded (this);
    }

    void paintGridBackground (juce::Graphics& g, const juce::Rectangle<float>& bounds) {
        const float corner = ShowTheme::kPanelCornerRadius;

        g.setColour (getPadSurfaceColour());
        g.fillRoundedRectangle (bounds, corner);

        const bool playingNow = isPlaybackPositionLive();

        if (isSelectedRowState)
        {
            g.setColour (getPadSelectionBorderColour());
            g.drawRoundedRectangle (bounds.reduced (1.0f), corner, 2.5f);
        }
        else
        {
            juce::Colour borderCol = isDarkMode ? juce::Colour (0xFF2F3542) : juce::Colour (0xFFE5E5EA);
            if (playingNow)
                borderCol = getActiveInkColour().withAlpha (0.55f);

            g.setColour (borderCol);
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }

        if (isArmedState)
        {
            g.setColour (getActiveInkColour());
            g.fillEllipse (bounds.getRight() - 14.0f, bounds.getY() + 6.0f, 8.0f, 8.0f);
        }
    }

    void paintGridWaveform (juce::Graphics& g, const juce::Rectangle<float>& bounds) {
        const double currentPos = getPlaybackPosition();
        double tStart = 0.0, tEnd = 0.0;
        getTrimmedDisplayRange (tStart, tEnd);
        const double effectiveLen = juce::jmax (0.0, tEnd - tStart);
        const auto& waveformBounds = paintResources.waveformBounds;
        const bool hasCustomTint = ! showcontrol::colours::isDefaultTagColour (tagColour);
        if (waveformBounds.getWidth() <= 0 || waveformBounds.getHeight() <= 0)
            return;

        if (thumbnail.getTotalLength() <= 0.0)
        {
            const auto pal = ShowTheme::get (isDarkMode);
            g.setColour (pal.textMuted.withAlpha (0.72f));
            g.setFont (ShowTheme::font (juce::jmax (8.5f, (float) waveformBounds.getHeight() * 0.28f)));
            g.drawFittedText (showcontrol::localization::tr (u8"Đang nạp..."),
                              waveformBounds, juce::Justification::centred, 1);
            return;
        }

        rebuildWaveformBaseCacheIfNeeded();

        const auto surface = getPadSurfaceColour();
        const auto dimInk = showcontrol::colours::opaqueWaveformInk (
            surface, getWaveformFillColour().withAlpha (1.0f),
            showcontrol::colours::kPadWaveformInkAlpha);
        const auto brightInk = showcontrol::colours::opaqueWaveformInk (
            surface, getWaveformFillColour().withAlpha (1.0f),
            juce::jmin (1.0f, showcontrol::colours::kPadWaveformInkAlpha
                              + (hasCustomTint ? 0.12f : 0.18f)));

        auto drawDimWaveform = [&]
        {
            if (waveformBaseCacheValid)
                g.drawImage (waveformBaseCache, waveformBounds.toFloat());
            else
            {
                g.setColour (surface);
                g.fillRect (waveformBounds);
                g.setColour (dimInk);
                thumbnail.drawChannel (g, waveformBounds, tStart, tEnd, 0, 1.0f);
            }
        };

        auto drawBrightWaveform = [&]
        {
            g.setColour (brightInk);
            thumbnail.drawChannel (g, waveformBounds, tStart, tEnd, 0, 1.0f);
        };

        if (effectiveLen > 0.0 && currentPos > tStart)
        {
            const double relativePos = juce::jlimit (tStart, tEnd, currentPos) - tStart;
            const float progress = showcontrol::gfx::isFinite ((float) (relativePos / effectiveLen))
                ? juce::jlimit (0.0f, 1.0f, (float) (relativePos / effectiveLen))
                : 0.0f;

            if (progress <= 0.0f)
            {
                drawDimWaveform();
            }
            else if (progress >= 1.0f)
            {
                g.setColour (surface);
                g.fillRect (waveformBounds);
                drawBrightWaveform();
            }
            else
            {
                const int splitX = showcontrol::gfx::clampDimension (
                    waveformBounds.getX()
                    + (int) std::round ((float) waveformBounds.getWidth() * progress));

                if (splitX > waveformBounds.getX())
                {
                    auto playedClip = waveformBounds;
                    playedClip.setWidth (splitX - waveformBounds.getX());

                    if (showcontrol::gfx::canClip (playedClip))
                    {
                        g.saveState();
                        g.reduceClipRegion (playedClip);
                        g.setColour (surface);
                        g.fillRect (waveformBounds);
                        drawBrightWaveform();
                        g.restoreState();
                    }
                }

                if (splitX < waveformBounds.getRight())
                {
                    auto unplayedClip = waveformBounds;
                    unplayedClip.setLeft (splitX);

                    if (showcontrol::gfx::canClip (unplayedClip))
                    {
                        g.saveState();
                        g.reduceClipRegion (unplayedClip);
                        drawDimWaveform();
                        g.restoreState();
                    }
                }
            }
        }
        else
        {
            drawDimWaveform();
        }

        const auto& palWave = ShowTheme::get (isDarkMode);
        if (isSelectedRowState)
        {
            g.setColour (getPadSelectionBorderColour());
            g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 2.5f);
        }
        else if (isPlaybackPositionLive())
        {
            g.setColour (palWave.padPlayingBorder);
            g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.5f);
        }
        else if (isCueListPlayback && isPaused())
        {
            g.setColour (palWave.warning);
            g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.5f);
        }
    }

    void paintGridHeader (juce::Graphics& g)
    {
        const auto pal = ShowTheme::get (isDarkMode);

        g.setColour (getPadTitleColour());

        if (isLoading())
        {
            g.setFont (paintResources.gridTitle);
            g.drawText (juce::String::fromUTF8 (u8"Đang nạp..."),
                        paintResources.gridTitleRect,
                        juce::Justification::topLeft, true);
            return;
        }

        if (! trackNameEditing)
        {
            g.setFont (paintResources.gridTitle);
            g.drawText (getPadName(),
                        paintResources.gridTitleRect,
                        juce::Justification::topLeft, false);
        }

        if (paintResources.gridArtistRect.getHeight() > 0)
        {
            g.setColour (pal.textMuted);
            g.setFont (paintResources.gridArtist);
            g.drawText (cachedMeta.artist,
                        paintResources.gridArtistRect,
                        juce::Justification::topLeft, true);
        }
    }

    void paintGridFooter (juce::Graphics& g)
    {
        if (! showGridDuration)
            return;

        const auto pal = ShowTheme::get (isDarkMode);
        double remainingTime = isPlaybackPositionLive() ? getRemainingSeconds() : 0.0;

        if (isPlaying() && remainingTime <= 5.0)
            g.setColour ((juce::Time::getMillisecondCounter() % 400) < 200 ? pal.danger : pal.textSecondary);
        else
            g.setColour (pal.textSecondary);

        g.setFont (paintResources.gridTimerBold);
        g.drawText (formatTimeString (remainingTime),
                    paintResources.gridRemainingRect,
                    juce::Justification::centredLeft);

        g.setColour (pal.textMuted);
        g.setFont (paintResources.gridTimer);
        g.drawText (formatTimeString (getEffectiveLength()),
                    paintResources.gridTotalRect,
                    juce::Justification::centredRight);
    }

    void paintGridBadge (juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        if (! showGridHotkeyBadge)
            return;

        juce::ignoreUnused (bounds);
        const auto& badgeRect = paintResources.gridBadgeRect;
        const auto pal = ShowTheme::get (isDarkMode);
        const bool hasCustomInk = ! showcontrol::colours::isDefaultTagColour (tagColour);
        const auto ink = getActiveInkColour();

        if (isPlaying())
            g.setColour (pal.success);
        else if (hasCustomInk)
            g.setColour (ink.withAlpha (isDarkMode ? 0.22f : 0.12f));
        else
            g.setColour (isDarkMode ? pal.shortcutBadgeBg : (hasFile ? pal.shortcutBadgeBg : pal.listRowBg));

        g.fillRoundedRectangle (badgeRect, 3.0f);

        if (isPlaying())
            g.setColour (juce::Colours::white);
        else if (hasCustomInk)
            g.setColour (ink);
        else
            g.setColour (hasFile ? pal.shortcutBadgeText : pal.textMuted);

        g.setFont (paintResources.gridBadge);
        g.drawText (shortcutLabel, badgeRect, juce::Justification::centred);
    }

    void unloadAudioFile()
    {
        cancelPendingAsyncWork();

        releaseThumbnailResources();

        realtimeSource.postStop();
        transportSource.setSource (nullptr);
        readerSource.reset();
        memorySource.reset();
        ownedFullRamBuffer.setSize (0, 0);
        hasFile = false;
        thumbnailLoaded = false;
        thumbnailPendingFile = juce::File();
        thumbnailLoadAllowedNow = false;

        hasPendingNormalization = false;
        pendingNormalizationFile = juce::File();
        measuredLoudness = 0.0;
        normalizationAllowedNow = false;
        transportUsesReadAhead = false;
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        setCueState (PadCueState::empty, CueTransitionReason::fileUnload, true);
        stopTimer();
    }
};