# ShowCue — Rig Backup 2 máy

Hướng dẫn vận hành **Primary + Backup** với đồng bộ live qua LAN (UDP/OSC). Mỗi máy phát audio từ file local — không stream nhạc qua mạng.

## Nguyên tắc

1. **Mỗi máy phát audio từ file local** — không stream cue qua WiFi.
2. **Cả hai cắm vào mixer** — Backup thường **mute** FOH cho đến khi takeover.
3. **Cùng project + media** trên cả hai máy trước khi vào show.
4. **LAN có dây** cho đồng bộ; WiFi chỉ phụ.

## Cấu hình phần mềm (B1)

**Cài đặt → tab Mạng**

| Vai trò | Mô tả |
|---------|--------|
| **Độc lập** | Không đồng bộ (OSC tùy chọn) |
| **Máy chính (Primary)** | Gửi GO / Panic / Stop / Pause sang máy phụ |
| **Máy phụ (Backup)** | Nhận lệnh (Follower), khóa điều khiển local |

| Trường | Ghi chú |
|--------|---------|
| **IP máy đối tác** | Primary: IP của Backup. Backup: IP Primary (cho takeover ping) |
| **Cổng UDP** | Mặc định **9000** (cùng port trên cả hai máy) |
| **Khóa Follower** | Backup không GO/Panic local cho đến khi **Takeover** |
| **Bật nhận OSC** | Tự bật khi chọn Primary/Backup |

Thanh trạng thái trên Master Deck: `Máy chính → IP` / `Máy phụ — Follower` / cảnh báo mất heartbeat.

**Mirror từ Primary sang Backup:** vị trí chọn (chuột, mũi tên, chuyển list Ctrl/Alt+số), chế độ Pad/Cue list, GO/Stop/Panic/Pause qua OSC. Phím **S** (stop all) trên grid cũng được gửi sang Backup.

## Chuẩn bị

| Bước | Primary (FOH) | Backup |
|------|---------------|--------|
| Cài ShowCue cùng version | ☐ | ☐ |
| **Nhập file cấu hình** hoặc copy media + project | ☐ | ☐ |
| Cùng sample rate / buffer (Preferences → Âm thanh) | ☐ | ☐ |
| Rehearsal: GO trên Primary, Backup mute, kiểm tra sync | ☐ | ☐ |
| Primary: vai trò **Máy chính**, IP = Backup | ☐ | |
| Backup: vai trò **Máy phụ**, Follower bật | ☐ | ☐ |

## Đồng bộ project

**Trước show / sau chỉnh sửa lớn:**

1. Primary: **Xuất file cấu hình** (`.showcue`)
2. Copy `.showcue` + thư mục media sang Backup
3. Backup: **Nhập file cấu hình**

## Trong show

### Primary vận hành bình thường

Mọi GO, Panic, Stop All, Pause All trên Primary được **gửi live** sang Backup.

### Backup (Follower)

- Nhận lệnh, phát từ file local (output **mute** hoặc input dự phòng trên mixer).
- Không bấm GO local (bị khóa nếu Follower lock bật).
- Nếu Primary crash: **Takeover** trong Cài đặt → Mạng, unmute mixer, tiếp tục.

### Heartbeat

Primary gửi heartbeat mỗi ~1s. Backup hiển thị **Mất kết nối Primary** sau ~3.5s không nhận tín hiệu.

## Protocol OSC (LAN)

| Địa chỉ | Ý nghĩa |
|---------|---------|
| `/showcue/sync/go` | `listIndex`, `padIndex`, `preWaitMs` |
| `/showcue/sync/panic` | Panic fade all |
| `/showcue/sync/stopAll` | Dừng tất cả |
| `/showcue/sync/pauseAll` | Pause tất cả pad grid |
| `/showcue/sync/stopCue` | `listIndex`, `padIndex` |
| `/showcue/sync/pauseCue` | `listIndex`, `padIndex` |
| `/showcue/sync/heartbeat` | `sequence` |
| `/showcue/sync/takeover` | `0/1` |

Legacy (standalone OSC): `/showcue/go`, `/showcue/panic`

## Cổng UDP & firewall

| Cổng | Giao thức | Mục đích |
|------|-----------|----------|
| **9000** | UDP (OSC) | Đồng bộ GO/Panic/Stop/heartbeat |
| **9001** | UDP (text) | Quét LAN, announce, ping peer |

**macOS:** bật **Quyền Mạng cục bộ** (Local Network) cho ShowCue — *System Settings → Privacy & Security → Local Network*.

**Windows:** Windows Defender thường **chặn UDP inbound** lần đầu — đặc biệt khi máy Windows là **Máy phụ** (phải *nhận* OSC cổng 9000 từ Primary). Khi bật vai trò **Máy chính** hoặc **Máy phụ**, ShowCue **tự thử** tạo rule firewall; nếu cần quyền Admin sẽ hiện hộp thoại **Cấp quyền (UAC)**. Hoặc chạy script thủ công:

```powershell
# PowerShell chạy quyền Administrator, từ root repo:
powershell -ExecutionPolicy Bypass -File ShowCue\scripts\setup-firewall-win.ps1
```

Hoặc thủ công: *Windows Security → Firewall → Allow an app* → bật **ShowCue** cho **Private network** (không chỉ Public).

**Lưu ý:** Cả hai máy phải cùng subnet LAN (Wi‑Fi/Ethernet thật, tránh chỉ qua VPN/VMware). Primary và Backup dùng **cùng cổng** trong Cài đặt → Mạng.

## Khắc phục kết nối chập chờn / treo UI

| Triệu chứng | Nguyên nhân thường gặp | Cách xử lý |
|-------------|------------------------|------------|
| Backup báo mất Primary | Firewall chặn UDP 9000, Mac chưa cấp Local Network | Mở firewall + quyền Mac |
| Quét LAN không thấy máy | Subnet khác, adapter ảo (VMware) | Dùng Wi‑Fi/Ethernet có gateway; tắt VPN |
| UI đơ / chập chờn khi Win làm **Máy phụ** | Firewall inbound + selection sync nặng | Chạy `setup-firewall-win.ps1`; build bản mới (bind Wi‑Fi, debounce selection) |
| Sync GO trễ | Wi‑Fi yếu | Ưu tiên Ethernet cho máy Primary |

Heartbeat: Primary gửi mỗi **~800 ms**; Backup coi là mất sau **~6 s** không nhận.

## Hardware failover (tùy chọn)

PlayAUDIO12 hoặc tương đương: hai USB vào interface failover — đổi máy không đổi cable. ShowCue B1 giữ playhead khớp qua LAN.

## Checklist trước show

- [ ] Cùng bản app + media
- [ ] Primary/Backup role + IP đúng
- [ ] Backup output muted
- [ ] Test GO + Panic sync
- [ ] Test Takeover trên Backup (rehearsal)

## Lộ trình tiếp (B2)

| Giai đoạn | Nội dung |
|-----------|----------|
| B2 | Snapshot project khi nối, hash media, failover UI nâng cao |

*Tham chiếu: QLab Cookbook “Main or Backup”, iConnectivity PlayAUDIO12.*
