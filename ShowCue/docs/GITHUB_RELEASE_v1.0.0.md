# GitHub Release — ShowCue v1.0.0

Hướng dẫn nội bộ. Copy phần **Release body** bên dưới vào GitHub.

| Mục | Giá trị |
|-----|---------|
| **Title** | `ShowCue v1.0.0` |
| **Tag** | `v1.0.0` |
| **Target branch** | `main` |

## Assets đính kèm (4 file)

| File | Nền tảng |
|------|----------|
| `ShowCue-v1.0.0-macOS-universal.dmg` | macOS — installer (khuyến nghị) |
| `ShowCue-v1.0.0-macOS-universal.zip` | macOS — portable |
| `ShowCue-Setup-1.0.0.exe` | Windows — installer (khuyến nghị) |
| `ShowCue-v1.0.0-Windows.zip` | Windows — portable |

**Không upload:** `ShowCue.app` lẻ, folder `ShowCue-v1.0.0-Windows` (đã đóng gói trong `.zip` / `.exe`).

---

## Release body

Copy từ dòng `# ShowCue v1.0.0` đến hết (bỏ khung ` ```markdown `).

```markdown
# ShowCue v1.0.0 — Bản phát hành đầu tiên

**ShowCue** là ứng dụng điều khiển cue âm thanh cho sân khấu và sự kiện — pad grid, cue list, fade, loop, routing đa bus, loudness và EQ tích hợp.

Giao diện **Tiếng Việt / English**, theme **sáng / tối**. Hỗ trợ **macOS** và **Windows**.

---

## Tải về

### macOS 11+ (Universal — Apple Silicon & Intel)

| File | Mô tả |
|------|--------|
| **ShowCue-v1.0.0-macOS-universal.dmg** | Cài đặt — kéo vào Applications *(khuyến nghị)* |
| **ShowCue-v1.0.0-macOS-universal.zip** | Portable — giải nén và chạy `ShowCue.app` |

### Windows 10/11 (x64)

| File | Mô tả |
|------|--------|
| **ShowCue-Setup-1.0.0.exe** | Installer *(khuyến nghị)* |
| **ShowCue-v1.0.0-Windows.zip** | Portable — giải nén, chạy `ShowCue.exe` |

---

## Cài đặt

### macOS (quan trọng)

Bản **v1.0.0** chưa ký **Developer ID** và chưa **notarize** (cần Apple Developer Program $99/năm). Lần đầu mở, macOS có thể cảnh báo bảo mật — **bình thường**, app không qua Mac App Store.

1. Mở `.dmg`, kéo **ShowCue** vào **Applications** (hoặc giải nén `.zip`).
2. **Chuột phải** vào `ShowCue.app` → **Mở** → bấm **Mở** lần nữa.
3. Hoặc: **System Settings → Privacy & Security → Open Anyway**.

Sau lần đầu, mở app bình thường (double-click).

### Windows

1. Chạy **ShowCue-Setup-1.0.0.exe** và làm theo wizard.
2. Hoặc giải nén **ShowCue-v1.0.0-Windows.zip**, chạy `ShowCue.exe`.

Nếu SmartScreen cảnh báo: bấm **More info** → **Run anyway** (app chưa có chứng chỉ code signing thương mại).

---

## Tính năng chính

- **Pad grid & Cue list** — GO, fade, loop, hotkey, panic
- **Master deck** — VU meter (post-limiter)
- **Inspector** — trim IN/OUT, EQ 6-band, đồng bộ loudness
- **Trim Editor** — resize ngang, quét chọn vùng + cắt nhạc, undo/redo
- **Loudness Manager** — đo & áp dụng loudness cho cả list
- **Preferences** — thiết bị audio, đặt tên output bus, theme, ngôn ngữ
- **Auto-update** — kiểm tra bản mới qua GitHub Releases

---

## Yêu cầu hệ thống

| Nền tảng | Yêu cầu |
|----------|---------|
| **macOS** | 11.0+, Universal Binary (arm64 + x86_64) |
| **Windows** | 10/11, x64 |

---

## Ghi chú kỹ thuật

- Framework: JUCE 8
- Bundle ID (macOS): `com.dgpco.showcue`
- `ffmpeg` được bundle (xử lý / chuyển đổi audio)

---

## English

### ShowCue v1.0.0 — First public release

**ShowCue** is a stage and live-event audio cue controller — pad grid, cue list, fades, loops, multi-bus routing, built-in loudness and EQ.

**Vietnamese / English** UI, **light / dark** theme. Available for **macOS** and **Windows**.

### Downloads

**macOS 11+ (Universal — Apple Silicon & Intel)**
- **ShowCue-v1.0.0-macOS-universal.dmg** — drag to Applications *(recommended)*
- **ShowCue-v1.0.0-macOS-universal.zip** — portable

**Windows 10/11 (x64)**
- **ShowCue-Setup-1.0.0.exe** — installer *(recommended)*
- **ShowCue-v1.0.0-Windows.zip** — portable (`ShowCue.exe`)

### Installation

**macOS:** Not Developer ID signed / not notarized yet. **Right-click** `ShowCue.app` → **Open** on first launch, or use **System Settings → Privacy & Security → Open Anyway**.

**Windows:** Run the installer, or unzip the portable package. If SmartScreen warns, choose **More info** → **Run anyway**.

### Highlights

- Pad grid & cue list — GO, fade, loop, hotkeys, panic
- Master deck with post-limiter VU meter
- Inspector — trim IN/OUT, 6-band EQ, loudness sync
- Trim Editor — horizontal resize, region select + audio cut, undo/redo
- Loudness Manager — measure and apply loudness across the cue list
- Preferences — audio device, output bus naming, light/dark theme, VI/EN UI
- Auto-update via GitHub Releases

### System requirements

- **macOS:** 11.0+, Universal Binary (arm64 + x86_64)
- **Windows:** 10/11, x64

---

**Phản hồi / báo lỗi:** [GitHub Issues](https://github.com/hayatuan/dgpcorp/issues)

*macOS Developer ID signing + notarization planned for a future release after joining the Apple Developer Program.*
```
