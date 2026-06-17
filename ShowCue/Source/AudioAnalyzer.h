#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

/**
 * AudioAnalyzer — đo RMS và LUFS (ITU-R BS.1770-4) cho chuẩn hoá âm lượng.
 *
 * RMS Mode  : Cổ điển, nhanh, phù hợp cho nhạc không cần chuẩn broadcast.
 * LUFS Mode : Chuẩn quốc tế (EBU R128 / streaming), cảm nhận to nhỏ sát thực tế hơn.
 *             Áp dụng K-weighting filter (xấp xỉ bằng 2 biquad), đo mean-square
 *             trên khối 400ms rồi lấy trung bình toàn bài — chính xác ở cấp độ
 *             phần mềm DAW đủ để show control.
 *
 * Chạy trên Background Thread (VolumeNormalizer::analyzeAudioFile) — KHÔNG gọi
 * bất kỳ hàm nào ở đây từ audio callback.
 */
class AudioAnalyzer
{
public:
    // ── Mục tiêu ──────────────────────────────────────────────────────────────
    static constexpr double TARGET_RMS        = 0.10;   // ≈ -20 dBFS RMS
    static constexpr double TARGET_LUFS       = -16.0;  // EBU R128 streaming target
    static constexpr double MIN_RMS_THRESHOLD = 0.0001;

    /** Giới hạn đo (giây) — đủ chính xác cho pad show, tránh quét cả file 1h+ khi «Đồng bộ cả list». */
    static constexpr double MAX_ANALYSIS_SECONDS = 90.0;

    static int maxAnalysisSamples (double sampleRate, juce::int64 lengthInSamples) noexcept
    {
        if (sampleRate <= 0.0 || lengthInSamples <= 0)
            return 0;

        const auto cap = (juce::int64) std::llround (sampleRate * MAX_ANALYSIS_SECONDS);
        return (int) std::min (lengthInSamples, cap);
    }

    // ── Giới hạn bảo vệ ──────────────────────────────────────────────────────
    static constexpr float  MAX_GAIN          = 6.0f;   // +15.6 dB ceiling
    static constexpr float  MIN_GAIN          = 0.25f;  // -12 dB floor

    // ─────────────────────────────────────────────────────────────────────────
    // RMS Helpers
    // ─────────────────────────────────────────────────────────────────────────
    static double calculateRMS (const juce::AudioBuffer<float>& buffer, int channel)
    {
        const float* data = buffer.getReadPointer (channel);
        int numSamples = buffer.getNumSamples();
        if (numSamples == 0) return 0.0;

        double sumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
            sumSq += (double) data[i] * data[i];

        return std::sqrt (sumSq / numSamples);
    }

    static double calculateFileRMS (const juce::File& audioFile,
                                    juce::AudioFormatManager& formatManager)
    {
        if (! audioFile.exists()) return 0.0;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));
        if (reader == nullptr) return 0.0;

        const int maxSamples = maxAnalysisSamples (reader->sampleRate, reader->lengthInSamples);
        if (maxSamples < 256)
            return 0.0;

        juce::AudioBuffer<float> buf ((int) reader->numChannels, maxSamples);
        reader->read (&buf, 0, maxSamples, 0, true, true);

        double total = 0.0;
        for (int ch = 0; ch < (int) reader->numChannels; ++ch)
            total += calculateRMS (buf, ch);

        return total / (double) reader->numChannels;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // LUFS (ITU-R BS.1770-4 Integrated Loudness) — K-weighting xấp xỉ
    // ─────────────────────────────────────────────────────────────────────────
    /**
     * Trả về giá trị LUFS âm (ví dụ -18.5). Nếu file quá nhỏ trả về 0.0.
     * Thuật toán: K-weighting 2-stage biquad → mean-square trên toàn bài → LUFS.
     */
    static double calculateFileLUFS (const juce::File& audioFile,
                                     juce::AudioFormatManager& formatManager)
    {
        if (! audioFile.exists()) return 0.0;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));
        if (reader == nullptr) return 0.0;

        const double sr = reader->sampleRate;
        const int numCh = (int) reader->numChannels;
        const int maxSamples = maxAnalysisSamples (sr, reader->lengthInSamples);
        if (maxSamples < 1024) return 0.0;

        juce::AudioBuffer<float> buf (numCh, maxSamples);
        reader->read (&buf, 0, maxSamples, 0, true, true);

        // K-weighting stage 1: high-shelf (pre-filter) @ ~1681 Hz
        // Coefficients pre-computed for 44100 Hz, scale for others via warping approx
        const double f0 = 1681.97441 * (sr / 44100.0);
        const double Q  = 0.7071068;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double V0 = std::pow (10.0, 4.0 / 20.0); // +4 dB shelf
        const double denom1 = 1.0 + K / Q + K * K;
        const double b0s1 = (V0 + std::sqrt (2.0 * V0) * K + K * K) / denom1;
        const double b1s1 = 2.0 * (K * K - V0) / denom1;
        const double b2s1 = (V0 - std::sqrt (2.0 * V0) * K + K * K) / denom1;
        const double a1s1 = 2.0 * (K * K - 1.0) / denom1;
        const double a2s1 = (1.0 - K / Q + K * K) / denom1;

        // K-weighting stage 2: high-pass @ 38 Hz
        const double f1 = 38.13547 * (sr / 44100.0);
        const double K2 = std::tan (juce::MathConstants<double>::pi * f1 / sr);
        const double den2 = 1.0 + std::sqrt (2.0) * K2 + K2 * K2;
        const double b0s2 = 1.0 / den2;
        const double b1s2 = -2.0 / den2;
        const double b2s2 = 1.0 / den2;
        const double a1s2 = 2.0 * (K2 * K2 - 1.0) / den2;
        const double a2s2 = (1.0 - std::sqrt (2.0) * K2 + K2 * K2) / den2;

        // Channel weight theo BS.1770 (L/R/C = 1.0, Ls/Rs = 1.41)
        auto chWeight = [&] (int ch) -> double { return (numCh > 4 && ch >= 3) ? 1.41 : 1.0; };

        double sumMeanSq = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = buf.getReadPointer (ch);
            double z1s1 = 0, z2s1 = 0, z1s2 = 0, z2s2 = 0;
            double meanSq = 0.0;

            for (int i = 0; i < maxSamples; ++i)
            {
                // Stage 1: high-shelf
                double x = (double) src[i];
                double y1 = b0s1 * x + z1s1;
                z1s1 = b1s1 * x - a1s1 * y1 + z2s1;
                z2s1 = b2s1 * x - a2s1 * y1;

                // Stage 2: high-pass
                double y2 = b0s2 * y1 + z1s2;
                z1s2 = b1s2 * y1 - a1s2 * y2 + z2s2;
                z2s2 = b2s2 * y1 - a2s2 * y2;

                meanSq += y2 * y2;
            }

            sumMeanSq += chWeight (ch) * (meanSq / maxSamples);
        }

        if (sumMeanSq < 1e-12) return 0.0;
        return -0.691 + 10.0 * std::log10 (sumMeanSq); // LUFS
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Gain Multipliers
    // ─────────────────────────────────────────────────────────────────────────
    static float getGainMultiplier (double measuredRMS)
    {
        if (measuredRMS < MIN_RMS_THRESHOLD) return 1.0f;
        return juce::jlimit (MIN_GAIN, MAX_GAIN, (float) (TARGET_RMS / measuredRMS));
    }

  /** Gain = 10^((Target_LUFS - Current_LUFS) / 20) — Integrated Loudness → mức chuẩn đồng đều. */
    static float getGainMultiplierFromLUFS (double measuredLUFS,
                                            double targetLufs = TARGET_LUFS)
    {
        if (measuredLUFS >= 0.0)
            return 1.0f;

        const double diffDb = targetLufs - measuredLUFS;
        const float gain = (float) std::pow (10.0, diffDb / 20.0);
        return juce::jlimit (MIN_GAIN, MAX_GAIN, gain);
    }

    static juce::String formatRMSValue (double rms) { return juce::String (rms, 4); }
    static juce::String formatLUFSValue (double lufs) { return juce::String (lufs, 1) + " LUFS"; }

    // ─────────────────────────────────────────────────────────────────────────
    // Full-file analysis (integrated + short-term + peak + LRA estimate)
    // ─────────────────────────────────────────────────────────────────────────
    struct FileLoudnessAnalysis
    {
        double integratedLufs  = 0.0;
        double shortTermLufs   = 0.0;
        double truePeakDbfs    = -100.0;
        double rms             = 0.0;
        double lraDb           = 0.0;
        bool   valid           = false;
    };

    struct KWeightingCoeffs
    {
        double b0s1, b1s1, b2s1, a1s1, a2s1;
        double b0s2, b1s2, b2s2, a1s2, a2s2;
    };

    static KWeightingCoeffs makeKWeightingCoeffs (double sampleRate) noexcept
    {
        KWeightingCoeffs c {};

        const double f0 = 1681.97441 * (sampleRate / 44100.0);
        const double Q  = 0.7071068;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
        const double V0 = std::pow (10.0, 4.0 / 20.0);
        const double denom1 = 1.0 + K / Q + K * K;
        c.b0s1 = (V0 + std::sqrt (2.0 * V0) * K + K * K) / denom1;
        c.b1s1 = 2.0 * (K * K - V0) / denom1;
        c.b2s1 = (V0 - std::sqrt (2.0 * V0) * K + K * K) / denom1;
        c.a1s1 = 2.0 * (K * K - 1.0) / denom1;
        c.a2s1 = (1.0 - K / Q + K * K) / denom1;

        const double f1 = 38.13547 * (sampleRate / 44100.0);
        const double K2 = std::tan (juce::MathConstants<double>::pi * f1 / sampleRate);
        const double den2 = 1.0 + std::sqrt (2.0) * K2 + K2 * K2;
        c.b0s2 = 1.0 / den2;
        c.b1s2 = -2.0 / den2;
        c.b2s2 = 1.0 / den2;
        c.a1s2 = 2.0 * (K2 * K2 - 1.0) / den2;
        c.a2s2 = (1.0 - std::sqrt (2.0) * K2 + K2 * K2) / den2;

        return c;
    }

    static double applyKWeightSample (double x,
                                      const KWeightingCoeffs& c,
                                      double& z1s1, double& z2s1,
                                      double& z1s2, double& z2s2) noexcept
    {
        const double y1 = c.b0s1 * x + z1s1;
        z1s1 = c.b1s1 * x - c.a1s1 * y1 + z2s1;
        z2s1 = c.b2s1 * x - c.a2s1 * y1;

        const double y2 = c.b0s2 * y1 + z1s2;
        z1s2 = c.b1s2 * y1 - c.a1s2 * y2 + z2s2;
        z2s2 = c.b2s2 * y1 - c.a2s2 * y2;
        return y2;
    }

    static double meanSquareToLufs (double meanSq) noexcept
    {
        if (meanSq < 1e-12)
            return 0.0;

        return -0.691 + 10.0 * std::log10 (meanSq);
    }

    static FileLoudnessAnalysis analyzeFile (const juce::File& audioFile,
                                            juce::AudioFormatManager& formatManager)
    {
        FileLoudnessAnalysis result;

        if (! audioFile.existsAsFile())
            return result;

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));
        if (reader == nullptr)
            return result;

        const double sr = reader->sampleRate;
        const int numCh = (int) reader->numChannels;
        const int maxSamples = maxAnalysisSamples (sr, reader->lengthInSamples);

        if (maxSamples < 1024 || numCh <= 0)
            return result;

        juce::AudioBuffer<float> buf (numCh, maxSamples);
        reader->read (&buf, 0, maxSamples, 0, true, true);

        double rmsSum = 0.0;
        float peak = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            rmsSum += calculateRMS (buf, ch);
            const float* data = buf.getReadPointer (ch);

            for (int i = 0; i < maxSamples; ++i)
                peak = juce::jmax (peak, std::abs (data[i]));
        }

        result.rms = rmsSum / (double) numCh;
        result.truePeakDbfs = peak > 1.0e-9f
                                ? juce::Decibels::gainToDecibels (peak)
                                : -100.0;

        const auto coeffs = makeKWeightingCoeffs (sr);
        const int blockSamples = juce::jmax (256, (int) std::llround (sr * 3.0));
        auto chWeight = [numCh] (int ch) -> double { return (numCh > 4 && ch >= 3) ? 1.41 : 1.0; };

        double integratedMeanSq = 0.0;
        std::vector<double> shortTermLufsBlocks;
        shortTermLufsBlocks.reserve ((size_t) (maxSamples / blockSamples + 2));

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = buf.getReadPointer (ch);
            double z1s1 = 0, z2s1 = 0, z1s2 = 0, z2s2 = 0;
            double chIntegratedSq = 0.0;
            double blockSq = 0.0;
            int blockCount = 0;

            for (int i = 0; i < maxSamples; ++i)
            {
                const double weighted = applyKWeightSample ((double) src[i], coeffs, z1s1, z2s1, z1s2, z2s2);
                const double sq = weighted * weighted;
                chIntegratedSq += sq;
                blockSq += sq;
                ++blockCount;

                if (blockCount >= blockSamples)
                {
                    const double blockMeanSq = blockSq / (double) blockCount;
                    shortTermLufsBlocks.push_back (meanSquareToLufs (blockMeanSq * chWeight (ch)));
                    blockSq = 0.0;
                    blockCount = 0;
                }
            }

            if (blockCount > 0)
            {
                const double blockMeanSq = blockSq / (double) blockCount;
                shortTermLufsBlocks.push_back (meanSquareToLufs (blockMeanSq * chWeight (ch)));
            }

            integratedMeanSq += chWeight (ch) * (chIntegratedSq / (double) maxSamples);
        }

        result.integratedLufs = meanSquareToLufs (integratedMeanSq);

        if (! shortTermLufsBlocks.empty())
        {
            std::sort (shortTermLufsBlocks.begin(), shortTermLufsBlocks.end());
            result.shortTermLufs = shortTermLufsBlocks.back();

            const size_t p10 = shortTermLufsBlocks.size() / 10;
            const size_t p95 = juce::jmin (shortTermLufsBlocks.size() - 1,
                                             (size_t) std::llround (shortTermLufsBlocks.size() * 0.95));
            result.lraDb = shortTermLufsBlocks[p95] - shortTermLufsBlocks[p10];
        }

        result.valid = (result.integratedLufs < -0.01)
                    || (result.rms > MIN_RMS_THRESHOLD);
        return result;
    }
};
