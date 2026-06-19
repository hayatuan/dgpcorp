# Mo UDP LAN cho ShowCue Primary/Backup tren Windows Defender Firewall.
# BAT BUOC: PowerShell "Run as administrator"
# Usage:
#   powershell -ExecutionPolicy Bypass -File ShowCue\scripts\setup-firewall-win.ps1
#   powershell -ExecutionPolicy Bypass -File ShowCue\scripts\setup-firewall-win.ps1 -ExePath "D:\APP\dgpcorp\build\windows-release\ShowCue\ShowCue_artefacts\Release\ShowCue.exe"

param(
    [int] $SyncPort = 9000,
    [string] $ExePath = ""
)

$ErrorActionPreference = "Stop"
$discoveryPort = $SyncPort + 1
$rulePrefix = "ShowCue LAN"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Show-ManualFirewallHelp {
    Write-Host ""
    Write-Host "=== Cach mo firewall thu cong (khong can script) ===" -ForegroundColor Yellow
    Write-Host "1. Windows Security -> Firewall & network protection"
    Write-Host "2. Allow an app through firewall -> Change settings"
    Write-Host "3. Tim ShowCue.exe -> bat ca Private (va Public neu can)"
    Write-Host "   Hoac Browse -> chon file ShowCue.exe trong build Release"
    Write-Host "4. Máy làm MAY PHU can inbound UDP $SyncPort-$discoveryPort"
    Write-Host ""
}

if (-not (Test-IsAdmin)) {
    Write-Host "LOI: Can quyen Administrator de tao firewall rule." -ForegroundColor Red
    Write-Host "Mo PowerShell: chuot phai -> Run as administrator" -ForegroundColor Yellow
    Write-Host "Roi chay lai lenh nay." -ForegroundColor Yellow
    Show-ManualFirewallHelp
    exit 1
}

function Remove-OldRules {
    Get-NetFirewallRule -DisplayName "$rulePrefix*" -ErrorAction SilentlyContinue |
        Remove-NetFirewallRule -ErrorAction SilentlyContinue
}

function Add-PortRules {
    param([string] $Name, [string] $Direction)
    New-NetFirewallRule -DisplayName $Name -Direction $Direction -Action Allow `
        -Protocol UDP -LocalPort "$SyncPort-$discoveryPort" -Profile Any | Out-Null
}

Write-Host "=== ShowCue Windows Firewall ===" -ForegroundColor Cyan
Write-Host "UDP ports: $SyncPort (sync/OSC), $discoveryPort (discovery)"

Remove-OldRules
Add-PortRules "$rulePrefix UDP In"  Inbound
Add-PortRules "$rulePrefix UDP Out" Outbound

if ($ExePath -ne "" -and (Test-Path $ExePath)) {
    $exe = (Resolve-Path $ExePath).Path
    New-NetFirewallRule -DisplayName "$rulePrefix App In"  -Direction Inbound  -Action Allow -Program $exe -Profile Any | Out-Null
    New-NetFirewallRule -DisplayName "$rulePrefix App Out" -Direction Outbound -Action Allow -Program $exe -Profile Any | Out-Null
    Write-Host "Da them rule cho: $exe" -ForegroundColor Green
}
elseif ($ExePath -ne "") {
    Write-Host "Canh bao: khong tim thay ExePath: $ExePath" -ForegroundColor Yellow
}

Write-Host "Xong. Rule inbound UDP $SyncPort-$discoveryPort da tao." -ForegroundColor Green
Write-Host "May PHU (Backup) can inbound — neu chi test MAY CHU truoc day van OK." -ForegroundColor Cyan
