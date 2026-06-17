# ShowCue v1.0.0 — Hướng dẫn build

Ứng dụng cue-based show control (JUCE 8, C++17). Hỗ trợ **macOS 11+** và **Windows 10/11**.

## Yêu cầu chung

| Thành phần | macOS | Windows |
|------------|-------|---------|
| CMake | ≥ 3.22 | ≥ 3.22 |
| Compiler | Xcode CLT hoặc Xcode | Visual Studio 2022 (Desktop C++) |
| Git | clone repo đầy đủ (kèm `ShowCue/JUCE`) | giống macOS |

## Clone

```bash
git clone https://github.com/hayatuan/dgpcorp.git
cd dgpcorp
```

Repo chứa JUCE vendored tại `ShowCue/JUCE` — **không** cần cài JUCE riêng.

## Build macOS

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

Mở **x64 Native Tools Command Prompt for VS 2022** hoặc PowerShell:

```bat
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target ShowCue -j
```

Exe: `build-win\ShowCue\ShowCue_artefacts\Release\ShowCue.exe`

Debug:

```bat
cmake --build build-win --config Debug --target ShowCue -j
```

### ffmpeg trên Windows

- Nếu có `ShowCue/ThirdParty/ffmpeg/win/ffmpeg.exe` → CMake copy vào cùng thư mục với `ShowCue.exe`.
- Nếu không: cài [ffmpeg](https://ffmpeg.org/download.html), thêm vào `PATH`, hoặc copy `ffmpeg.exe` cạnh `ShowCue.exe` thủ công (cần cho trích audio từ video).

## Cursor / clangd (tùy chọn)

Sau build, `compile_commands.json` được sync vào `ShowCue/` (local only, không commit).

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
