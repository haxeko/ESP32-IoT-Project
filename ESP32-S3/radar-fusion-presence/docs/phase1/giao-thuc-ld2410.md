# Phase 1 — HLK-LD2410 / LD2410B / LD2410C: giao thức và năng lực dữ liệu đã kiểm chứng

Nhãn **[TL] [CĐ] [GT] [ĐX]**: xem `nguon-tai-lieu.md`. Tên viết tắt tài liệu: **P102** = LD2410 protocol V1.02, **B107** = LD2410B protocol V1.07, **C100** = LD2410C protocol V1.00, **C108** = LD2410C protocol V1.07/V1.08, **MB104** = LD2410B manual V1.04, **DC100** = LD2410C datasheet V1.00.

Mã nguồn tương ứng: `components/radar_proto/include/ld2410_proto.h`, `hlk_frame.h`; Python: `tools/hlk_proto.py`; vector test: `test/vectors/hlk_vectors.txt`.

## 1. Tóm tắt năng lực dữ liệu

| Dữ liệu | Có? | Nguồn |
|---|---|---|
| Trạng thái mục tiêu (không / chuyển động / đứng yên / cả hai) | Có, mọi báo cáo | [TL] P102, B107, C108 bảng "Target state" |
| Khoảng cách mục tiêu chuyển động và đứng yên (cm), năng lượng tổng (0–100) của mỗi loại, "detection distance" (cm) | Có, mọi báo cáo | [TL] bảng "Target basic information" |
| **Năng lượng moving và still theo từng distance gate (0–100), 9 gate** | Có, **chỉ khi bật engineering mode** (mất khi mất điện / restart) | [TL] P102 §2.2.5, bảng "Engineering mode target data" |
| Cảm biến ánh sáng (0–255) và trạng thái chân OUT trong báo cáo engineering | LD2410B từ V1.07, LD2410C từ V1.06 | [TL] B107 bảng 15, C108 sửa đổi V1.06. LD2410 đời đầu: 2 byte "retain data" không định nghĩa |
| Toạ độ X/Y, góc, vận tốc, số người | **Không**, chỉ có khoảng cách. Tài liệu không mô tả cấu hình anten (khác LD2450, ghi rõ 1T2R) | [TL] giao thức không có trường hay lệnh nào về góc/toạ độ |
| Tín hiệu thô (I/Q, phổ range-Doppler) | **Không** | [TL] |
| Thuật toán tính "energy", hằng số thời gian, cách gộp theo gate | **Không được công bố** | [TL] chỉ nói "target energy value (range 0 to 100)" |
| Checksum khung | **Không**. Byte "check" luôn là 0x00 | [TL] bảng "Intra-frame data" |
| Tần suất báo cáo | **Không ghi trong tài liệu** | đo ở E1; driver H2/C3 của repo quan sát ~10 Hz |

## 2. Phần cứng và điện **[TL]**

- Nguồn 5 V (LD2410C cho phép 5–12 V, khuyên 5 V — C100 bảng 1), nguồn cấp phải chịu **> 200 mA**; dòng trung bình **79 mA** (MB104, DC100), **80 mA** (manual LD2410 V1.02).
- Mức logic UART và chân OUT: **3,3 V** → nối thẳng ESP32-S3.
- UART mặc định **256000 baud, 8N1**; đổi được bằng lệnh 0x00A1 (9600 … 460800), có hiệu lực sau khi khởi động lại.
- Chân OUT: mức cao khi có người, thấp khi không (đảo được bằng lệnh 0x00AD trên B/C).
- Băng tần 24–24,25 GHz, FMCW, băng quét **250 MHz**, góc phát hiện **±60°**, "distance resolution 0.75 m", khoảng cách "0.75 m ~ 6 m, adjustable" (bảng) nhưng phần giới thiệu ghi "up to 5 meters" (MB104, DC100). Lắp được trên trần hoặc tường (MB104 §5.4).
- Bluetooth (LD2410B/C): bật mặc định, mật khẩu mặc định "HiLink"; bật lại bằng cách cắt/cấp nguồn > 5 lần trong 2–3 s (MB104 §6.3). App HLKRadarTool đọc/ghi thông số và **cập nhật firmware qua BLE** (tài liệu V2.44) → mặc định dự án **tắt Bluetooth của radar** [ĐX], như driver H2/C3 của repo.

## 3. Khung truyền **[TL]**

Little-endian. Hai loại khung trên đường TX của radar:

```
Báo cáo:  F4 F3 F2 F1 | LEN (2) | TYPE  AA  <dữ liệu mục tiêu>  55  00 | F8 F7 F6 F5
ACK:      FD FC FB FA | LEN (2) | CMD|0x0100 (2)  STATUS (2)  <giá trị trả về> | 04 03 02 01
```

Lệnh gửi tới radar dùng dạng ACK nhưng không có STATUS và không có cờ 0x0100. `TYPE` = 0x01 (engineering) hoặc 0x02 (cơ bản). `STATUS` = 0 thành công, 1 thất bại.

Hệ quả cho parser **[ĐX]** (đã cài trong `hlk_frame.cpp`):
- Không có checksum → khung được nhận dựa trên header, LEN (≤ 64), footer, rồi decoder kiểm tra byte 0xAA, 0x55, 0x00 bên trong và giới hạn số gate. Lỗi đường truyền đổi một byte năng lượng sẽ **không** bị phát hiện → tầng trên dùng kiểm tra hợp lý (năng lượng ≤ 100, khoảng cách ≤ 9 × gate) và thống kê.
- ACK có thể chứa đúng chuỗi footer trong giá trị (thấy ở LD2450, xem `giao-thuc-ld2450.md`) → luôn tách khung theo LEN, không dò footer.
- Sau một khung hỏng, parser quét lại các byte sau byte đầu của khung đó, nên không mất khung thật bắt đầu bên trong khung hỏng (vector `truncated_then_reports`).

## 4. Báo cáo cơ bản (TYPE 0x02, LEN = 13) **[TL]**

| Offset trong payload | Kích thước | Trường |
|---|---|---|
| 0 | 1 | TYPE = 0x02 |
| 1 | 1 | 0xAA |
| 2 | 1 | Trạng thái: 0 không có mục tiêu, 1 chuyển động, 2 đứng yên, 3 cả hai; 4/5/6 = đang đo / đo xong / đo lỗi nhiễu nền (chỉ khi chạy lệnh 0x000B, B107/C108) |
| 3 | 2 | Khoảng cách mục tiêu chuyển động (cm) |
| 5 | 1 | Năng lượng mục tiêu chuyển động (0–100) |
| 6 | 2 | Khoảng cách mục tiêu đứng yên (cm) |
| 8 | 1 | Năng lượng mục tiêu đứng yên (0–100) |
| 9 | 2 | "Detection distance" (cm) |
| 11 | 1 | 0x55 |
| 12 | 1 | 0x00 ("check", cố định) |

Ví dụ của tài liệu: `F4 F3 F2 F1 0D 00 02 AA 02 51 00 00 00 00 3B 00 00 55 00 F8 F7 F6 F5` → trạng thái 2, moving 81 cm / 0, still 0 cm / 59. (Giá trị trong ví dụ không nhất quán về mặt vật lý — trạng thái "đứng yên" nhưng khoảng cách đứng yên 0 — nhưng đúng cấu trúc.)

## 5. Báo cáo engineering (TYPE 0x01) **[TL]**

Sau 11 byte cơ bản (offset 0–10) là:

| Offset | Kích thước | Trường |
|---|---|---|
| 11 | 1 | "Maximum moving distance gate N" |
| 12 | 1 | "Maximum still distance gate N" |
| 13 | N_m + 1 | Năng lượng chuyển động của gate 0 … N_m |
| … | N_s + 1 | Năng lượng đứng yên của gate 0 … N_s |
| … | M | "Retain data": 2 byte trong mọi ví dụ. LD2410B V1.07 / LD2410C ≥ V1.06: byte 1 = giá trị quang trở (0–255), byte 2 = trạng thái OUT (0 không người, 1 có người) |
| cuối | 2 | 0x55 0x00 |

Ví dụ (C100): `F4 F3 F2 F1 23 00 01 AA 03 1E 00 3C 00 00 39 00 00 08 08 3C 22 05 03 03 04 03 06 05 00 00 39 10 13 06 06 08 04 03 05 55 00 F8 F7 F6 F5` → N = 8 (9 gate), moving gate = 60, 34, 5, 3, 3, 4, 3, 6, 5; still gate = 0, 0, 57, 16, 19, 6, 6, 8, 4.

Chưa rõ, cần kiểm ở Phase 1:
- **N có thay đổi theo cấu hình gate xa nhất (0x0060) không** [GT, E2]. Mọi ví dụ đều N = 8 với cấu hình mặc định. Decoder chấp nhận N bất kỳ ≤ 8 (vector `ld2410_engineering_fewer_gates`); ESPHome thì giả định luôn 9 gate.
- **Ý nghĩa vật lý của năng lượng** [GT, E5–E7]: tài liệu chỉ nói radar báo mục tiêu khi năng lượng > ngưỡng của gate. Không biết đó là công suất tuyến tính hay log, có làm mượt theo thời gian không, có trừ nền tĩnh (clutter) không, "still" là dải tần Doppler nào.
- Gate 0 và 1 có ngưỡng still không chỉnh được (bảng mặc định ghi "-") [TL] → năng lượng still gần radar thường cao; cần đo nền ở E5.
- Ở độ phân giải 0,2 m (0x00AA) vẫn là 9 gate → chỉ phủ ~1,8 m [TL]; 0,2 m là kích thước gate sau xử lý, **không** làm tốt hơn độ phân giải vật lý c/(2B) = 0,6 m của băng quét 250 MHz [GT: suy ra từ vật lý, xem thiết kế tổng thể §1].

## 6. Lệnh cấu hình **[TL]**

Quy trình: gửi **0x00FF (enable configuration, giá trị 0x0001)** → các lệnh khác → **0x00FE (end configuration)**. Lệnh gửi ngoài chế độ cấu hình bị bỏ qua. Tài liệu LD2450 nói rõ radar **ngừng gửi báo cáo** khi ở chế độ cấu hình; với LD2410 điều này hợp lý nhưng chưa được ghi rõ [GT, E1: probe ghi lại khoảng trống báo cáo quanh mỗi job].

| Lệnh | Giá trị gửi | Giá trị trả về (sau STATUS) | Lưu khi mất điện | Có từ |
|---|---|---|---|---|
| 0x00FF bật cấu hình | 0x0001 | phiên bản giao thức (2) = 0x0001, kích thước bộ đệm (2) = 0x0040 | — | P102 |
| 0x00FE kết thúc cấu hình | — | — | — | P102 |
| 0x0060 gate xa nhất + thời gian "no-one" | 3 cặp (từ 2 byte + giá trị 4 byte): gate moving 2–8, gate still 2–8, thời gian 0–65535 s | — | Có | P102 |
| 0x0061 đọc tham số | — | 0xAA, N (8), gate moving xa nhất, gate still xa nhất, ngưỡng moving gate 0…N, ngưỡng still gate 0…N, thời gian no-one (2) | — | P102 |
| 0x0062 / 0x0063 bật / tắt engineering | — | — | **Không** (tắt sau mỗi lần cấp nguồn) | P102 |
| 0x0064 ngưỡng gate | gate (hoặc 0xFFFF = mọi gate), ngưỡng moving, ngưỡng still (0–100; 100 = bỏ qua gate) | — | Có | P102 |
| 0x00A0 phiên bản firmware | — | loại (2), major (2), minor (4) → `V<major hi>.<major lo hex>.<minor hex>` | — | P102 |
| 0x00A1 baud rate | chỉ số 1–8 (9600…460800; 7 = 256000) | — | Có, sau restart | P102 |
| 0x00A2 khôi phục mặc định | — | — | Sau restart | P102 |
| 0x00A3 khởi động lại | — | — (radar restart sau khi trả lời) | — | P102 |
| 0x00A4 Bluetooth | 0x0001 bật / 0x0000 tắt | — | Có, sau restart | B/C |
| 0x00A5 MAC | 0x0001 | 6 byte MAC; khi Bluetooth tắt radar trả `08 05 04 03 02 01` [CĐ ESPHome] | — | B/C |
| 0x00A8 / 0x00A9 quyền / mật khẩu Bluetooth | 6 byte | — (0x00A8 chỉ trả lời qua Bluetooth) | Có | B/C |
| 0x00AA / 0x00AB độ phân giải gate | 0 = 0,75 m, 1 = 0,2 m | chỉ số (2) | Có, sau restart | B/C (C100, B V1.04) |
| 0x00AD / 0x00AE điều khiển phụ bằng ánh sáng | 4 byte: chế độ (0 tắt, 1 sáng < ngưỡng, 2 sáng > ngưỡng), ngưỡng 0–255 (mặc định 0x80), mức nghỉ chân OUT (0/1), 0 | 4 byte như trên | — | B107, C108 (sửa đổi V1.06) |
| 0x000B đo nhiễu nền + tự đặt ngưỡng | thời gian đo (s) | — ; báo cáo mang trạng thái 4/5/6; radar chờ 10 s rồi đo | Ngưỡng mới được lưu | B107, C108 (V1.07), firmware V2.44.24073110 |
| 0x001B trạng thái đo nhiễu nền | — | 0 chưa chạy, 1 đang chạy, 2 xong | — | như trên |

Giá trị mặc định **[TL]**: gate xa nhất 8/8, no-one 5 s, 256000 baud; ngưỡng moving gate 0–8 = 50, 50, 40, 30, 20, 15, 15, 15, 15; ngưỡng still gate 2–8 = 40, 40, 30, 30, 20, 20, 20 (gate 0–1 không chỉnh được).

## 7. Những điều probe Phase 1 phải đo (không có trong tài liệu)

| Câu hỏi | Thí nghiệm | Cách đo bằng công cụ |
|---|---|---|
| Tần suất và độ đều của báo cáo, ở chế độ cơ bản và engineering | E1 | `report.py`: mục "Stream quality" |
| Báo cáo có dừng trong chế độ cấu hình không, dừng bao lâu | E1 | khoảng trống giữa các `@D` quanh `job ... start/done` |
| N trong khung engineering khi đổi gate xa nhất / độ phân giải | E2 | `cmd 1 0x0060 ...` rồi xem cột `moving_gates` của `ld2410.csv` |
| Nền năng lượng từng gate khi phòng trống; độ trôi theo giờ | E5 | `report.py` (median, MAD, P95, P99) |
| Người ngồi yên ở 1–6 m: năng lượng still theo khoảng cách, khả năng phân biệt với nền | E6 | segment `seg sit_<m>m` vs `seg empty`, `--compare` |
| Năng lượng still có giữ dao động của nhịp thở (0,1–0,5 Hz) không | E7 | cột "still 0.1-0.5 Hz %" |
| Ảnh hưởng của LD2450 đang hoạt động lên LD2410 (nhiễu 24 GHz) | E8 | segment `ld2450_off` / `ld2450_on` |
| Trễ tương đối của LD2410 so với LD2450 | E10 | tương quan chéo khoảng cách theo thời gian |
| Ý nghĩa của 2 byte "retain data" trên module của bạn | E1 | cột `light`, `out_pin` so với chân OUT nối GPIO (`@E,...,out_pin`) |
