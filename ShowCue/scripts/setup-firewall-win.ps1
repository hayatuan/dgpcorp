# Mo UDP LAN cho ShowCue Primary/Backup tren Windows Defender Firewall.
# Chay PowerShell Admin.
# Usage:
#   powershell -ExecutionPolicy Bypass -File ShowCue\scripts\setup-firewall-win.ps1
#   powershell -ExecutionPolicy Bypass -File ShowCue\scripts\setup-firewall-win.ps1 -ExePath "C:\app\dgpcorp\build\windows-release\ShowCue\ShowCue_artefacts\Release\ShowCue.exe"

param(
    [int] $SyncPort = 9000,
    [string] $ExePath = ""
)

$ErrorActionPreference = "Stop"
$discoveryPort = $SyncPort + 1
$rulePrefix = "ShowCue LAN"

function Remove-OldRules {
    Get-NetFirewallRule -DisplayName "$rulePrefix*" -ErrorAction SilentlyContinue |
        Remove-NetFirewallRule -ErrorAction SilentlyContinue
}

function Add-PortRules {
    param([string] $Name, [string] $Direction)
    New-NetFirewallRule -DisplayName $Name -Direction $Direction -Action Allow `
        -Protocol UDP -LocalPort "$SyncPort-$discoveryPort" | Out-Null
}

Write-Host "=== ShowCue Windows Firewall ===" -ForegroundColor Cyan
Write-Host "UDP ports: $SyncPort (sync/OSC), $discoveryPort (discovery)"

Remove-OldRules
Add-PortRules "$rulePrefix UDP In"  Inbound
Add-PortRules "$rulePrefix UDP Out" Outbound

if ($ExePath -ne "" -and (Test-Path $ExePath)) {
    $exe = (Resolve-Path $ExePath).Path
    New-NetFirewallRule -DisplayName "$rulePrefix App In"  -Direction Inbound  -Action Allow -Program $exe | Out-Null
    New-NetFirewallRule -DisplayName "$rulePrefix App Out" -Direction Outbound -Action Allow -Program $exe | Out-Null
    Write-Host "Da them rule cho: $exe" -ForegroundColor Green
}

Write-Host "Xong. Kiem tra Windows Security -> Firewall -> Allow an app." -ForegroundColor Green
Write-Host "Mac can bat Quyen Mang cuc bo (Local Network) trong System Settings." -ForegroundColor Yellow
