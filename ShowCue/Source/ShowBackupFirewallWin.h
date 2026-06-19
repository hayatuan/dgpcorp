#pragma once

#include <juce_core/juce_core.h>

namespace showcontrol::backup::win
{
/** True nếu rule UDP + app đã có trong Windows Firewall. */
bool areWindowsFirewallRulesPresent (int syncPort);

/** True nếu rule firewall đã có hoặc tạo thành công (cần Admin lần đầu). */
bool ensureWindowsFirewallRules (int syncPort);

/** Hộp thoại một lần / phiên khi chưa có quyền firewall (Máy phụ cần inbound UDP). */
void promptWindowsFirewallAccessIfNeeded (int syncPort, bool rulesReady);

/** Mở UAC để PowerShell tạo rule (gọi từ nút trong hộp thoại). */
void requestElevatedFirewallSetup (int syncPort);

/** Cảnh báo một lần nếu app đang chạy quyền Administrator (kéo-thả file bị khóa). */
void warnIfRunningAsAdministrator();

} // namespace showcontrol::backup::win
