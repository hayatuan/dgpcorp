#pragma once
#include <juce_core/juce_core.h>

/** Định dạng âm thanh hỗ trợ qua JUCE AudioFormatManager::registerBasicFormats() (Foobar-style). */
namespace ShowAudioFormats
{
    inline bool extensionIsSupported (const juce::String& extLower)
    {
        static const char* const kExts[] = {
            ".wav", ".wave", ".mp3", ".m4a", ".m4b", ".m4p", ".aac",
            ".flac", ".ogg", ".oga", ".aif", ".aiff", ".wma", ".wv"
        };

        for (auto* e : kExts)
            if (extLower == e)
                return true;

        return false;
    }

    inline bool isSupportedAudioFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;

        return extensionIsSupported (file.getFileExtension().toLowerCase());
    }

    inline juce::String fileChooserWildcard()
    {
        return "*.wav;*.wave;*.mp3;*.m4a;*.m4b;*.aac;*.flac;*.ogg;*.oga;*.aif;*.aiff;*.wma;*.wv;"
               "*.WAV;*.WAVE;*.MP3;*.M4A;*.M4B;*.AAC;*.FLAC;*.OGG;*.OGA;*.AIF;*.AIFF;*.WMA;*.WV";
    }

    /** Audio + video — hộp thoại «Thêm file» / kéo-thả. */
    inline juce::String mediaFileChooserWildcard()
    {
        return fileChooserWildcard()
             + ";*.mp4;*.mov;*.mkv;*.avi;*.m4v;*.webm;*.wmv;*.mpg;*.mpeg;*.3gp"
               ";*.MP4;*.MOV;*.MKV;*.AVI;*.M4V;*.WEBM;*.WMV;*.MPG;*.MPEG;*.3GP";
    }

    /** Cờ chuẩn mở nhiều file cùng lúc (JUCE: canSelectMultipleItems). */
    inline int fileChooserOpenMultipleFlags() noexcept
    {
        return juce::FileBrowserComponent::openMode
             | juce::FileBrowserComponent::canSelectFiles
             | juce::FileBrowserComponent::canSelectMultipleItems;
    }

    /** Giới hạn quét nông — tránh treo UI khi thả nhầm thư mục quá lớn. */
    inline constexpr int kMaxShallowFolderAudioFiles = 5000;

    /** Quét file nhạc hợp lệ trong một cấp thư mục (không đệ quy). */
    inline void collectAudioFilesFromFolderShallow (const juce::File& folder,
                                                    juce::Array<juce::File>& outFiles,
                                                    int maxFiles = kMaxShallowFolderAudioFiles)
    {
        outFiles.clear();

        if (! folder.isDirectory() || ! folder.exists())
            return;

        // Bỏ qua gốc ổ đĩa / filesystem root — tránh findChildFiles trên toàn ổ.
        if (folder.getParentDirectory() == folder)
            return;

        juce::Array<juce::File> found;
        // "*" + isSupportedAudioFile — lọc extension không phân biệt hoa thường (macOS/APFS).
        folder.findChildFiles (found, juce::File::findFiles, false, "*");
        found.sort();

        for (const auto& f : found)
        {
            if (! isSupportedAudioFile (f))
                continue;

            outFiles.add (f);

            if (outFiles.size() >= maxFiles)
                break;
        }
    }

    inline void collectAudioFilesFromDrop (const juce::StringArray& paths, juce::StringArray& outPaths)
    {
        outPaths.clear();

        for (const auto& path : paths)
        {
            juce::File item (path);

            if (item.isDirectory())
            {
                juce::Array<juce::File> found;
                item.findChildFiles (found, juce::File::findFiles, true, "*");

                found.sort();

                for (const auto& f : found)
                    if (isSupportedAudioFile (f))
                        outPaths.add (f.getFullPathName());
            }
            else if (isSupportedAudioFile (item))
            {
                outPaths.add (item.getFullPathName());
            }
        }
    }
}
