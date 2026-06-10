#pragma once
#include <juce_core/juce_core.h>

//==============================================================================
// VideoAudioExtractor: Tách audio từ video qua ffmpeg (background thread).
// Không chạy trong audio callback — chỉ I/O subprocess trên worker thread.
//==============================================================================
namespace VideoAudioExtractor
{
inline bool extensionIsVideo (const juce::String& extLower)
{
    static const char* const kExts[] = {
        ".mp4", ".mov", ".mkv", ".avi", ".m4v", ".webm", ".wmv", ".mpg", ".mpeg", ".3gp"
    };
    for (auto* e : kExts)
        if (extLower == e)
            return true;
    return false;
}

inline bool isVideoFile (const juce::File& file)
{
    return file.existsAsFile() && extensionIsVideo (file.getFileExtension().toLowerCase());
}

inline void collectVideoFilesFromDrop (const juce::StringArray& paths, juce::StringArray& outPaths)
{
    outPaths.clear();
    for (const auto& path : paths)
    {
        juce::File item (path);
        if (item.isDirectory())
        {
            juce::Array<juce::File> found;
            item.findChildFiles (found, juce::File::findFiles, true, "*");
            for (const auto& f : found)
                if (isVideoFile (f))
                    outPaths.addIfNotAlreadyThere (f.getFullPathName());
        }
        else if (isVideoFile (item))
        {
            outPaths.addIfNotAlreadyThere (item.getFullPathName());
        }
    }
}

inline void addFfmpegCandidate (juce::Array<juce::File>& out, const juce::File& candidate)
{
    if (candidate.existsAsFile() && ! out.contains (candidate))
        out.add (candidate);
}

/** Đường dẫn ffmpeg đi kèm .app (CMake POST_BUILD copy vào Contents/MacOS). */
inline juce::Array<juce::File> bundledFfmpegCandidates()
{
    juce::Array<juce::File> paths;

    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    addFfmpegCandidate (paths, exeDir.getChildFile ("ffmpeg"));

   #if JUCE_MAC
    const auto appBundle = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    if (appBundle.exists())
    {
        addFfmpegCandidate (paths, appBundle.getChildFile ("Contents/MacOS/ffmpeg"));
        addFfmpegCandidate (paths, appBundle.getChildFile ("Contents/Resources/ffmpeg"));
        addFfmpegCandidate (paths, appBundle.getChildFile ("Contents/Resources/bin/ffmpeg"));
    }
   #elif JUCE_WINDOWS
    addFfmpegCandidate (paths, exeDir.getChildFile ("ffmpeg.exe"));
   #endif

    return paths;
}

inline juce::File findFfmpegExecutable()
{
    for (const auto& bundled : bundledFfmpegCandidates())
        return bundled;

    // Fallback: Homebrew / hệ thống (app GUI thường không có PATH).
    const juce::StringArray candidates {
        "/opt/homebrew/bin/ffmpeg",
        "/opt/homebrew/opt/ffmpeg/bin/ffmpeg",
        "/usr/local/bin/ffmpeg",
        "/usr/local/opt/ffmpeg/bin/ffmpeg",
        "/usr/bin/ffmpeg"
    };

    for (const auto& path : candidates)
    {
        const juce::File exe (path);
        if (exe.existsAsFile())
            return exe;
    }

    // Cellar: ffmpeg/8.x/bin/ffmpeg (khi brew link chưa chạy)
    for (const auto& prefix : { "/opt/homebrew/Cellar/ffmpeg", "/usr/local/Cellar/ffmpeg" })
    {
        juce::Array<juce::File> versions;
        juce::File (prefix).findChildFiles (versions, juce::File::findDirectories, false);
        versions.sort();
        for (int i = versions.size(); --i >= 0;)
        {
            const juce::File exe = versions.getReference (i).getChildFile ("bin/ffmpeg");
            if (exe.existsAsFile())
                return exe;
        }
    }

    juce::ChildProcess whichProc;
    if (whichProc.start (juce::StringArray { "/bin/sh", "-c",
                                              "export PATH=\"/opt/homebrew/bin:/usr/local/bin:$PATH\"; which ffmpeg 2>/dev/null" }))
    {
        const auto whichOut = whichProc.readAllProcessOutput().trim();
        if (whichOut.isNotEmpty())
        {
            const juce::File exe (whichOut);
            if (exe.existsAsFile())
                return exe;
        }
    }

    return {};
}

inline bool isFfmpegAvailable()
{
    return findFfmpegExecutable().existsAsFile();
}

inline bool isUsingBundledFfmpeg()
{
    const auto found = findFfmpegExecutable();
    if (! found.existsAsFile())
        return false;

    for (const auto& bundled : bundledFfmpegCandidates())
        if (found == bundled)
            return true;

    return false;
}

inline juce::File findHomebrewExecutable()
{
    const juce::StringArray candidates {
        "/opt/homebrew/bin/brew",
        "/usr/local/bin/brew"
    };

    for (const auto& path : candidates)
    {
        const juce::File exe (path);
        if (exe.existsAsFile())
            return exe;
    }

    return {};
}

inline bool isHomebrewAvailable()
{
    return findHomebrewExecutable().existsAsFile();
}

inline juce::String brewInstallFfmpegCommand()
{
    return "brew install ffmpeg";
}

/** Tạo file WAV tạm; callback trên message thread. */
inline void extractAudioToWavAsync (const juce::File& videoFile,
                                    std::function<void (bool ok, juce::File wavFile, juce::String error)> onComplete)
{
    if (! videoFile.existsAsFile())
    {
        if (onComplete)
            juce::MessageManager::callAsync ([onComplete]
            {
                onComplete (false, {}, juce::String::fromUTF8 (u8"File video không tồn tại."));
            });
        return;
    }

    const auto ffmpeg = findFfmpegExecutable();
    if (! ffmpeg.existsAsFile())
    {
        if (onComplete)
            juce::MessageManager::callAsync ([onComplete]
            {
                onComplete (false, {}, juce::String::fromUTF8 (u8"MISSING_FFMPEG"));
            });
        return;
    }

    const juce::File outWav = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ShowControl_")
                                .getChildFile (videoFile.getFileNameWithoutExtension() + "_extracted.wav");

    outWav.getParentDirectory().createDirectory();

    juce::Thread::launch ([videoFile, ffmpeg, outWav, cb = std::move (onComplete)]() mutable
    {
        juce::String error;
        bool ok = false;

        juce::StringArray command;
        command.add (ffmpeg.getFullPathName());
        command.add ("-y");
        command.add ("-i");
        command.add (videoFile.getFullPathName());
        command.add ("-vn");
        command.add ("-ar");
        command.add ("44100");
        command.add ("-ac");
        command.add ("2");
        command.add ("-f");
        command.add ("wav");
        command.add (outWav.getFullPathName());

        juce::ChildProcess proc;
        ok = proc.start (command);

        if (ok)
        {
            proc.waitForProcessToFinish (300000);
            ok = proc.getExitCode() == 0 && outWav.existsAsFile() && outWav.getSize() > 44;
            if (! ok)
                error = juce::String::fromUTF8 (u8"ffmpeg không tách được audio.");
        }
        else
        {
            error = juce::String::fromUTF8 (u8"Không khởi chạy được ffmpeg.");
        }

        juce::MessageManager::callAsync ([cb, ok, outWav, error]() mutable
        {
            if (cb)
                cb (ok, ok ? outWav : juce::File(), error);
        });
    });
}

} // namespace VideoAudioExtractor
