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
#include "ShowWaveformCache.h"

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
} // namespace showcontrol::background

//==============================================================================
class VolumeNormalizer
{
public:
    /** useLufs: true → LUFS; false → RMS. Đo trên thread pool, callback luôn trên message thread. */
    void analyzeAudioFile (const juce::File& file,
                           bool useLufs,
                           std::function<void (double measured, bool isLufs)> onComplete)
    {
        if (! file.existsAsFile() || ! onComplete)
            return;

        isAnalyzing.store (true, std::memory_order_release);

        showcontrol::background::enqueue ([file, useLufs, cb = std::move (onComplete)]() mutable
        {
            juce::AudioFormatManager localFormatManager;
            localFormatManager.registerBasicFormats();

            const double measured = useLufs ? AudioAnalyzer::calculateFileLUFS (file, localFormatManager)
                                            : AudioAnalyzer::calculateFileRMS (file, localFormatManager);

            juce::MessageManager::callAsync ([cb = std::move (cb), measured, useLufs]() mutable
            {
                cb (measured, useLufs);
            });
        });
    }

    void markFinished() noexcept { isAnalyzing.store (false, std::memory_order_release); }
    bool isBusy() const noexcept { return isAnalyzing.load (std::memory_order_acquire); }

private:
    std::atomic<bool> isAnalyzing { false };
};

//==============================================================================
class SoundPad : public juce::Component, public juce::Timer 
{
public:
    /** ~0.74s @ 44.1kHz — đọc đĩa trên TimeSliceThread, không trong audio callback. */
    static constexpr int kReadAheadBufferSamples = 32768;

    SoundPad() : thumbnail (512, formatManager, showcontrol::waveform::sharedCache()),
                 realtimeSource (transportSource)
    {
        formatManager.registerBasicFormats();
        setWantsKeyboardFocus (false);
    }
    
    ~SoundPad() override
    {
        cancelPendingAsyncWork();
        stopTimer();
        releaseThumbnailResources();
        realtimeSource.postStop();
        transportSource.setSource (nullptr);
        readerSource.reset();
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
    std::function<void(SoundPad*)> onPadReorderBegin;
    std::function<void(SoundPad*, juce::Point<int>)> onPadReorderMove;
    std::function<void(SoundPad*)> onPadReorderEnd;
    std::function<void(SoundPad*)> onTrackFinished; // Cổng callback báo tử chuyển bài liên tục Foobar2000 Mode
    std::function<void(SoundPad*)> onAudioFileLoaded;
    /** Background normalize xong — Inspector cập nhật nhãn LUFS/RMS. */
    std::function<void(SoundPad*)> onNormalizationComplete;
    /** Farrago/QLab: click pad hoặc phím GO — MainComponent xử lý pre-wait. */
    std::function<void(SoundPad*)> onRequestGo;
    /** true trong lúc startup reassert — chặn triggerPlay/Stop ảo từ UI. */
    std::function<bool()> isPlaybackCommandBlocked;

    /** wireSoundPad chỉ gán callback một lần — tránh đăng ký listener trùng khi loadList. */
    bool isUiCallbacksWired = false;

    /** Chặn trigger hotkey/GO trùng trên cùng pad (message thread, 300ms). */
    bool tryClaimPadHotkeyTrigger (juce::uint32 holdoffMs = 300) noexcept
    {
        const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
        const juce::uint32 untilMs = hotkeyTriggerGuardUntilMs.load (std::memory_order_acquire);

        if (nowMs < untilMs)
            return false;

        hotkeyTriggerGuardUntilMs.store (nowMs + holdoffMs, std::memory_order_relaxed);
        return true;
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

    /** Đọc trước buffer trên message thread — gọi trước GO để giảm trễ. */
    void prepareForInstantPlay()
    {
        ensureReadAheadBuffer();
    }

    /**
     * Predictive preload: đảm bảo reader + read-ahead sẵn sàng khi user chọn dòng BGM.
     * Chỉ message thread gọi; I/O nặng đã ở ThreadPool.
     */
    void requestPreloadForPlayback() noexcept
    {
        playbackPreloadRequested.store (true, std::memory_order_relaxed);

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
    void setThumbnailLoadAllowed (bool allow) noexcept
    {
        thumbnailLoadAllowedNow = allow;
        if (allow)
            ensureThumbnailLoaded();
    }

    void setNormalizationLoadAllowed (bool allow)
    {
        normalizationAllowedNow = allow;
        if (allow)
            maybeStartNormalization();
    }

    bool isThumbnailLoaded() const noexcept { return thumbnailLoaded; }

    /** UI đọc cueState (cập nhật ngay khi bấm); audio thread đồng bộ sau. */
    bool isPlaying() const { return getCueState() == PadCueState::playing; }
    bool isPaused() const { return getCueState() == PadCueState::paused; }
    bool isStopping() const { return getCueState() == PadCueState::stopping; }
    bool isTransportActive() const
    {
        return isPlaying() || isPaused() || isFading() || isStopping();
    }
    bool usesCuePauseResume() const noexcept { return isCueListPlayback; }

    void setCueListPlayback (bool isCueList) noexcept { isCueListPlayback = isCueList; }

    bool isRegisteredWithMasterMixer() const noexcept { return mixerRegisteredWithMaster; }
    void markRegisteredWithMasterMixer() noexcept { mixerRegisteredWithMaster = true; }
    void markUnregisteredFromMasterMixer() noexcept { mixerRegisteredWithMaster = false; }
    double getPlaybackPosition() const { return realtimeSource.getPublishedPosition(); }
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
        isLoopingState = s;
        if (readerSource)
            readerSource->setLooping (s);
        realtimeSource.setLooping (s);
        repaint();
    }
    juce::String getFilePath() const { return hasFile ? musicFile.getFullPathName() : ""; }

    juce::String getPadName() const
    {
        if (customName.isNotEmpty()) return customName;
        if (hasFile && cachedMeta.title.isNotEmpty()) return cachedMeta.title;
        return hasFile ? cachedFileName : juce::String::fromUTF8 (u8"Trống");
    }

    void setCustomName (const juce::String& name)
    {
        customName = name.trim();
        repaint();
    }

    juce::String getSearchableTokens() const
    {
        return shortcutLabel + " " + cachedFileName + " " + customName + " "
             + cachedMeta.title + " " + cachedMeta.artist + " " + cachedMeta.album
             + " " + getFilePath();
    }
    juce::String getShortcutLabel() const { return shortcutLabel; }
    bool hasAudioFile() const { return hasFile && ! isLoading(); }
    /** CUE grid động: giữ ô khi đang load hoặc đã gán đường dẫn — tránh compact xóa nhầm lúc import folder. */
    bool occupiesCueGridSlot() const noexcept
    {
        return isLoading() || hasFile || musicFile.existsAsFile();
    }
    int getPadIndex() const { return myIndex; }

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

    double getEffectiveLength() const
    {
        const double total = getPlaybackLength();
        const double end   = (trimEnd > 0.0) ? std::min (trimEnd, total) : total;
        return std::max (0.0, end - trimStart);
    }

    void updateTheme (bool isDark) { isDarkMode = isDark; setOpaque (true); repaint(); }
    void setIsSelectedRow (bool select) { isSelectedRowState = select; repaint(); }
    void setRenderMode (bool asGrid) { isRenderAsGridMode = asGrid; repaint(); }

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

    void setPadIndex (int index) {
        myIndex = index;
        const juce::String matrix40 = "1234567890QWERTYUIOPASDFGHJKL;ZXCVBNM,.";
        if (index >= 0 && index < matrix40.length())
        {
            shortcutLabel = juce::String::charToString (matrix40[index]);
        }
        else if (index >= matrix40.length() && index < matrix40.length() + 8)
        {
            // Bổ sung 8 phím chuẩn còn lại để đủ 48 PAD.
            shortcutLabel = "F" + juce::String (index - matrix40.length() + 1);
        }
        else
        {
            shortcutLabel = juce::String (index + 1);
        }
        repaint();
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

                startFadeOut();
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
            startTimer (40);
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

        startTimer (40);
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
        startTimer (40);
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
        startTimer (40);
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
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        lastDeferredStopGeneration = realtimeSource.getDeferredStopGeneration();
    }

    void triggerStop()
    {
        if (isPlaybackCommandBlocked && isPlaybackCommandBlocked())
            return;

        // ── Guard idempotency: nếu đang FadingOut, bỏ toàn bộ stop dội
        // (không re-touch gain/transportSource.stop để tránh giật).
        if (playState.load (std::memory_order_acquire) == PlayState::FadingOut
            || isFadeOutArmed()
            || isStopping())
        {
            playState.store (PlayState::FadingOut, std::memory_order_release);
            return;
        }

        playState.store (PlayState::Stopped, std::memory_order_release);

        setCueState (PadCueState::stopped, CueTransitionReason::userStop);
        realtimeSource.postStop();
        transportSource.stop();
        // Ack finishedGen hiện tại để khi timer restart (do triggerPlay tiếp theo),
        // nó không nhầm lẫn gen cũ từ chu kỳ phát trước với natural end mới.
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        stopTimer();
        notifyPlaybackStateChanged();
    }
    void loadExternalFile (const juce::File& file) { loadAudioFileInternal (file); }

    void startFadeIn (double durationMs = 500.0)
    {
        realtimeSource.postFadeIn ((float) durationMs, transportSource.getGain());
        startTimer (40);
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
        if (! realtimeSource.postFadeOut ((float) std::max (100.0, dur)))
        {
            playState.store (isPlaying() ? PlayState::Playing : PlayState::Stopped,
                             std::memory_order_release);
            setCueState (PadCueState::ready, CueTransitionReason::userStop, true);
            return;
        }

        startTimer (40);
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
        refreshLufsSyncGain();
    }
    bool getAutoNormalize() const { return autoNormalizeEnabled; }

    void setNormalizeUseLufs (bool useLufs) noexcept
    {
        normalizeUseLufs = useLufs;
        refreshLufsSyncGain();
    }
    bool getNormalizeUseLufs() const noexcept { return normalizeUseLufs; }

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
    }
    double getDetectedRMS() const { return measuredLoudness; }

    float getNormalizedGain() const
    {
        if (! hasValidLoudnessMeasurement())
            return 1.0f;

        return normalizeUseLufs ? AudioAnalyzer::getGainMultiplierFromLUFS (measuredLoudness)
                                 : AudioAnalyzer::getGainMultiplier (measuredLoudness);
    }

    bool hasValidLoudnessMeasurement() const noexcept
    {
        if (! hasFile)
            return false;

        if (normalizeUseLufs)
            return measuredLoudness <= -0.01;

        return measuredLoudness > AudioAnalyzer::MIN_RMS_THRESHOLD;
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

        const auto gainStr = juce::String (getNormalizedGain(), 2);

        if (normalizeUseLufs)
        {
            if (measuredLoudness < -0.01)
                return AudioAnalyzer::formatLUFSValue (measuredLoudness) + " · Gain " + gainStr + "x";

            return juce::String::fromUTF8 (u8"LUFS: tín hiệu quá nhỏ · Gain ") + gainStr + "x";
        }

        if (measuredLoudness > AudioAnalyzer::MIN_RMS_THRESHOLD)
            return juce::String::fromUTF8 (u8"RMS ") + juce::String (measuredLoudness, 4) + " · Gain " + gainStr + "x";

        return juce::String::fromUTF8 (u8"RMS: tín hiệu quá nhỏ · Gain ") + gainStr + "x";
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
        timeInSeconds = std::max (0.0, timeInSeconds);
        int mins = static_cast<int> (timeInSeconds) / 60;
        int secs = static_cast<int> (timeInSeconds) % 60;
        int ms   = static_cast<int> (timeInSeconds * 10) % 10; 
        return juce::String::formatted ("%02d:%02d.%d", mins, secs, ms);
    }

    void timerCallback() override
    {
        consumeDeferredTransportStopRequests();
        consumeRealtimeDiagnostics();

        if (isStopping() && ! isFading())
        {
            finalizeStoppingAfterFadeOut();
            return;
        }

        const uint32_t finishedGen = realtimeSource.getTrackFinishedGeneration();
        if (finishedGen != lastTrackFinishedGen)
        {
            lastTrackFinishedGen = finishedGen;
            setCueState (PadCueState::ready, CueTransitionReason::naturalEnd, true);
            // consumeDeferredTransportStopRequests() đã chạy trước nhưng skip stop
            // vì lúc đó getCueState() còn là playing (shouldApplyPublishedCueState block).
            // Luôn dừng transport trực tiếp tại đây để tránh tự loop từ vị trí trimStart.
            transportSource.stop();
            stopTimer();
            repaint();
            if (onPlaybackStateChanged)
                onPlaybackStateChanged();
            if (! isCueListPlayback && onTrackFinished)
                onTrackFinished (this);
            return;
        }

        syncCueStateFromPlayback();

        if (isLoading() || isTransportActive())
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

        if (onSelected)
            onSelected (this, e.mods);

        if (clickToTriggerOnClick && hasAudioFile() && onRequestGo != nullptr)
            onRequestGo (this);
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
            if (onPadReorderBegin)
                onPadReorderBegin (this);
        }

        if (onPadReorderMove)
        {
            juce::Point<int> pos = e.getPosition();

            if (auto* scrollParent = findParentComponentOfClass<juce::Viewport>())
            {
                if (auto* viewed = scrollParent->getViewedComponent())
                    pos = e.getEventRelativeTo (viewed).getPosition();
            }

            onPadReorderMove (this, pos);
        }
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
    }

    void paintGridMode (juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        paintGridBackground (g, bounds);
        if (hasFile) paintGridWaveform (g, bounds);
        paintGridBadge (g, bounds);
    }

    void paintListMode (juce::Graphics& g, const juce::Rectangle<float>& bounds)
    {
        const auto pal = ShowTheme::get (isDarkMode);

        if (isSelectedRowState) g.fillAll (pal.rowSelected);
        else if (isMouseHovering) g.fillAll (pal.rowHover);
        else g.fillAll (pal.listRowBg);

        g.setColour (pal.borderSubtle);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, bounds.getWidth());

        int textX = showcontrol::bgmList::kNameStartDefault;
        g.setColour (pal.textMuted);
        g.setFont (ShowTheme::fontBold (11.0f));
        g.drawText (juce::String (myIndex + 1),
                    showcontrol::bgmList::kIndexX, 0,
                    showcontrol::bgmList::kIndexWidth, getHeight(),
                    juce::Justification::centred);

        const bool highlightRow = isPlaying() || isPaused();
        if (isPlaying()) {
            const auto iconBounds = showcontrol::bgmList::statusIconBounds (getHeight());
            showcontrol::icons::paintSpeakerIcon (g, iconBounds,
                                                  showcontrol::icons::speakerPlayingColour (isSelectedRowState),
                                                  isSelectedRowState);
            textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }
        else if (isCueListPlayback && isPaused()) {
            const auto iconBounds = showcontrol::bgmList::statusIconBounds (getHeight());
            const auto iconCol = showcontrol::icons::iconColourForListState (isSelectedRowState, isDarkMode);
            showcontrol::icons::paintPauseIcon (g, iconBounds, iconCol);
            textX = showcontrol::bgmList::kNameStartWithStatusIcon;
        }

        g.setColour (highlightRow ? ((isCueListPlayback && isPaused()) ? pal.warning : pal.success)
                                 : pal.textPrimary);
        g.setFont (ShowTheme::font (13.5f, highlightRow || isSelectedRowState ? "Bold" : "Plain"));
        if (isLoading())
        {
            g.setColour (pal.warning);
            g.setFont (ShowTheme::fontBold (13.5f));
            g.drawText (juce::String::fromUTF8 (u8"Đang nạp..."), textX, 0,
                        showcontrol::bgmList::nameColumnMaxWidth (getWidth(), textX), getHeight(),
                        juce::Justification::centredLeft, true);
            return;
        }

        // Tên bài — nếu có artist, thu hẹp để chừa chỗ cho artist
        const bool showArtist = cachedMeta.artist.isNotEmpty() && getHeight() >= 30;
        const int availW = showcontrol::bgmList::nameColumnMaxWidth (getWidth(), textX);
        const int nameWidth = showArtist ? juce::jmax (0, availW / 2 - 4) : availW;
        g.drawText (getPadName(), textX, 0, nameWidth, getHeight(), juce::Justification::centredLeft, true);

        if (showArtist)
        {
            g.setColour (pal.textMuted);
            g.setFont (ShowTheme::font (11.5f));
            g.drawText (cachedMeta.artist, textX + nameWidth + 4, 0, availW - nameWidth - 4, getHeight(), juce::Justification::centredLeft, true);
        }

        if (! isCueListPlayback && isLooping() && hasFile) {
            showcontrol::icons::paintLoopIcon (g,
                                               showcontrol::bgmList::loopIconBounds (getWidth(), getHeight()),
                                               pal.accent, true);
        }

        if (hasFile) {
            const double remainingTime = isTransportActive() ? getRemainingSeconds() : 0.0;
            if ((isPlaying() || isPaused()) && remainingTime <= 5.0 && isPlaying()) {
                if ((juce::Time::getMillisecondCounter() % 400) < 200) g.setColour (pal.danger);
                else g.setColour (pal.textSecondary);
            } else { g.setColour (pal.textSecondary); }
            const auto remainingRect = showcontrol::bgmList::timeRemainingBounds (getWidth(), getHeight());
            const auto totalRect     = showcontrol::bgmList::totalDurationBounds (getWidth(), getHeight());

            g.setFont (ShowTheme::timerFont (12.5f, true));
            g.drawText (formatTimeString (remainingTime), remainingRect, juce::Justification::centred);

            g.setColour (pal.textMuted);
            g.setFont (ShowTheme::timerFont (12.5f));
            const double displayTotal = getEffectiveLength();
            g.drawText (formatTimeString (displayTotal), totalRect, juce::Justification::centred);
        }
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

    juce::File musicFile; bool hasFile = false, isLoopingState = false, isDarkMode = true; bool isRenderAsGridMode = true, isSelectedRowState = false, isMouseHovering = false; int myIndex = 0; juce::String shortcutLabel, cachedFileName, customName; double trimStart = 0.0, trimEnd = 0.0;
    AudioMetadata cachedMeta;
    double fadeInMs  = 0.0;
    double fadeOutMs = 0.0;
    struct LoadedAudioPayload
    {
        juce::File file;
        std::unique_ptr<juce::AudioFormatReader> reader;
        double sampleRate = 44100.0;
        juce::String displayName;
        AudioMetadata meta;
    };

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    PadRealtimeSource realtimeSource;
    juce::AudioThumbnail thumbnail;
    VolumeNormalizer normalizer;
    double measuredLoudness = 0.0;
    bool autoNormalizeEnabled = true;
    bool normalizeUseLufs = false;
    uint32_t lastTrackFinishedGen = 0;
    float pendingLoadGain = 1.0f;
    bool thumbnailLoadAllowedNow = false;
    bool thumbnailLoaded = false;
    juce::File thumbnailPendingFile;

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

    /** fadeInMs > 0 → fade-in; ngược lại postPlay() tức thì (message thread). */
    void postPlayOrFadeIn (CueTransitionReason reason)
    {
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
        if (! hasFile || readerSource == nullptr || sharedTimeSliceThread == nullptr || transportUsesReadAhead)
            return;

        const float g = transportSource.getGain();
        transportSource.setSource (readerSource.get(), kReadAheadBufferSamples, sharedTimeSliceThread, sourceSampleRate);
        transportSource.setGain (g);
        transportUsesReadAhead = true;
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

        normalizer.analyzeAudioFile (fileToAnalyze, normalizeUseLufs,
            [safePad, fileToAnalyze] (double measured, bool isLufs)
            {
                dispatchNormalizationComplete (safePad, fileToAnalyze, measured, isLufs);
            });
    }

    void notifyPlaybackStateChanged()
    {
        repaint();
        if (onPlaybackStateChanged)
            onPlaybackStateChanged();
    }

    /** Kết thúc fade-out stop im lặng — không kích hoạt onTrackFinished / GO. */
    void finalizeStoppingAfterFadeOut()
    {
        if (getCueState() != PadCueState::stopping)
            return;

        playState.store (PlayState::Stopped, std::memory_order_release);
        realtimeSource.clearStaleFadeOutArmOnMessageThread();
        setCueState (PadCueState::stopped, CueTransitionReason::userStop, true);
        lastTrackFinishedGen = realtimeSource.getTrackFinishedGeneration();
        lastDeferredStopGeneration = realtimeSource.getDeferredStopGeneration();

        // Message thread: dừng transport sau khi audio thread đã ramp gain về 0.
        transportSource.setGain (getOutputGain());
        transportSource.stop();
        transportSource.setPosition (trimStart);
        realtimeSource.postSetGain (getOutputGain());

        if (! isTransportActive())
            stopTimer();

        repaint();

        if (onPlaybackStateChanged)
            onPlaybackStateChanged();
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
            if (getCueState() != PadCueState::stopping)
                setCueState (PadCueState::playing, CueTransitionReason::timerSync);
            return;
        }

        const auto local = getCueState();
        const auto published = realtimeSource.getPublishedCueState();

        if (! shouldApplyPublishedCueState (local, published))
            return;

        if (published == PadCueState::empty)
            setCueState (PadCueState::ready, CueTransitionReason::timerSync);
        else
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
                                               double measured,
                                               bool isLufs) noexcept
    {
        juce::ignoreUnused (isLufs);

        if (safePad == nullptr)
            return;

        safePad->normalizer.markFinished();

        if (fileToAnalyze != safePad->musicFile)
            return;

        safePad->measuredLoudness = measured;

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
        if (! f.existsAsFile())
            return;

        const uint32_t generation = audioLoadGeneration.fetch_add (1, std::memory_order_acq_rel) + 1;

        musicFile = f;
        hasFile = false;
        setCueState (PadCueState::loading, CueTransitionReason::fileLoadStart, true);
        repaint();

        juce::Component::SafePointer<SoundPad> safePad (this);

        showcontrol::background::enqueue ([safePad, f, generation]()
        {
            juce::AudioFormatManager localFormatManager;
            localFormatManager.registerBasicFormats();

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
            payload->displayName = f.getFileNameWithoutExtension();
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
        if (payload == nullptr || payload->reader == nullptr)
        {
            setCueState (PadCueState::empty, CueTransitionReason::fileLoadFail, true);
            repaint();
            return;
        }

        realtimeSource.postStop();
        transportSource.setSource (nullptr);
        readerSource.reset();

        const auto& f = payload->file;
        musicFile = f;
        cachedFileName = payload->displayName;
        const double previousBpm = cachedMeta.bpm;
        cachedMeta = payload->meta;
        if (cachedMeta.bpm <= 0.0 && previousBpm > 0.0)
            cachedMeta.bpm = previousBpm;
        sourceSampleRate = payload->sampleRate;
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (payload->reader.release(), true);

        if (! isCueListPlayback && isLoopingState)
            isLoopingState = false;

        readerSource->setLooping (isLoopingState);
        realtimeSource.setLooping (isLoopingState);

        const int readAhead = sharedTimeSliceThread != nullptr ? kReadAheadBufferSamples : 0;
        transportUsesReadAhead = readAhead > 0;
        transportSource.setSource (readerSource.get(), readAhead, sharedTimeSliceThread, sourceSampleRate);
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

        if (playbackPreloadRequested.load (std::memory_order_relaxed))
            ensureReadAheadBuffer();

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
        const auto pal = ShowTheme::get (isDarkMode);
        juce::Colour gradientTop    = isMouseHovering ? pal.panelElevated : pal.padGradientTop;
        juce::Colour gradientBottom = isDarkMode ? pal.padGradientBottom : (isMouseHovering ? pal.rowHover : pal.padGradientBottom);
        g.setGradientFill (juce::ColourGradient (gradientTop, 0, 0, gradientBottom, 0, bounds.getHeight(), false));
        g.fillRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius);
        const bool playingNow = isPlaying() || isFading();
        g.setColour (playingNow ? pal.padPlayingBorder : pal.padBorder);
        g.drawRoundedRectangle (bounds, ShowTheme::kPanelCornerRadius, 1.0f);

        if (isArmedState)
        {
            g.setColour (pal.accent);
            g.fillEllipse (bounds.getRight() - 14.0f, bounds.getY() + 6.0f, 8.0f, 8.0f);
        }
    }

    void paintGridWaveform (juce::Graphics& g, const juce::Rectangle<float>& bounds) {
        const double currentPos = getPlaybackPosition();
        double tStart = 0.0, tEnd = 0.0;
        getTrimmedDisplayRange (tStart, tEnd);
        const double effectiveLen = juce::jmax (0.0, tEnd - tStart);
        auto waveformBounds = showcontrol::gfx::sanitise (getLocalBounds().withTrimmedTop (32).withTrimmedBottom (28).reduced (8, 0));
        if (waveformBounds.getWidth() <= 0 || waveformBounds.getHeight() <= 0)
            return;

        // Zoom waveform vào đúng vùng trim (không vẽ full file + mask).
        g.setColour (ShowTheme::get (isDarkMode).waveformFill);
        thumbnail.drawChannel (g, waveformBounds, tStart, tEnd, 0, 1.0f);

        if (effectiveLen > 0.0 && currentPos > tStart)
        {
            const double relativePos = juce::jlimit (tStart, tEnd, currentPos) - tStart;
            const float progress = showcontrol::gfx::isFinite ((float) (relativePos / effectiveLen))
                ? juce::jlimit (0.0f, 1.0f, (float) (relativePos / effectiveLen))
                : 0.0f;
            auto progressBounds = showcontrol::gfx::sanitise (
                waveformBounds.toFloat().withWidth ((float) waveformBounds.getWidth() * progress));

            if (showcontrol::gfx::canClip (progressBounds))
            {
                g.saveState();
                g.reduceClipRegion (progressBounds.toNearestInt());
                g.setColour (ShowTheme::get (isDarkMode).waveformPlayhead);
                thumbnail.drawChannel (g, waveformBounds, tStart, tEnd, 0, 1.0f);
                g.restoreState();
            }
        }

        const auto& palWave = ShowTheme::get (isDarkMode);
        if (isSelectedRowState)
        {
            g.setColour (palWave.accent);
            g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 2.0f);
        }
        else if (isPlaying() || isFading())
        {
            g.setColour (palWave.padPlayingBorder);
            g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.5f);
        }
        else if (isCueListPlayback && isPaused())
        {
            g.setColour (palWave.warning);
            g.drawRoundedRectangle (bounds.reduced (0.5f), ShowTheme::kPanelCornerRadius, 1.5f);
        }
        paintGridMetadata (g, tStart, tEnd, currentPos);
    }

    void paintGridMetadata (juce::Graphics& g, double tStart, double tEnd, double currentPos) {
        const auto pal = ShowTheme::get (isDarkMode);
        constexpr int kTitlePadLeft  = 12;
        constexpr int kTitlePadRight = 10;
        const int titleWidth = getWidth() - kTitlePadLeft - kTitlePadRight;

        g.setColour (pal.textPrimary);
        if (isLoading())
        {
            g.setFont (ShowTheme::fontBold (13.0f));
            g.drawText (juce::String::fromUTF8 (u8"Đang nạp..."), kTitlePadLeft, 10, titleWidth, 18,
                        juce::Justification::topLeft, true);
            return;
        }

        // Tên bài — full width góc phải (không còn BPM badge che chữ)
        g.setFont (ShowTheme::fontBold (13.0f));
        g.drawText (getPadName(), kTitlePadLeft, 8, titleWidth, 17, juce::Justification::topLeft, true);

        // Artist — hiện nếu có, nhỏ hơn và muted
        if (cachedMeta.artist.isNotEmpty())
        {
            g.setColour (pal.textMuted);
            g.setFont (ShowTheme::font (10.5f));
            g.drawText (cachedMeta.artist, kTitlePadLeft, 27, titleWidth, 14, juce::Justification::topLeft, true);
        }

        // Thời gian còn lại (bottom-left)
        double remainingTime = isTransportActive() ? getRemainingSeconds() : 0.0;
        if (isPlaying() && remainingTime <= 5.0) {
            g.setColour ((juce::Time::getMillisecondCounter() % 400) < 200 ? pal.danger : pal.textSecondary);
        } else { g.setColour (pal.textSecondary); }
        g.setFont (ShowTheme::timerFont (11.5f, true));
        g.drawText (formatTimeString (remainingTime), 12, getHeight() - 22, 80, 15, juce::Justification::bottomLeft);

        // Tổng thời lượng (bottom-right)
        g.setColour (pal.textMuted);
        g.setFont (ShowTheme::timerFont (11.0f));
        g.drawText (formatTimeString (getEffectiveLength()), getWidth() - 92, getHeight() - 22, 80, 15, juce::Justification::bottomRight);
    }

    void paintGridBadge (juce::Graphics& g, const juce::Rectangle<float>& bounds) {
        auto badgeWidth = 24.0f, badgeHeight = 15.0f;
        auto badgeRect = juce::Rectangle<float>((getWidth() - badgeWidth) / 2.0f, getHeight() - badgeHeight - 4.0f, badgeWidth, badgeHeight);
        const auto pal = ShowTheme::get (isDarkMode);
        if (isPlaying()) g.setColour (pal.success);
        else g.setColour (hasFile ? pal.shortcutBadgeBg : pal.listRowBg);
        g.fillRoundedRectangle (badgeRect, 3.0f);
        g.setColour (isPlaying() ? juce::Colours::white : (hasFile ? pal.shortcutBadgeText : pal.textMuted));
        g.setFont (ShowTheme::fontBold (10.0f));
        g.drawText (shortcutLabel, badgeRect, juce::Justification::centred);
    }

    void unloadAudioFile()
    {
        cancelPendingAsyncWork();

        releaseThumbnailResources();

        realtimeSource.postStop();
        transportSource.setSource (nullptr);
        readerSource.reset();
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