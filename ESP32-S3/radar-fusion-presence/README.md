# AI Radar Fusion Presence Sensor (ESP32-S3 + HLK-LD2410 + HLK-LD2450)

Cảm biến hiện diện kết hợp hai radar 24 GHz trên ESP32-S3 (N8R8 / N16R8): **HLK-LD2450** theo dõi người di chuyển (toạ độ X/Y, tối đa 3 người), **HLK-LD2410** đo năng lượng vi chuyển động theo từng khoảng cách để giữ hiện diện của người ngồi yên. Mục tiêu: báo hiện diện tin cậy qua **Matter over Wi-Fi**, tự học tín hiệu của phòng trống, ít báo nhầm khi có quạt/rèm/điều hoà, không mất người ngồi yên lâu.

**Tài liệu chính:**

| Tài liệu | Nội dung |
|---|---|
| [`docs/thiet-ke-tong-the.md`](docs/thiet-ke-tong-the.md) | Phương án tổng thể theo 12 mục: khả thi và giới hạn vật lý, kiến trúc Hybrid, sơ đồ khối, luồng dữ liệu, học phòng trống, nhận diện người ngồi im (bộ lọc Bayes, state machine, confidence), phân biệt quạt/người, so sánh mô hình AI, dataset, firmware ESP-IDF + Matter/HA, linh kiện và đấu nối, roadmap |
| [`docs/phase1/nguon-tai-lieu.md`](docs/phase1/nguon-tai-lieu.md) | Tài liệu Hi-Link đã đọc (phiên bản, nguồn, mã băm), các chỗ tài liệu tự mâu thuẫn, nguồn cộng đồng |
| [`docs/phase1/giao-thuc-ld2410.md`](docs/phase1/giao-thuc-ld2410.md) | LD2410/B/C: khung, báo cáo cơ bản và engineering, lệnh, điều chưa có trong tài liệu |
| [`docs/phase1/giao-thuc-ld2450.md`](docs/phase1/giao-thuc-ld2450.md) | LD2450: khung, mã hoá dấu-độ lớn, lệnh, lọc vùng, hành vi với người tĩnh |
| [`docs/phase1/thi-nghiem-phase1.md`](docs/phase1/thi-nghiem-phase1.md) | Kế hoạch thí nghiệm E0–E12, tiêu chí nghiệm thu, tiêu chí Go/No-Go của Phase 1 |

Mọi khẳng định trong tài liệu được gắn nhãn: **[TL]** tài liệu chính thức Hi-Link, **[CĐ]** nguồn cộng đồng, **[GT]** giả thuyết cần kiểm chứng, **[ĐX]** phương án đề xuất, **[ĐO]** đã đo.

## Trạng thái

| Phase | Nội dung | Trạng thái |
|---|---|---|
| 1 | Nghiên cứu tài liệu chính thức, kiểm chứng khả năng dữ liệu | **Đang làm**: tài liệu, thư viện giao thức, firmware probe, công cụ PC, kế hoạch thí nghiệm **xong và đã kiểm thử trên máy tính**; còn chạy thí nghiệm E0–E12 trên radar thật |
| 2 | Driver sản phẩm, thu dữ liệu thực tế | chưa bắt đầu |
| 3 | Học phòng trống (không AI) | chưa bắt đầu |
| 4 | Sensor fusion, tracking, state machine | chưa bắt đầu |
| 5 | Dataset, huấn luyện TinyML | chưa bắt đầu |
| 6 | Tích hợp mô hình vào ESP32-S3 | chưa bắt đầu |
| 7 | Matter over Wi-Fi | chưa bắt đầu (repo đã có Matter Occupancy cho ESP32-C3/H2 để tái sử dụng) |
| 8 | Thử nghiệm thực tế, tối ưu | chưa bắt đầu |

Kết luận quan trọng nhất của Phase 1 đến giờ (từ tài liệu, chưa đo): hai radar cùng băng 24 GHz và Hi-Link cảnh báo nhiễu lẫn nhau → **thí nghiệm nhiễu E8 phải làm đầu tiên**; KPI người ngồi yên 4–6 m chỉ được chốt sau E6. Chi tiết: §0 và §1 của thiết kế tổng thể.

## Phase 1: firmware probe và công cụ

### Probe là gì

`radar_probe/` là firmware **đo đạc** (không phải sản phẩm): ESP32-S3 đọc LD2410 trên UART1 và LD2450 trên UART2, gắn mốc thời gian µs cho từng khung, ghi **khung thô** + ACK + sự kiện ra cổng USB thành các dòng văn bản có checksum. Probe tự nhận ra radar nào nối vào UART nào, tự dò baud rate nếu radar không trả lời ở 256000, đọc phiên bản firmware và cấu hình của radar lúc khởi động, bật engineering mode của LD2410 (và bật lại sau mỗi lần radar khởi động lại). Không có thuật toán hiện diện nào trong probe.

### Đấu nối

| Radar | Chân radar | ESP32-S3 (mặc định) |
|---|---|---|
| HLK-LD2410C | TX | GPIO17 (UART1 RX) |
| HLK-LD2410C | RX | GPIO18 (UART1 TX) |
| HLK-LD2410C | OUT (tuỳ chọn) | GPIO7 — bật `PROBE_LD2410_OUT_GPIO` |
| HLK-LD2450 | Tx | GPIO4 (UART2 RX) |
| HLK-LD2450 | Rx | GPIO5 (UART2 TX) |
| cả hai | VCC / 5V | 5 V (nguồn ≥ 1 A; tuỳ chọn qua công tắc tải, `PROBE_CH1/CH2_POWER_GPIO`) |
| cả hai | GND | GND chung với ESP32-S3 |

Mức logic của cả hai radar là 3,3 V, nối thẳng. Đổi chân trong `idf.py menuconfig` → *Radar probe (Phase 1)*; probe không chạy kênh nào dùng chân flash (26–32), chân PSRAM octal của module R8/N16R8 (33–37) hoặc USB (19–20). Bố trí hai radar khi đo: cạnh nhau, song song, cùng nhìn vào phòng, bắt đầu cách nhau 20 cm (E8 quyết định bố trí cuối).

### Build và nạp

Yêu cầu: **ESP-IDF v6.0.2** (giống CI). Bằng Docker:

```bash
cd ESP32-S3/radar-fusion-presence/radar_probe
docker run --rm -it -v $PWD/../../..:/project espressif/idf:v6.0.2 \
    bash -c 'cd /project/ESP32-S3/radar-fusion-presence/radar_probe && idf.py set-target esp32s3 && idf.py build'
idf.py -p /dev/ttyACM0 flash        # hoặc esptool với file merged bên dưới
```

Hoặc cài local: `. ~/esp/esp-idf/export.sh && idf.py set-target esp32s3 && idf.py build flash`.

Mỗi lần push, GitHub Actions (`.github/workflows/radar-fusion-s3.yml`) chạy test trên máy tính và build probe; tab **Actions** → **Artifacts** có `radar_probe_merged.bin`, nạp một lần ở địa chỉ 0:

```bash
esptool.py --chip esp32s3 -p /dev/ttyACM0 write_flash 0x0 radar_probe_merged.bin
```

Probe dùng cổng USB "native" của ESP32-S3 (USB-Serial-JTAG, thường ghi **USB** trên DevKitC). Board chỉ có cổng qua chip USB-UART: `menuconfig` → *Component config → ESP System Settings → Channel for console output → Default: UART0*.

### Ghi, giải mã, báo cáo

```bash
pip install pyserial numpy
cd ESP32-S3/radar-fusion-presence/tools
python3 capture.py -p /dev/ttyACM0 --cmd "info all" --marks --label "E5 phòng ngủ, trống, quạt tắt"
#   gõ trong lúc ghi:  seg empty   |   seg sit_2.6m   |   seg end   |   !eng off   (dòng bắt đầu bằng ! là lệnh probe)
python3 decode.py capture_20261020_093000.rlog          # → thư mục _csv: ld2410.csv, ld2450.csv, replies.csv, events.csv...
python3 report.py capture_20261020_093000.rlog --compare empty sit_2.6m -o report.md
```

Thí nghiệm có lịch tự động (ví dụ E8, bật/tắt LD2450 mỗi 5 phút qua công tắc tải):

```bash
python3 capture.py -p /dev/ttyACM0 --schedule schedules/e8_ld2450_ab.txt --duration 3610
python3 report.py capture_....rlog --compare ld2450_off ld2450_on
```

Chưa có phần cứng: thử toàn bộ chuỗi công cụ với dữ liệu **giả lập** (số bịa, không phải số đo):

```bash
python3 simulate.py -o demo.rlog --scenario empty:300,fan:300,person_still:600,walk:120
python3 report.py demo.rlog --compare empty fan
```

### Lệnh của probe

Gõ trên cổng USB (bằng `capture.py --marks` với tiền tố `!`, hoặc `idf.py monitor`); kênh = số UART (`1`, `2`) hoặc tên radar (`2410`, `2450`):

| Lệnh | Tác dụng |
|---|---|
| `info [kênh\|all]` | đọc phiên bản, cấu hình, Bluetooth (chỉ đọc); `all` thử cả lệnh đọc của loại radar kia |
| `eng [kênh] on\|off` | engineering mode LD2410 (năng lượng từng gate) |
| `track single\|multi` | chế độ mục tiêu LD2450 (lưu trong radar) |
| `bt <kênh> on\|off` | Bluetooth của radar (lưu, radar khởi động lại) |
| `restart <kênh>` | khởi động lại radar |
| `scan <kênh>` | dò baud rate của radar |
| `baud <kênh> <tốc độ>` | chỉ đổi tốc độ UART của ESP32 |
| `setbaud <kênh> <tốc độ>` | ghi baud rate vào radar (khởi động lại), rồi theo tốc độ mới |
| `cmd <kênh> <lệnh> [hex]` | gửi lệnh bất kỳ giữa enable/end configuration (cho người biết rõ lệnh) |
| `raw on\|off` | bản ghi khung `@D/@A` (ACK luôn được ghi) |
| `dump <kênh> on\|off` | byte UART thô `@R` (khi radar gửi định dạng lạ) |
| `hum on\|off` | dòng tóm tắt mỗi giây `@H` |
| `stats` | bộ đếm ngay (`@S`) |
| `mark <chữ>` | sự kiện đánh dấu (nhãn thí nghiệm) |
| `sync <ms>` | đồng bộ đồng hồ PC (capture.py tự gửi mỗi 10 s) |
| `pwr <kênh> on\|off\|cycle` | công tắc nguồn radar (nếu có đấu) |

Nút BOOT (GPIO0) ghi sự kiện `button`: dùng để đánh dấu khi người đo đang ở trong phòng.

### Định dạng bản ghi

Mỗi dòng `@<loại>,<trường...>*<checksum>`, checksum là XOR các ký tự giữa `@` và `*` (như NMEA). Dòng không bắt đầu bằng `@` là log của ESP-IDF.

| Loại | Trường | Ý nghĩa |
|---|---|---|
| `B` | t_us, khoá=giá trị… | khởi động: phiên bản probe, ESP-IDF, chip, lý do reset |
| `C` | kênh, t_us, uart, rx, tx, baud | cấu hình kênh (cả khi đổi baud) |
| `D` | kênh, seq, t_us, hex | **khung báo cáo của radar** (nguyên byte); t_us = thời điểm byte cuối của khung |
| `A` | kênh, seq, t_us, hex | ACK của radar (hoặc lệnh của chính ta bị vòng lại: lỗi đấu dây) |
| `X` | kênh, t_us, hex | lệnh probe đã gửi |
| `R` | kênh, t_us, hex | byte UART thô (`dump`) |
| `I` | kênh, t_us, khoá, nội dung | giải mã ACK: `radar`, `version`, `params`, `bluetooth`, `resolution`, `tracking`, `zones`… |
| `E` | t_us, nguồn, nội dung | marker, nút, job, radar im lặng/hoạt động lại, lỗi cấu hình |
| `S` | kênh, t_us, khoá=giá trị… | bộ đếm: khung, rác, footer sai, lỗi giải mã, mục tiêu bất hợp lý, tràn UART, tuổi khung (kênh 0: probe) |
| `H` | t_us, nội dung | tóm tắt cho người đọc |
| `T` | t_us, ms PC | trả lời `sync` |
| `K` | t_us, ok/error … | trả lời lệnh console |

### Test trên máy tính

```bash
test/host/run.sh
```

Không cần ESP32 hay ESP-IDF, chỉ cần g++ và Python 3 (+ numpy cho test báo cáo). Gồm:
- **Giao thức** (`test_radar_proto.cpp`): 35 dòng byte trong `test/vectors/hlk_vectors.txt` (các ví dụ in trong tài liệu Hi-Link + các ca nhiễu đường truyền), mỗi dòng nạp nguyên khối, từng byte và từng 7 byte; giải mã phản hồi và tạo lệnh so với ví dụ của tài liệu; dòng byte ngẫu nhiên. Biên dịch với AddressSanitizer/UBSan.
- **Probe** (`test_probe.cpp`): chạy đúng mã `radar_probe/main/probe_radar.cpp` với radar **mô phỏng** theo tài liệu (`sim_probe.cpp`): nhận dạng radar, chuỗi lệnh `info`, engineering mode, dò baud khi radar ở 115200, radar thiếu lệnh mới, không có radar, job từ console (tắt engineering, restart, đổi baud), radar mất rồi có lại; kiểm tra mốc thời gian của từng khung (±2 µs).
- **Định dạng hai đầu**: bản ghi do probe sinh ra được `decode.py` giải mã không lỗi.
- **Công cụ Python** (`tools/tests`): cùng vector giao thức (bộ giải mã Python phải cho kết quả giống hệt C++), giải mã `.rlog`, phát hiện checksum sai / mất bản ghi / khởi động lại, báo cáo trên dữ liệu giả lập (quạt làm tăng gate 4, chu kỳ 12 s, dải thở), AUC, độ trễ giữa hai radar, lịch lệnh.

## Cấu trúc thư mục

```
radar-fusion-presence/
├── README.md
├── docs/
│   ├── thiet-ke-tong-the.md        # phương án tổng thể (12 mục)
│   └── phase1/                     # nguồn tài liệu, giao thức hai radar, kế hoạch thí nghiệm
├── components/radar_proto/         # tách khung, giải mã báo cáo/ACK, tạo lệnh (C/C++ thuần, dùng chung probe và firmware sau này)
│   ├── include/hlk_frame.h  ld2410_proto.h  ld2450_proto.h
│   └── hlk_frame.cpp  ld2410_proto.cpp  ld2450_proto.cpp
├── radar_probe/                    # firmware đo đạc Phase 1 (ESP-IDF v6.0.2, esp32s3)
│   └── main/probe_main.cpp (console, nút, tóm tắt)  probe_radar.cpp (task từng radar, job, dò baud)  probe_out.cpp (bản ghi)
├── tools/                          # capture.py, decode.py, report.py, simulate.py, hlk_proto.py, schedules/, tests/
└── test/
    ├── host/                       # run.sh, test C++ (giao thức, probe + radar mô phỏng, stub ESP-IDF)
    └── vectors/hlk_vectors.txt     # vector giao thức dùng chung C++ / Python
```
