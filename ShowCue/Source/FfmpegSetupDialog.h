#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "VideoAudioExtractor.h"
#include "ErrorHandler.h"

//==============================================================================
/** Hộp thoại khi thiếu ffmpeg — không chạy trên audio thread. */
namespace showcontrol::ui
{
enum class FfmpegPromptChoice
{
    dismissed = 0,
    installHomebrew = 1,
    copyInstallCommand = 2,
    openBrewWebsite = 3
};

inline juce::String ffmpegMissingExplanation()
{
   #if JUCE_WINDOWS
    return juce::String::fromUTF8 (
        u8"Tính năng kéo thả video cần ffmpeg.\n\n"
        u8"Bản cài đặt đầy đủ thường đã có ffmpeg.exe cạnh ShowCue.exe.\n"
        u8"Nếu thiếu, chạy script: ShowCue\\scripts\\setup-thirdparty-win.ps1\n"
        u8"Hoặc cài qua winget: winget install --id Gyan.FFmpeg");
   #else
    return juce::String::fromUTF8 (
        u8"Tính năng kéo thả video cần ffmpeg.\n\n"
        u8"Bản cài đặt đầy đủ thường đã có ffmpeg trong app; nếu thiếu:\n"
        u8"• Có Homebrew: chọn «Cài tự động» hoặc chạy: brew install ffmpeg\n"
        u8"• Chưa có Homebrew: cài từ brew.sh, sau đó cài ffmpeg.");
   #endif
}

inline void promptMissingFfmpeg (juce::Component* parent,
                                 std::function<void (FfmpegPromptChoice)> onChoice)
{
    juce::AlertWindow w (juce::String::fromUTF8 (u8"Tách audio video"),
                         ffmpegMissingExplanation(),
                         juce::MessageBoxIconType::WarningIcon,
                         parent);

   #if JUCE_WINDOWS
    w.addButton (juce::String::fromUTF8 (u8"Sao chép lệnh winget"), 5,
                 juce::KeyPress (juce::KeyPress::returnKey));
    w.addButton (juce::String::fromUTF8 (u8"Mở trang tải ffmpeg"), 4);
   #else
    const bool hasBrew = VideoAudioExtractor::isHomebrewAvailable();

    if (hasBrew)
        w.addButton (juce::String::fromUTF8 (u8"Cài tự động (Homebrew)"), 1,
                     juce::KeyPress (juce::KeyPress::returnKey));

    w.addButton (juce::String::fromUTF8 (u8"Sao chép lệnh cài"), 2);
    w.addButton (juce::String::fromUTF8 (u8"Mở brew.sh"), 3);

    if (! hasBrew)
        w.addButton (juce::String::fromUTF8 (u8"Mở trang tải ffmpeg"), 4);
   #endif

    w.addButton (juce::String::fromUTF8 (u8"Để sau"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w.enterModalState (true,
        juce::ModalCallbackFunction::create ([onChoice = std::move (onChoice)] (int result)
        {
            if (! onChoice)
                return;

            switch (result)
            {
                case 1:  onChoice (FfmpegPromptChoice::installHomebrew); break;
                case 2:
                    juce::SystemClipboard::copyTextToClipboard (VideoAudioExtractor::brewInstallFfmpegCommand());
                    onChoice (FfmpegPromptChoice::copyInstallCommand);
                    break;
                case 3:
                    juce::URL ("https://brew.sh").launchInDefaultBrowser();
                    onChoice (FfmpegPromptChoice::openBrewWebsite);
                    break;
                case 4:
                    juce::URL ("https://ffmpeg.org/download.html").launchInDefaultBrowser();
                    onChoice (FfmpegPromptChoice::openBrewWebsite);
                    break;
               #if JUCE_WINDOWS
                case 5:
                    juce::SystemClipboard::copyTextToClipboard ("winget install --id Gyan.FFmpeg");
                    onChoice (FfmpegPromptChoice::copyInstallCommand);
                    break;
               #endif
                default: onChoice (FfmpegPromptChoice::dismissed); break;
            }
        }),
        true);
}

class FfmpegInstallProgressThread : public juce::ThreadWithProgressWindow
{
public:
    explicit FfmpegInstallProgressThread (juce::Component* centreAround)
        : juce::ThreadWithProgressWindow (juce::String::fromUTF8 (u8"Đang cài ffmpeg…"),
                                          true,
                                          true,
                                          60000,
                                          juce::String::fromUTF8 (u8"Hủy"),
                                          centreAround)
    {
        setStatusMessage (juce::String::fromUTF8 (u8"Homebrew đang tải và cài ffmpeg. Vui lòng đợi…"));
    }

    bool installSucceeded() const noexcept { return succeeded; }
    juce::String getErrorMessage() const { return errorMessage; }

    std::function<void (bool ok, juce::String error)> onFinished;

    void threadComplete (bool userPressedCancel) override
    {
        const bool ok = ! userPressedCancel && succeeded;
        juce::String err;
        if (! ok)
            err = getErrorMessage().isNotEmpty()
                    ? getErrorMessage()
                    : juce::String::fromUTF8 (u8"Không cài được ffmpeg.");

        if (onFinished)
            onFinished (ok, err);

        delete this;
    }

    void run() override
    {
        const auto brew = VideoAudioExtractor::findHomebrewExecutable();
        if (! brew.existsAsFile())
        {
            errorMessage = juce::String::fromUTF8 (u8"Không tìm thấy Homebrew (brew).");
            return;
        }

        juce::ChildProcess proc;
        juce::StringArray args;
        args.add (brew.getFullPathName());
        args.add ("install");
        args.add ("ffmpeg");

        if (! proc.start (args))
        {
            errorMessage = juce::String::fromUTF8 (u8"Không khởi chạy được brew install ffmpeg.");
            return;
        }

        while (! threadShouldExit())
        {
            if (proc.waitForProcessToFinish (400))
                break;
        }

        if (threadShouldExit())
        {
            proc.kill();
            errorMessage = juce::String::fromUTF8 (u8"Đã hủy cài đặt.");
            return;
        }

        const int code = proc.getExitCode();
        succeeded = VideoAudioExtractor::isFfmpegAvailable();

        if (! succeeded)
        {
            auto stderrOut = proc.readAllProcessOutput();
            errorMessage = juce::String::fromUTF8 (u8"Cài ffmpeg thất bại");
            if (code >= 0)
                errorMessage << juce::String::fromUTF8 (u8" (mã ") << code << u8")";
            if (stderrOut.trim().isNotEmpty())
                errorMessage << ":\n" << stderrOut.trim().substring (0, 400);
        }
    }

private:
    bool succeeded = false;
    juce::String errorMessage;
};

/** Hiển thị progress modal; callback trên message thread khi xong (thread tự delete). */
inline void installFfmpegWithProgress (juce::Component* parent,
                                       std::function<void (bool ok, juce::String error)> onFinished)
{
    auto* thread = new FfmpegInstallProgressThread (parent);
    thread->onFinished = std::move (onFinished);
    thread->launchThread();
}

} // namespace showcontrol::ui
