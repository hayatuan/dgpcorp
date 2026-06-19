#include "ShowBackupFirewallWin.h"

#if JUCE_WINDOWS

 #include "ShowLocalization.h"

 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shellapi.h>
 #include <shlobj.h>
 #include <netfw.h>
 #include <comdef.h>
 #include <atomic>

namespace showcontrol::backup::win
{
namespace
{
constexpr const wchar_t* kRuleUdpIn   = L"ShowCue LAN UDP In";
constexpr const wchar_t* kRuleUdpOut  = L"ShowCue LAN UDP Out";
constexpr const wchar_t* kRuleAppIn   = L"ShowCue LAN App In";
constexpr const wchar_t* kRuleAppOut  = L"ShowCue LAN App Out";

struct ComScope
{
    ComScope()  { CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() { CoUninitialize(); }
};

bool ruleExists (INetFwRules* rules, const wchar_t* name)
{
    if (rules == nullptr || name == nullptr)
        return false;

    INetFwRule* rule = nullptr;
    const HRESULT hr = rules->Item (_bstr_t (name), &rule);

    if (rule != nullptr)
        rule->Release();

    return SUCCEEDED (hr) && rule != nullptr;
}

bool portRulesPresent (INetFwRules* rules, int syncPort)
{
    juce::ignoreUnused (syncPort);
    return ruleExists (rules, kRuleUdpIn) && ruleExists (rules, kRuleUdpOut);
}

bool addUdpPortRule (INetFwRules* rules,
                     const wchar_t* name,
                     NET_FW_RULE_DIRECTION direction,
                     const wchar_t* ports)
{
    if (rules == nullptr)
        return false;

    INetFwRule* rule = nullptr;

    if (FAILED (CoCreateInstance (__uuidof (NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof (INetFwRule), reinterpret_cast<void**> (&rule))))
        return false;

    rule->put_Name (_bstr_t (name));
    rule->put_Protocol (NET_FW_IP_PROTOCOL_UDP);
    rule->put_LocalPorts (_bstr_t (ports));
    rule->put_Direction (direction);
    rule->put_Action (NET_FW_ACTION_ALLOW);
    rule->put_Enabled (VARIANT_TRUE);
    rule->put_Profiles (NET_FW_PROFILE2_ALL);

    const HRESULT hr = rules->Add (rule);
    rule->Release();
    return SUCCEEDED (hr);
}

bool addAppRule (INetFwRules* rules,
                 const wchar_t* name,
                 NET_FW_RULE_DIRECTION direction,
                 const wchar_t* exePath)
{
    if (rules == nullptr || exePath == nullptr || exePath[0] == L'\0')
        return false;

    INetFwRule* rule = nullptr;

    if (FAILED (CoCreateInstance (__uuidof (NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof (INetFwRule), reinterpret_cast<void**> (&rule))))
        return false;

    rule->put_Name (_bstr_t (name));
    rule->put_ApplicationName (_bstr_t (exePath));
    rule->put_Direction (direction);
    rule->put_Action (NET_FW_ACTION_ALLOW);
    rule->put_Enabled (VARIANT_TRUE);
    rule->put_Profiles (NET_FW_PROFILE2_ALL);

    const HRESULT hr = rules->Add (rule);
    rule->Release();
    return SUCCEEDED (hr);
}

bool tryCreateRulesViaCom (int syncPort, const juce::String& exePath)
{
    ComScope com;

    INetFwPolicy2* policy = nullptr;

    if (FAILED (CoCreateInstance (__uuidof (NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof (INetFwPolicy2), reinterpret_cast<void**> (&policy))))
        return false;

    INetFwRules* rules = nullptr;

    if (FAILED (policy->get_Rules (&rules)))
    {
        policy->Release();
        return false;
    }

    const int discoveryPort = syncPort + 1;
    const juce::String portRange = juce::String (syncPort) + "-" + juce::String (discoveryPort);
    const auto portsWide = portRange.toWideCharPointer();
    const auto exeWide   = exePath.toWideCharPointer();

    bool ok = true;

    if (! ruleExists (rules, kRuleUdpIn))
        ok = addUdpPortRule (rules, kRuleUdpIn, NET_FW_RULE_DIR_IN, portsWide) && ok;

    if (! ruleExists (rules, kRuleUdpOut))
        ok = addUdpPortRule (rules, kRuleUdpOut, NET_FW_RULE_DIR_OUT, portsWide) && ok;

    if (exePath.isNotEmpty())
    {
        if (! ruleExists (rules, kRuleAppIn))
            ok = addAppRule (rules, kRuleAppIn, NET_FW_RULE_DIR_IN, exeWide) && ok;

        if (! ruleExists (rules, kRuleAppOut))
            ok = addAppRule (rules, kRuleAppOut, NET_FW_RULE_DIR_OUT, exeWide) && ok;
    }

    const bool present = portRulesPresent (rules, syncPort);

    rules->Release();
    policy->Release();
    return ok && present;
}

bool queryRulesPresent (int syncPort)
{
    juce::ignoreUnused (syncPort);
    ComScope com;

    INetFwPolicy2* policy = nullptr;

    if (FAILED (CoCreateInstance (__uuidof (NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof (INetFwPolicy2), reinterpret_cast<void**> (&policy))))
        return false;

    INetFwRules* rules = nullptr;

    if (FAILED (policy->get_Rules (&rules)))
    {
        policy->Release();
        return false;
    }

    const bool present = portRulesPresent (rules, syncPort);
    rules->Release();
    policy->Release();
    return present;
}

std::atomic<bool> gFirewallPromptShown { false };
} // namespace

bool areWindowsFirewallRulesPresent (int syncPort)
{
    const int port = juce::jlimit (1024, 65535, syncPort);
    return queryRulesPresent (port);
}

bool ensureWindowsFirewallRules (int syncPort)
{
    const int port = juce::jlimit (1024, 65535, syncPort);

    if (queryRulesPresent (port))
        return true;

    const auto exePath = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                             .getFullPathName();

    if (tryCreateRulesViaCom (port, exePath))
        return queryRulesPresent (port);

    return false;
}

void requestElevatedFirewallSetup (int syncPort)
{
    const int port          = juce::jlimit (1024, 65535, syncPort);
    const int discoveryPort = port + 1;
    const auto exePath      = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                  .getFullPathName();

    const juce::String ps =
        "New-NetFirewallRule -DisplayName 'ShowCue LAN UDP In' -Direction Inbound -Action Allow "
        "-Protocol UDP -LocalPort '" + juce::String (port) + "-" + juce::String (discoveryPort)
        + "' -Profile Any -ErrorAction SilentlyContinue; "
        "New-NetFirewallRule -DisplayName 'ShowCue LAN UDP Out' -Direction Outbound -Action Allow "
        "-Protocol UDP -LocalPort '" + juce::String (port) + "-" + juce::String (discoveryPort)
        + "' -Profile Any -ErrorAction SilentlyContinue; "
        "New-NetFirewallRule -DisplayName 'ShowCue LAN App In' -Direction Inbound -Action Allow "
        "-Program '" + exePath + "' -Profile Any -ErrorAction SilentlyContinue; "
        "New-NetFirewallRule -DisplayName 'ShowCue LAN App Out' -Direction Outbound -Action Allow "
        "-Program '" + exePath + "' -Profile Any -ErrorAction SilentlyContinue";

    const juce::String params = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \""
                              + ps.replace ("\"", "\\\"") + "\"";

    ShellExecuteW (nullptr,
                 L"runas",
                 L"powershell.exe",
                 params.toWideCharPointer(),
                 nullptr,
                 SW_HIDE);
}

void promptWindowsFirewallAccessIfNeeded (int syncPort, bool rulesReady)
{
    if (rulesReady)
        return;

    if (gFirewallPromptShown.exchange (true))
        return;

    const int port          = juce::jlimit (1024, 65535, syncPort);
    const int discoveryPort = port + 1;

    const auto title = showcontrol::localization::tr (u8"ShowCue — Firewall Windows");
    const auto body  = showcontrol::localization::tr (
        u8"Vai trò Máy phụ (hoặc nhận lệnh từ máy khác) cần cho phép UDP cổng ")
        + juce::String (port) + "-" + juce::String (discoveryPort)
        + showcontrol::localization::tr (
        u8" qua Windows Firewall.\n\n"
          u8"Chọn «Cấp quyền» để mở hộp thoại Administrator (UAC), hoặc tự bật ShowCue trong "
          u8"Windows Security → Firewall → Allow an app.");

    juce::AlertWindow::showOkCancelBox (
        juce::AlertWindow::WarningIcon,
        title,
        body,
        showcontrol::localization::tr (u8"Cấp quyền (UAC)"),
        showcontrol::localization::tr (u8"Để sau"),
        nullptr,
        juce::ModalCallbackFunction::create ([port] (int result)
        {
            if (result == 1)
            {
                requestElevatedFirewallSetup (port);
                gFirewallPromptShown.store (false);
            }
        }));
}

void warnIfRunningAsAdministrator()
{
    if (IsUserAnAdmin() == FALSE)
        return;

    static std::atomic<bool> warned { false };

    if (warned.exchange (true))
        return;

    const auto title = showcontrol::localization::tr (u8"ShowCue — Quyền Administrator");
    const auto body  = showcontrol::localization::tr (
        u8"ShowCue đang chạy với quyền Administrator.\n\n"
          u8"Windows thường chặn kéo-thả file nhạc từ Explorer trong chế độ này. "
          u8"Hãy đóng app và mở lại bình thường (không chọn «Run as administrator»).");

    juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, title, body,
                                            showcontrol::localization::tr (u8"Đã hiểu"));
}

} // namespace showcontrol::backup::win

#endif
