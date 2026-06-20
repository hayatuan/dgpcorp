#pragma once

#include <JuceHeader.h>
#include <limits>
#include <map>
#include <set>
#include "AudioMetadataReader.h"
#include "VideoAudioExtractor.h"

namespace showcontrol::preload
{
#if JUCE_MAC
inline constexpr double kPreloadSeconds = 5.0;
inline constexpr size_t kMaxCachePoolBytes = 512u * 1024u * 1024u;
#elif JUCE_WINDOWS && ! JUCE_64BIT
inline constexpr double kPreloadSeconds = 1.0;
inline constexpr size_t kMaxCachePoolBytes = 128u * 1024u * 1024u;
#else
inline constexpr double kPreloadSeconds = 4.0;
inline constexpr size_t kMaxCachePoolBytes = 512u * 1024u * 1024u;
#endif

inline constexpr int kStreamingChunkBytes = 8192;

#if JUCE_64BIT || JUCE_MAC
inline constexpr size_t kFullRamLoadMaxBytes = 15u * 1024u * 1024u;
inline constexpr bool kAllowFullRamLoad = true;
#else
inline constexpr size_t kFullRamLoadMaxBytes = 0;
inline constexpr bool kAllowFullRamLoad = false;
#endif

inline int readAheadSamplesForRate (double sampleRate) noexcept
{
    const int fromPreload = static_cast<int> (kPreloadSeconds * juce::jmax (1.0, sampleRate));
    return juce::jlimit (8192, 524288, fromPreload);
}

inline size_t bufferFootprintBytes (const juce::AudioBuffer<float>& buffer) noexcept
{
    return static_cast<size_t> (buffer.getNumChannels())
         * static_cast<size_t> (buffer.getNumSamples())
         * sizeof (float);
}

struct PreloadedAudioCue
{
    juce::int64 trackId = 0;
    juce::File file;
    juce::AudioBuffer<float> slicePreloadBuffer;
    juce::AudioBuffer<float> fullRamBuffer;
    std::unique_ptr<juce::MemoryMappedFile> mappedFile;
    std::unique_ptr<juce::AudioFormatReader> reader;
    double sampleRate = 44100.0;
    juce::String displayName;
    AudioMetadata meta;
    bool usesFullRam = false;
    size_t memoryFootprintBytes = 0;
    juce::int64 lastAccessMs = 0;
};

inline juce::int64 trackIdForFile (const juce::File& file) noexcept
{
    return file.hashCode64();
}

inline size_t cueFootprintBytes (const PreloadedAudioCue& cue) noexcept
{
    return cue.memoryFootprintBytes;
}

inline std::unique_ptr<PreloadedAudioCue> buildPreloadedCue (const juce::File& file, juce::int64 trackId)
{
    juce::AudioFormatManager localFormatManager;
    localFormatManager.registerBasicFormats();

    auto cue = std::make_unique<PreloadedAudioCue>();
    cue->trackId = trackId;
    cue->file = file;

    const auto fileSize = static_cast<size_t> (file.getSize());

    if constexpr (kAllowFullRamLoad)
    {
        if (fileSize > 0 && fileSize <= kFullRamLoadMaxBytes)
        {
            if (auto reader = std::unique_ptr<juce::AudioFormatReader> (localFormatManager.createReaderFor (file)))
            {
                const int numSamples = static_cast<int> (reader->lengthInSamples);
                const int numChannels = juce::jmax (1, (int) reader->numChannels);

                cue->fullRamBuffer.setSize (numChannels, numSamples, false, true, true);
                cue->fullRamBuffer.clear();

                if (reader->read (&cue->fullRamBuffer, 0, numSamples, 0, true, true))
                {
                    cue->usesFullRam = true;
                    cue->sampleRate = reader->sampleRate;
                    cue->displayName = VideoAudioExtractor::displayNameFromAudioPath (file);
                    cue->meta = AudioMetadataReader::readFromReader (reader.get(), file);
                    cue->memoryFootprintBytes = bufferFootprintBytes (cue->fullRamBuffer);
                    return cue;
                }
            }
        }

        cue->mappedFile = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readOnly);
    }

    cue->reader.reset (localFormatManager.createReaderFor (file));

    if (cue->reader == nullptr)
        return nullptr;

    cue->sampleRate = cue->reader->sampleRate;
    cue->displayName = VideoAudioExtractor::displayNameFromAudioPath (file);
    cue->meta = AudioMetadataReader::readFromReader (cue->reader.get(), file);

    const int sliceSamples = juce::jmin (readAheadSamplesForRate (cue->sampleRate),
                                         static_cast<int> (cue->reader->lengthInSamples));

    if (sliceSamples > 0)
    {
        cue->slicePreloadBuffer.setSize (juce::jmax (1, (int) cue->reader->numChannels),
                                         sliceSamples,
                                         false,
                                         true,
                                         true);
        cue->slicePreloadBuffer.clear();
        cue->reader->read (&cue->slicePreloadBuffer, 0, sliceSamples, 0, true, true);
    }

    cue->memoryFootprintBytes = bufferFootprintBytes (cue->slicePreloadBuffer);
    return cue;
}

class AudioPreloadPool final
{
public:
    void requestPreload (const juce::File& file)
    {
        if (! file.existsAsFile())
            return;

        const auto trackId = trackIdForFile (file);

        {
            const juce::ScopedLock guard (lock);

            if (readyPool.find (trackId) != readyPool.end())
                return;

            if (inFlight.find (trackId) != inFlight.end())
                return;
        }

        struct PreloadJob final : juce::ThreadPoolJob
        {
            PreloadJob (AudioPreloadPool& poolIn, juce::File fileIn, juce::int64 trackIdIn)
                : juce::ThreadPoolJob ("sc-preload"), owner (poolIn), file (std::move (fileIn)), trackId (trackIdIn) {}

            JobStatus runJob() override
            {
                auto cue = buildPreloadedCue (file, trackId);

                juce::MessageManager::callAsync ([ownerPtr = &owner, trackId = trackId, cue = std::move (cue)]() mutable
                {
                    ownerPtr->finishPreloadJob (trackId, std::move (cue));
                });

                return jobHasFinished;
            }

            AudioPreloadPool& owner;
            juce::File file;
            juce::int64 trackId;
        };

        workerPool.addJob (new PreloadJob (*this, file, trackId), true);

        const juce::ScopedLock guard (lock);
        inFlight.insert (trackId);
    }

    std::unique_ptr<PreloadedAudioCue> tryTake (const juce::File& file)
    {
        if (! file.existsAsFile())
            return nullptr;

        const auto trackId = trackIdForFile (file);
        const juce::ScopedLock guard (lock);

        auto it = readyPool.find (trackId);

        if (it == readyPool.end())
            return nullptr;

        auto cue = std::move (it->second);
        usedBytes -= cueFootprintBytes (*cue);
        readyPool.erase (it);
        inFlight.erase (trackId);

        if (cue != nullptr)
            cue->lastAccessMs = juce::Time::getMillisecondCounterHiRes();

        return cue;
    }

    bool hasReadyCue (const juce::File& file) const
    {
        if (! file.existsAsFile())
            return false;

        const juce::ScopedLock guard (lock);
        return readyPool.find (trackIdForFile (file)) != readyPool.end();
    }

    void releaseFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return;

        const auto trackId = trackIdForFile (file);
        const juce::ScopedLock guard (lock);
        inFlight.erase (trackId);

        auto it = readyPool.find (trackId);

        if (it == readyPool.end())
            return;

        usedBytes -= cueFootprintBytes (*it->second);
        readyPool.erase (it);
    }

    void clear() noexcept
    {
        const juce::ScopedLock guard (lock);
        readyPool.clear();
        inFlight.clear();
        usedBytes = 0;
    }

    size_t getUsedBytes() const noexcept
    {
        const juce::ScopedLock guard (lock);
        return usedBytes;
    }

    size_t getReadyCount() const noexcept
    {
        const juce::ScopedLock guard (lock);
        return readyPool.size();
    }

    void shutdown() noexcept
    {
        workerPool.removeAllJobs (true, 5000);
        clear();
    }

private:
    void finishPreloadJob (juce::int64 trackId, std::unique_ptr<PreloadedAudioCue> cue)
    {
        if (cue == nullptr)
        {
            const juce::ScopedLock guard (lock);
            inFlight.erase (trackId);
            return;
        }

        const size_t bytes = cueFootprintBytes (*cue);

        juce::ScopedLock guard (lock);
        inFlight.erase (trackId);
        evictIfNeededLocked (bytes);
        cue->lastAccessMs = juce::Time::getMillisecondCounterHiRes();
        usedBytes += bytes;
        readyPool[trackId] = std::move (cue);
    }

    void evictIfNeededLocked (size_t bytesNeeded)
    {
        while (usedBytes + bytesNeeded > kMaxCachePoolBytes && ! readyPool.empty())
        {
            juce::int64 oldestId = 0;
            juce::int64 oldestAccess = std::numeric_limits<juce::int64>::max();

            for (const auto& entry : readyPool)
            {
                if (entry.second != nullptr && entry.second->lastAccessMs < oldestAccess)
                {
                    oldestAccess = entry.second->lastAccessMs;
                    oldestId = entry.first;
                }
            }

            auto it = readyPool.find (oldestId);

            if (it == readyPool.end())
                break;

            usedBytes -= cueFootprintBytes (*it->second);
            readyPool.erase (it);
        }
    }

    mutable juce::CriticalSection lock;
    juce::ThreadPool workerPool { 3 };
    std::map<juce::int64, std::unique_ptr<PreloadedAudioCue>> readyPool;
    std::set<juce::int64> inFlight;
    size_t usedBytes = 0;
};

inline AudioPreloadPool& sharedPool()
{
    static AudioPreloadPool pool;
    return pool;
}

inline void prepareSharedPool()
{
    // Touch singleton early at startup.
    sharedPool();
}

inline void shutdownSharedPool() noexcept
{
    sharedPool().shutdown();
}

} // namespace showcontrol::preload
