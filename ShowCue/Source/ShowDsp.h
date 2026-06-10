#pragma once

#include <array>
#include <atomic>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "AudioAnalyzer.h"

//==============================================================================
/** 6-band parametric EQ (JUCE ProcessorChain + IIR stereo) — cấu hình riêng mỗi pad. */
class PadParametricEq6
{
public:
    static constexpr int kNumBands = 6;

    enum class BandIndex : int
    {
        highPass = 0,
        lowShelf = 1,
        lowMidPeak = 2,
        highMidPeak = 3,
        highShelf = 4,
        lowPass = 5
    };

    static constexpr float kBandFreqHz[kNumBands] =
    {
        40.0f,    // HP rumble
        120.0f,   // Low shelf
        500.0f,   // Low-mid peak
        2000.0f,  // High-mid peak
        8000.0f,  // High shelf
        18000.0f  // LP air
    };

    static constexpr float kBandQ[kNumBands] =
    {
        0.707f, 0.707f, 1.2f, 1.0f, 0.707f, 0.707f
    };

    /** IIR L/R riêng — gán coeff trực tiếp (ProcessorDuplicator::state không cập nhật Filter). */
    struct StereoBandFilter
    {
        juce::dsp::IIR::Filter<float> left;
        juce::dsp::IIR::Filter<float> right;

        void prepare (const juce::dsp::ProcessSpec& spec) noexcept
        {
            juce::dsp::ProcessSpec mono = spec;
            mono.numChannels = 1;
            left.prepare (mono);
            right.prepare (mono);
        }

        void reset() noexcept
        {
            left.reset();
            right.reset();
        }

        void setCoefficients (juce::dsp::IIR::Coefficients<float>::Ptr coeffs,
                              bool resetFilterState = false) noexcept
        {
            left.coefficients  = coeffs;
            right.coefficients = coeffs;

            // Chỉ reset khi prepare / Reset mặc định — tránh rẹt khi kéo EQ liên tục.
            if (resetFilterState)
            {
                left.reset();
                right.reset();
            }
        }

        void process (juce::dsp::AudioBlock<float>& block) noexcept
        {
            if (block.getNumSamples() == 0)
                return;

            if (block.getNumChannels() > 0)
            {
                auto ch0 = block.getSingleChannelBlock (0);
                juce::dsp::ProcessContextReplacing<float> ctx0 (ch0);
                left.process (ctx0);
            }

            if (block.getNumChannels() > 1)
            {
                auto ch1 = block.getSingleChannelBlock (1);
                juce::dsp::ProcessContextReplacing<float> ctx1 (ch1);
                right.process (ctx1);
            }
        }
    };

    void prepare (double sampleRate, int maxBlockSize) noexcept
    {
        spec.sampleRate       = sampleRate > 0.0 ? sampleRate : 44100.0;
        spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maxBlockSize);
        spec.numChannels      = 2;

        for (auto& band : bands)
            band.prepare (spec);

        updateAllBandCoefficients (true);
        appliedRevision.store (gainRevision.load (std::memory_order_relaxed), std::memory_order_release);
        prepared.store (true, std::memory_order_release);
    }

    void reset() noexcept
    {
        for (auto& band : bands)
            band.reset();
    }

    void setEnabled (bool on) noexcept
    {
        enabled.store (on, std::memory_order_relaxed);

        if (on)
            gainRevision.fetch_add (1, std::memory_order_release);
    }

    void setEnabledNoCoeffBump (bool on) noexcept
    {
        enabled.store (on, std::memory_order_relaxed);
    }

    void markUnprepared() noexcept
    {
        prepared.store (false, std::memory_order_release);
    }

    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    double getSampleRate() const noexcept { return spec.sampleRate; }

    static juce::String formatBandFrequencyHz (float hz) noexcept
    {
        if (hz >= 10000.0f)
            return juce::String (hz / 1000.0f, 1) + " kHz";

        if (hz >= 1000.0f)
        {
            const float kHz = hz / 1000.0f;
            if (std::abs (kHz - std::round (kHz)) < 0.05f)
                return juce::String ((int) std::round (kHz)) + " kHz";

            return juce::String (kHz, 1) + " kHz";
        }

        return juce::String ((int) std::round (hz)) + " Hz";
    }

    /** Đặt lại 6 band = 0 dB, tắt EQ (mặc định factory). */
    void resetToDefaults() noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
            bandGainDb[(size_t) b].store (0.0f, std::memory_order_relaxed);

        enabled.store (false, std::memory_order_relaxed);
        resetFiltersOnNextSync.store (true, std::memory_order_release);
        gainRevision.fetch_add (1, std::memory_order_release);
    }

    /** Gain dB mỗi band — chỉ ghi atomic; audio thread áp coeff (tránh race/câm). */
    void setBandGainDb (int band, float gainDb) noexcept
    {
        if (band < 0 || band >= kNumBands)
            return;

        bandGainDb[(size_t) band].store (juce::jlimit (-24.0f, 24.0f, gainDb), std::memory_order_relaxed);
        gainRevision.fetch_add (1, std::memory_order_release);
    }

    float getBandGainDb (int band) const noexcept
    {
        if (band < 0 || band >= kNumBands)
            return 0.0f;

        return bandGainDb[(size_t) band].load (std::memory_order_relaxed);
    }

    /** Tương thích project cũ (3-band). */
    void setLegacyGainsDb (float lowDb, float midDb, float highDb) noexcept
    {
        setBandGainDb ((int) BandIndex::lowShelf,   lowDb);
        setBandGainDb ((int) BandIndex::lowMidPeak, midDb);
        setBandGainDb ((int) BandIndex::highShelf, highDb);
    }

    float getLegacyLowDb()  const noexcept { return getBandGainDb ((int) BandIndex::lowShelf); }
    float getLegacyMidDb()  const noexcept { return getBandGainDb ((int) BandIndex::lowMidPeak); }
    float getLegacyHighDb() const noexcept { return getBandGainDb ((int) BandIndex::highShelf); }

    bool isEffectivelyFlat() const noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
            if (std::abs (getBandGainDb (b)) > 0.001f)
                return false;

        return true;
    }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (! enabled.load (std::memory_order_relaxed) || numSamples <= 0)
            return;

        if (! prepared.load (std::memory_order_acquire))
            return;

        if (isEffectivelyFlat())
            return;

        if (buffer.getNumChannels() <= 0)
            return;

        syncCoefficientsOnAudioThread();

        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubBlock ((size_t) startSample, (size_t) numSamples);

        for (int b = 0; b < kNumBands; ++b)
        {
            if (std::abs (getBandGainDb (b)) > 0.001f)
                bands[(size_t) b].process (sub);
        }
    }

    using EqCoeffPtr = juce::dsp::IIR::Coefficients<float>::Ptr;

    /** Hệ số IIR một band — dùng cho vẽ đường cong PEQ (khớp chuỗi xử lý thật). */
    static EqCoeffPtr makeBandCoefficients (int band, float gainDb, double sampleRate) noexcept
    {
        if (band < 0 || band >= kNumBands || sampleRate <= 0.0)
            return {};

        const float freq = kBandFreqHz[band];
        const float q    = kBandQ[band];

        switch (band)
        {
            case (int) BandIndex::highPass:
            case (int) BandIndex::lowPass:
                if (std::abs (gainDb) < 0.001f)
                    return bypassCoefficients (sampleRate, band);
                return juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                    sampleRate, freq, q, juce::Decibels::decibelsToGain (gainDb));

            case (int) BandIndex::lowShelf:
                if (std::abs (gainDb) < 0.001f)
                    return bypassCoefficients (sampleRate, band);
                return juce::dsp::IIR::Coefficients<float>::makeLowShelf (
                    sampleRate, freq, q, juce::Decibels::decibelsToGain (gainDb));

            case (int) BandIndex::lowMidPeak:
            case (int) BandIndex::highMidPeak:
                if (std::abs (gainDb) < 0.001f)
                    return bypassCoefficients (sampleRate, band);
                return juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                    sampleRate, freq, q, juce::Decibels::decibelsToGain (gainDb));

            case (int) BandIndex::highShelf:
                if (std::abs (gainDb) < 0.001f)
                    return bypassCoefficients (sampleRate, band);
                return juce::dsp::IIR::Coefficients<float>::makeHighShelf (
                    sampleRate, freq, q, juce::Decibels::decibelsToGain (gainDb));

            default:
                break;
        }

        return {};
    }

    /** Đáp ứng magnitude chuỗi 6 band (dB) — đường cong VEQ mượt, không cộng “chuông” góc. */
    static float getCombinedMagnitudeDb (float hz,
                                         double sampleRate,
                                         const std::array<float, kNumBands>& gainsDb) noexcept
    {
        if (sampleRate <= 0.0)
            return 0.0f;

        double linear = 1.0;

        for (int b = 0; b < kNumBands; ++b)
        {
            const auto coeffs = makeBandCoefficients (b, gainsDb[(size_t) b], sampleRate);
            if (coeffs == nullptr)
                continue;

            linear *= (double) coeffs->getMagnitudeForFrequency ((double) hz, sampleRate);
        }

        return juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, (float) linear));
    }

private:
    static EqCoeffPtr bypassCoefficients (double sampleRate, int band) noexcept
    {
        return juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, kBandFreqHz[band], kBandQ[band], 1.0f);
    }

    juce::dsp::ProcessSpec spec;
    std::array<StereoBandFilter, kNumBands> bands {};

    std::atomic<bool> enabled { false };
    std::atomic<bool> prepared { false };
    std::atomic<uint32_t> gainRevision { 1 };
    std::atomic<uint32_t> appliedRevision { 0 };
    std::atomic<bool> resetFiltersOnNextSync { false };
    std::array<std::atomic<float>, kNumBands> bandGainDb {};

    void syncCoefficientsOnAudioThread() noexcept
    {
        const uint32_t rev = gainRevision.load (std::memory_order_acquire);
        const bool needsReset = resetFiltersOnNextSync.exchange (false, std::memory_order_acq_rel);

        if (! needsReset && rev == appliedRevision.load (std::memory_order_relaxed))
            return;

        updateAllBandCoefficients (needsReset);
        appliedRevision.store (rev, std::memory_order_release);
    }

    void updateBandCoefficients (int band, bool resetFilterState) noexcept
    {
        if (band < 0 || band >= kNumBands || spec.sampleRate <= 0.0)
            return;

        const float g = bandGainDb[(size_t) band].load (std::memory_order_relaxed);
        auto coeffs = makeBandCoefficients (band, g, spec.sampleRate);
        if (coeffs != nullptr)
            bands[(size_t) band].setCoefficients (coeffs, resetFilterState);
    }

    void updateAllBandCoefficients (bool resetFilterState = false) noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
            updateBandCoefficients (b, resetFilterState);
    }
};

//==============================================================================
/** Gain LUFS = 10^((Target - Current) / 20) — áp dụng trên audio thread (atomic). */
class LoudnessSyncGain
{
public:
    static float gainFromLufsDelta (double targetLufs, double currentLufs) noexcept
    {
        return AudioAnalyzer::getGainMultiplierFromLUFS (currentLufs, targetLufs);
    }

    void setGain (float linearGain) noexcept
    {
        lufsSyncGain.store (juce::jlimit (AudioAnalyzer::MIN_GAIN, AudioAnalyzer::MAX_GAIN, linearGain),
                            std::memory_order_relaxed);
    }

    void setFromMeasuredLufs (double measuredLufs) noexcept
    {
        setGain (gainFromLufsDelta (AudioAnalyzer::TARGET_LUFS, measuredLufs));
    }

    float getGain() const noexcept { return lufsSyncGain.load (std::memory_order_relaxed); }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        const float g = lufsSyncGain.load (std::memory_order_relaxed);
        if (std::abs (g - 1.0f) < 1.0e-6f || numSamples <= 0)
            return;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch, startSample), g, numSamples);
    }

private:
    std::atomic<float> lufsSyncGain { 1.0f };
};

//==============================================================================
/** Master bus (FOH) — juce::dsp::Limiter cuối chuỗi output. */
class MasterOutputDynamics
{
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept
    {
        spec.sampleRate       = sampleRate > 0.0 ? sampleRate : 44100.0;
        spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maxBlockSize);
        spec.numChannels      = 2;

        scratch.setSize (2, maxBlockSize, false, false, true);
        maxPreparedSamples = maxBlockSize;

        limiter.prepare (spec);
        limiter.reset();
        limiter.setThreshold ((float) thresholdDb.load (std::memory_order_relaxed));
        limiter.setRelease   ((float) releaseMs.load (std::memory_order_relaxed));
    }

    void reset() noexcept { limiter.reset(); }

    void setEnabled (bool on) noexcept { enabled.store (on, std::memory_order_relaxed); }
    bool isEnabled() const noexcept    { return enabled.load (std::memory_order_relaxed); }

    void setThresholdDb (float db) noexcept
    {
        thresholdDb.store (juce::jlimit (-24.0f, 0.0f, db), std::memory_order_relaxed);
        limiter.setThreshold (thresholdDb.load (std::memory_order_relaxed));
    }

    void setReleaseMs (float ms) noexcept
    {
        releaseMs.store (juce::jlimit (5.0f, 500.0f, ms), std::memory_order_relaxed);
        limiter.setRelease (releaseMs.load (std::memory_order_relaxed));
    }

    float getThresholdDb() const noexcept { return thresholdDb.load (std::memory_order_relaxed); }
    float getReleaseMs()   const noexcept { return releaseMs.load (std::memory_order_relaxed); }

    void process (float* L, float* R, int numSamples) noexcept
    {
        if (! enabled.load (std::memory_order_relaxed) || numSamples <= 0 || L == nullptr)
            return;

        if (numSamples > maxPreparedSamples)
            return;

        scratch.copyFrom (0, 0, L, numSamples);
        if (R != nullptr)
            scratch.copyFrom (1, 0, R, numSamples);
        else
            scratch.copyFrom (1, 0, L, numSamples);

        juce::dsp::AudioBlock<float> block (scratch);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        limiter.process (ctx);

        juce::FloatVectorOperations::copy (L, scratch.getReadPointer (0), numSamples);
        if (R != nullptr)
            juce::FloatVectorOperations::copy (R, scratch.getReadPointer (1), numSamples);
    }

private:
    juce::dsp::Limiter<float> limiter;
    juce::dsp::ProcessSpec spec;
    juce::AudioBuffer<float> scratch;
    int maxPreparedSamples = 0;

    std::atomic<bool>  enabled     { true };
    std::atomic<float> thresholdDb { -1.0f };
    std::atomic<float> releaseMs   { 80.0f };
};

//==============================================================================
/** Chuỗi pad: EQ 6-band → LUFS sync (pre-scan Integrated Loudness). */
class PadDspChain
{
public:
    void prepare (double sampleRate, int maxBlockSize) noexcept
    {
        eq.prepare (sampleRate, maxBlockSize);
    }

    void reset() noexcept { eq.reset(); }

    PadParametricEq6&          getEq()       noexcept { return eq; }
    const PadParametricEq6&    getEq() const noexcept { return eq; }
    LoudnessSyncGain&          getLoudness() noexcept { return loudness; }
    const LoudnessSyncGain&    getLoudness() const noexcept { return loudness; }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        eq.process (buffer, startSample, numSamples);
        loudness.process (buffer, startSample, numSamples);
    }

private:
    PadParametricEq6   eq;
    LoudnessSyncGain   loudness;
};
