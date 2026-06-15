#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "PadCueState.h"
#include "ShowDsp.h"
#include "ShowOutputRouting.h"

//==============================================================================
/** Lệnh POD — chỉ UI/message thread ghi, audio thread đọc. */
enum class PadCommandType : uint8_t
{
    play,
    stop,
    pause,
    resume,
    togglePlay,
    seek,
    setGain,
    startFadeIn,
    startFadeOut,
    resetOutputIdle
};

struct PadCommand
{
    PadCommandType type = PadCommandType::stop;
    float param0 = 0.0f;
    float param1 = 0.0f;
};

//==============================================================================
/** FIFO lock-free giữa UI và audio (juce::AbstractFifo). */
class PadCommandQueue
{
public:
    static constexpr int kCapacity = 64;

    bool push (const PadCommand& cmd) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 <= 0)
        {
            droppedCount.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        buffer[(size_t) scope.startIndex1] = cmd;
        return true;
    }

    template <typename Fn>
    void drain (Fn&& handler) noexcept
    {
        const int ready = fifo.getNumReady();
        const auto scope = fifo.read (ready);
        for (int i = 0; i < scope.blockSize1; ++i)
            handler (buffer[(size_t) scope.startIndex1 + (size_t) i]);
    }

    uint32_t consumeDroppedCount() noexcept
    {
        return droppedCount.exchange (0, std::memory_order_relaxed);
    }

private:
    juce::AbstractFifo fifo { kCapacity };
    std::array<PadCommand, kCapacity> buffer {};
    std::atomic<uint32_t> droppedCount { 0 };
};

//==============================================================================
/** Bọc AudioTransportSource: xử lý lệnh, fade và trim/EOS trên luồng audio. */
class PadRealtimeSource final : public juce::AudioSource
{
public:
    explicit PadRealtimeSource (juce::AudioTransportSource& transportToWrap) noexcept
        : transport (transportToWrap)
    {
    }

    void setTrimRange (double startSec, double endSec) noexcept
    {
        trimStartSec.store ((float) std::max (0.0, startSec), std::memory_order_relaxed);
        trimEndSec.store ((float) std::max (0.0, endSec), std::memory_order_relaxed);
    }

    void setLooping (bool shouldLoop) noexcept
    {
        looping.store (shouldLoop, std::memory_order_relaxed);
    }

    bool postCommand (const PadCommand& cmd) noexcept
    {
        return commandQueue.push (cmd);
    }

    uint32_t consumeDroppedCommandCount() noexcept
    {
        return commandQueue.consumeDroppedCount();
    }

    /** Message thread: gỡ arm cũ sau fade-out xong — không chạm audio thread. */
    void clearStaleFadeOutArmOnMessageThread() noexcept
    {
        if (! fadeActive.load (std::memory_order_acquire)
            && ! outputGainProcessor.isSmoothing())
            releaseFadeOutArm();
    }

    bool postPlay() noexcept
    {
        if (fadeActive.load (std::memory_order_acquire))
            return false;

        releaseFadeOutArm();
        return postCommand ({ PadCommandType::play, 0.0f, 0.0f });
    }

    bool postStop() noexcept
    {
        return postCommand ({ PadCommandType::stop, 0.0f, 0.0f });
    }

    bool postPause() noexcept
    {
        return postCommand ({ PadCommandType::pause, 0.0f, 0.0f });
    }

    bool postResume() noexcept
    {
        if (fadeActive.load (std::memory_order_acquire))
            return false;

        releaseFadeOutArm();
        return postCommand ({ PadCommandType::resume, 0.0f, 0.0f });
    }

    bool postTogglePlay() noexcept
    {
        if (fadeActive.load (std::memory_order_acquire))
            return false;

        releaseFadeOutArm();
        return postCommand ({ PadCommandType::togglePlay, 0.0f, 0.0f });
    }
    bool postSeek (double positionSec) noexcept { return postCommand ({ PadCommandType::seek, (float) positionSec, 0.0f }); }
    bool postSetGain (float gain) noexcept { return postCommand ({ PadCommandType::setGain, gain, 0.0f }); }
    bool postFadeIn (float durationMs, float targetGain) noexcept
    {
        if (fadeActive.load (std::memory_order_acquire))
            return false;

        releaseFadeOutArm();
        return postCommand ({ PadCommandType::startFadeIn, durationMs, targetGain });
    }
    bool postFadeOut (float durationMs) noexcept
    {
        bool expected = false;
        if (! fadeOutArm.compare_exchange_strong (expected, true, std::memory_order_acq_rel))
            return false;

        return postCommand ({ PadCommandType::startFadeOut, durationMs, 0.0f });
    }

    bool postResetOutputIdle() noexcept
    {
        return postCommand ({ PadCommandType::resetOutputIdle, 0.0f, 0.0f });
    }

    // Output bus routing — ghi từ UI thread (message thread), đọc từ audio thread.
    // RT-safe: atomic relaxed load trong audio callback, không cần fence.
    void setOutputBus (int bus) noexcept
    {
        outputBusIndex.store (juce::jlimit (0, showcontrol::routing::kInspectorBusCount - 1, bus),
                              std::memory_order_relaxed);
    }

    int getOutputBus() const noexcept
    {
        return outputBusIndex.load (std::memory_order_relaxed);
    }

    bool isPlayingPublished() const noexcept
    {
        return playingState.load (std::memory_order_relaxed);
    }

    bool isPausedPublished() const noexcept
    {
        return pausedState.load (std::memory_order_relaxed);
    }

    PadCueState getPublishedCueState() const noexcept
    {
        return static_cast<PadCueState> (publishedCueState.load (std::memory_order_relaxed));
    }

    double getPublishedPosition() const noexcept
    {
        return publishedPosition.load (std::memory_order_relaxed);
    }

    double getPublishedLength() const noexcept
    {
        return publishedLength.load (std::memory_order_relaxed);
    }

    float getPublishedGain() const noexcept
    {
        return publishedGain.load (std::memory_order_relaxed);
    }

    /** Chỉ fade do user (GO / hotkey / fade-out), không tính ramp volume lúc prepare. */
    bool isFadeActive() const noexcept
    {
        return fadeActive.load (std::memory_order_relaxed);
    }

    bool isFadeOutArmed() const noexcept
    {
        return fadeOutArm.load (std::memory_order_relaxed);
    }

    bool isOutputGainSmoothing() const noexcept
    {
        return outputGainProcessor.isSmoothing();
    }

    PadDspChain&       getDsp()       noexcept { return dspChain; }
    const PadDspChain& getDsp() const noexcept { return dspChain; }

    double getDeviceSampleRate() const noexcept { return sampleRateHz; }

    /** Tăng mỗi lần bài kết thúc (không loop); UI so sánh generation. */
    uint32_t getTrackFinishedGeneration() const noexcept
    {
        return trackFinishedGeneration.load (std::memory_order_relaxed);
    }

    /** Audio thread chỉ publish cờ; message thread poll và gọi transport.stop(). */
    uint32_t getDeferredStopGeneration() const noexcept
    {
        return deferredStopGeneration.load (std::memory_order_relaxed);
    }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        blockSize = samplesPerBlockExpected;
        sampleRateHz = sampleRate;
        transport.prepareToPlay (samplesPerBlockExpected, sampleRate);
        transport.setGain (1.0f);
        dspChain.prepare (sampleRate, samplesPerBlockExpected);

        juce::dsp::ProcessSpec gainSpec;
        gainSpec.sampleRate       = sampleRate;
        gainSpec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlockExpected);
        gainSpec.numChannels      = 2;
        outputGainProcessor.prepare (gainSpec);
        outputGainProcessor.setRampDurationSeconds (0.0);
        outputGainProcessor.setGainLinear (baseGain);

        publishedLength.store (transport.getLengthInSeconds(), std::memory_order_relaxed);
    }

    void releaseResources() override
    {
        transport.releaseResources();
        dspChain.getEq().markUnprepared();
    }

    /** Message thread: đợi audio callback hoàn tất block hiện tại trước khi hủy pad. */
    void waitUntilAudioIdle() const noexcept
    {
        for (int i = 0; i < 500; ++i)
        {
            if (audioProcessDepth.load (std::memory_order_acquire) <= 0)
                return;

            juce::Thread::sleep (1);
        }
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        const struct ProcessDepthGuard
        {
            std::atomic<int>& depth;
            explicit ProcessDepthGuard (std::atomic<int>& d) noexcept
                : depth (d) { depth.fetch_add (1, std::memory_order_acq_rel); }
            ~ProcessDepthGuard() noexcept { depth.fetch_sub (1, std::memory_order_release); }
        } guard (audioProcessDepth);

        juce::ScopedNoDenormals noDenormals;
        processCommandQueue();

        if (pausedState.load (std::memory_order_relaxed))
        {
            info.clearActiveBufferRegion();
            publishPlaybackState();
            return;
        }

        // Chỉ đọc transport khi user-state còn "đang phát" hoặc đang fade — tránh replay sau fade-out
        // (transport có thể vẫn isPlaying() cho đến khi message thread gọi stop()).
        const bool wantsOutput = playingState.load (std::memory_order_relaxed)
                              || fadeActive.load (std::memory_order_relaxed)
                              || outputGainProcessor.isSmoothing();

        if (! wantsOutput)
        {
            info.clearActiveBufferRegion();
            publishPlaybackState();
            return;
        }

        const bool transportRunning = transport.isPlaying();
        const bool fadeOutTail      = fadeActive.load (std::memory_order_relaxed)
                                   && ! fadeIsIn
                                   && outputGainProcessor.isSmoothing();

        if (transportRunning)
        {
            transport.getNextAudioBlock (info);
            applyOutputGain (info);
        }
        else if (fadeOutTail)
        {
            // Transport đã dừng nhưng ramp gain chưa xong — làm mượt phần đuôi (tránh click/block zip).
            info.clearActiveBufferRegion();
            applyOutputGain (info);
        }
        else
        {
            info.clearActiveBufferRegion();
            publishPlaybackState();
            return;
        }

        processFadeCompletionAndTrimEnd (info);

        if (info.buffer != nullptr && info.numSamples > 0)
            dspChain.process (*info.buffer, info.startSample, info.numSamples);

        publishPlaybackState();
    }

private:
    juce::AudioTransportSource& transport;
    PadDspChain dspChain;
    PadCommandQueue commandQueue;

    int blockSize = 512;
    double sampleRateHz = 44100.0;

    std::atomic<float> trimStartSec { 0.0f };
    std::atomic<float> trimEndSec { 0.0f };
    std::atomic<bool> looping { false };

    std::atomic<bool> playingState { false };
    std::atomic<bool> pausedState { false };
    std::atomic<uint8_t> publishedCueState { static_cast<uint8_t> (PadCueState::empty) };
    std::atomic<double> publishedPosition { 0.0 };
    std::atomic<double> publishedLength { 0.0 };
    std::atomic<float> publishedGain { 1.0f };
    std::atomic<bool> fadeActive { false };
    /** Message thread: latch ngay khi postFadeOut — audio thread bỏ qua lệnh play/stop trùng. */
    std::atomic<bool> fadeOutArm { false };
    std::atomic<int>  outputBusIndex { 0 };       // RT-safe bus routing index
    std::atomic<uint32_t> trackFinishedGeneration { 0 };
    std::atomic<uint32_t> deferredStopGeneration { 0 };
    std::atomic<int> audioProcessDepth { 0 };

    static constexpr float kGainSilenceThreshold = 1.0e-5f;

    float baseGain = 1.0f;
    float fadeTargetGain = 1.0f;
    bool fadeIsIn = true;

    /** Ramp gain theo sample (juce::dsp::Gain) — UI chỉ setTarget, không đổi gain theo block. */
    juce::dsp::Gain<float> outputGainProcessor;

    void requestDeferredTransportStop() noexcept
    {
        deferredStopGeneration.fetch_add (1, std::memory_order_relaxed);
    }

    /** RT-safe: ngắt transport ngay trên audio thread + khôi phục gain gốc cho lần play sau. */
    void restoreTransportUserGain() noexcept
    {
        transport.setGain (1.0f);
        outputGainProcessor.setRampDurationSeconds (0.0);
        outputGainProcessor.setGainLinear (baseGain);
        publishedGain.store (baseGain, std::memory_order_relaxed);
    }

    void applyOutputGain (const juce::AudioSourceChannelInfo& info) noexcept
    {
        if (info.buffer == nullptr || info.numSamples <= 0)
            return;

        juce::dsp::AudioBlock<float> block (*info.buffer);
        auto sub = block.getSubBlock ((size_t) info.startSample, (size_t) info.numSamples);

        if (sub.getNumChannels() < 1)
            return;

        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        outputGainProcessor.process (ctx);
    }

    void scheduleOutputGainRamp (float targetGain, double rampSec) noexcept
    {
        outputGainProcessor.setRampDurationSeconds (juce::jmax (0.001, rampSec));
        outputGainProcessor.setGainLinear (targetGain);
    }

    /**
     * RT-safe: không gọi transport.stop() trên audio thread.
     * Message thread (SoundPad timer) sẽ dừng transport sau khi gain đã về 0.
     */
    void deferTransportStopFromAudioThread() noexcept
    {
        requestDeferredTransportStop();
    }

    void releaseFadeOutArm() noexcept
    {
        fadeOutArm.store (false, std::memory_order_release);
    }

    void completeFadeOutHardStop() noexcept
    {
        fadeActive.store (false, std::memory_order_relaxed);
        fadeOutArm.store (false, std::memory_order_release);
        pausedState.store (false, std::memory_order_relaxed);
        playingState.store (false, std::memory_order_relaxed);
        publishedCueState.store (static_cast<uint8_t> (PadCueState::stopped), std::memory_order_relaxed);
        outputGainProcessor.setRampDurationSeconds (0.0);
        outputGainProcessor.setGainLinear (0.0f);
        const double trimStart = (double) trimStartSec.load (std::memory_order_relaxed);
        transport.setPosition (trimStart);
        publishedPosition.store (trimStart, std::memory_order_relaxed);
        deferTransportStopFromAudioThread();
    }

    /** Startup / load project: dừng hẳn, không fade, snap gain (RT-safe trên audio thread). */
    void resetOutputIdleState() noexcept
    {
        fadeActive.store (false, std::memory_order_relaxed);
        releaseFadeOutArm();
        fadeIsIn = true;
        pausedState.store (false, std::memory_order_relaxed);
        playingState.store (false, std::memory_order_relaxed);
        publishedCueState.store (static_cast<uint8_t> (PadCueState::ready), std::memory_order_relaxed);
        transport.setGain (1.0f);
        outputGainProcessor.setRampDurationSeconds (0.0);
        outputGainProcessor.setGainLinear (baseGain);
        publishedGain.store (baseGain, std::memory_order_relaxed);
        transport.stop();
        transport.setPosition ((double) trimStartSec.load (std::memory_order_relaxed));
        requestDeferredTransportStop();
    }

    double getEffectiveEndSeconds() const noexcept
    {
        const double fileLen = transport.getLengthInSeconds();
        const double trimEnd = (double) trimEndSec.load (std::memory_order_relaxed);
        return (trimEnd > 0.0) ? std::min (trimEnd, fileLen) : fileLen;
    }

    void processCommandQueue() noexcept
    {
        commandQueue.drain ([this] (const PadCommand& cmd)
        {
            switch (cmd.type)
            {
                case PadCommandType::pause:
                    pausePlayback();
                    break;

                case PadCommandType::resume:
                    resumePlayback();
                    break;

                case PadCommandType::seek:
                {
                    const double fileLen = transport.getLengthInSeconds();
                    const double seekSec = juce::jlimit (0.0, juce::jmax (0.0, fileLen), (double) cmd.param0);
                    transport.setPosition (seekSec);
                    break;
                }

                case PadCommandType::setGain:
                    baseGain = cmd.param0;
                    if (! fadeActive.load (std::memory_order_relaxed))
                        scheduleOutputGainRamp (baseGain, 0.02);
                    break;

                case PadCommandType::startFadeIn:
                    beginFade (true, cmd.param0, cmd.param1);
                    break;

                case PadCommandType::startFadeOut:
                    if (fadeActive.load (std::memory_order_relaxed) && ! fadeIsIn)
                        break;
                    beginFade (false, cmd.param0, 0.0f);
                    break;

                case PadCommandType::play:
                case PadCommandType::togglePlay:
                    if (fadeActive.load (std::memory_order_relaxed) && ! fadeIsIn)
                        break;

                    releaseFadeOutArm();

                    if (cmd.type == PadCommandType::play)
                        startPlaybackFromTrim();
                    else if (pausedState.load (std::memory_order_relaxed))
                        resumePlayback();
                    else if (transport.isPlaying())
                        pausePlayback();
                    else
                        startPlaybackFromTrim();
                    break;

                case PadCommandType::stop:
                    if (fadeActive.load (std::memory_order_relaxed) && ! fadeIsIn)
                        break;

                    stopPlayback();
                    break;

                case PadCommandType::resetOutputIdle:
                    resetOutputIdleState();
                    break;

                default:
                    break;
            }
        });
    }

    void startPlaybackFromTrim() noexcept
    {
        const double trimStart = (double) trimStartSec.load (std::memory_order_relaxed);
        const double effectiveEnd = getEffectiveEndSeconds();
        double pos = transport.getCurrentPosition();

        if (pos >= effectiveEnd - 0.05 || pos < trimStart)
            transport.setPosition (trimStart);

        pausedState.store (false, std::memory_order_relaxed);
        transport.setGain (1.0f);
        outputGainProcessor.setRampDurationSeconds (0.0);
        outputGainProcessor.setGainLinear (baseGain);
        transport.start();
        playingState.store (true, std::memory_order_relaxed);
        publishedCueState.store (static_cast<uint8_t> (PadCueState::playing), std::memory_order_relaxed);
    }

    void pausePlayback() noexcept
    {
        pausedState.store (true, std::memory_order_relaxed);
        playingState.store (false, std::memory_order_relaxed);
        fadeActive.store (false, std::memory_order_relaxed);
        fadeOutArm.store (false, std::memory_order_release);
        publishedCueState.store (static_cast<uint8_t> (PadCueState::paused), std::memory_order_relaxed);
        requestDeferredTransportStop();
    }

    void resumePlayback() noexcept
    {
        const double trimStart = (double) trimStartSec.load (std::memory_order_relaxed);
        const double effectiveEnd = getEffectiveEndSeconds();
        double pos = transport.getCurrentPosition();

        if (pos >= effectiveEnd - 0.05 || pos < trimStart)
            transport.setPosition (trimStart);

        pausedState.store (false, std::memory_order_relaxed);
        transport.setGain (1.0f);
        outputGainProcessor.setRampDurationSeconds (0.0);
        outputGainProcessor.setGainLinear (baseGain);
        transport.start();
        playingState.store (true, std::memory_order_relaxed);
        publishedCueState.store (static_cast<uint8_t> (PadCueState::playing), std::memory_order_relaxed);
    }

    void stopPlayback() noexcept
    {
        if (fadeActive.load (std::memory_order_relaxed) && ! fadeIsIn)
        {
            completeFadeOutHardStop();
            return;
        }

        fadeActive.store (false, std::memory_order_relaxed);
        fadeOutArm.store (false, std::memory_order_release);
        pausedState.store (false, std::memory_order_relaxed);
        playingState.store (false, std::memory_order_relaxed);
        publishedCueState.store (static_cast<uint8_t> (PadCueState::stopped), std::memory_order_relaxed);
        outputGainProcessor.setRampDurationSeconds (0.0);
        outputGainProcessor.setGainLinear (0.0f);
        const double trimStart = (double) trimStartSec.load (std::memory_order_relaxed);
        transport.setPosition (trimStart);
        publishedPosition.store (trimStart, std::memory_order_relaxed);
        deferTransportStopFromAudioThread();
    }

    void finishTrackNaturally() noexcept
    {
        fadeActive.store (false, std::memory_order_relaxed);
        releaseFadeOutArm();
        pausedState.store (false, std::memory_order_relaxed);
        playingState.store (false, std::memory_order_relaxed);
        publishedCueState.store (static_cast<uint8_t> (PadCueState::ready), std::memory_order_relaxed);
        trackFinishedGeneration.fetch_add (1, std::memory_order_relaxed);
        deferTransportStopFromAudioThread();
    }

    void beginFade (bool fadeIn, float durationMs, float endGain) noexcept
    {
        if (durationMs <= 0.0f)
            durationMs = 1.0f;

        if (! fadeIn && fadeActive.load (std::memory_order_relaxed) && ! fadeIsIn)
            return;

        if (fadeIn)
            releaseFadeOutArm();

        fadeIsIn = fadeIn;
        fadeTargetGain = fadeIn ? endGain : 0.0f;
        const double rampSec = (double) durationMs * 0.001;
        fadeActive.store (true, std::memory_order_relaxed);

        transport.setGain (1.0f);

        if (fadeIn && ! transport.isPlaying())
        {
            transport.setPosition ((double) trimStartSec.load (std::memory_order_relaxed));
            transport.start();
            playingState.store (true, std::memory_order_relaxed);
            outputGainProcessor.setRampDurationSeconds (0.0);
            outputGainProcessor.setGainLinear (0.0f);
            outputGainProcessor.setRampDurationSeconds (rampSec);
            outputGainProcessor.setGainLinear (fadeTargetGain);
        }
        else if (fadeIn)
        {
            scheduleOutputGainRamp (fadeTargetGain, rampSec);
        }
        else
        {
            if (outputGainProcessor.getGainLinear() < baseGain * 0.25f)
            {
                outputGainProcessor.setRampDurationSeconds (0.0);
                outputGainProcessor.setGainLinear (baseGain);
            }

            scheduleOutputGainRamp (0.0f, rampSec);
        }
    }

    void processFadeCompletionAndTrimEnd (const juce::AudioSourceChannelInfo& info) noexcept
    {
        juce::ignoreUnused (info);

        if (fadeActive.load (std::memory_order_relaxed))
        {
            if (! fadeIsIn)
            {
                if (! outputGainProcessor.isSmoothing()
                    && outputGainProcessor.getGainLinear() <= kGainSilenceThreshold)
                {
                    if (info.buffer != nullptr && info.numSamples > 0)
                        info.buffer->clear (info.startSample, info.numSamples);

                    completeFadeOutHardStop();
                    return;
                }
            }
            else if (! outputGainProcessor.isSmoothing())
            {
                fadeActive.store (false, std::memory_order_relaxed);
                baseGain = fadeTargetGain;
                outputGainProcessor.setRampDurationSeconds (0.0);
                outputGainProcessor.setGainLinear (baseGain);
                publishedGain.store (baseGain, std::memory_order_relaxed);
            }
        }

        if (! transport.isPlaying())
            return;

        const double currentPos = transport.getCurrentPosition();
        const double effectiveEnd = getEffectiveEndSeconds();
        const bool atEnd = transport.hasStreamFinished()
                        || currentPos >= effectiveEnd - 0.02;

        if (! atEnd)
            return;

        if (looping.load (std::memory_order_relaxed))
        {
            transport.setPosition ((double) trimStartSec.load (std::memory_order_relaxed));
            return;
        }

        finishTrackNaturally();
    }

    void publishPlaybackState() noexcept
    {
        if (pausedState.load (std::memory_order_relaxed))
        {
            playingState.store (false, std::memory_order_relaxed);
            publishedCueState.store (static_cast<uint8_t> (PadCueState::paused), std::memory_order_relaxed);
            publishedPosition.store (transport.getCurrentPosition(), std::memory_order_relaxed);
            publishedLength.store (transport.getLengthInSeconds(), std::memory_order_relaxed);
            publishedGain.store (outputGainProcessor.getGainLinear(), std::memory_order_relaxed);
            return;
        }

        // Dùng playingState (được set bởi startPlaybackFromTrim/stop/finish/pause)
        // thay vì transport.isPlaying() để tránh overwrite state mà finishTrackNaturally()
        // đã set (ready + trackFinishedGeneration++) trong cùng audio block.
        const bool playing = playingState.load (std::memory_order_relaxed);

        if (playing)
            publishedCueState.store (static_cast<uint8_t> (PadCueState::playing), std::memory_order_relaxed);
        else
        {
            const auto cue = static_cast<PadCueState> (publishedCueState.load (std::memory_order_relaxed));
            // Chỉ hạ playing → ready; không ghi đè stopped/paused (fade-out stop im lặng).
            if (cue == PadCueState::playing)
                publishedCueState.store (static_cast<uint8_t> (PadCueState::ready), std::memory_order_relaxed);
        }

        publishedPosition.store (transport.getCurrentPosition(), std::memory_order_relaxed);
        publishedLength.store (transport.getLengthInSeconds(), std::memory_order_relaxed);

        const auto cue = static_cast<PadCueState> (publishedCueState.load (std::memory_order_relaxed));
        if (! playing && (cue == PadCueState::stopped || cue == PadCueState::ready || cue == PadCueState::paused))
            publishedGain.store (baseGain, std::memory_order_relaxed);
        else
            publishedGain.store (outputGainProcessor.getGainLinear(), std::memory_order_relaxed);
    }
};
