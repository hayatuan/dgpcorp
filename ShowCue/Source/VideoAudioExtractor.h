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

inline constexpr const char* showControlWavTag() noexcept { return ".showcontrol"; }

/** ~/Library/Application Support/ShowCue/AudioCache/ — WAV trích từ video, ẩn khỏi thư mục người dùng. */
inline juce::File getAudioCacheDirectory()
{
    auto cacheDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile ("ShowCue")
                      .getChildFile ("AudioCache");

    if (! cacheDir.exists())
        cacheDir.createDirectory();

    return cacheDir;
}

inline bool isAudioCacheFile (const juce::File& audioFile)
{
    if (! audioFile.existsAsFile())
        return false;

    return audioFile.getParentDirectory().getFullPathName()
         == getAudioCacheDirectory().getFullPathName();
}

/** WAV đích ffmpeg trong AudioCache — {tênVideo}_{hash đường dẫn}.wav */
inline juce::File makeCachedExtractedWavPath (const juce::File& videoFile)
{
    const auto cacheDir = getAudioCacheDirectory();
    const juce::String safeName = videoFile.getFileNameWithoutExtension()
                                + "_"
                                + juce::String (juce::String (videoFile.getFullPathName()).hashCode64())
                                + ".wav";
    return cacheDir.getChildFile (safeName);
}

/** @deprecated Dùng makeCachedExtractedWavPath — giữ alias tương thích nội bộ. */
inline juce::File makeSafeExtractedWavPath (const juce::File& videoFile)
{
    return makeCachedExtractedWavPath (videoFile);
}

/** Tên hiển thị UI từ file video gốc — không có .mp4 / .showcontrol. */
inline juce::String displayNameFromVideoFile (const juce::File& videoFile)
{
    return videoFile.getFileNameWithoutExtension();
}

/** Tên hiển thị từ đường dẫn audio — gỡ hậu tố cache / sidecar cũ. */
inline juce::String displayNameFromAudioPath (const juce::File& audioFile)
{
    auto base = audioFile.getFileNameWithoutExtension();
    const juce::String tag (showControlWavTag());

    if (base.endsWithIgnoreCase (tag))
        return base.dropLastCharacters (tag.length());

    if (isAudioCacheFile (audioFile))
    {
        const int lastUnderscore = base.lastIndexOfChar ('_');

        if (lastUnderscore > 0)
        {
            const auto suffix = base.substring (lastUnderscore + 1);

            if (suffix.containsOnly ("0123456789-"))
                return base.substring (0, lastUnderscore);
        }
    }

    return base;
}

/** argv tách rời — macOS execvp tự cô lập khoảng trắng / ngoặc trong đường dẫn. */
inline juce::StringArray buildFfmpegExtractArguments (const juce::File& ffmpegExe,
                                                      const juce::File& videoFile,
                                                      const juce::File& outWav)
{
    juce::StringArray command;
    command.add (ffmpegExe.getFullPathName());
    command.add ("-y");
    command.add ("-hide_banner");
    command.add ("-loglevel");
    command.add ("error");
    command.add ("-i");
    command.add (videoFile.getFullPathName());
    command.add ("-vn");
    command.add ("-acodec");
    command.add ("pcm_s16le");
    command.add ("-ar");
    command.add ("44100");
    command.add ("-ac");
    command.add ("2");
    command.add (outWav.getFullPathName());
    return command;
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

    const juce::File outWav = makeCachedExtractedWavPath (videoFile);

    if (outWav.existsAsFile() && outWav.getSize() > 44
        && outWav.getLastModificationTime() >= videoFile.getLastModificationTime())
    {
        if (onComplete)
            juce::MessageManager::callAsync ([onComplete, outWav]
            {
                onComplete (true, outWav, {});
            });
        return;
    }

    outWav.getParentDirectory().createDirectory();

    juce::Thread::launch ([videoFile, ffmpeg, outWav, cb = std::move (onComplete)]() mutable
    {
        juce::String error;
        bool ok = false;

        const auto command = buildFfmpegExtractArguments (ffmpeg, videoFile, outWav);

        juce::ChildProcess proc;
        ok = proc.start (command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);

        if (ok)
        {
            proc.waitForProcessToFinish (300000);
            const auto processLog = proc.readAllProcessOutput().trim();
            ok = proc.getExitCode() == 0 && outWav.existsAsFile() && outWav.getSize() > 44;

            if (! ok)
            {
                error = processLog.isNotEmpty()
                          ? processLog
                          : juce::String::fromUTF8 (u8"ffmpeg không tách được audio.");
            }
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
