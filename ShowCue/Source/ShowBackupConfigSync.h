#pragma once

#include <JuceHeader.h>

/** Chunked project/config transfer Primary → Backup over OSC/UDP. */
namespace showcontrol::backup::configsync
{
inline constexpr const char* configBegin = "/showcue/sync/configBegin";
inline constexpr const char* configChunk = "/showcue/sync/configChunk";
inline constexpr const char* configEnd   = "/showcue/sync/configEnd";

inline constexpr int kChunkPayloadBytes = 480;
inline constexpr int kInterChunkDelayMs = 8;

inline int chunkCountForPayload (int payloadBytes) noexcept
{
    if (payloadBytes <= 0)
        return 0;

    return (payloadBytes + kChunkPayloadBytes - 1) / kChunkPayloadBytes;
}

inline juce::StringArray splitPayload (const juce::String& payload)
{
    juce::StringArray chunks;
    const auto utf8 = payload.toUTF8();
    const int total = payload.getNumBytesAsUTF8();

    for (int offset = 0; offset < total; offset += kChunkPayloadBytes)
    {
        const int len = juce::jmin (kChunkPayloadBytes, total - offset);
        chunks.add (juce::String::fromUTF8 (utf8.getAddress() + offset, len));
    }

    return chunks;
}

inline juce::String joinChunks (const juce::StringArray& chunks)
{
    juce::String out;
    out.preallocateBytes (chunks.size() * kChunkPayloadBytes);

    for (const auto& chunk : chunks)
        out += chunk;

    return out;
}

class Receiver final
{
public:
    void reset() noexcept
    {
        transferId  = -1;
        totalBytes  = 0;
        chunkCount  = 0;
        received    = 0;
        chunks.clear();
    }

    bool begin (int id, int total, int numChunks)
    {
        if (id < 0 || total <= 0 || numChunks <= 0)
            return false;

        transferId = id;
        totalBytes = total;
        chunkCount = numChunks;
        received   = 0;
        chunks.clearQuick();

        for (int i = 0; i < chunkCount; ++i)
            chunks.add ({});
        return true;
    }

    bool addChunk (int id, int index, const juce::String& data)
    {
        if (id != transferId || ! juce::isPositiveAndBelow (index, chunkCount))
            return false;

        if (chunks[index].isEmpty())
            ++received;

        chunks.set (index, data);
        return true;
    }

    bool isComplete() const noexcept
    {
        return transferId >= 0 && received >= chunkCount && chunkCount > 0;
    }

    int getTransferId() const noexcept { return transferId; }

    juce::String takePayload()
    {
        if (! isComplete())
            return {};

        const auto payload = joinChunks (chunks);
        reset();
        return payload;
    }

private:
    int transferId  = -1;
    int totalBytes  = 0;
    int chunkCount  = 0;
    int received    = 0;
    juce::StringArray chunks;
};

} // namespace showcontrol::backup::configsync
