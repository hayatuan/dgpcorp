# ShowCue v1.0.0 — Hướng dẫn build

Ứng dụng cue-based show control (JUCE 8, C++17). Hỗ trợ **macOS 11+** và **Windows 10/11**.

**Thành viên Windows mới:** xem [ONBOARDING_WINDOWS.md](ONBOARDING_WINDOWS.md).

## Yêu cầu chung

| Thành phần | macOS | Windows |
|------------|-------|---------|
| CMake | ≥ 3.22 | ≥ 3.22 |
| Compiler | Xcode CLT hoặc Xcode | Visual Studio 2022 hoặc **2026** (Desktop C++) |
| CMake (VS 2026) | — | ≥ **4.2** (generator `Visual Studio 18 2026`) |
| Git | clone repo đầy đủ (kèm `ShowCue/JUCE`) | giống macOS |

## Clone

```bash
git clone https://github.com/hayatuan/dgpcorp.git
cd dgpcorp
```

Repo chứa JUCE vendored tại `ShowCue/JUCE` — **không** cần cài JUCE riêng.

## CMake Presets (Phase A)

Repo có `CMakePresets.json` ở root — dùng chung trên macOS và Windows (CMake Tools / CLI).

| Preset | Mục đích |
|--------|----------|
| `macos-debug` | Dev Debug (Universal Binary) |
| `macos-release` | Dev Release |
| `windows-debug` | Windows Debug (VS 2026 x64) |
| `windows-release` | Windows Release (VS 2026 x64) |
| `windows-release-vs2022` | Windows Release (VS 2022 x64) |
| `macos-ci` / `windows-ci` | CI: tắt IDE index, build app + tests |

```bash
# macOS — configure + build + test
cmake --preset macos-release
cmake --build --preset macos-release
ctest --preset macos-release --output-on-failure
```

```bat
REM Windows — configure + build + test
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release -C Release --output-on-failure
```

Tắt unit tests khi configure: `-DSHOWCUE_BUILD_TESTS=OFF`.

### Parity macOS / Windows (Phase B)

Trước release, chạy checklist **[PLATFORM_QA.md](PLATFORM_QA.md)** trên **cả hai** OS.

Điểm khác biệt có chủ đích:

| Tính năng | macOS | Windows |
|-----------|-------|---------|
| Chuyển BGM list | ⌘ + số/chữ | Ctrl + số/chữ |
| Chuyển Cue list | ^ + số/chữ | Alt + số/chữ |
| ffmpeg thiếu | Homebrew / brew.sh | winget / trang tải |
| Resources (icon, QR) | `.app/Contents/Resources/` | `Resources/` cạnh `.exe` |

Script ffmpeg dev (Windows): `powershell -ExecutionPolicy Bypass -File ShowCue/scripts/setup-thirdparty-win.ps1`

### Autosave, backup & export (Phase C nhẹ)

| Tính năng | Mô tả |
|-----------|--------|
| `projectSchema` | Phiên bản JSON config (hiện tại `1`) |
| Autosave | ~5 phút → lưu atomically |
| `backups/` | Tối đa 12 snapshot `config-*` + `project-*` |
| **Xuất / Nhập cấu hình** | Menu Tệp / Apple menu — file `.showcue` (zip `manifest.json` + `config.json` + `project.xml`) |

Rig backup 2 máy (không code): [BACKUP_RIG.md](BACKUP_RIG.md)

### OSC In (điều khiển LAN, mặc định tắt)

Bật tạm khi dev: `SHOWCUE_OSC_ENABLE=1 ./ShowCue` hoặc `oscEnabled=true` trong `ShowCue.settings`.

| OSC | Hành động |
|-----|-----------|
| `/showcue/panic` | Panic fade all |
| `/showcue/go` + `listIndex`, `padIndex` | GO pad |

Port mặc định **9000** (`oscPort` / `backupSyncPort` trong settings).

### Backup Primary/Backup (B1)

Tab **Cài đặt → Mạng**: vai trò Primary/Backup, IP peer, cổng UDP, Follower lock, Takeover. Chi tiết: [BACKUP_RIG.md](BACKUP_RIG.md).

### CI (GitHub Actions)

Workflow `.github/workflows/showcue-ci.yml` chạy trên **macos-latest** và **windows-latest**:

1. `cmake --preset macos-ci` / `windows-ci`
2. Build `ShowCue` + `ShowCueTests`
3. `ctest --preset …`

## Build macOS (thủ công)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target ShowCue -j
```

App: `build/ShowCue/ShowCue_artefacts/Debug/ShowCue.app`

Release:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target ShowCue -j
```

Universal Binary (Intel + Apple Silicon) được bật tự động qua root `CMakeLists.txt`.

## Build Windows (Visual Studio)

Thư mục dự án ví dụ: `D:\app\dgpcorp` (root repo — cùng cấu trúc với macOS).

Mở **x64 Native Tools Command Prompt** (VS 2026 hoặc VS 2022) hoặc PowerShell, `cd` vào root repo:

```bat
cd /d D:\app\dgpcorp
```

### Visual Studio 2026 (khuyến nghị nếu máy chỉ có VS 18)

Cần **CMake ≥ 4.2**. Kiểm tra: `cmake --version`

```bat
cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64
cmake --build build-win --config Release --target ShowCue -j
```

### Visual Studio 2022

```bat
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target ShowCue -j
```

Exe: `build-win\ShowCue\ShowCue_artefacts\Release\ShowCue.exe`

Debug:

```bat
cmake --build build-win --config Debug --target ShowCue -j
```

### Cursor trên Windows

1. Mở folder `D:\app\dgpcorp` trong Cursor, đồng bộ git với máy Mac.
2. Cài workload **Desktop development with C++** trong VS 2026.
3. Nếu CMake Tools không nhận VS 2026, thêm `.vscode/settings.json`:

```json
{
  "cmake.generator": "Visual Studio 18 2026",
  "cmake.configureSettings": {
    "CMAKE_GENERATOR_PLATFORM": "x64"
  }
}
```

4. Configure một lần bằng lệnh ở trên, rồi build **Release** (multi-config — không dùng `-DCMAKE_BUILD_TYPE=Release` khi configure).

### ffmpeg trên Windows

- Nếu có `ShowCue/ThirdParty/ffmpeg/win/ffmpeg.exe` → CMake copy vào cùng thư mục với `ShowCue.exe`.
- Nếu không: cài [ffmpeg](https://ffmpeg.org/download.html), thêm vào `PATH`, hoặc copy `ffmpeg.exe` cạnh `ShowCue.exe` thủ công (cần cho trích audio từ video).

## Cursor / clangd (tùy chọn)

Sau build, `compile_commands.json` được sync vào `ShowCue/` (local only, không commit).

### IDE index (P4)

`ShowControlIDEIndex.cpp` chỉ để clangd index header-only — **không** nằm trong target `ShowCue`.

| Target | Mục đích |
|--------|----------|
| `ShowCue` | App phát hành (Release/Debug) |
| `ShowCueIDEIndex` | TU tùy chọn cho clangd (`EXCLUDE_FROM_ALL`) |
| `showcue_ide_index` | Alias build index + sync DB |
| `sync_clangd_db` | Copy `compile_commands.json` + `compile_flags.txt` |

```bash
# Build app (không compile IDE index)
cmake --build build --target ShowCue -j

# Index đủ header cho clangd
cmake --build build --target showcue_ide_index -j
```

Tắt hoàn toàn target index khi configure: `-DSHOWCUE_BUILD_IDE_INDEX=OFF`.

## Kiểm tra nhanh sau build

1. Mở app, thêm file WAV/MP3
2. Cue list / Pad grid
3. Preferences → Âm thanh + Giao diện
4. Trim Editor (double-click waveform) + cắt nhạc
5. Lưu/mở lại project (`~/ShowCue_Project.dat`)

## Phát hành GitHub Release

Tag: `v1.0.0`

| Nền tảng | Gói đề xuất |
|----------|-------------|
| macOS | `.dmg` hoặc `.zip` (Universal) |
| Windows | `.zip` chứa `ShowCue.exe` + `ffmpeg.exe` |

Updater đọc: `https://api.github.com/repos/hayatuan/dgpcorp/releases/latest`
