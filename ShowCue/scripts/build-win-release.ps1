# Build ShowCue Release (Premium) on Windows from repo root dgpcorp.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File ShowCue/scripts/build-win-release.ps1
#   powershell -ExecutionPolicy Bypass -File ShowCue/scripts/build-win-release.ps1 -CopyToDesktop
#   powershell -ExecutionPolicy Bypass -File ShowCue/scripts/build-win-release.ps1 -SkipClean
#
param(
    [string] $BuildDirName = "build-win",
    [switch] $CopyToDesktop,
    [switch] $SkipClean
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$buildDir = Join-Path $repoRoot $BuildDirName
$exePath = Join-Path $buildDir "ShowCue\ShowCue_artefacts\Release\ShowCue.exe"

function Invoke-CMakeConfigure {
    param([string] $Generator)
    cmake -S $repoRoot -B $buildDir -G $Generator -A x64
}

Write-Host "=== ShowCue Premium Release Build ===" -ForegroundColor Cyan
Write-Host "Repo:  $repoRoot"
Write-Host "Build: $buildDir"
Write-Host ""

if (-not $SkipClean -and (Test-Path $buildDir)) {
    Write-Host "[1/3] Remove old build dir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    Write-Host "[2/3] CMake configure..." -ForegroundColor Yellow
    $configured = $false
    foreach ($generator in @("Visual Studio 18 2026", "Visual Studio 17 2022")) {
        Write-Host "  Try generator: $generator"
        Invoke-CMakeConfigure -Generator $generator
        if ($LASTEXITCODE -eq 0) {
            $configured = $true
            break
        }
    }
    if (-not $configured) {
        throw "CMake configure failed. Install Visual Studio 2022/2026 and CMake."
    }
}
else {
    Write-Host "[2/3] CMakeCache exists, skip configure." -ForegroundColor DarkGray
}

$jobs = $env:NUMBER_OF_PROCESSORS
if ([string]::IsNullOrWhiteSpace($jobs)) { $jobs = 4 }

Write-Host "[3/3] Build Release (parallel $jobs)..." -ForegroundColor Yellow
cmake --build $buildDir --config Release --parallel $jobs --target ShowCue
if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed (exit $LASTEXITCODE)"
}

if (-not (Test-Path $exePath)) {
    throw "ShowCue.exe not found at: $exePath"
}

Write-Host ""
Write-Host "Build OK:" -ForegroundColor Green
Write-Host "  $exePath"

if ($CopyToDesktop) {
    $desktop = [Environment]::GetFolderPath("Desktop")
    $dest = Join-Path $desktop "ShowCue.exe"
    Copy-Item -Force $exePath $dest
    Write-Host "  Copied to $dest" -ForegroundColor Green
}

Write-Host ""
Write-Host "Package for distribution:" -ForegroundColor Cyan
Write-Host '  powershell -ExecutionPolicy Bypass -File ShowCue/scripts/package-win-release.ps1'
