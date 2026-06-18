# Sinh compile_commands.json cho clangd (Visual Studio generator khong tao file nay).
# Usage: powershell -ExecutionPolicy Bypass -File ShowCue/scripts/refresh-ide-db-win.ps1

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ideBuild = Join-Path $repoRoot "build-clangd"
$showCueDir = Join-Path $repoRoot "ShowCue"

$ninja = @(
    "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

$vcvars = @(
    "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $ninja) { throw "Khong tim thay ninja.exe trong Visual Studio." }
if (-not $vcvars) { throw "Khong tim thay vcvars64.bat." }

Write-Host "=== refresh-ide-db-win ===" -ForegroundColor Cyan
Write-Host "Ninja: $ninja"
Write-Host "Build: $ideBuild"

$cmakeCmd = @"
call "$vcvars" >nul
cmake -S "$repoRoot" -B "$ideBuild" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_MAKE_PROGRAM="$ninja"
if errorlevel 1 exit /b 1
"@

cmd /c $cmakeCmd
if ($LASTEXITCODE -ne 0) { throw "CMake/Ninja configure or build failed (exit $LASTEXITCODE)" }

$ccSrc = Join-Path $ideBuild "compile_commands.json"
$ccDst = Join-Path $showCueDir "compile_commands.json"
$flagsOut = Join-Path $showCueDir "compile_flags.txt"

if (-not (Test-Path $ccSrc)) { throw "Thieu $ccSrc" }

Copy-Item -Force $ccSrc $ccDst
Write-Host "Copied compile_commands.json -> $ccDst" -ForegroundColor Green

$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) {
    & $py.Source (Join-Path $PSScriptRoot "generate_compile_flags.py") $ccDst $flagsOut
    Write-Host "Generated compile_flags.txt" -ForegroundColor Green
}

Write-Host "Xong. Trong Cursor: clangd: Restart language server" -ForegroundColor Cyan
