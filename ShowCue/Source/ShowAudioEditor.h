#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "ShowAudioFormats.h"

namespace showcontrol::audioedit
{

inline juce::File editsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("ShowControl")
               .getChildFile ("AudioEdits");
}

inline juce::File uniqueEditDestination (const juce::File& source)
{
    editsDirectory().createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
    return editsDirectory().getChildFile (source.getFileNameWithoutExtension()
                                          + "_cut_" + stamp + ".wav");
}

inline double mapTimeAfterRegionRemoval (double t, double cutStart, double cutEnd) noexcept
{
    if (t < cutStart)
        return t;

    if (t >= cutEnd)
        return t - (cutEnd - cutStart);

    return cutStart;
}

inline void adjustTrimAfterCut (double oldStart, double oldEnd, double oldTotalLen,
                                double cutStart, double cutEnd,
                                double& outStart, double& outEnd)
{
    const double effectiveOldEnd = (oldEnd > 0.0) ? std::min (oldEnd, oldTotalLen) : oldTotalLen;
    outStart = mapTimeAfterRegionRemoval (oldStart, cutStart, cutEnd);
    const double mappedEnd = mapTimeAfterRegionRemoval (effectiveOldEnd, cutStart, cutEnd);
    const double newTotalLen = std::max (0.0, oldTotalLen - (cutEnd - cutStart));
    outEnd = (oldEnd > 0.0) ? juce::jlimit (0.0, newTotalLen, mappedEnd) : 0.0;
}

struct CutResult
{
    bool success = false;
    juce::String error;
    juce::File outputFile;
};

inline CutResult cutRegionToWavFile (const juce::File& source,
                                     const juce::File& destination,
                                     double cutStartSec,
                                     double cutEndSec)
{
    CutResult result;

    if (! source.existsAsFile())
    {
        result.error = "Source file missing";
        return result;
    }

    if (cutEndSec <= cutStartSec + 0.005)
    {
        result.error = "Invalid cut range";
        return result;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (source));

    if (reader == nullptr)
    {
        result.error = "Cannot read audio file";
        return result;
    }

    const double sampleRate = reader->sampleRate;
    const int numChannels = juce::jmax (1, (int) reader->numChannels);
    const juce::int64 totalSamples = reader->lengthInSamples;

    const juce::int64 cutStartSample = (juce::int64) std::llround (cutStartSec * sampleRate);
    const juce::int64 cutEndSample   = (juce::int64) std::llround (cutEndSec * sampleRate);
    const juce::int64 safeCutStart   = juce::jlimit<juce::int64> (0, totalSamples, cutStartSample);
    const juce::int64 safeCutEnd     = juce::jlimit (safeCutStart + 1, totalSamples, cutEndSample);
    const juce::int64 outSamples     = safeCutStart + (totalSamples - safeCutEnd);

    if (outSamples <= 0)
    {
        result.error = "Cut would remove entire file";
        return result;
    }

    destination.getParentDirectory().createDirectory();

    auto* wavFormat = formatManager.findFormatForFileExtension (".wav");

    if (wavFormat == nullptr)
    {
        result.error = "WAV encoder unavailable";
        return result;
    }

    std::unique_ptr<juce::OutputStream> fileStream (destination.createOutputStream());

    if (fileStream == nullptr)
    {
        result.error = "Cannot create output file";
        return result;
    }

    const auto writerOptions = juce::AudioFormatWriterOptions {}
                                   .withSampleRate (sampleRate)
                                   .withNumChannels (numChannels)
                                   .withBitsPerSample (16);

    auto writer = wavFormat->createWriterFor (fileStream, writerOptions);

    if (writer == nullptr)
    {
        result.error = "Cannot create audio writer";
        return result;
    }

    const int blockSize = 8192;
    juce::AudioBuffer<float> buffer (numChannels, blockSize);

    auto writeRange = [&] (juce::int64 startSample, juce::int64 numSamples) -> bool
    {
        juce::int64 pos = startSample;

        while (pos < startSample + numSamples)
        {
            const int toRead = (int) juce::jmin<juce::int64> (blockSize, startSample + numSamples - pos);
            buffer.setSize (numChannels, toRead, false, false, true);

            if (! reader->read (&buffer, 0, toRead, pos, true, true))
                return false;

            if (! writer->writeFromAudioSampleBuffer (buffer, 0, toRead))
                return false;

            pos += toRead;
        }

        return true;
    };

    if (! writeRange (0, safeCutStart)
        || ! writeRange (safeCutEnd, totalSamples - safeCutEnd))
    {
        result.error = "Failed while writing audio";
        writer.reset();
        destination.deleteFile();
        return result;
    }

    writer.reset();
    result.success = true;
    result.outputFile = destination;
    return result;
}

} // namespace showcontrol::audioedit
