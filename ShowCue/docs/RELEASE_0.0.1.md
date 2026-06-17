# ShowCue v0.0.1 — Chuẩn bị phát hành

Checklist cho bản đầu tiên gửi người dùng. App hỗ trợ **Tiếng Việt / English**, **giao diện sáng / tối**, **macOS + Windows**.

## Đã xử lý trong nhánh hiện tại

### Loudness Manager
- [x] Dialog riêng, live apply, preview toàn list
- [x] Inspector: toggle + "Cài đặt nâng cao..." (ẩn nút RMS/LUFS/list cũ)
- [x] Áp dụng `LoudnessSettings` đầy đủ cho cả list
- [x] Layout dialog (preset/profile 1 hàng, footer nút, bảng preview)

### i18n (EN dictionary)
- [x] Bổ sung chuỗi Loudness Manager + Inspector nâng cao
- [x] Preset/profile loudness qua `localization::tr`
- [x] Chuỗi trạng thái preview (Đang đo…, v.v.)

### Tab Âm thanh (Preferences)
- [x] Nhóm "Cấu hình Audio" + JUCE device selector (Roboto LAF)
- [x] Nút "Đặt tên Output Bus" → popup lưới 6 bus
- [x] Đổi ngôn ngữ: cập nhật tên bus mặc định + popup đang mở
- [x] Đổi theme: refresh màu device selector + bus editors

### EQ (tối thiểu cho 0.0.1)
- [x] Sửa filter HP/LP (HPF/LPF thật khi cắt)
- [x] Sửa tooltip "Bật EQ" (không còn nhầm Bypass)
- [x] Double-click node EQ → reset band 0 dB
- [x] i18n chuỗi loudness inspector (Đang đo…, Chưa đo, Peak/Gain)
- [x] Roadmap EQ chi tiết → `docs/EQ_ROADMAP.md`

## Nên kiểm tra thủ công trước ship

| Khu vực | macOS | Windows |
|---------|-------|---------|
| Mở project, load audio | ☐ | ☐ |
| Cue list / pad grid / BGM | ☐ | ☐ |
| Loudness dialog + áp dụng list | ☐ | ☐ |
| EQ dialog + nghe live | ☐ | ☐ |
| Preferences → Tab Âm thanh (device + bus naming) | ☐ | ☐ |
| Đổi ngôn ngữ VI ↔ EN (Preferences) | ☐ | ☐ |
| Đổi theme sáng / tối | ☐ | ☐ |
| Lưu/mở lại project (loudness + EQ) | ☐ | ☐ |
| Hotkey / Space / GO / Panic | ☐ | ☐ |
| Multi-output / bus routing | ☐ | ☐ |

## Sau v0.0.1 (xem EQ_ROADMAP.md)

- Preset EQ, A/B, batch list
- Kéo Hz, spectrum overlay
- Auto-trim EQ output

---

*File này chỉ theo dõi nội bộ; cập nhật khi đóng milestone.*
