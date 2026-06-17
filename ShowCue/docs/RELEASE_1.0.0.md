# ShowCue v1.0.0 — Bản phát hành đầu tiên

Ứng dụng điều khiển cue âm thanh sân khấu. **VI/EN**, **sáng/tối**, **macOS + Windows**.

## Tính năng chính

- Pad grid / Cue list, GO, fade, loop, hotkey
- Master deck + VU meter (post-limiter)
- Inspector: trim IN/OUT, EQ 6-band, loudness sync
- **Trim Editor**: resize ngang, quét chọn + cắt nhạc, undo/redo
- Preferences: audio device, output bus naming, theme, ngôn ngữ, quyền hệ thống
- Auto-update qua GitHub Releases

## Checklist QA trước ship

| Khu vực | macOS | Windows |
|---------|-------|---------|
| Mở project, load audio | ☐ | ☐ |
| Cue list / pad grid / BGM | ☐ | ☐ |
| Loudness Manager + áp dụng list | ☐ | ☐ |
| EQ dialog + nghe live | ☐ | ☐ |
| Trim Editor + cắt nhạc + undo | ☐ | ☐ |
| Preferences (device, bus, theme, VI/EN) | ☐ | ☐ |
| Lưu/mở lại project | ☐ | ☐ |
| Hotkey / Space / GO / Panic | ☐ | ☐ |
| Multi-output / bus routing | ☐ | ☐ |
| Menu File/Edit/Help (Windows in-window) | — | ☐ |

## Build

Xem [BUILD.md](BUILD.md).

## Sau v1.0.0

- EQ roadmap: `docs/EQ_ROADMAP.md`
