#pragma once

#include "AudioAnalyzer.h"
#include "ShowLocalization.h"
#include <juce_core/juce_core.h>

/** Preset + profile chuẩn hoá âm lượng (studio / live show). */
namespace showcontrol::loudness
{

enum class Preset : int
{
    musicStream = 0,
    liveShow    = 1,
    speech      = 2,
    custom      = 3
};

enum class ContentProfile : int
{
    general = 0,
    ballad  = 1,
    edm     = 2,
    fx      = 3,
    speech  = 4
};

enum class MeasureMode : int
{
    rms  = 0,
    lufs = 1
};

struct TargetSpec
{
    double integratedLufs   = -16.0;
    double truePeakCeilingDb = -1.0;
};

struct LoudnessSettings
{
    bool enabled            = true;
    Preset preset           = Preset::liveShow;
    ContentProfile profile  = ContentProfile::general;
    MeasureMode mode        = MeasureMode::lufs;
    double customTargetLufs = -16.0;
    bool safeMode           = true;
    bool abCompareOriginal  = false;
};

inline TargetSpec targetSpecForPreset (Preset preset) noexcept
{
    switch (preset)
    {
        case Preset::musicStream: return { -14.0, -1.0 };
        case Preset::liveShow:    return { -16.0, -1.0 };
        case Preset::speech:      return { -20.0, -3.0 };
        case Preset::custom:
        default:                  return { -16.0, -1.0 };
    }
}

inline double profileLufsOffset (ContentProfile profile) noexcept
{
    switch (profile)
    {
        case ContentProfile::ballad:  return -0.5;
        case ContentProfile::edm:     return +1.0;
        case ContentProfile::fx:      return -2.0;
        case ContentProfile::speech:  return -1.0;
        case ContentProfile::general:
        default:                      return 0.0;
    }
}

inline double effectiveIntegratedTargetLufs (const LoudnessSettings& settings) noexcept
{
    auto spec = targetSpecForPreset (settings.preset);
    double target = (settings.preset == Preset::custom)
                        ? settings.customTargetLufs
                        : spec.integratedLufs;
    target += profileLufsOffset (settings.profile);
    return juce::jlimit (-24.0, -10.0, target);
}

inline double effectiveTruePeakCeilingDb (const LoudnessSettings& settings) noexcept
{
    return targetSpecForPreset (settings.preset).truePeakCeilingDb;
}

inline float computeSafeGain (const AudioAnalyzer::FileLoudnessAnalysis& analysis,
                              const LoudnessSettings& settings) noexcept
{
    if (! analysis.valid)
        return 1.0f;

    const double targetLufs = effectiveIntegratedTargetLufs (settings);
    const double peakCeiling = effectiveTruePeakCeilingDb (settings);

    float gain = 1.0f;

    if (settings.mode == MeasureMode::lufs)
    {
        if (analysis.integratedLufs < -0.01)
            gain = AudioAnalyzer::getGainMultiplierFromLUFS (analysis.integratedLufs, targetLufs);
    }
    else if (analysis.rms > AudioAnalyzer::MIN_RMS_THRESHOLD)
    {
        gain = AudioAnalyzer::getGainMultiplier (analysis.rms);
    }

    if (settings.safeMode && gain > 1.0f)
    {
        if (analysis.lraDb > 12.0)
            gain = juce::jmin (gain, juce::Decibels::decibelsToGain (3.0f));

        const double predictedPeakDb = analysis.truePeakDbfs
                                       + juce::Decibels::gainToDecibels (gain);
        if (predictedPeakDb > peakCeiling)
        {
            const double trimDb = predictedPeakDb - peakCeiling;
            gain *= (float) std::pow (10.0, -trimDb / 20.0);
        }
    }

    return juce::jlimit (AudioAnalyzer::MIN_GAIN, AudioAnalyzer::MAX_GAIN, gain);
}

inline juce::String presetDisplayName (Preset preset)
{
    switch (preset)
    {
        case Preset::musicStream: return showcontrol::localization::tr (u8"Music / Stream (-14 LUFS)");
        case Preset::liveShow:    return showcontrol::localization::tr (u8"Live Show (-16 LUFS)");
        case Preset::speech:      return showcontrol::localization::tr (u8"Speech / VO (-20 LUFS)");
        case Preset::custom:
        default:                  return showcontrol::localization::tr (u8"Tùy chỉnh");
    }
}

inline juce::String profileDisplayName (ContentProfile profile)
{
    switch (profile)
    {
        case ContentProfile::ballad:  return showcontrol::localization::tr (u8"Ballad / Acoustic");
        case ContentProfile::edm:       return "EDM / Club";
        case ContentProfile::fx:        return showcontrol::localization::tr (u8"SFX / Stinger");
        case ContentProfile::speech:    return showcontrol::localization::tr (u8"Thoại / MC");
        case ContentProfile::general:
        default:                        return showcontrol::localization::tr (u8"Tổng quát");
    }
}

inline LoudnessSettings loudnessSettingsFromLegacy (bool autoNormalize,
                                                    bool useLufs) noexcept
{
    LoudnessSettings s;
    s.enabled = autoNormalize;
    s.mode = useLufs ? MeasureMode::lufs : MeasureMode::rms;
    return s;
}

struct ListPreviewRow
{
    juce::String title;
    juce::String beforeText;
    juce::String afterText;
    juce::String gainText;
    juce::String peakText;
    bool analyzing = false;
};

inline ListPreviewRow buildListPreviewRow (const juce::String& title,
                                           const AudioAnalyzer::FileLoudnessAnalysis& analysis,
                                           bool analyzing,
                                           const LoudnessSettings& settings) noexcept
{
    ListPreviewRow row;
    row.title = title;

    if (analyzing)
    {
        row.beforeText = showcontrol::localization::tr (u8"Đang đo…");
        row.afterText  = juce::String::fromUTF8 (u8"—");
        row.gainText   = juce::String::fromUTF8 (u8"—");
        row.peakText   = juce::String::fromUTF8 (u8"—");
        row.analyzing  = true;
        return row;
    }

    if (! analysis.valid)
    {
        row.beforeText = juce::String::fromUTF8 (u8"—");
        row.afterText  = juce::String::fromUTF8 (u8"—");
        row.gainText   = juce::String::fromUTF8 (u8"—");
        row.peakText   = juce::String::fromUTF8 (u8"—");
        return row;
    }

    if (settings.mode == MeasureMode::lufs)
        row.beforeText = AudioAnalyzer::formatLUFSValue (analysis.integratedLufs);
    else
        row.beforeText = juce::String::fromUTF8 (u8"RMS ") + juce::String (analysis.rms, 4);

    row.afterText = AudioAnalyzer::formatLUFSValue (effectiveIntegratedTargetLufs (settings));
    row.gainText  = juce::String (computeSafeGain (analysis, settings), 2) + "x";
    row.peakText  = juce::String (analysis.truePeakDbfs, 1) + " dBFS";
    return row;
}

} // namespace showcontrol::loudness
