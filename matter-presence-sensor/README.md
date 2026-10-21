# Matter Presence Sensor (HLK-LD2410C)

Cảm biến hiện diện chuẩn Matter (device type **Occupancy Sensor 0x0107**) dùng radar 24 GHz **HLK-LD2410C**. Một mã nguồn firmware chung cho mọi bo: cùng thuật toán, cùng quy định về tên, phiên bản, entity, khoá lưu trữ. Các bo chỉ khác nhau ở **file cấu hình bo** (chân, LED, nút, console), tức là mỗi bo một bản firmware build từ cùng mã.

| Thư mục | Nội dung |
|---|---|
| [`firmware/`](firmware) | Firmware ESP-IDF + esp-matter chung cho mọi bo, host test, script build. Hướng dẫn đầy đủ: [`firmware/README.md`](firmware/README.md) |
| [`hardware/c3-radar-r45/`](hardware/c3-radar-r45) | Mạch in KiCad của bo **c3-radar r45** (ESP32-C3-WROOM-02-N4 + LD2410C, tròn Ø45 mm): sơ đồ, PCB, gerber, BOM, ảnh render, cấu hình ESPHome cũ |
| [`.github/workflows/matter-presence-sensor.yml`](../.github/workflows/matter-presence-sensor.yml) | CI: host test, rồi build mọi bo và lưu file firmware vào Artifacts |

## Các bo

| Bo (`tools/build.sh <bo>`) | Chip, kết nối | Radar (ESP RX ← radar TX, ESP TX → radar RX) | LED | Nút | Log | Trạng thái |
|---|---|---|---|---|---|---|
| `c3-supermini` | ESP32-C3 SuperMini, Wi-Fi | GPIO3, GPIO4 | GPIO8 một màu, sáng khi mức thấp | GPIO9 (BOOT) | USB-C (USB-Serial-JTAG) | build được; firmware cũ cùng chân đã chạy trên bo |
| `c3-wroom-devkit` | ESP32-C3-WROOM-02-N4 (kiểu DevKitC-02), Wi-Fi | GPIO3, GPIO4 | WS2812 GPIO8 | GPIO9 (BOOT) | USB (USB-Serial-JTAG) | build được; cùng cấu hình biến thể `esplearn` 1.0.175 (trước đó `wroom_n4`) |
| `c3-radar-r45` | mạch [`hardware/c3-radar-r45`](hardware/c3-radar-r45), ESP32-C3-WROOM-02-N4, Wi-Fi | IO10, IO3 | không có | pad IO9 (chập xuống GND) | pad RX0 / TX0 (UART0), USB-C chỉ cấp nguồn | build được; **chưa chạy trên bo** |
| `h2-*` (ESP32-H2 WROOM, H2 mini) | ESP32-H2, Thread | — | — | — | — | **chưa có**: bước sau, cần sơ đồ chân của hai bo |

Bản ESP32-H2 hiện tại vẫn ở `ESP32-H2/matter-presence-ld2410-learning` (chưa đổi). Khi chuyển vào đây, bản H2 sẽ dùng mã chung này.

## Quy định chung

- **Firmware mặc định giống nhau cho mọi bo:** radar quyết định có người hay không; ESP32 học phòng trống 120 s và đặt ngưỡng cho radar từ mức cao nhất (`LD2410_LEARN_PEAK`, xem `firmware/README.md`). Hold time mặc định 30 s.
- **Tên, phiên bản:** Manufacturer `Haxeko`, Model `Presence LD2410`, tên thiết bị `Presence-` + 4 ký tự cuối số serial, số serial = MAC trong eFuse, phiên bản `1.0.<số commit>`.
- **Matter:** VID `0xFFF1` (test). PID **`0x8001` cho ESP32-C3 (Wi-Fi)**, **`0x8000` cho ESP32-H2 (Thread)**: giữ như các bản đã ghép nối, không đổi. Endpoint 1 Occupancy Sensor (cùng Hold time, Learn empty room), 2 Range, 3 Radar resolution, 4 Radar Bluetooth.
- **Phiên bản và OTA:** SoftwareVersion là số commit của cả repo (`git rev-list --count HEAD`), nên phải lớn hơn bản đang chạy trên bo. Khi đưa nhánh vào `main`, dùng **merge commit, không squash**: squash làm số commit nhỏ đi và bo sẽ từ chối bản OTA.
- **OTA giữa các bo:** mọi bo C3 cùng VID/PID và cùng số phiên bản, Matter Server không phân biệt được. Chỉ cập nhật một bo bằng file có tên bo đó (`presence-<bo>-<phiên bản>.ota`). Nạp nhầm file của bo khác thì chân radar, LED, nút và cổng log sai (chưa thử trên bo): nạp lại đúng file qua USB / pad nạp.
- **Không đổi khi chưa thống nhất:** chân, giao thức radar, VID/PID, device type, endpoint, cluster, khoá NVS, bảng phân vùng, khoá và chứng chỉ.

## Build nhanh

```bash
cd matter-presence-sensor/firmware
tools/build.sh c3-wroom-devkit          # hoặc c3-supermini, c3-radar-r45
tools/build.sh c3-radar-r45 unique_setup  # thêm tính năng: features/<tên>.defaults
```

Cần ESP-IDF v6.0.2 + esp-matter 1.6.1 (Docker `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`). Kết quả trong `firmware/dist/`. Chi tiết: mục **Build** của `firmware/README.md`.

## Nguồn gốc

- `firmware/`: bản ổn định `ESP32-C3/matter-presence-ld2410` (nhánh `claude/c3-matter-presence-rewrite`) cộng cách học phòng trống của biến thể `esplearn` 1.0.175 (nhánh `claude/c3-wroom-esp-presence`). Bộ lọc năng lượng `espfilter` không đưa sang. Các thư mục cũ `ESP32-C3/`, `ESP32-H2/` để nguyên.
- `hardware/c3-radar-r45/`: toàn bộ nội dung repo `haxeko/presence-pcb` (commit `decc513`), không kèm lịch sử; thư mục `firmware/` của repo đó (ESPHome) đổi tên thành `esphome/`.
