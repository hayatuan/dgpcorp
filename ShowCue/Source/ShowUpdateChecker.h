#pragma once
#include <JuceHeader.h>
#include <functional>
#include <optional>

namespace showcontrol::update
{
inline constexpr const char* kUpdateApiUrl =
    "https://api.showcontrol.tuannv.vn/v1/update/macOS/latest";

struct RemoteVersionInfo
{
    juce::String version;
    juce::String downloadUrl;
};

/** So sánh semver đơn giản a.b.c — trả về >0 nếu lhs mới hơn rhs. */
inline int compareVersionStrings (const juce::String& lhs, const juce::String& rhs)
{
    const auto lhsParts = juce::StringArray::fromTokens (lhs, ".", "");
    const auto rhsParts = juce::StringArray::fromTokens (rhs, ".", "");
    const int maxParts = juce::jmax (lhsParts.size(), rhsParts.size());

    for (int i = 0; i < maxParts; ++i)
    {
        const int l = (i < lhsParts.size()) ? lhsParts[i].getIntValue() : 0;
        const int r = (i < rhsParts.size()) ? rhsParts[i].getIntValue() : 0;

        if (l != r)
            return l - r;
    }

    return 0;
}

inline std::optional<RemoteVersionInfo> parseUpdatePayload (const juce::String& jsonText)
{
    const auto parsed = juce::JSON::parse (jsonText);

    if (parsed.isVoid() || ! parsed.isObject())
        return std::nullopt;

    RemoteVersionInfo info;
    info.version     = parsed.getProperty ("version", {}).toString().trim();
    info.downloadUrl = parsed.getProperty ("download_url", {}).toString().trim();

    if (info.version.isEmpty())
        return std::nullopt;

    return info;
}

/** Tương đương downloadStringAsync — fetch background thread, callback message thread. */
inline void downloadStringAsync (const juce::URL& url,
                                 std::function<void (const juce::String& response, bool success)> callback)
{
    juce::Thread::launch ([url, cb = std::move (callback)]
    {
        juce::String response;
        bool success = false;

        const juce::URL::InputStreamOptions options (
            juce::URL::ParameterHandling::inAddress);

        if (auto stream = url.createInputStream (options.withConnectionTimeoutMs (12000)
                                                      .withNumRedirectsToFollow (3)))
        {
            response = stream->readEntireStreamAsString();
            success  = response.isNotEmpty();
        }

        juce::MessageManager::callAsync ([cb, response, success]
        {
            if (cb != nullptr)
                cb (response, success);
        });
    });
}

inline void checkForUpdatesAsync (std::function<void()> onFinished = nullptr)
{
    auto* checkingAlert = new juce::AlertWindow (juce::String::fromUTF8 (u8"Kiểm tra cập nhật"),
                                                 juce::String::fromUTF8 (u8"Đang kiểm tra cập nhật..."),
                                                 juce::AlertWindow::InfoIcon);
    checkingAlert->setColour (juce::AlertWindow::backgroundColourId,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                           .findColour (juce::ResizableWindow::backgroundColourId));
    checkingAlert->setColour (juce::AlertWindow::textColourId,
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                         .findColour (juce::Label::textColourId));

    const juce::Component::SafePointer<juce::AlertWindow> safeChecking (checkingAlert);
    checkingAlert->enterModalState (false);

    downloadStringAsync (juce::URL (kUpdateApiUrl),
                         [safeChecking, onFinished] (const juce::String& response, bool success)
    {
        if (safeChecking != nullptr)
            safeChecking->exitModalState (0);

        const auto currentVersion = juce::String (ProjectInfo::versionString);

        if (! success)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                juce::String::fromUTF8 (u8"Kiểm tra cập nhật"),
                juce::String::fromUTF8 (u8"Không thể kết nối máy chủ cập nhật.\nVui lòng thử lại sau."),
                juce::String::fromUTF8 (u8"Đóng"));

            if (onFinished != nullptr)
                onFinished();

            return;
        }

        const auto remote = parseUpdatePayload (response);

        if (! remote.has_value())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                juce::String::fromUTF8 (u8"Kiểm tra cập nhật"),
                juce::String::fromUTF8 (u8"Dữ liệu cập nhật không hợp lệ."),
                juce::String::fromUTF8 (u8"Đóng"));

            if (onFinished != nullptr)
                onFinished();

            return;
        }

        if (compareVersionStrings (remote->version, currentVersion) > 0)
        {
            juce::AlertWindow::showOkCancelBox (
                juce::AlertWindow::InfoIcon,
                juce::String::fromUTF8 (u8"Có bản cập nhật mới"),
                juce::String::fromUTF8 (u8"Phiên bản ")
                    + remote->version
                    + juce::String::fromUTF8 (u8" đã sẵn sàng.\nBạn có muốn tải xuống ngay không?"),
                juce::String::fromUTF8 (u8"Tải xuống ngay"),
                juce::String::fromUTF8 (u8"Để sau"),
                nullptr,
                juce::ModalCallbackFunction::create ([url = remote->downloadUrl] (int result)
                {
                    if (result == 1 && url.isNotEmpty())
                        juce::URL (url).launchInDefaultBrowser();
                }));
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::InfoIcon,
                juce::String::fromUTF8 (u8"Kiểm tra cập nhật"),
                juce::String::fromUTF8 (u8"Ứng dụng của bạn đã là phiên bản mới nhất (v")
                    + currentVersion
                    + juce::String::fromUTF8 (u8")."),
                juce::String::fromUTF8 (u8"Đóng"));
        }

        if (onFinished != nullptr)
            onFinished();
    });
}
} // namespace showcontrol::update
