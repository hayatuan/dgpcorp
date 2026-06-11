#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace showcontrol::update
{
inline constexpr const char* kGitHubLatestReleaseUrl =
    "https://api.github.com/repos/hayatuan/dgpcorp/releases/latest";

struct RemoteVersionInfo
{
    juce::String version;
    juce::String downloadUrl;
};

/** So sánh semver đơn giản a.b.c — trả về >0 nếu lhs mới hơn rhs. */
int compareVersionStrings (const juce::String& lhs, const juce::String& rhs);

std::optional<RemoteVersionInfo> parseUpdateJson (const juce::String& jsonText);

/** Kiểm tra cập nhật bất đồng bộ — request trên luồng ngầm, UI trên message thread. */
class ShowUpdateChecker final : public juce::Thread
{
public:
    ShowUpdateChecker();
    ~ShowUpdateChecker() override;

    /** @param userInitiated false = khởi động tự động (lỗi mạng im lặng); true = người dùng bấm kiểm tra thủ công. */
    void checkForUpdatesAsync (bool userInitiated = false,
                               std::function<void()> onFinished = nullptr);

private:
    void run() override;
    void dispatchUiResult (bool success, int statusCode, const juce::String& jsonText);

    std::shared_ptr<std::atomic<bool>> uiAlive;
    std::function<void()> onFinishedCallback;
    juce::CriticalSection callbackLock;
    bool userInitiatedCheck = false;
};

} // namespace showcontrol::update
