#include "ShowUpdateChecker.h"
#include "ShowLocalization.h"
#include <iostream>

namespace showcontrol::update
{
namespace
{
juce::String normaliseVersionTag (juce::String version)
{
    version = version.trim();

    if (version.startsWithChar ('v') || version.startsWithChar ('V'))
        version = version.substring (1);

    return version.trim();
}
} // namespace

int compareVersionStrings (const juce::String& lhs, const juce::String& rhs)
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

std::optional<RemoteVersionInfo> parseUpdateJson (const juce::String& jsonText)
{
    const auto parsed = juce::JSON::parse (jsonText);

    if (parsed.isVoid() || ! parsed.isObject())
        return std::nullopt;

    RemoteVersionInfo info;

    if (parsed.hasProperty ("tag_name"))
    {
        info.version = normaliseVersionTag (parsed.getProperty ("tag_name", {}).toString());

        if (auto* assets = parsed.getProperty ("assets", {}).getArray())
        {
            for (const auto& assetVar : *assets)
            {
                const auto assetUrl = assetVar.getProperty ("browser_download_url", {}).toString().trim();

                if (assetUrl.isNotEmpty())
                {
                    info.downloadUrl = assetUrl;
                    break;
                }
            }
        }

        if (info.downloadUrl.isEmpty())
            info.downloadUrl = parsed.getProperty ("html_url", {}).toString().trim();
    }
    else
    {
        info.version     = normaliseVersionTag (parsed.getProperty ("version", {}).toString());
        info.downloadUrl = parsed.getProperty ("download_url", {}).toString().trim();
    }

    if (info.version.isEmpty())
        return std::nullopt;

    return info;
}

ShowUpdateChecker::ShowUpdateChecker()
    : juce::Thread ("ShowUpdateChecker")
{
}

ShowUpdateChecker::~ShowUpdateChecker()
{
    if (uiAlive != nullptr)
        uiAlive->store (true, std::memory_order_release);

    signalThreadShouldExit();

    if (isThreadRunning())
        waitForThreadToExit (1000);
}

void ShowUpdateChecker::checkForUpdatesAsync (bool userInitiated,
                                              std::function<void()> onFinished)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (isThreadRunning())
    {
        signalThreadShouldExit();
        waitForThreadToExit (1000);
    }

    {
        const juce::ScopedLock lock (callbackLock);
        onFinishedCallback = std::move (onFinished);
    }

    userInitiatedCheck = userInitiated;
    uiAlive = std::make_shared<std::atomic<bool>> (false);

    startThread (juce::Thread::Priority::background);
}

void ShowUpdateChecker::run()
{
    juce::URL url (kGitHubLatestReleaseUrl);
    int statusCode = 0;
    juce::String jsonText;

    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                             .withConnectionTimeoutMs (5000)
                             .withNumRedirectsToFollow (3)
                             .withExtraHeaders ("User-Agent: ShowCue\r\nAccept: application/vnd.github+json")
                             .withStatusCode (&statusCode);

    if (auto stream = url.createInputStream (options))
    {
        if (! threadShouldExit())
            jsonText = stream->readEntireStreamAsString();
    }

    if (threadShouldExit())
        return;

    const bool success = (statusCode == 200 && jsonText.isNotEmpty());
    const auto alive = uiAlive;

    juce::MessageManager::callAsync ([this, alive, success, statusCode, jsonText]
    {
        if (alive != nullptr && alive->load (std::memory_order_acquire))
            return;

        dispatchUiResult (success, statusCode, jsonText);
    });
}

void ShowUpdateChecker::dispatchUiResult (bool success, int statusCode, const juce::String& jsonText)
{
    juce::ignoreUnused (statusCode);

    std::function<void()> finished;
    {
        const juce::ScopedLock lock (callbackLock);
        finished = std::move (onFinishedCallback);
    }

    const auto invokeFinished = [&finished]
    {
        if (finished != nullptr)
            finished();
    };

    const auto currentVersion = juce::String (ProjectInfo::versionString);

    if (! success)
    {
        if (! userInitiatedCheck)
        {
            std::cout << "[UPDATE] [INFO] Connection failed or no internet. Skipping update check silently."
                      << std::endl;
            invokeFinished();
            return;
        }

        juce::MessageManager::callAsync ([]
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                juce::String::fromUTF8 (u8"Kết nối thất bại"),
                juce::String::fromUTF8 (u8"Không thể kết nối tới máy chủ cập nhật. Vui lòng kiểm tra lại mạng mạng hiện trường."),
                juce::String::fromUTF8 (u8"Đóng"),
                nullptr);
        });

        invokeFinished();
        return;
    }

    const auto remote = parseUpdateJson (jsonText);

    if (! remote.has_value())
    {
        if (! userInitiatedCheck)
        {
            std::cout << "[UPDATE] [INFO] Invalid update response. Skipping update check silently."
                      << std::endl;
            invokeFinished();
            return;
        }

        juce::MessageManager::callAsync ([]
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                showcontrol::localization::tr (u8"Kiểm tra cập nhật"),
                showcontrol::localization::tr (u8"Dữ liệu cập nhật không hợp lệ."),
                showcontrol::localization::tr (u8"Đóng"),
                nullptr);
        });

        invokeFinished();
        return;
    }

    if (compareVersionStrings (remote->version, currentVersion) > 0)
    {
        const auto downloadUrl = remote->downloadUrl;
        const auto latestVersion = remote->version;
        auto prompt = showcontrol::localization::tr (u8"Đã có phiên bản mới. Bạn có muốn tải về không?");

        if (latestVersion.isNotEmpty())
            prompt += "\n" + latestVersion;

        juce::MessageManager::callAsync ([prompt, downloadUrl]
        {
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::InfoIcon)
                    .withTitle (showcontrol::localization::tr (u8"Cập nhật phần mềm"))
                    .withMessage (prompt)
                    .withButton (showcontrol::localization::tr (u8"Tải về ngay"))
                    .withButton (showcontrol::localization::tr (u8"Để sau")),
                [downloadUrl] (int result)
                {
                    if (result == 0 && downloadUrl.isNotEmpty())
                        juce::URL (downloadUrl).launchInDefaultBrowser();
                });
        });
    }
    else if (userInitiatedCheck)
    {
        juce::MessageManager::callAsync ([]
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::InfoIcon,
                showcontrol::localization::tr (u8"Kiểm tra cập nhật"),
                showcontrol::localization::tr (u8"Ứng dụng của bạn đã là phiên bản mới nhất!"),
                showcontrol::localization::tr (u8"Đóng"),
                nullptr);
        });
    }

    invokeFinished();
}

} // namespace showcontrol::update
