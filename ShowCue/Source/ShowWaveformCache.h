#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

//==============================================================================
/** Waveform cache dùng chung — TimeSliceThread nội bộ của AudioThumbnailCache. */
namespace showcontrol::waveform
{
inline juce::int64 hashForAudioFile (const juce::File& file) noexcept
{
    return file.hashCode64();
}

inline juce::File legacyWaveformCacheDirectory()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("ShowControl")
               .getChildFile ("Waveforms");
}

inline juce::File waveformCacheDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("ShowCue")
               .getChildFile ("WaveformCache");
}

inline juce::File waveformCacheFileForHash (juce::int64 hashCode)
{
    return waveformCacheDirectory().getChildFile (juce::String::toHexString (hashCode) + ".wfc");
}

inline void migrateLegacyWaveformCacheIfNeeded()
{
    const auto legacyDir = legacyWaveformCacheDirectory();
    const auto persistentDir = waveformCacheDirectory();

    if (! legacyDir.isDirectory())
        return;

    persistentDir.createDirectory();

    for (const auto& legacyFile : legacyDir.findChildFiles (juce::File::findFiles, false, "*.wfc"))
    {
        const auto dest = persistentDir.getChildFile (legacyFile.getFileName());

        if (! dest.existsAsFile())
            legacyFile.copyFileTo (dest);
    }
}

class DiskBackedThumbnailCache final : public juce::AudioThumbnailCache
{
public:
    static constexpr int kMaxCachedThumbnails = 768;

    DiskBackedThumbnailCache()
        : juce::AudioThumbnailCache (kMaxCachedThumbnails)
    {
        getTimeSliceThread().stopThread (200);
        getTimeSliceThread().startThread (juce::Thread::Priority::high);
    }

    juce::TimeSliceThread& getSharedTimeSliceThread() noexcept
    {
        return getTimeSliceThread();
    }

protected:
    void saveNewlyFinishedThumbnail (const juce::AudioThumbnailBase& thumb, juce::int64 hashCode) override
    {
        const auto dest = waveformCacheFileForHash (hashCode);
        dest.getParentDirectory().createDirectory();

        if (auto out = std::unique_ptr<juce::FileOutputStream> (dest.createOutputStream()))
            thumb.saveTo (*out);
    }

    bool loadNewThumb (juce::AudioThumbnailBase& thumb, juce::int64 hashCode) override
    {
        const auto src = waveformCacheFileForHash (hashCode);

        if (! src.existsAsFile())
            return false;

        if (auto in = std::unique_ptr<juce::FileInputStream> (src.createInputStream()))
            return thumb.loadFrom (*in);

        return false;
    }
};

inline DiskBackedThumbnailCache& sharedCache()
{
    static DiskBackedThumbnailCache cache;
    return cache;
}

/** Gọi sớm lúc startup — tạo thư mục cache bền + nâng priority thread decode waveform. */
inline void prepareSharedCache()
{
    migrateLegacyWaveformCacheIfNeeded();
    waveformCacheDirectory().createDirectory();
    sharedCache().getSharedTimeSliceThread();
}

/** Gọi từ ~MainComponent sau khi mọi AudioThumbnail đã clear — tránh leak ThumbnailCacheEntry. */
inline void shutdownSharedCache() noexcept
{
    sharedCache().clear();
    sharedCache().getSharedTimeSliceThread().stopThread (500);
}

} // namespace showcontrol::waveform
