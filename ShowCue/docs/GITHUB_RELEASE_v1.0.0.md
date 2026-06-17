# GitHub Release — ShowCue v1.0.0

Copy nội dung dưới đây vào **GitHub → Releases → v1.0.0 → Edit release**.

**Title:** `ShowCue v1.0.0`

**Tag:** `v1.0.0` (đã có trên repo)

**Assets đính kèm (macOS):**
- `ShowCue-v1.0.0-macOS-universal.dmg`
- `ShowCue-v1.0.0-macOS-universal.zip`

---

## Release body (Tiếng Việt + English)

```markdown
# ShowCue v1.0.0 — Bản phát hành đầu tiên

**ShowCue** là ứng dụng điều khiển cue âm thanh cho sân khấu và sự kiện — pad grid, cue list, fade, loop, routing đa bus, loudness và EQ tích hợp.

---

## Tải về

| Nền tảng | File | Ghi chú |
|----------|------|---------|
| **macOS 11+** | `.dmg` hoặc `.zip` (Universal: Apple Silicon + Intel) | Khuyến nghị dùng `.dmg` |
| **Windows 10/11** | *(sắp có)* | Build từ source — xem [BUILD.md](ShowCue/docs/BUILD.md) |

---

## Cài đặt trên macOS (quan trọng)

Bản **v1.0.0** chưa ký **Developer ID** và chưa **notarize** (cần Apple Developer Program $99/năm). Lần đầu mở, macOS có thể hiện cảnh báo bảo mật — **đây là bình thường**, app không qua Mac App Store.

**Cách mở lần đầu:**

1. Tải và mở file `.dmg`, kéo **ShowCue** vào **Applications** (hoặc giải nén `.zip`).
2. **Chuột phải** (hoặc Control + click) vào `ShowCue.app` → chọn **Mở**.
3. Trong hộp thoại, bấm **Mở** lần nữa.

**Hoặc:** **System Settings → Privacy & Security** → cuộn xuống → **Open Anyway** (nếu macOS chặn sau lần thử đầu).

Sau lần đầu, mở app như bình thường (double-click).

---

## Tính năng chính

- **Pad grid & Cue list** — GO, fade, loop, hotkey, panic
- **Master deck** — VU meter (post-limiter)
- **Inspector** — trim IN/OUT, EQ 6-band, đồng bộ loudness
- **Trim Editor** — resize ngang, quét chọn vùng + cắt nhạc, undo/redo
- **Loudness Manager** — đo & áp dụng loudness cho cả list
- **Preferences** — thiết bị audio, đặt tên output bus, theme sáng/tối, Tiếng Việt / English
- **Auto-update** — kiểm tra bản mới qua GitHub Releases

---

## Yêu cầu hệ thống

- **macOS:** 11.0 trở lên, Universal Binary (arm64 + x86_64)
- **Windows:** 10/11 x64 *(bản cài sẵn sắp bổ sung)*

---

## Ghi chú kỹ thuật

- Framework: JUCE 8
- Bundle ID: `com.dgpco.showcue`
- ffmpeg được bundle trong app (chuyển đổi / xử lý audio)

---

## English

### ShowCue v1.0.0 — First public release

**ShowCue** is a stage and live-event audio cue controller — pad grid, cue list, fades, loops, multi-bus routing, built-in loudness and EQ.

### Download

| Platform | File | Notes |
|----------|------|-------|
| **macOS 11+** | `.dmg` or `.zip` (Universal: Apple Silicon + Intel) | `.dmg` recommended |
| **Windows 10/11** | *(coming soon)* | Build from source — see [BUILD.md](ShowCue/docs/BUILD.md) |

### macOS install (important)

**v1.0.0** is not **Developer ID signed** or **notarized** yet (requires paid Apple Developer Program). On first launch, macOS may show a security warning — **this is expected**; the app is not distributed via the Mac App Store.

**First launch:**

1. Download and open the `.dmg`, drag **ShowCue** to **Applications** (or unzip the `.zip`).
2. **Right-click** (or Control + click) `ShowCue.app` → **Open**.
3. In the dialog, click **Open** again.

**Or:** **System Settings → Privacy & Security** → scroll down → **Open Anyway** (if macOS blocked after the first attempt).

After the first successful launch, open the app normally (double-click).

### Highlights

- Pad grid & cue list — GO, fade, loop, hotkeys, panic
- Master deck with post-limiter VU meter
- Inspector — trim IN/OUT, 6-band EQ, loudness sync
- Trim Editor — horizontal resize, region select + audio cut, undo/redo
- Loudness Manager — measure and apply loudness across the cue list
- Preferences — audio device, output bus naming, light/dark theme, Vietnamese / English UI
- Auto-update via GitHub Releases

### System requirements

- **macOS:** 11.0+, Universal Binary (arm64 + x86_64)
- **Windows:** 10/11 x64 *(installer build coming soon)*

---

Phản hồi / báo lỗi: mở [Issue](https://github.com/hayatuan/dgpcorp/issues) trên GitHub.

*Bản ký Developer ID + notarize dự kiến trong bản phát hành sau khi đăng ký Apple Developer Program.*
```
