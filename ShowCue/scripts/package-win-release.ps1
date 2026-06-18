# Dong goi phat hanh Windows -> D:\APP\RC
#   ShowCue-v1.0.0-Windows\  (portable)
#   ShowCue-v1.0.0-Windows.zip
#   ShowCue-Setup-1.0.0.exe  (Inno Setup, neu co ISCC)
#
# Usage (tu root repo dgpcorp):
#   powershell -ExecutionPolicy Bypass -File ShowCue/scripts/package-win-release.ps1
#
# Tuy chon:
#   -SkipBuild       khong chay cmake build
#   -SkipInstaller   chi tao portable + zip
#   -Version "1.0.0"
#   -OutputDir "D:\APP\RC"
#   -BuildDirName "build-win"

param(
    [string] $Version = "1.0.0",
    [string] $OutputDir = "D:\APP\RC",
    [string] $BuildDirName = "build-win",
    [switch] $SkipBuild,
    [switch] $SkipInstaller
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$buildOut = Join-Path $repoRoot "$BuildDirName\ShowCue\ShowCue_artefacts\Release"
$appExe = Join-Path $buildOut "ShowCue.exe"
$ffmpegBuild = Join-Path $buildOut "ffmpeg.exe"
$ffmpegFallback = Join-Path $repoRoot "ShowCue\ThirdParty\ffmpeg\win\ffmpeg.exe"
$issFile = Join-Path $PSScriptRoot "ShowCue-installer.iss"

$portableFolderName = "ShowCue-v$Version-Windows"
$portableDir = Join-Path $OutputDir $portableFolderName
$zipPath = Join-Path $OutputDir "$portableFolderName.zip"
$setupExe = Join-Path $OutputDir "ShowCue-Setup-$Version.exe"

function Find-ISCC {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    foreach ($p in $candidates) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

Write-Host "=== ShowCue Windows release packager ===" -ForegroundColor Cyan
Write-Host "Repo:    $repoRoot"
Write-Host "Output:  $OutputDir"
Write-Host "Version: $Version"
Write-Host ""

if (-not $SkipBuild) {
    Write-Host "[1/4] Build Release..." -ForegroundColor Yellow
    $cmakeBuildDir = Join-Path $repoRoot $BuildDirName
    if (-not (Test-Path (Join-Path $cmakeBuildDir "CMakeCache.txt"))) {
        Write-Host "  Chua configure. Vi du:"
        Write-Host '  cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64'
        throw "Thieu CMakeCache trong $cmakeBuildDir"
    }
    Push-Location $repoRoot
    try {
        cmake --build $BuildDirName --config Release --target ShowCue
        if ($LASTEXITCODE -ne 0) { throw "cmake build that bai (exit $LASTEXITCODE)" }
    }
    finally {
        Pop-Location
    }
    Write-Host "  Build xong." -ForegroundColor Green
}
else {
    Write-Host "[1/4] Bo qua build (-SkipBuild)." -ForegroundColor DarkGray
}

if (-not (Test-Path $appExe)) {
    throw "Khong tim thay ShowCue.exe tai: $appExe"
}

$ffmpegSource = $null
if (Test-Path $ffmpegBuild) {
    $ffmpegSource = $ffmpegBuild
}
elseif (Test-Path $ffmpegFallback) {
    $ffmpegSource = $ffmpegFallback
    Write-Host "  Canh bao: dung ffmpeg tu ThirdParty fallback." -ForegroundColor DarkYellow
}
else {
    throw "Thieu ffmpeg.exe. Chay setup-thirdparty-win.ps1 roi build lai."
}

Write-Host "[2/4] Portable -> $portableDir" -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
if (Test-Path $portableDir) { Remove-Item $portableDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $portableDir | Out-Null

Copy-Item -Force $appExe (Join-Path $portableDir "ShowCue.exe")
Copy-Item -Force $ffmpegSource (Join-Path $portableDir "ffmpeg.exe")

$readmePath = Join-Path $portableDir "HUONG-DAN-CHAY.txt"
@(
    "ShowCue v$Version - Windows 10/11 (64-bit)"
    ""
    "Portable:"
    "1. Giai nen hoac chay trong thu muc nay."
    "2. Nhan doi ShowCue.exe."
    "3. Giu ffmpeg.exe cung thu muc (can cho video)."
    ""
    "Cai dat: dung ShowCue-Setup-$Version.exe trong D:\APP\RC"
    ""
    "---"
    "Double-click ShowCue.exe. Keep ffmpeg.exe alongside."
) | Set-Content -Path $readmePath -Encoding UTF8

Write-Host "  Da copy ShowCue.exe + ffmpeg.exe." -ForegroundColor Green

Write-Host "[3/4] Zip -> $zipPath" -ForegroundColor Yellow
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path (Join-Path $portableDir "*") -DestinationPath $zipPath -Force
$zipMb = [math]::Round((Get-Item $zipPath).Length / 1048576, 1)
Write-Host ('  Zip xong: ' + $zipMb + ' MB') -ForegroundColor Green

if ($SkipInstaller) {
    Write-Host "[4/4] Bo qua Inno Setup (-SkipInstaller)." -ForegroundColor DarkGray
}
else {
    Write-Host "[4/4] Inno Setup installer..." -ForegroundColor Yellow
    $iscc = Find-ISCC
    if ($null -eq $iscc) {
        Write-Host "  ISCC.exe khong tim thay - bo qua Setup.exe." -ForegroundColor DarkYellow
        Write-Host "  Cai Inno Setup 6 hoac Compile ShowCue-installer.iss thu cong."
    }
    else {
        & $iscc $issFile
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup compile that bai (exit $LASTEXITCODE)" }
        if (-not (Test-Path $setupExe)) {
            throw "ISCC chay xong nhung khong thay: $setupExe"
        }
        $setupMb = [math]::Round((Get-Item $setupExe).Length / 1048576, 1)
        Write-Host ('  Setup: ' + $setupExe + ' - ' + $setupMb + ' MB') -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "=== Hoan tat: $OutputDir ===" -ForegroundColor Cyan
Get-ChildItem $OutputDir -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "ShowCue*" } |
    Sort-Object Name |
    ForEach-Object {
        if ($_.PSIsContainer) {
            $size = "(folder)"
        }
        else {
            $size = ([math]::Round($_.Length / 1048576, 1).ToString() + " MB")
        }
        Write-Host ('  ' + $_.Name + '  ' + $size)
    }

Write-Host ""
Write-Host "Upload GitHub Releases:"
Write-Host "  - $portableFolderName.zip"
if (Test-Path $setupExe) {
    Write-Host "  - ShowCue-Setup-$Version.exe"
}
