# ShowCue — Onboarding Windows cho thành viên mới

Hướng dẫn từng bước để clone, build và chạy **ShowCue** trên Windows 10/11.  
Repo: https://github.com/hayatuan/dgpcorp

Tài liệu build chi tiết hơn: [BUILD.md](BUILD.md)

---

## Checklist nhanh

```
[ ] Cài Git
[ ] Cài Visual Studio + workload "Desktop development with C++"
[ ] cmake --version OK (≥ 4.2 nếu dùng VS 2026)
[ ] git clone https://github.com/hayatuan/dgpcorp.git
[ ] git pull origin main
[ ] setup-thirdparty-win.ps1 (ffmpeg)
[ ] cmake --preset windows-debug
[ ] cmake --build --preset windows-debug
[ ] Chạy ShowCue.exe OK
[ ] Mở Cursor/VS Code tại thư mục root dgpcorp (không phải ShowCue/)
```

---

## Bước 1 — Yêu cầu hệ thống

| Thành phần | Ghi chú |
|------------|---------|
| OS | Windows 10/11, 64-bit |
| Ổ đĩa | ~5 GB trống (VS + build + ffmpeg) |
| Git | https://git-scm.com/download/win |
| Visual Studio | **2026** (VS 18) khuyến nghị, hoặc **2022** (VS 17) |
| CMake | ≥ **4.2** với VS 2026; ≥ **3.22** với VS 2022 |

### Cài Visual Studio

Workload bắt buộc: **Desktop development with C++** (MSVC, Windows SDK).

Kiểm tra sau khi cài:

```powershell
cmake --version
```

Nếu thiếu CMake: https://cmake.org/download/ hoặc `winget install Kitware.CMake`

---

## Bước 2 — Clone repository

```powershell
cd D:\APP
git clone https://github.com/hayatuan/dgpcorp.git
cd dgpcorp
git pull origin main
```

JUCE đã nằm trong `ShowCue/JUCE` — **không** cần cài JUCE riêng.

Kiểm tra:

```powershell
Test-Path ShowCue\JUCE\CMakeLists.txt
```

Kết quả phải là `True`.

---

## Bước 3 — Tải ffmpeg (khuyến nghị)

Cần cho trích audio từ video. Chạy **một lần** sau clone:

```powershell
cd D:\APP\dgpcorp
powershell -ExecutionPolicy Bypass -File ShowCue\scripts\setup-thirdparty-win.ps1
```

File được đặt tại `ShowCue\ThirdParty\ffmpeg\win\ffmpeg.exe`.  
CMake sẽ copy `ffmpeg.exe` cạnh `ShowCue.exe` khi build.

---

## Bước 4 — Mở project trong Cursor / VS Code

1. **File → Open Folder** → chọn thư mục **root** `dgpcorp` (cùng cấp với `CMakeLists.txt` gốc).
2. Cài extension **CMake Tools** (Microsoft).
3. Repo đã có `.vscode/settings.json` cấu hình sẵn cho VS 2026.

---

## Bước 5 — Configure và build

### Máy có Visual Studio 2026 (khuyến nghị)

```powershell
cd D:\APP\dgpcorp
cmake --preset windows-debug
cmake --build --preset windows-debug
```

Chạy app Debug:

```powershell
.\build\windows-debug\ShowCue\ShowCue_artefacts\Debug\ShowCue.exe
```

Build Release:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
```

Exe Release:

```powershell
.\build\windows-release\ShowCue\ShowCue_artefacts\Release\ShowCue.exe
```

### Máy chỉ có Visual Studio 2022

```powershell
cmake --preset windows-release-vs2022
cmake --build --preset windows-release-vs2022
```

### Build thủ công (không dùng preset)

VS 2026:

```powershell
cmake -S . -B build-win -G "Visual Studio 18 2026" -A x64
cmake --build build-win --config Debug --target ShowCue
```

VS 2022:

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target ShowCue
```

### Trong Cursor (CMake Tools)

1. **CMake: Select Configure Preset**
   - VS 2026 → **Windows Debug (VS 2026)** hoặc **Windows Release (VS 2026)**
   - VS 2022 → **Windows Release (VS 2022)**
2. **CMake: Configure**
3. **CMake: Build** — target `ShowCue`

---

## Bước 6 — Xác nhận build thành công

```powershell
Test-Path .\build\windows-debug\ShowCue\ShowCue_artefacts\Debug\ShowCue.exe
Test-Path .\build\windows-debug\ShowCue\ShowCue_artefacts\Debug\ffmpeg.exe
```

Mở `ShowCue.exe` — cửa sổ app hiện ra, không thoát ngay.

---

## Bước 7 — Đồng bộ code với team

**Trước khi làm việc mỗi ngày:**

```powershell
cd D:\APP\dgpcorp
git pull origin main
```

**Khi được yêu cầu đẩy code:**

```powershell
git status
git add <các file đã sửa>
git commit -m "mô tả ngắn gọn"
git push origin main
```

Luôn `git pull` trước khi sửa để tránh conflict với máy Mac / máy Windows khác.

---

## Bước 8 — Giảm lỗi đỏ trong Problems (clangd)

Generator Visual Studio không tạo `compile_commands.json` đầy đủ cho IDE. Chạy:

```powershell
powershell -ExecutionPolicy Bypass -File ShowCue\scripts\refresh-ide-db-win.ps1
```

Trong Cursor: **Command Palette** → `clangd: Restart language server`

---

## Lỗi thường gặp

| Triệu chứng | Cách xử lý |
|-------------|------------|
| `Visual Studio 17 2022 could not find any instance` | Máy không có VS 2022 — dùng preset **VS 2026** hoặc cài VS 2022 |
| Configure fail sau đổi preset | **CMake: Delete Cache and Reconfigure** hoặc xóa thư mục `build/windows-debug` |
| `cmake` không được nhận | Cài CMake, mở lại terminal |
| Build lâu lần đầu (~2–5 phút) | Bình thường — JUCE build `juceaide` |
| Không trích audio từ video | Chạy `setup-thirdparty-win.ps1`, build lại |
| Tab Mạng hiển thị IP sai (VMware, Hyper-V…) | `git pull origin main` — bản mới ưu tiên Wi‑Fi có gateway |

---

## Khác biệt phím tắt so với macOS

| Thao tác | macOS | Windows |
|----------|-------|---------|
| Chuyển BGM list | ⌘ + số/chữ | **Ctrl** + số/chữ |
| Chuyển Cue list | ^ + số/chữ | **Alt** + số/chữ |

---

## Liên hệ / tài liệu thêm

| Tài liệu | Nội dung |
|----------|----------|
| [BUILD.md](BUILD.md) | Build macOS/Windows, CI, clangd |
| [BACKUP_RIG.md](BACKUP_RIG.md) | Cấu hình Primary/Backup 2 máy |
| [PLATFORM_QA.md](PLATFORM_QA.md) | Checklist QA đa nền tảng |
