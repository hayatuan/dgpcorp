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

inline juce::File waveformCacheDirectory()
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("ShowControl")
               .getChildFile ("Waveforms");
}

inline juce::File waveformCacheFileForHash (juce::int64 hashCode)
{
    return waveformCacheDirectory().getChildFile (juce::String::toHexString (hashCode) + ".wfc");
}

class DiskBackedThumbnailCache final : public juce::AudioThumbnailCache
{
public:
    static constexpr int kMaxCachedThumbnails = 500;

    DiskBackedThumbnailCache()
        : juce::AudioThumbnailCache (kMaxCachedThumbnails)
    {
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

/** Gọi từ ~MainComponent sau khi mọi AudioThumbnail đã clear — tránh leak ThumbnailCacheEntry. */
inline void shutdownSharedCache() noexcept
{
    sharedCache().clear();
}

} // namespace showcontrol::waveform
