# Phase 1 — HLK-LD2450: giao thức và năng lực dữ liệu đã kiểm chứng

Nhãn **[TL] [CĐ] [GT] [ĐX]**: xem `nguon-tai-lieu.md`. Viết tắt: **P103** = LD2450 Serial Communication Protocol V1.03 (2023-10-17), **M100** = LD2450 manual V1.00 (2023-05-10), **OM** = LD2450 operation manual (FAQ), **PC** = "2450 serial port parsing code representation".

Mã nguồn tương ứng: `components/radar_proto/include/ld2450_proto.h`; Python: `tools/hlk_proto.py`; vector test: `test/vectors/hlk_vectors.txt`.

## 1. Tóm tắt năng lực dữ liệu

| Dữ liệu | Có? | Nguồn |
|---|---|---|
| Tối đa **3 mục tiêu**: X, Y (mm), tốc độ (cm/s), "distance resolution" (mm) | Có, **10 khung/giây** | [TL] P103 §2.3, M100 §6 và bảng 4 |
| Chế độ một mục tiêu / nhiều mục tiêu | Có (mặc định nhiều mục tiêu) | [TL] P103 §2.2.3–2.2.5 |
| Lọc vùng phía radar: tối đa 3 hình chữ nhật, "chỉ phát hiện trong vùng" hoặc "bỏ qua trong vùng" | Có, lưu khi mất điện, hiệu lực ngay | [TL] P103 §2.2.12–2.2.13 |
| **Năng lượng radar, distance gate, ngưỡng độ nhạy, engineering mode** | **Không** — không có trường nào, không có lệnh nào | [TL] P103 (toàn bộ danh sách lệnh) |
| Độ cao / góc ngẩng của mục tiêu | **Không** (anten 1T2R chỉ cho góc phương vị) | [TL] M100 §1 "one-transmitter-two-receiver"; suy luận về góc: [GT] |
| Định danh (ID) bền của mục tiêu qua các khung | Không ghi trong tài liệu: chỉ có 3 "ô" target 1/2/3 | [TL] P103 bảng 9; ô có giữ nguyên người hay không: [GT, E3/E4] |
| Trạng thái "đứng yên" của mục tiêu | Không có trường riêng; chỉ có tốc độ (0 = không chuyển động) | [TL] |
| Checksum | **Không**; khung có độ dài cố định, chỉ có header `AA FF 03 00` và footer `55 CC`, không có trường độ dài | [TL] P103 bảng 9 |

Kết luận cho dự án: LD2450 là **cảm biến vị trí/chuyển động**, không phải cảm biến năng lượng. Không thể làm "background subtraction" trên LD2450; chỉ có thể học **vùng có mục tiêu ma** (ghost) và **vùng ra/vào** từ lịch sử toạ độ [ĐX].

## 2. Phần cứng và điện **[TL]** (M100 §4, §5, §8)

- Chân: 5V, GND, Tx, Rx (có cả dạng chân cắm và socket). Nguồn **5 V, > 200 mA**, dòng trung bình **120 mA**. Mức logic **3,3 V**.
- UART mặc định **256000 baud, 8N1**; đổi được 9600 … 460800 (lệnh 0x00A1, sau restart).
- 24–24,25 GHz, FMCW, băng quét **250 MHz**, anten **1 phát 2 thu**, tầm **6 m**, góc **±60° phương vị, ±35° ngẩng**, **10 Hz**, kích thước 15 × 44 mm.
- Lắp **trên tường, cao 1,5–2 m** (M100 §7). Cảnh báo lắp đặt: tránh vật chuyển động liên tục (rèm, cây trước cửa gió, động vật), vật phản xạ mạnh diện rộng, điều hoà/quạt khi lắp tường; mặt sau radar cũng "thấy" vật chuyển động (búp sóng sau) → che bằng tấm kim loại.
- **Nhiều radar 24 GHz: không lắp đối diện chùm sóng nhau, đặt càng xa càng tốt để tránh nhiễu lẫn nhau** (M100 §7.1). Đây là xác nhận chính thức duy nhất về nhiễu chéo, áp dụng trực tiếp cho cặp LD2450 + LD2410 của dự án.
- Anten ngoài của module là **anten Bluetooth** (OM FAQ 4.4). Bluetooth bật mặc định; app HLKRadarTool chỉnh thông số và **cập nhật firmware (OTA)** qua Bluetooth (OM §3.2) → dự án tắt Bluetooth của radar sau khi cài đặt xong [ĐX].
- Firmware mới nhất ghi trong OM: **2.04.23101915**. Bản cũ chỉ hỗ trợ OTA, chưa có trang thông số (OM FAQ 4.2). Bản v2.14.x (cuối 2025) **đổi baud rate** [CĐ, issue #391 của Everything Presence Lite] → probe tự dò baud.

## 3. Khung báo cáo **[TL]** (P103 §2.3)

```
AA FF 03 00 | target 1 (8 byte) | target 2 (8 byte) | target 3 (8 byte) | 55 CC      (30 byte, 10 khung/s)
target:  X (2) | Y (2) | tốc độ (2) | distance resolution (2)
```

- **X, Y, tốc độ dùng dạng dấu-độ lớn, không phải bù hai**: bit 15 = 1 → dương, = 0 → âm; 15 bit thấp là độ lớn. X, Y tính bằng mm, tốc độ bằng cm/s.
  - Ví dụ P103: `0E 03` → 0x030E = 782, bit 15 = 0 → **X = −782 mm**; `B1 86` → 0x86B1, bit 15 = 1 → 0x06B1 = **Y = 1713 mm**; `10 00` → **tốc độ −16 cm/s**.
  - Mã mẫu chính thức (PC) ghi chú: Y luôn dương (trước mặt radar) → Y âm là dữ liệu bất thường. Decoder đánh dấu "không hợp lý" mục tiêu có Y < 0, |X| > 8 m, Y > 8 m hoặc |tốc độ| > 8 m/s [ĐX], nhưng vẫn giải mã để thống kê.
- "Distance resolution" (uint16, mm) = "kích thước một distance gate". OM FAQ 4.3: luôn là `68 01` (360 mm), cố định, **không cần dùng**. Ví dụ P103 dùng `40 01` (320 mm). Dự án dùng trường này như một dấu hiệu kiểm tra (giá trị phải hằng trên một module) [ĐX].
- **Mục tiêu không tồn tại: cả 8 byte bằng 0** [TL].
- **Chiều của dấu tốc độ**: tài liệu chỉ nói "bit 1 = tốc độ dương". ESPHome hiểu dương = **rời xa radar**, âm = **tiến lại gần** [CĐ] → kiểm chứng ở E3.
- **Chiều trục X** (bên trái hay bên phải radar là dương): tài liệu chỉ thể hiện trên hình, chữ không nói rõ; ESPHome tính góc = atan2(−X, Y) [CĐ] → kiểm chứng ở E3.

## 4. Lệnh cấu hình **[TL]** (P103 §2.2)

Khung lệnh/ACK giống LD2410 (`FD FC FB FA | LEN | lệnh | giá trị | 04 03 02 01`, ACK có cờ 0x0100 và STATUS). Phải gửi 0x00FF trước, 0x00FE sau. **Radar ngừng gửi báo cáo sau khi trả lời lệnh bật cấu hình** (OM §2.3) → mọi lần đọc cấu hình tạo ra một khoảng trống dữ liệu; firmware sản phẩm không được vào chế độ cấu hình trong lúc vận hành bình thường [ĐX].

| Lệnh | Giá trị gửi | Giá trị trả về (sau STATUS) | Ghi chú |
|---|---|---|---|
| 0x00FF bật cấu hình | 0x0001 | phiên bản giao thức (2) = 0x0001, bộ đệm (2) = 0x0040 | |
| 0x00FE kết thúc cấu hình | — | — | |
| 0x0080 một mục tiêu | — | — | |
| 0x0090 nhiều mục tiêu | — | — | mặc định |
| 0x0091 đọc chế độ | — | 0x0001 một / 0x0002 nhiều | thêm ở V1.03 |
| 0x00A0 phiên bản firmware | — | loại (2) = 0x0000, major (2), minor (4) | ví dụ V1.02.22062416 |
| 0x00A1 baud rate | chỉ số 1–8 (7 = 256000) | — | sau restart |
| 0x00A2 khôi phục mặc định | — | — | mặc định: 256000, Bluetooth bật, nhiều mục tiêu, tắt lọc vùng |
| 0x00A3 khởi động lại | — | — | |
| 0x00A4 Bluetooth | `01 00` bật / `00 00` tắt | — | lưu, sau restart |
| 0x00A5 MAC | 0x0001 | 6 byte | |
| 0x00C1 đọc lọc vùng | — | loại (2) + 3 vùng × (x1, y1, x2, y2) | thêm ở V1.02 |
| 0x00C2 đặt lọc vùng | 26 byte như trên | — | lưu, hiệu lực ngay |

Định dạng lọc vùng: loại 0 = tắt, 1 = **chỉ** phát hiện trong vùng, 2 = **không** phát hiện trong vùng; mỗi vùng là hai đỉnh chéo của hình chữ nhật; **toạ độ là int16 bù hai** (ví dụ `18 FC` = −1000), **khác** dạng dấu-độ lớn của khung báo cáo; vùng toàn 0 = không dùng [TL]. ACK đọc vùng dài 40 byte và **có thể chứa chuỗi `04 03 02 01` bên trong toạ độ** [CĐ ESPHome] → parser tách theo LEN (vector `footer_bytes_inside_zone_ack`).

## 5. Hành vi với người đứng yên

- Tài liệu mô tả sản phẩm là "**motion** target detection and tracking", đo "distance, angle and speed measurement of the **moving** target" (M100 §1) [TL].
- Đồng thời quảng cáo cảm nhận được "vi chuyển động" mà cảm biến truyền thống không thấy (M100 §2.2) [TL], không có số liệu.
- Cộng đồng: yếu với hiện diện tĩnh ngoài ~2 m [CĐ atomic14].

→ Dự án **không** dựa vào LD2450 để giữ hiện diện của người ngồi yên. LD2450 cung cấp: sự kiện vào/ra, vị trí cuối cùng trước khi mất dấu, quỹ đạo [ĐX]. Thời gian LD2450 giữ một người đứng yên ở từng khoảng cách được đo ở **E4**; kết quả quyết định trọng số của "LD2450 không thấy ai" trong bộ lọc Bayes (thiết kế tổng thể §6).

## 6. Những điều probe Phase 1 phải đo

| Câu hỏi | Thí nghiệm |
|---|---|
| Tần suất thật, độ trễ giữa khung, tỉ lệ khung hỏng | E1 |
| Chiều dương của X, ý nghĩa dấu tốc độ, sai số X/Y theo vị trí | E3 |
| Ô target có giữ người khi có 2–3 người không; hoán đổi ô | E3 |
| Thời gian giữ một người đứng yên/ngồi yên theo khoảng cách; vị trí mục tiêu "xuất hiện lại" | E4 |
| Mục tiêu ma (quạt, rèm, gương, TV) khi phòng trống | E5, E9 |
| Ảnh hưởng của LD2410 lên LD2450 (mục tiêu ma tăng khi LD2410 hoạt động?) | E8 |
| Giá trị "distance resolution" của module thật | E1 |
| Baud rate và firmware thực của module (đổi sau OTA?) | E1 (`scan`, `info`) |
