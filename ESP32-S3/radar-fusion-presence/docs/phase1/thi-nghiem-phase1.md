# Phase 1 — Kế hoạch thí nghiệm kiểm chứng khả năng dữ liệu của hai radar

Mục tiêu Phase 1: **thay các giả thuyết [GT] của dự án bằng số đo [ĐO]** trước khi viết driver sản phẩm và thuật toán. Mọi thí nghiệm dùng chung một bộ công cụ:

| Công cụ | Vai trò |
|---|---|
| `radar_probe/` (firmware ESP32-S3) | Đọc LD2410 trên UART1 và LD2450 trên UART2, gắn mốc thời gian µs cho từng khung, ghi khung thô + ACK + sự kiện ra cổng USB dạng bản ghi có checksum; truy vấn phiên bản/cấu hình; tự dò baud rate; nhận lệnh `mark`, `eng`, `pwr`... |
| `tools/capture.py` | Ghi luồng của probe vào file `.rlog` (kèm giờ máy tính), đồng bộ đồng hồ 10 s/lần, gửi marker gõ tay (`--marks`) hoặc theo lịch (`--schedule`) |
| `tools/decode.py` | `.rlog` → `ld2410.csv`, `ld2450.csv`, `replies.csv`, `events.csv`, `stats.csv`, `summary.json` |
| `tools/report.py` | Báo cáo Phase 1: chất lượng luồng, thống kê từng gate, chu kỳ, dải 0,1–0,5 Hz, mục tiêu LD2450, so sánh hai đoạn (`--compare`), độ trễ giữa hai radar |
| `tools/simulate.py` | Dữ liệu **giả lập** để thử công cụ khi chưa có phần cứng. **Không bao giờ** dùng làm bằng chứng |

Cài đặt và cách nạp firmware: xem `README.md` của dự án.

## 0. Quy ước chung cho mọi phiên đo

**Đặt tên file:** `<ngày>_<phòng>_<thí nghiệm>_<lần>.rlog`, ví dụ `20261020_ngu1_E6_r2.rlog`.

**Đánh dấu đoạn (segment):** trong lúc ghi, gõ (với `--marks`) hoặc lập lịch:
```
seg <tên>     bắt đầu đoạn <tên> (kết thúc đoạn đang mở)
seg end       kết thúc đoạn
<chữ bất kỳ>  sự kiện tự do (ví dụ "ngồi xuống", "quạt số 2")
```
`report.py` gộp các đoạn cùng tên và so sánh hai tên bằng `--compare A B`. Nút BOOT (GPIO0) cũng ghi sự kiện `button,press <n>` — dùng khi người đo đang ở trong phòng, không chạm được máy tính.

**Siêu dữ liệu bắt buộc** (ghi vào `--label` hoặc file `.yaml` cạnh `.rlog`):
```yaml
room: ngu1, 3.2 x 4.1 x 2.7 m, tường gạch, cửa gỗ ở góc (0.3, 4.1)
mount: LD2450 cao 1.6 m giữa tường ngắn, nhìn thẳng; LD2410C cách 20 cm bên phải, cùng độ cao, song song
firmware: (lấy từ dòng @I version của probe)
radar_config: mặc định nhà sản xuất | (ghi các thay đổi)
people: P1 (1.70 m, 65 kg), ...   # mã hoá, không ghi tên
interference: quạt đứng X tại (1.0, 3.0), tốc độ 2, xoay; điều hoà tắt; rèm đóng
ground_truth: kịch bản + nút bấm | video cục bộ | quan sát trực tiếp
```

**Sự thật nền (ground truth):** kịch bản có thời gian + marker gõ ngay khi sự kiện xảy ra; với các phiên dài (E12) dùng thêm quan sát độc lập (video lưu cục bộ, xoá sau khi gán nhãn). Đoạn nào không chắc → đặt tên `uncertain`, loại khỏi phân tích.

**Quy tắc an toàn dữ liệu:** probe chỉ đọc cấu hình trừ khi gõ lệnh ghi (`eng`, `track`, `bt`, `setbaud`, `cmd`). Trước mọi thay đổi cấu hình radar: chạy `info` để lưu giá trị cũ; sau thí nghiệm: khôi phục.

## 1. Bố trí phần cứng Phase 1

| Linh kiện | Ghi chú |
|---|---|
| ESP32-S3-DevKitC-1 N8R8 hoặc N16R8 | Probe chạy được trên mọi module S3; dùng cổng USB "native" (USB-Serial-JTAG) |
| HLK-LD2410C (hoặc B) và HLK-LD2450 | Ghi lại phiên bản firmware (probe tự đọc) |
| Nguồn 5 V ≥ 1 A | Cổng USB của máy tính thường chỉ 0,5 A: dùng nguồn 5 V riêng cho radar, **nối chung GND** với ESP32-S3 |
| 2 công tắc tải (tuỳ chọn, cần cho E8) | TPS22918 hoặc P-MOSFET AO3401A + transistor kéo; chân EN từ GPIO15 (LD2410) / GPIO16 (LD2450); bật `CONFIG_PROBE_CHx_POWER_GPIO` |
| Giá gá | Tấm gỗ/nhựa có vạch: khoảng cách giữa hai radar 3 / 10 / 20 / 40 cm, xoay được; thước laser; băng dính đánh dấu lưới trên sàn |
| Quạt đứng có xoay, rèm, (tuỳ chọn) robot hút bụi | Cho E9 |

Đấu nối (mặc định của probe, đổi trong `idf.py menuconfig` → *Radar probe (Phase 1)*):

| Radar | Chân radar | ESP32-S3 |
|---|---|---|
| LD2410 | TX | GPIO17 (UART1 RX) |
| LD2410 | RX | GPIO18 (UART1 TX) |
| LD2410 | OUT (tuỳ chọn) | GPIO7 → bật `CONFIG_PROBE_LD2410_OUT_GPIO=7` |
| LD2450 | TX | GPIO4 (UART2 RX) |
| LD2450 | RX | GPIO5 (UART2 TX) |
| cả hai | VCC / 5V | 5 V (qua công tắc tải nếu có) |
| cả hai | GND | GND |

Probe từ chối chân của flash (26–32), PSRAM octal của module R8/N16R8 (33–37) và USB (19–20).

## 2. Các thí nghiệm

Mỗi thí nghiệm ghi: mục đích, câu hỏi/giả thuyết, cách làm, phân tích, tiêu chí nghiệm thu. "Ghi nhận" nghĩa là kết quả là một con số dùng cho thiết kế, không có ngưỡng đạt/trượt.

### E0 — Khởi động bàn thử

- **Cách làm:** nạp probe, mở `capture.py -p PORT --cmd "info all"` 10 phút, phòng trống.
- **Nghiệm thu:** có `@B`; hai dòng `@I,...,radar,LD2410` và `@I,...,radar,LD2450`; job `info` "done" với số lệnh được chấp nhận đúng loại radar (LD2410C mới: 6; LD2450: 4); `summary.json` có `checksum_errors = 0`, `decode_errors = 0`. `info all` cho biết radar của bạn trả lời những lệnh nào của radar kia (thường là "refused" hoặc không trả lời) → ghi vào bảng kết quả.

### E1 — Luồng dữ liệu và định dạng khung

- **Câu hỏi:** tần suất thật của mỗi radar ở chế độ cơ bản / engineering; độ đều; tỉ lệ khung hỏng; khoảng trống dữ liệu khi vào chế độ cấu hình; giá trị 2 byte cuối khung engineering; trường "distance resolution" của LD2450.
- **Cách làm:** 3 lần × 20 phút, phòng trống: (a) chỉ LD2410 cắm, `eng off`; (b) chỉ LD2410, `eng on`; (c) cả hai radar. Trong (c), cứ 5 phút gõ `info` một lần.
- **Phân tích:** `report.py` mục *Stream quality* (Hz, P5/P50/P95 khoảng cách khung, số khoảng trống > 300 ms) và *Capture* (bộ đếm `garbage`, `bad_footer`, `decode_err`, `implausible`). Khoảng trống quanh mỗi job `info` = thời gian radar ngừng báo cáo trong chế độ cấu hình. So cột `out_pin` của `ld2410.csv` với sự kiện `out_pin` của GPIO7.
- **Nghiệm thu:** ≥ 99,9 % khung hợp lệ (`frames / (frames + bad_footer + bad_length)`), không có khoảng trống ngoài các job; ghi nhận tần suất của LD2410 (tài liệu không nêu) và LD2450 (tài liệu: 10 Hz — đạt nếu 9,5–10,5 Hz).

### E2 — Cấu trúc khung engineering khi đổi cấu hình (LD2410)

- **Câu hỏi:** N ("maximum distance gate") trong khung engineering có theo cấu hình gate xa nhất không; khung ở độ phân giải 0,2 m.
- **Cách làm:** `info 1` (lưu cấu hình cũ), rồi lần lượt mỗi cấu hình 2 phút:
  ```
  cmd 1 0x0060 000005000000010005000000020005000000   # gate xa nhất moving 5, still 5, no-one 5 s
  cmd 1 0x0060 000008000000010003000000020005000000   # moving 8, still 3
  cmd 1 0x00AA 0100                                    # 0,2 m mỗi gate (LD2410B/C)
  restart 1
  cmd 1 0x00AA 0000                                    # trả lại 0,75 m
  restart 1
  cmd 1 0x0060 000008000000010008000000020005000000   # trả lại 8 / 8 / 5 s (hoặc giá trị cũ)
  info 1
  ```
- **Phân tích:** cột `moving_gates`, `still_gates` và độ dài khung trong `ld2410.csv`.
- **Nghiệm thu:** mô tả được hành vi; decoder không báo lỗi giải mã ở mọi cấu hình; radar trở về đúng cấu hình cũ.

### E3 — Quy ước toạ độ và độ chính xác của LD2450

- **Câu hỏi:** chiều dương của X (trái/phải nhìn từ radar); dấu tốc độ khi tiến lại / rời xa; sai số X, Y; ô target có giữ người khi có 2–3 người không.
- **Cách làm:** lưới điểm trên sàn Y = 1, 2, 3, 4, 5 m; X = −1,5, 0, +1,5 m. Một người đứng ở mỗi điểm 30 s (lắc nhẹ người để radar giữ được mục tiêu), marker `seg p_<x>_<y>`. Sau đó đi thẳng từ 5 m về 1 m và ngược lại 5 lần (`seg toward`, `seg away`). Cuối cùng 2 rồi 3 người đi chéo nhau.
- **Phân tích:** trung bình và độ lệch chuẩn X, Y theo từng đoạn (từ `ld2450.csv`); dấu cột `t1_speed_cm_s` trong `toward`/`away`; mục *runs* của report cho thấy ô bị đổi.
- **Nghiệm thu:** xác định chiều X và dấu tốc độ; bảng sai số theo điểm (ghi nhận; kỳ vọng sơ bộ |sai số| < 0,3 m trong 4 m — chưa phải tiêu chí).

### E4 — LD2450 với người đứng yên / ngồi yên

- **Câu hỏi:** LD2450 giữ một người ngồi yên được bao lâu ở từng khoảng cách; sau khi mất, mục tiêu có xuất hiện lại ở đúng chỗ không.
- **Cách làm:** người đi vào, ngồi ghế tại Y = 1, 2, 3, 4, 5 m (X = 0), bấm nút BOOT lúc ngồi xuống, ngồi đọc/thở bình thường 10 phút, rồi đứng dậy đi ra. 3 lần mỗi khoảng cách. Lặp lại với tư thế đứng yên.
- **Phân tích:** từ thời điểm `button` tới khi ô target trống liên tục > 0,3 s (mục *runs*; thời lượng mỗi run và vị trí bắt đầu/kết thúc). Vẽ đường "xác suất còn giữ mục tiêu theo thời gian" cho từng khoảng cách.
- **Nghiệm thu:** ghi nhận đường cong (≥ 3 lần mỗi khoảng cách). Đây là đầu vào cho trọng số "LD2450 không thấy ai" trong bộ lọc Bayes (thiết kế tổng thể §6.4).

### E5 — Nền năng lượng của phòng trống (LD2410)

- **Câu hỏi:** phân bố năng lượng moving/still từng gate khi phòng trống, độ trôi theo giờ và ngày/đêm; mục tiêu ma của LD2450.
- **Cách làm:** phòng trống, cửa đóng, quạt/điều hoà tắt: 60 phút ban ngày; lặp lại sau 24 h và một phiên ban đêm. Ghi lại thiết bị điện đang chạy trong phòng bên cạnh.
- **Phân tích:** bảng gate của report (median, MAD, P95, P99, chu kỳ); `--compare` giữa các phiên (độ trôi ΔP95, AUC); tỉ lệ báo cáo có mục tiêu theo quyết định của chính LD2410 (báo nhầm của radar với ngưỡng mặc định); số mục tiêu LD2450.
- **Nghiệm thu:** ghi nhận nền; độ trôi giữa các phiên → chọn hằng số thời gian cho học thích nghi (Phase 3).

### E6 — Người ngồi yên theo khoảng cách (LD2410) — quyết định KPI 4–6 m

- **Câu hỏi:** ở khoảng cách nào năng lượng của người ngồi yên còn phân biệt được với nền.
- **Cách làm:** ghế đặt ở tâm gate: 1,1 / 1,9 / 2,6 / 3,4 / 4,1 / 4,9 / 5,6 m (gate 1–7 ở 0,75 m/gate), nhìn về radar. Mỗi vị trí: `seg empty` 5 phút (người ra ngoài tầm radar) → `seg sit_<m>m` 10 phút ngồi yên đọc sách → lặp 3 lần, ≥ 2 người, ≥ 2 tư thế (thẳng lưng, ngả lưng).
- **Phân tích:** `report.py f.rlog --compare empty sit_4.1m`: cột **AUC 10 s** của gate tương ứng (và hai gate bên cạnh), KS D, ΔP95. AUC tính trên trung vị các cửa sổ 10 s không chồng nhau, nên gần với độc lập. Lặp với cửa sổ 60 s bằng cách tự gộp (xem `report.window_medians`).
- **Nghiệm thu:** ghi nhận **khoảng cách lớn nhất có AUC ≥ 0,95 (cửa sổ 10 s)** và **≥ 0,99 (60 s)**. Nếu 4 m không đạt AUC ≥ 0,95 ở 60 s → KPI "recall > 95 % ở 4 m" phải sửa (xem §4).

### E7 — Năng lượng still có mang nhịp thở không

- **Câu hỏi:** chuỗi năng lượng still (10 Hz) có giữ dao động 0,1–0,5 Hz của nhịp thở không, hay radar đã làm mượt mất.
- **Cách làm:** ngồi tại 1,5 m và 3 m: `seg breath` 2 phút thở bình thường → `seg hold` nín thở 30 s → `seg slow` thở chậm sâu (6 lần/phút, 0,1 Hz) 2 phút; lặp 4 lần.
- **Phân tích:** cột "still 0.1-0.5 Hz %" và "still period" của gate có người, so `breath` / `hold` / `slow` và với `empty`.
- **Nghiệm thu:** kết luận có/không dùng đặc trưng dải tần thở (thiết kế §6.2). Tiêu chí "có": tỉ lệ công suất dải thở ở `breath` cao hơn `empty` với AUC ≥ 0,8 trên các cửa sổ 64 s, và chu kỳ phát hiện được trùng nhịp thở đếm tay ± 20 %.

### E8 — Nhiễu chéo 24 GHz giữa LD2410 và LD2450

- **Câu hỏi:** LD2450 hoạt động có làm tăng năng lượng/ báo nhầm của LD2410 không, và ngược lại; khoảng cách và hướng lắp nào loại được ảnh hưởng. (Hi-Link cảnh báo chính thức về nhiễu giữa các radar 24 GHz; cộng đồng báo LD2410 báo nhầm khi LD2450 chạy.)
- **Cách làm:** phòng trống, cần công tắc tải. Chạy lịch có sẵn:
  ```
  python3 capture.py -p PORT --schedule schedules/e8_ld2450_ab.txt --duration 3610 --label "E8 20cm side-by-side"
  python3 capture.py -p PORT --schedule schedules/e8_ld2410_ab.txt --duration 3610 --label "E8 20cm reverse"
  ```
  Lặp cho các bố trí: cách nhau 3 / 10 / 20 / 40 cm cạnh nhau; xếp chồng dọc; cạnh nhau có tấm kim loại nối GND chắn giữa; hai radar nghiêng ra hai phía 15°.
- **Phân tích:** `report.py f.rlog --compare ld2450_off ld2450_on`: mọi gate phải có |ΔP95| nhỏ và AUC 10 s gần 0,5; so tỉ lệ báo nhầm của chính LD2410. Lịch ngược: so số mục tiêu LD2450 (mục tiêu ma) giữa `ld2410_off` / `ld2410_on`.
- **Nghiệm thu:** tìm được ít nhất một bố trí thoả **mọi gate |ΔP95| ≤ 2 và AUC 10 s ≤ 0,6**, không tăng báo nhầm của LD2410, không tăng mục tiêu ma của LD2450. Bố trí đó trở thành bố trí chuẩn của sản phẩm.

### E9 — Quạt, rèm, điều hoà (khảo sát trước cho Phase 3/5)

- **Câu hỏi:** dấu hiệu của từng nguồn nhiễu trên năng lượng từng gate và trên LD2450; người ngồi gần quạt có làm thay đổi phân bố không (giả thuyết của thiết kế §7 — kiểm chứng đầy đủ ở Phase 3/5).
- **Cách làm:** các khối 10 phút, thứ tự ngẫu nhiên: `empty_fan_off`, `fan_low`, `fan_high`, `fan_osc` (xoay), `fan_person_same_gate` (ngồi cạnh quạt, cùng khoảng cách), `fan_person_other_gate` (cách quạt ≥ 1,5 m theo khoảng cách), `fan_person_occluding` (ngồi giữa radar và quạt), `fan_leave` (người đi ra, quạt vẫn chạy), `curtain` (rèm bay trước quạt/cửa sổ), `ac_on`. Ghi vị trí quạt (gate) vào siêu dữ liệu.
- **Phân tích:** report: chu kỳ ở gate có quạt (quạt xoay → chu kỳ 5–30 s), AUC giữa `fan_*` và `fan_person_*` từng gate (cả hai chiều: tăng **hoặc giảm**), mục tiêu ma LD2450 và vị trí của chúng.
- **Nghiệm thu:** ghi nhận bảng dấu hiệu từng nguồn nhiễu; bảng AUC sơ bộ cho từng điều kiện "quạt + người".

### E10 — Độ trễ giữa hai radar và độ trễ phát hiện người vào

- **Câu hỏi:** chênh lệch thời gian giữa thông tin của LD2410 và LD2450 (để ghép mẫu); thời gian từ lúc người bước vào tới khi mỗi radar báo.
- **Cách làm:** (a) một người đi đi lại lại dọc trục radar từ 1 tới 5 m, ~1 m/s, 2 phút. (b) 20 lần: người đứng ngoài cửa, bấm nút BOOT (gắn dây dài hoặc nút phụ ở cửa) đúng lúc bước qua cửa vào tầm radar.
- **Phân tích:** report mục *Delay between the radars* cho (a). Cho (b): thời gian từ `button` tới báo cáo LD2450 đầu tiên có mục tiêu hợp lý và tới báo cáo LD2410 đầu tiên có trạng thái moving (từ `events.csv`, `ld2450.csv`, `ld2410.csv`).
- **Nghiệm thu:** ghi nhận độ trễ tương đối (kỳ vọng < 300 ms [GT]); phân bố độ trễ phát hiện người vào (P50, P95) của từng radar — cơ sở cho KPI "< 2 s".

### E11 — Ánh xạ hình học X/Y (LD2450) → distance gate (LD2410)

- **Câu hỏi:** với bố trí chuẩn của E8, khoảng cách LD2410 đo được (trường "still/moving distance" và gate có năng lượng cao nhất) quan hệ thế nào với √(X² + Y²) của LD2450; năng lượng của một người lan sang bao nhiêu gate.
- **Cách làm:** dùng lại lưới E3; ở mỗi điểm đứng 30 s rồi ngồi 30 s (cùng marker).
- **Phân tích:** hồi quy r₂₄₁₀ ≈ a·r₂₄₅₀ + b; phân bố gate có năng lượng still lớn nhất theo r₂₄₅₀ → bảng xác suất P(gate | r) thực nghiệm; độ lệch chuẩn σ_r dùng trong thiết kế §6.1.
- **Nghiệm thu:** ghi nhận a, b, σ_r và bảng P(gate | r).

### E12 — Ổn định dài hạn

- **Cách làm:** ghi liên tục 72 h trong sinh hoạt bình thường (có người ra vào), đồng bộ đồng hồ bật.
- **Phân tích:** bộ đếm `@S` (UART error, overflow, resync), sự kiện `radar silent`, `out_drops` và heap thấp nhất của probe, khoảng trống dữ liệu.
- **Nghiệm thu:** không có `radar silent` không rõ nguyên nhân; ≥ 99,9 % khung hợp lệ; probe không khởi động lại (chỉ một `@B`).

## 3. Tiêu chí kết thúc Phase 1 (Go / No-Go)

| Mã | Tiêu chí | Từ thí nghiệm |
|---|---|---|
| G1 | Hai luồng dữ liệu được giải mã ổn định, ≥ 99,9 % khung hợp lệ trong 72 h | E1, E12 |
| G2 | Năng lượng từng gate của LD2410 lấy được liên tục; cấu trúc khung engineering đã rõ | E1, E2 |
| G3 | Có bố trí lắp hai radar không gây nhiễu đáng kể, hoặc đã chọn biện pháp giảm nhiễu | E8 |
| G4 | Đã đo: khả năng phân biệt người ngồi yên theo khoảng cách (LD2410), thời gian LD2450 giữ người đứng yên, tính hữu dụng của dải tần thở → **KPI được xác nhận hoặc sửa bằng văn bản** | E4, E6, E7 |
| G5 | Đã có quy ước toạ độ, độ trễ, tham số ánh xạ hình học | E3, E10, E11 |
| G6 | Tài liệu dự án đã cập nhật: mỗi giả thuyết [GT] liên quan được đổi thành [ĐO] kèm số liệu, hoặc bị bác bỏ | tất cả |

## 4. Phương án khi kết quả xấu

| Kết quả | Hướng xử lý |
|---|---|
| E8: không bố trí nào đạt | Tách hai radar xa hơn (vỏ riêng, ≥ 50 cm), che chắn kim loại; hoặc chia thời gian: tắt LD2450 theo chu kỳ khi đang ở trạng thái STATIONARY (mất khả năng theo dõi trong lúc tắt); cuối cùng: thay một radar bằng băng tần khác (ngoài phạm vi hiện tại) |
| E6: 4 m không đạt AUC ≥ 0,95 ở 60 s | Sửa KPI: recall > 95 % ở khoảng cách đã đạt; 4–6 m là "giữ hiện diện nhờ lịch sử LD2450 + logic giữ có giới hạn", không cam kết recall; xem xét lắp LD2410 gần chỗ ngồi chính hoặc trên trần |
| E7: không có thông tin nhịp thở | Bỏ đặc trưng dải tần thở; dùng mức năng lượng và độ biến động |
| E4: LD2450 giữ người ngồi yên tốt ở ≤ 2 m | Dùng LD2450 như bằng chứng dương mạnh trong vùng đó |
| E1: LD2410 không giữ engineering mode ổn định | Probe/driver tự bật lại định kỳ; nếu firmware không hỗ trợ: chỉ còn năng lượng tổng của báo cáo cơ bản → giảm năng lực thuật toán học nền, cân nhắc đổi module |
| E1/E12: khung hỏng nhiều | Kiểm tra nguồn và dây (GND chung, dây ngắn, ferrite); hạ baud rate (115200 vẫn đủ: ~750 byte/s mỗi radar) |

## 5. Bảng kết quả (điền khi đo)

| Thí nghiệm | Ngày | Phòng / bố trí | Firmware LD2410 / LD2450 | File | Kết quả chính | Kết luận |
|---|---|---|---|---|---|---|
| E0 | | | | | | |
| E1 | | | | | | |
| E2 | | | | | | |
| E3 | | | | | | |
| E4 | | | | | | |
| E5 | | | | | | |
| E6 | | | | | | |
| E7 | | | | | | |
| E8 | | | | | | |
| E9 | | | | | | |
| E10 | | | | | | |
| E11 | | | | | | |
| E12 | | | | | | |
