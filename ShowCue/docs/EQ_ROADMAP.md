# Equalizer — Đề xuất phát triển (lưu cho sau v0.0.1)

Tài liệu này ghi lại đánh giá EQ hiện tại và roadmap đề xuất. **Không** nằm trong phạm vi phát hành 0.0.1 trừ các mục đã được đánh dấu hoàn thành trong RELEASE_0.0.1.md.

## Hiện trạng (v0.0.1)

| Thành phần | Mô tả |
|------------|--------|
| DSP | `PadParametricEq6` — 6 band IIR stereo, RT-safe (atomic + revision) |
| UI | `PadEqualizerDialog` — curve log Hz, kéo gain dọc, live apply |
| Chuỗi | `PadDspChain`: EQ → LUFS sync → bus → master limiter |
| Lưu | `dspEq`, `dspEqBand0…5` per pad (tương thích legacy 3-band) |

### 6 band cố định

| Band | Hz | Kiểu |
|------|-----|------|
| HP | 40 | High-pass (kéo xuống = cắt bass) |
| LS | 120 | Low shelf |
| P1 | 500 | Peak |
| P2 | 2000 | Peak |
| HS | 8000 | High shelf |
| LP | 18000 | Low-pass (kéo xuống = cắt treble) |

---

## Vấn đề đã biết

- [x] HP/LP dùng peak filter thay vì HPF/LPF thật → **đã sửa v0.0.1**
- [x] Tooltip EQ ghi "Bypass" trong khi nút là "Bật EQ" → **đã sửa v0.0.1**
- [x] Double-click node reset band về 0 dB → **đã sửa v0.0.1**
- [ ] Không có preset EQ
- [ ] Không A/B so sánh
- [ ] Không áp dụng EQ cho cả list
- [ ] Chỉ chỉnh gain, không chỉnh Hz/Q
- [ ] Không auto-trim sau boost (nguy cơ clip trước master limiter)
- [ ] Chỉnh EQ không tái phân tích loudness

---

## Phase A — Nhanh (1–2 ngày)

| # | Hạng mục | Ghi chú |
|---|----------|---------|
| A1 | **Preset EQ** | Tổng quát, Thoại/MC, Cắt rumble, Sáng vocal, EDM punch |
| A2 | **A/B nghe** | Bypass tạm, giữ setting (giống Loudness) |
| A3 | ~~Double-click reset band~~ | **Done v0.0.1** |
| A4 | **Output trim** | Tự giảm gain khi tổng boost vượt ngưỡng |
| A5 | **Footer UI** | Đồng bộ style với Loudness Manager |

---

## Phase B — Producer-grade (3–5 ngày)

| # | Hạng mục | Ghi chú |
|---|----------|---------|
| B1 | Kéo ngang tần số (P1/P2/LS/HS) | HP/LP chỉ kéo dọc |
| B2 | Nhập số gain từng band | Click label dưới curve |
| B3 | **Áp dụng cả list** | Callback `MainComponent` |
| B4 | Copy / Paste EQ giữa pad | Clipboard nội bộ |
| B5 | Spectrum overlay | FFT từ file đã load (offline) |
| B6 | Thứ tự EQ ↔ Loudness | Tùy chọn pre/post normalize |

---

## Phase C — Nâng cao (sau)

- Analyzer realtime khi phát
- Linear-phase EQ (FIR)
- EQ theo bus (FOH / monitor)
- Gợi ý EQ rule-based từ phân tích file (mud ~300 Hz, harsh ~4 kHz)

---

## Đa nền tảng & i18n

Mọi chuỗi UI EQ phải qua `showcontrol::localization::tr()`. Dialog EQ dùng `ShowTheme` cho dark/light. Không dùng API macOS-only trong dialog (drag window đã bọc `#if JUCE_MAC`).

---

*Cập nhật: chuẩn bị ShowCue v0.0.1*
