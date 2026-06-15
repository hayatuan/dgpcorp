# Tải ffmpeg.exe vào ThirdParty — chạy một lần sau clone trên Windows.
# Usage: powershell -ExecutionPolicy Bypass -File ShowCue/scripts/setup-thirdparty-win.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$destDir = Join-Path $root "ShowCue\ThirdParty\ffmpeg\win"
$destExe = Join-Path $destDir "ffmpeg.exe"

New-Item -ItemType Directory -Force -Path $destDir | Out-Null

if (Test-Path $destExe) {
    Write-Host "ffmpeg da co san: $destExe"
    exit 0
}

$url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip"
$zip = Join-Path $env:TEMP "showcue-ffmpeg-win64.zip"
$extract = Join-Path $env:TEMP "showcue-ffmpeg-extract"

Write-Host "Dang tai ffmpeg (~200MB)..."
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Expand-Archive -Path $zip -DestinationPath $extract -Force

$exe = Get-ChildItem -Path $extract -Recurse -Filter ffmpeg.exe | Select-Object -First 1
if (-not $exe) { throw "Khong tim thay ffmpeg.exe trong goi tai ve." }

Copy-Item $exe.FullName $destExe -Force
Write-Host "Da dat ffmpeg tai: $destExe"
Write-Host "Chay lai CMake configure + build de copy ffmpeg.exe canh ShowCue.exe:"
Write-Host "  cmake -S . -B build -G `"Visual Studio 18 2026`" -A x64"
Write-Host "  cmake --build build --config Release --target ShowCue"
