#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <cctype>
#include <cstdint>
#include <cstring>

//==============================================================================
// AudioMetadataReader: Đọc metadata ID3/AAC/FLAC theo phong cách foobar2000
// - Đọc Title, Artist, Album, Year, Genre, Track Number
// - Đọc BPM (tempo) nếu có
// - Đọc Comment / Lyrics
// - Đọc artwork (album art) nếu có
// - Tính toán thời lượng chính xác
//==============================================================================

struct AudioMetadata
{
    juce::String title;
    juce::String artist;
    juce::String album;
    juce::String year;
    juce::String genre;
    juce::String trackNumber;
    juce::String comment;
    juce::String composer;
    double       bpm          = 0.0;
    double       durationSecs = 0.0;
    int          sampleRate   = 0;
    int          numChannels  = 0;
    int          bitDepth     = 0;
    int64_t      fileSizeBytes = 0;
    bool         hasArtwork   = false;

    // Trả về chuỗi hiển thị đầy đủ
    juce::String getDisplayTitle() const
    {
        if (title.isNotEmpty()) return title;
        return juce::String::fromUTF8 (u8"(Không có tiêu đề)");
    }

    juce::String getDisplayArtist() const
    {
        if (artist.isNotEmpty()) return artist;
        return juce::String::fromUTF8 (u8"(Không rõ nghệ sĩ)");
    }

    juce::String getFormatInfo() const
    {
        juce::String info;
        if (sampleRate > 0)
            info += juce::String (sampleRate / 1000) + "kHz";
        if (bitDepth > 0)
            info += " / " + juce::String (bitDepth) + "bit";
        if (numChannels == 1)
            info += " / Mono";
        else if (numChannels == 2)
            info += " / Stereo";
        return info;
    }

    juce::String getDurationString() const
    {
        int totalSecs = static_cast<int> (durationSecs);
        int mins = totalSecs / 60;
        int secs = totalSecs % 60;
        return juce::String::formatted ("%d:%02d", mins, secs);
    }
};

class AudioMetadataReader
{
public:
    // Tìm giá trị metadata theo danh sách key ưu tiên
    static juce::String getMetaValue (const juce::StringPairArray& meta,
                                      std::initializer_list<const char*> keys)
    {
        for (auto key : keys)
        {
            juce::String val = meta.getValue (key, "");
            if (val.isNotEmpty()) return val;
        }
        return {};
    }

    // Format file size thân thiện
    static juce::String formatFileSize (int64_t bytes)
    {
        if (bytes < 1024)
            return juce::String (bytes) + " B";
        else if (bytes < 1024 * 1024)
            return juce::String::formatted ("%.1f KB", bytes / 1024.0);
        else
            return juce::String::formatted ("%.1f MB", bytes / (1024.0 * 1024.0));
    }

    // Đọc nhanh chỉ lấy title + duration (dùng cho hiển thị danh sách)
    static juce::String getQuickTitle (const juce::File& file,
                                       juce::AudioFormatManager& formatManager)
    {
        if (auto* reader = formatManager.createReaderFor (file))
        {
            auto& metaData = reader->metadataValues;
            juce::String title = getMetaValue (metaData, {"TITLE", "title", "TIT2"});
            delete reader;
            if (title.isNotEmpty()) return title;
        }
        return file.getFileNameWithoutExtension();
    }

private:
    static int parseSynchsafeSize (const std::uint8_t* bytes) noexcept
    {
        return ((int) (bytes[0] & 0x7f) << 21) | ((int) (bytes[1] & 0x7f) << 14)
             | ((int) (bytes[2] & 0x7f) << 7)  |  (int) (bytes[3] & 0x7f);
    }

    static double parseBpmNumber (const juce::String& text)
    {
        auto t = text.trim();
        if (t.isEmpty())
            return 0.0;

        t = t.retainCharacters ("0123456789.,");
        const double v = t.replaceCharacter (',', '.').getDoubleValue();
        return (v > 20.0 && v < 400.0) ? v : 0.0;
    }

    /** Chỉ lấy byte ASCII in được — tránh juce::String(data,len) / fromUTF8 trên frame ID3 nhị phân. */
    static double parseBpmFromAsciiBytes (const char* bytes, int len) noexcept
    {
        if (bytes == nullptr || len <= 0)
            return 0.0;

        char ascii[64] {};
        int n = 0;

        for (int i = 0; i < len && n < 63; ++i)
        {
            const unsigned char c = (unsigned char) bytes[i];
            if (c >= 32 && c < 127)
                ascii[n++] = (char) c;
        }

        if (n <= 0)
            return 0.0;

        return parseBpmNumber (juce::String (juce::CharPointer_ASCII (ascii)));
    }

    static double parseBpmFromFreeText (const juce::String& text)
    {
        if (text.isEmpty())
            return 0.0;

        const auto lower = text.toLowerCase();
        if (! lower.contains ("bpm") && ! lower.contains ("tempo"))
            return 0.0;

        double v = parseBpmNumber (text);
        if (v > 0.0)
            return v;

        for (auto& token : juce::StringArray::fromTokens (text, " ,;|=\t\r\n", ""))
        {
            v = parseBpmNumber (token);
            if (v > 0.0)
                return v;
        }

        return 0.0;
    }

    static double extractBpmFromMetadataArray (const juce::StringPairArray& md)
    {
        const double direct = parseBpmNumber (getMetaValue (md, {
            "BPM", "bpm", "TBPM", "TEMPO", "Tempo", "tempo",
            "BEATS_PER_MINUTE", "BeatsPerMinute", "beats per minute",
            "acid tempo", "Acid Tempo"
        }));

        if (direct > 0.0)
            return direct;

        for (auto& key : md.getAllKeys())
        {
            if (key.containsIgnoreCase ("bpm") || key.containsIgnoreCase ("tempo"))
            {
                const double bpm = parseBpmNumber (md[key]);
                if (bpm > 0.0)
                    return bpm;
            }
        }

        for (auto& val : md.getAllValues())
        {
            const double bpm = parseBpmFromFreeText (val);
            if (bpm > 0.0)
                return bpm;
        }

        return 0.0;
    }

    /** JUCE MP3 reader bỏ qua ID3 — đọc TBPM trực tiếp từ tag đầu file. */
    static double readBpmFromId3v2Tag (const juce::File& file)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return 0.0;

        std::uint8_t header[10] {};
        if (in.read (header, 10) != 10)
            return 0.0;

        if (std::memcmp (header, "ID3", 3) != 0)
            return 0.0;

        const int versionMajor = (int) header[3];
        const juce::int64 tagEnd = 10 + (juce::int64) parseSynchsafeSize (header + 6);
        juce::int64 pos = 10;

        while (pos + 10 <= tagEnd)
        {
            in.setPosition (pos);

            char frameId[5] {};
            if (in.read (frameId, 4) != 4)
                break;

            if (frameId[0] == 0)
                break;

            std::uint8_t sizeBytes[4] {};
            if (in.read (sizeBytes, 4) != 4)
                break;

            const int frameSize = (versionMajor == 4)
                                    ? parseSynchsafeSize (sizeBytes)
                                    : (int) (((juce::uint32) sizeBytes[0] << 24)
                                           | ((juce::uint32) sizeBytes[1] << 16)
                                           | ((juce::uint32) sizeBytes[2] << 8)
                                           |  (juce::uint32) sizeBytes[3]);

            in.skipNextBytes (2);

            if (frameSize <= 0 || pos + 10 + (juce::int64) frameSize > tagEnd)
                break;

            if (std::memcmp (frameId, "TBPM", 4) == 0)
            {
                juce::MemoryBlock data ((size_t) frameSize, true);
                in.read (data.getData(), frameSize);

                const auto* bytes = static_cast<const char*> (data.getData());
                int start = 0;
                if (frameSize > 1 && (std::uint8_t) bytes[0] < 4)
                    start = 1;

                return parseBpmFromAsciiBytes (bytes + start, frameSize - start);
            }

            pos += 10 + (juce::int64) frameSize;
        }

        return 0.0;
    }

    static int indexOfAsciiPattern (const char* data, int size, const char* pattern, int start) noexcept
    {
        const int patLen = (int) std::strlen (pattern);
        if (patLen <= 0 || start < 0 || start + patLen > size)
            return -1;

        for (int i = start; i <= size - patLen; ++i)
        {
            bool match = true;
            for (int p = 0; p < patLen; ++p)
            {
                const char a = data[i + p];
                const char b = pattern[p];
                if (std::tolower ((unsigned char) a) != std::tolower ((unsigned char) b))
                {
                    match = false;
                    break;
                }
            }

            if (match)
                return i;
        }

        return -1;
    }

    /** FLAC/Ogg Vorbis comment & iXML thường chứa BPM= / tempo= dạng text. */
    static double readBpmFromEmbeddedText (const juce::File& file)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk())
            return 0.0;

        juce::MemoryBlock block;
        in.readIntoMemoryBlock (block, 512 * 1024);
        if (block.getSize() == 0)
            return 0.0;

        const auto* data = static_cast<const char*> (block.getData());
        const int size = (int) block.getSize();

        // Không dùng juce::String(data, size) — file nhị phân gây assertion juce_String.cpp:350.
        for (const char* pattern : { "BPM=", "bpm=", "BPM:", "bpm:", "TEMPO=", "tempo:" })
        {
            const int patLen = (int) std::strlen (pattern);
            int idx = 0;

            while (idx >= 0 && idx + patLen < size)
            {
                idx = indexOfAsciiPattern (data, size, pattern, idx);
                if (idx < 0)
                    break;

                const int start = idx + patLen;
                const int chunkLen = juce::jmin (32, size - start);
                const double bpm = parseBpmFromAsciiBytes (data + start, chunkLen);
                if (bpm > 0.0)
                    return bpm;

                idx += patLen;
            }
        }

        return 0.0;
    }

    static void fillBpmIfMissing (AudioMetadata& meta,
                                  const juce::StringPairArray& md,
                                  const juce::File& file)
    {
        if (meta.bpm > 0.0)
            return;

        meta.bpm = extractBpmFromMetadataArray (md);

        if (meta.bpm <= 0.0 && file.hasFileExtension ("mp3"))
            meta.bpm = readBpmFromId3v2Tag (file);

        if (meta.bpm <= 0.0)
            meta.bpm = readBpmFromEmbeddedText (file);
    }

public:
    static AudioMetadata readFromReader (juce::AudioFormatReader* reader, const juce::File& file)
    {
        AudioMetadata meta;
        if (reader == nullptr)
            return meta;

        meta.fileSizeBytes = file.getSize();
        meta.sampleRate   = static_cast<int> (reader->sampleRate);
        meta.numChannels  = static_cast<int> (reader->numChannels);
        meta.bitDepth     = reader->bitsPerSample;
        if (reader->sampleRate > 0)
            meta.durationSecs = (double) reader->lengthInSamples / reader->sampleRate;

        const auto& md = reader->metadataValues;
        meta.title       = getMetaValue (md, {"TITLE",       "title",       "TIT2"});
        meta.artist      = getMetaValue (md, {"ARTIST",      "artist",      "TPE1"});
        meta.album       = getMetaValue (md, {"ALBUM",       "album",       "TALB"});
        meta.year        = getMetaValue (md, {"DATE",        "year",        "TDRC", "TYER"});
        meta.genre       = getMetaValue (md, {"GENRE",       "genre",       "TCON"});
        meta.trackNumber = getMetaValue (md, {"TRACKNUMBER", "track",       "TRCK"});
        meta.comment     = getMetaValue (md, {"COMMENT",     "comment",     "COMM"});
        meta.composer    = getMetaValue (md, {"COMPOSER",    "composer",    "TCOM"});

        const juce::String bpmStr = getMetaValue (md, {"BPM", "bpm", "TBPM"});
        if (bpmStr.isNotEmpty())
            meta.bpm = parseBpmNumber (bpmStr);

        fillBpmIfMissing (meta, md, file);
        return meta;
    }

    static AudioMetadata readFromFile (const juce::File& file,
                                       juce::AudioFormatManager& formatManager)
    {
        AudioMetadata meta;
        if (! file.existsAsFile())
            return meta;

        if (auto* reader = formatManager.createReaderFor (file))
        {
            meta = readFromReader (reader, file);
            delete reader;
        }

        return meta;
    }

    /** Đọc lại BPM từ đĩa (ID3 / vorbis / acid) khi tag không có trong JUCE reader. */
    static void supplementBpm (AudioMetadata& meta, const juce::File& file)
    {
        fillBpmIfMissing (meta, {}, file);
    }
};
