# Phase 1 — Nguồn tài liệu và sai sót trong tài liệu Hi-Link

Tài liệu này ghi lại **những gì đã đọc**, **lấy ở đâu**, **phiên bản nào**, và **các chỗ tài liệu Hi-Link tự mâu thuẫn**. Mọi khẳng định gắn nhãn **[TL]** trong `giao-thuc-ld2410.md`, `giao-thuc-ld2450.md` và `../thiet-ke-tong-the.md` đều truy về một dòng trong bảng dưới.

Quy ước nhãn dùng chung cho toàn bộ tài liệu của dự án:

| Nhãn | Ý nghĩa |
|---|---|
| **[TL]** | Đã được tài liệu chính thức của Hi-Link xác nhận (ghi tên tài liệu, phiên bản, mục) |
| **[CĐ]** | Nguồn cộng đồng / mã nguồn mở (ESPHome, diễn đàn Home Assistant...). Có giá trị tham khảo, **không** phải Hi-Link xác nhận |
| **[GT]** | Giả thuyết, cần kiểm chứng bằng thí nghiệm (ghi mã thí nghiệm, ví dụ E6) |
| **[ĐX]** | Phương án thiết kế đề xuất của dự án |
| **[ĐO]** | Đã đo trên phần cứng thật trong dự án (chưa có mục nào ở thời điểm viết: Phase 1 chưa chạy trên radar thật) |

## 1. Tài liệu chính thức đã đọc

Các file PDF lấy từ các thư mục Google Drive mà trang sản phẩm của Hi-Link (`hlktech.net/index.php?id=1157`) liên kết tới, trừ khi ghi khác. **PDF không được đưa vào repo** (bản quyền Hi-Link): tải lại từ các liên kết dưới, so khớp bằng SHA-256 (16 ký tự đầu) để chắc là cùng bản.

| Tài liệu | Phiên bản / ngày | Trang | SHA-256 (đầu) | Nguồn |
|---|---|---|---|---|
| HLK-LD2450 Serial Communication Protocol | V1.03, 2023-10-17 | 14 | `9725e6c2408e7de5` | Drive "HLK-LD2450 Human body tracking detection" (`drive.google.com/drive/folders/1kTt0Z3hjKKrIF3OCIDGdwQ4KotDJ8SGA`) |
| HLK-LD2450 1T2R motion target detection and tracking module manual | V1.00, 2023-05-10 | 23 | `dd4d44fffa50d74f` | cùng thư mục |
| HLK-LD2450 operation manual (hướng dẫn thao tác + FAQ) | không ghi | 16 | `b73ba2f61afa3ae3` | cùng thư mục |
| 2450 serial port parsing code representation | không ghi | 1 | `196767dbb1bf23f8` | cùng thư mục |
| HLK-LD2450 Instruction manual (bản sao) | V1.00, 2023-05-10 | 22 | `a8da22dcc403caf8` | `elemon.com.ar` (bản sao của nhà phân phối, nội dung trùng bản trên) |
| LD2410B Serial communication protocol | V1.07 (bảng sửa đổi tới V1.04 + phần 2.2.18–2.2.21) | 23 | `165b54e3b1749ade` | Drive "LD2410B" (`drive.google.com/drive/folders/16zI-fium_BZeP08EyQke0rWp0BJTMvw3`) |
| LD2410B V2.44 (24073110) — introduction of new features | firmware V2.44.24073110 | 5 | `174f5199a7417484` | cùng thư mục |
| HLK LD2410B Life Presence Sensing Module Manual | V1.04 | 17 | `aa8758ef74f7e8f8` | cùng thư mục |
| HLK-LD2410C Serial communication protocol | ghi V1.07 (2024-08-05), bảng sửa đổi tới **V1.08 (2024-11-22)** | 27 | `3910e3088cdd589a` | Drive "HLK-LD2410C" (`drive.google.com/drive/folders/1ypOlacBmmFXY6lDQ0f1hEJFmczNe-0WG`) |
| HLK LD2410C Life Presence Sensor Module Data Sheet | V1.00 | 19 | `64582bca6f55d0c0` | cùng thư mục |
| HLK-LD2410 Serial Communication Protocol | V1.02 | 22 | `da3b7ff70a01b694` | Drive "HLK-LD2410" (`drive.google.com/drive/folders/1lCQv3mfHJ3XKXzweeHPFnJ_8_D_EWEKk`) |
| HLK-LD2410 user manual | V1.02 | 15 | `b899b8a555ac3ca6` | cùng thư mục |
| HLK-LD2410C Serial communication protocol | V1.00, 2022-11-07 | 23 | `041644df21dfea67` | bản sao `sudo.is/docs/esphome/components/ld2410/` |
| HLK-LD2410B Serial communication protocol | V1.03 (tên file ghi v1.04), 2022-07-19 | 19 | `30c6d409f00c443a` | bản sao `sudo.is/docs/esphome/components/ld2410/` |

Ghi chú:
- Tài liệu dạng PDF này mô tả **giao thức và thông số**, không mô tả thuật toán xử lý tín hiệu bên trong radar. Mọi suy luận về ý nghĩa vật lý của "energy" đều là **[GT]**.
- Repo này đã có driver LD2410 cho ESP32-H2/C3 (`ESP32-H2/matter-presence-ld2410-learning`), viện dẫn thêm "LD2410C V1.09 mục 2.2.20–2.2.21" cho lệnh 0x000B. Bản V1.09 không có trong các thư mục trên ở thời điểm đọc; nội dung lệnh 0x000B/0x001B đã được xác nhận lại từ LD2410B V1.07 và LD2410C V1.07/V1.08.

## 2. Sai sót và mâu thuẫn trong tài liệu Hi-Link

Các điểm dưới được phát hiện khi đối chiếu ví dụ byte với mô tả chữ. Cách xử lý của dự án được ghi ở cột cuối; các vector test `test/vectors/hlk_vectors.txt` dùng đúng các ví dụ byte của tài liệu.

| # | Tài liệu, mục | Mâu thuẫn | Dự án xử lý |
|---|---|---|---|
| 1 | LD2410C V1.00 §2.2.16, LD2410B V1.07 §2.2.16 | Chữ ghi "Factory default value is 0x0001, which is 0.75m", nhưng bảng ghi 0x0000 = 0,75 m, 0x0001 = 0,2 m | Theo bảng: 0x0000 = 0,75 m (trùng ESPHome và driver H2/C3 của repo) |
| 2 | LD2410C V1.00 §2.2.16 | ACK ví dụ của lệnh 0x00AA in `A1 01` | Phải là `AA 01` (lệnh \| 0x0100) |
| 3 | LD2410 các bản §2.2.13, LD2450 V1.03 §2.2.11 | Chữ: "1 byte fixed type (0x00) + 3 bytes MAC"; ví dụ trả 6 byte MAC, độ dài ACK 0x000A khớp 6 byte | Giải mã 6 byte |
| 4 | LD2410C V1.00 §2.2.8 | Byte ví dụ `07 01 16 15 09 22` được in thành "V1.07.22091615"; đọc little-endian là 0x22091516 | In `V1.07.22091516`. Tài liệu LD2450 V1.03 §2.2.6 in đúng kiểu little-endian (`16 24 06 22` → "V1.02.22062416"), ESPHome cũng vậy |
| 5 | LD2410C V1.07/V1.08 §2.3.2 | Ví dụ byte engineering kết thúc bằng `03 05`, nhưng phần "Example analysis" ghi `60 photosensitive 01 out status` (chép từ tài liệu LD2410B) và chỉ liệt kê 8 giá trị cho mỗi nhóm năng lượng | Tin cấu trúc (trường N, độ dài khung) hơn ví dụ; cả hai ví dụ đều có trong vector test |
| 6 | Manual LD2410/LD2410B/LD2410C | Phần giới thiệu: "farthest sensing distance can reach 5 meters"; bảng thông số: "Detection distance 0.75 m ~ 6 m, adjustable" | Coi 5 m là tầm "đáng tin" theo nhà sản xuất, 6 m là giới hạn cấu hình; đo thực tế ở E6 |
| 7 | LD2450 manual V1.00 §6 | "0x40 + 0x01 × 256 = 360 mm" (phép tính đúng là 320) | Protocol V1.03 sửa thành 320; FAQ của operation manual nói trường này "always 68 01" (360 mm), cố định, không cần dùng |
| 8 | LD2410 các bản §2.2.12, LD2450 V1.03 §2.2.10 | Chữ: "0x0100 turn on bluetooth"; ví dụ gửi byte `01 00` (= 0x0001 little-endian) | Gửi `01 00` để bật, `00 00` để tắt |
| 9 | LD2410C protocol "V1.07" | Trang bìa ghi V1.07 / 2024-08-05, bảng sửa đổi có V1.08 / 2024-11-22 | Gọi là "V1.07/V1.08" |
| 10 | LD2450 V1.03 §1.2.2 | "automatically searches for the LD2410 module" | Lỗi sao chép, không ảnh hưởng giao thức |
| 11 | LD2410 các bản | Không ghi tần suất gửi báo cáo | LD2450 ghi rõ 10 khung/s **[TL]**; LD2410 phải đo (E1). Driver H2/C3 của repo ghi nhận khoảng 10 báo cáo/s khi chạy thực tế |
| 12 | LD2410B/C manual | Không có "data refresh rate"; LD2450 manual có (10 Hz) | Như trên |

## 3. Nguồn cộng đồng đã tham khảo **[CĐ]**

| Nguồn | Nội dung dùng | Độ tin cậy |
|---|---|---|
| ESPHome `components/ld2410/ld2410.cpp`, `components/ld2450/ld2450.cpp` (nhánh `dev`, đọc 2026-10) | Offset byte, cách giải mã dấu của LD2450, cách đọc light/OUT, MAC giả `08:05:04:03:02:01` khi Bluetooth tắt, ACK truy vấn vùng có thể chứa chuỗi `04 03 02 01` trong toạ độ, quy ước speed dương = rời xa | Mã nguồn được dùng rộng rãi; vẫn cần đối chiếu dữ liệu thật |
| Diễn đàn Home Assistant: "Interference Issue Between LD2410 and LD2450 (Both on ESP32-S3)" | LD2410 báo nhầm khi LD2450 hoạt động, kể cả khi vùng phát hiện không chồng nhau; một người dùng báo LD2450 + LD2412 cách ~15 cm thì ổn, gần hơn thì nhiễu | Giai thoại, chưa có số đo |
| atomic14.com — trang HLK-LD2450 | "Weak at static presence beyond ~2 m", nên ghép với LD2410/LD2412 cho người ngồi; firmware v2.14.x cuối 2025 làm hỏng tích hợp ESPHome ở một số module | Bài đánh giá cá nhân |
| GitHub `EverythingSmartHome/everything-presence-lite` issue #391 | Firmware LD2450 v2.14.25112412 đổi baud rate (theo người bảo trì), parser báo khung sai độ dài cho tới khi khôi phục | Báo cáo lỗi, có xác nhận của người bảo trì sản phẩm thương mại dùng LD2450 |
| `aio-ld2410.readthedocs.io` | Danh sách tài liệu chính thức, quan sát trên LD2410C firmware v2.04.23022511 | Thư viện mã nguồn mở |

Hệ quả cho thiết kế: probe Phase 1 **tự dò baud rate** (`scan`), ghi **phiên bản firmware** của từng radar vào mọi bản ghi dữ liệu, và mặc định **không** tin vào ngữ nghĩa nào mà chỉ cộng đồng nêu cho tới khi đo được.

## 4. Phần mềm nền đã kiểm tra (cho các phase sau)

| Thành phần | Phiên bản | Yêu cầu ESP-IDF | Ghi chú |
|---|---|---|---|
| ESP-IDF | v6.0.2 | — | Repo đang dùng (CI `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`). Probe Phase 1 đã build với v6.0.2 cho esp32s3, không cảnh báo |
| esp-matter | release/v1.6 (Matter 1.6) | IDF v6.0.2 cho bản 1.6.1 | Giống các project Matter trong repo |
| esp-tflite-micro | 1.4.1 | IDF ≥ 5.1, cần `esp-nn` ≥ 1.3.0 | Chưa thử với IDF 6.0 **[GT]**, kiểm ở Phase 6 |
| ESP-DL | 3.3.13 | IDF ≥ 5.3 (S3) | Mô hình `.espdl` qua ESP-PPQ; chưa thử với IDF 6.0 **[GT]** |
| Matter Occupancy Sensing (0x0406) | spec hiện hành | — | Feature Radar (RAD), `HoldTime`, `HoldTimeLimits`; **không có thuộc tính "độ tin cậy"**. Bản master của connectedhomeip có feature `PRED`/`PredictedOccupancy` ở mức *provisional* (chưa dùng được cho sản phẩm) |
