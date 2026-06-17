# ShowCue — Kiểm tra parity macOS / Windows (Phase B)

Checklist smoke test trước mỗi release. Chạy **cả hai nền tảng** khi có thay đổi UI, audio, persistence, hoặc shortcut.

**Build:** xem [BUILD.md](BUILD.md) · **CI:** `.github/workflows/showcue-ci.yml` (compile + unit tests)

---

## 1. Khởi động & shell

| # | Kiểm tra | macOS | Windows |
|---|----------|:-----:|:-------:|
| 1.1 | App mở không crash | ☐ | ☐ |
| 1.2 | Menu: Preferences, About, Check updates | ☐ (menu Apple + Chỉnh sửa) | ☐ (Tệp / Trợ giúp) |
| 1.3 | Phím tắt Preferences: **⌘,** / **Ctrl+,** | ☐ | ☐ |
| 1.4 | Theme sáng / tối, đổi ngôn ngữ VI/EN | ☐ | ☐ |
| 1.5 | About: icon + QR donate (nếu có `DonateQR.png`) | ☐ | ☐ |
| 1.6 | Dòng phiên bản: Universal Binary / Windows x64 | ☐ | ☐ |

---

## 2. Audio & thiết bị

| # | Kiểm tra | macOS | Windows |
|---|----------|:-----:|:-------:|
| 2.1 | Preferences → chọn soundcard, output bus | ☐ | ☐ |
| 2.2 | Play / stop pad WAV hoặc MP3 | ☐ | ☐ |
| 2.3 | VU meter Master deck | ☐ | ☐ |
| 2.4 | Fade, loop, panic | ☐ | ☐ |
| 2.5 | Preferences → Quyền: microphone grant | ☐ | ☐ |
| 2.6 | Win: không chạy Admin (drag-drop card cảnh báo) | — | ☐ |

---

## 3. Pad grid, cue list, shortcut

| # | Kiểm tra | macOS | Windows |
|---|----------|:-----:|:-------:|
| 3.1 | Kéo thả audio vào pad | ☐ | ☐ |
| 3.2 | Cue list: GO, reorder, scroll giữ vị trí | ☐ | ☐ |
| 3.3 | Hotkey pad (ma trận Farrago) | ☐ | ☐ |
| 3.4 | Chuyển BGM list: **⌘+1** / **Ctrl+1** | ☐ | ☐ |
| 3.5 | Chuyển Cue list: **^+1** / **Alt+1** | ☐ | ☐ |
| 3.6 | Sidebar hiển thị đúng prefix phím (⌘/^ hoặc Ctrl/Alt) | ☐ | ☐ |

---

## 4. Video / ffmpeg

| # | Kiểm tra | macOS | Windows |
|---|----------|:-----:|:-------:|
| 4.1 | Kéo thả video → pad audio (ffmpeg bundled) | ☐ | ☐ |
| 4.2 | Thiếu ffmpeg → dialog đúng nền tảng (brew vs winget) | ☐ | ☐ |
| 4.3 | `ffmpeg` / `ffmpeg.exe` cạnh binary sau build | ☐ | ☐ |

---

## 5. Inspector & editor

| # | Kiểm tra | macOS | Windows |
|---|----------|:-----:|:-------:|
| 5.1 | Trim IN/OUT, EQ, loudness sync | ☐ | ☐ |
| 5.2 | Trim Editor: resize, cắt, undo/redo | ☐ | ☐ |
| 5.3 | Loudness Manager | ☐ | ☐ |

---

## 6. Persistence & update

| # | Kiểm tra | macOS | Windows |
|---|----------|:-----:|:-------:|
| 6.1 | Lưu project, đóng/mở lại | ☐ | ☐ |
| 6.2 | Check for updates (mạng) — không crash offline | ☐ | ☐ |

---

## 7. Tự động (CI / local)

```bash
# macOS
cmake --preset macos-ci && cmake --build --preset macos-ci && ctest --preset macos-ci --output-on-failure

# Windows (x64 Native Tools / PowerShell)
cmake --preset windows-ci
cmake --build --preset windows-ci
ctest --preset windows-ci --output-on-failure
```

| # | Kiểm tra | Ghi chú |
|---|----------|---------|
| 7.1 | `ShowCue` build Release | CI matrix |
| 7.2 | `ShowCueTests` pass | semver, atomic save, keyboard routing |
| 7.3 | Không regression compile Win sau push | GitHub Actions |

---

## Ghi chú parity (Phase B)

- **Playlist shortcut:** macOS dùng ⌘ (grid) và ^ (cue); Windows dùng **Ctrl** (grid) và **Alt** (cue) vì JUCE gộp `commandModifier` với `ctrlModifier` trên Win.
- **ffmpeg:** macOS bundle `ffmpeg`; Windows bundle `ffmpeg.exe` + `Resources/` (icon, QR).
- **Menu:** macOS dùng menu Apple + Chỉnh sửa; Windows dùng Tệp / Chỉnh sửa / Trợ giúp — cùng lệnh, khác vị trí.

---

*Cập nhật: Phase B — parity macOS / Windows.*
