# ESP32-H2 Matter over Thread: công tắc relay GPIO4

Firmware ESP-IDF + [esp-matter](https://github.com/espressif/esp-matter) cho **ESP32-H2**. Thiết bị là một **công tắc/ổ cắm Matter** (On/Off Plug-in Unit) điều khiển relay ở **GPIO4**, kết nối mạng bằng **Thread** (802.15.4), commissioning qua **BLE**.

Cấu trúc Matter giống bản ESP32-C3 Wi-Fi trong repo này (`ESP32-C3/matter-wifi/matter-energy-bl0942`), bỏ phần đo công suất và thay Wi-Fi bằng OpenThread.

| Chức năng | Chân |
|---|---|
| Relay (active-high mặc định) | **GPIO4** |
| Nút BOOT, nhấn ngắn: bật/tắt relay | GPIO9 |
| Nút BOOT, giữ 5 s rồi thả: factory reset | GPIO9 |
| LED RGB WS2812 trên board: sáng xanh lá khi relay ON | GPIO8 |

- Trạng thái relay được lưu NVS và khôi phục sau khi mất điện.
- Khi khởi động, GPIO4 được đặt mức OFF *trước* khi chuyển sang output, và không bật pull-up, nên relay không bị nhảy lúc boot.
- **Số serial** (Basic Information → SerialNumber) lấy từ địa chỉ MAC gốc trong eFuse, 12 ký tự hex in hoa, ví dụ `744DBDA1B2C3`. Mỗi chip một số khác nhau, không đổi sau `erase-flash`. Xem trong Apple Home (phụ kiện → cài đặt → **Số sê-ri**) hoặc dòng log `serial_number: Serial number: ...` khi khởi động.
- Thiết bị chạy dạng **FTD** (Full Thread Device): cấp nguồn lưới nên có thể làm Thread Router, giúp mở rộng mạng mesh.

## Yêu cầu mạng

Matter over Thread cần một **Thread Border Router** trong nhà, ví dụ:

- Apple: HomePod mini / HomePod 2 / Apple TV 4K (bản có Ethernet hoặc 2022)
- Google: Nest Hub (2nd gen) / Nest Hub Max / Nest Wifi Pro
- Amazon: Echo 4th gen
- Home Assistant: SkyConnect / ZBT-1 + OpenThread Border Router add-on
- Tự làm: ESP Thread Border Router (ESP32-S3 + ESP32-H2)

Điện thoại gửi thông tin mạng Thread cho thiết bị qua BLE trong lúc ghép nối, nên không cần nhập SSID/mật khẩu.

## Cấu hình

`idf.py menuconfig` → **Relay Configuration**:

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `RELAY_GPIO` | 4 | chân điều khiển relay |
| `RELAY_ACTIVE_LEVEL` | 1 | 1 = active-high, 0 = active-low (module relay opto rẻ tiền thường là active-low) |
| `SERIAL_NUMBER_PREFIX` | (trống) | tiền tố số serial, ví dụ `RLY-` → `RLY-744DBDA1B2C3` (*Device Identity*) |
| `STATUS_LED_ENABLE` | y | LED WS2812 GPIO8 hiển thị trạng thái relay; tắt nếu board tự làm không có LED này |

Hoặc sửa trực tiếp `sdkconfig.defaults.esp32h2` rồi xoá `sdkconfig` để áp dụng lại.

Khi đổi chân relay, tránh các chân strapping của ESP32-H2 (GPIO2, GPIO3, GPIO8, GPIO9, GPIO25) và chân flash (GPIO15–GPIO21). GPIO4 là chân MTCK của JTAG, nhưng mặc định JTAG đi qua USB-Serial-JTAG nên GPIO4 dùng làm GPIO bình thường.

## Build

Yêu cầu: ESP-IDF v6.0.2 + esp-matter release/v1.6 (đã `export.sh` cả hai). Cách nhanh nhất là dùng Docker:

```bash
docker run --rm -it -v $PWD:/project \
  espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c "cd /project && idf.py set-target esp32h2 && idf.py build"
```

Hoặc cài local:

```bash
. ~/esp/esp-idf/export.sh
. ~/esp/esp-matter/export.sh
idf.py set-target esp32h2
idf.py build
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

GitHub Actions (`.github/workflows/matter-relay-h2-thread.yml` ở gốc repo) tự build và upload file `.bin`, trong đó có `matter_relay_h2_merged.bin` để nạp một lần ở địa chỉ `0x0`:

```bash
esptool.py --chip esp32h2 -p /dev/ttyACM0 write_flash 0x0 matter_relay_h2_merged.bin
```

Vào tab **Actions** → lần chạy mới nhất → **Artifacts**.

## Phiên bản firmware

Mỗi lần build, firmware tự có phiên bản mới dạng `1.0.<yyMMddHHmm>-<commit>` (giờ build theo UTC + 7 ký tự đầu mã commit git). Ví dụ `1.0.2610121430-6a81e10` là bản build lúc 14:30 UTC ngày 12/10/2026 từ commit `6a81e10`. Xem ở Apple Home (phụ kiện → cài đặt → **Firmware**) hoặc dòng `App version:` trong log khởi động.

- Hậu tố `-dirty` (ví dụ `1.0.2610121430-6a81e10-dirty`) nghĩa là lúc build thư mục dự án có thay đổi chưa commit.
- `nogit` nghĩa là build ngoài repo git (ví dụ thư mục đã copy ra ngoài).
- Số phiên bản Matter dùng cho OTA (`SoftwareVersion`) chỉ là phần thời gian `yyMMddHHmm`, nên luôn tăng.
- Build local: commit mới (hoặc checkout) sẽ tự cấu hình lại và lấy phiên bản mới ở lần build sau. Ngoài ra chạy `idf.py fullclean` để lấy số mới.

## Commissioning

Firmware dùng chứng chỉ test (DAC test) của esp-matter, mã ghép nối mặc định:

- Manual code: **34970112332**
- QR: `MT:Y.K9042C00KA0648G00`
- Discriminator 3840, passcode 20202021

Mở Apple Home / Google Home / SmartThings / Home Assistant → *Add Matter device* → quét QR hoặc nhập mã ở trên. Vì dùng chứng chỉ test, app sẽ cảnh báo "Uncertified accessory": chọn tiếp tục.

Với chip-tool (lấy dataset từ Border Router, ví dụ `ot-ctl dataset active -x`):

```bash
chip-tool pairing ble-thread 0x7283 hex:<ACTIVE_DATASET> 20202021 3840
chip-tool onoff toggle 0x7283 1
chip-tool onoff read on-off 0x7283 1
```

Sau khi bật nguồn, thiết bị quảng bá BLE nhanh trong 30 s rồi chuyển sang chậm (log `bleAdv Timeout : Start slow advertisement`, chu kỳ 500 ms). Đây là bình thường: thiết bị vẫn chờ ghép nối, cứ quét QR như bình thường. Cửa sổ ghép nối mở 15 phút; hết thời gian thì rút điện cắm lại.

> Nếu thiết bị đã từng được thêm vào Home với firmware khác, hãy xoá phụ kiện, `erase-flash`, nạp lại rồi thêm lại.

## Xử lý sự cố: điện thoại không tìm thấy thiết bị

Ghép nối Matter over Thread có 2 giai đoạn:

1. **Qua BLE:** điện thoại tìm thấy thiết bị, kiểm tra chứng chỉ, gửi thông tin mạng Thread. Log: `Role detached -> child`, `advertising srp service: ...._matter._tcp`.
2. **Qua mạng IP:** điện thoại tìm dịch vụ `_matter._tcp` của thiết bị thông qua Border Router (Wi-Fi/LAN → Thread) rồi mở phiên bảo mật CASE. Log thành công: `Commissioning complete`.

Nếu giai đoạn 1 xong mà app báo không tìm thấy thiết bị, thường là do mạng nhà, không phải do firmware:

- Điện thoại và Border Router phải cùng một mạng Wi-Fi/LAN (không dùng mạng khách, tắt VPN trên điện thoại).
- Router Wi-Fi phải bật **IPv6** và không chặn **mDNS/multicast**; tắt "AP/client isolation".
- Border Router phải là của hệ đang dùng (Apple Home → HomePod/Apple TV làm Home hub; Google Home → Nest Hub/Nest Wifi; Home Assistant → OTBR add-on). Thử khởi động lại Border Router.
- Mỗi lần thử lại: `erase-flash` hoặc factory reset (giữ nút BOOT 5 s) để xoá thông tin mạng Thread cũ.

### Log chẩn đoán tự động

Khi chưa được ghép nối, cứ 10 s firmware tự in khối `thread_diag` ra log (tắt bằng `THREAD_DIAG_LOG`, tự dừng khi ghép nối xong):

```
I thread_diag: ===== Thread diagnostics =====
I thread_diag: Role: router, RLOC16: 0x7c00
I thread_diag: Address: fd61:c2f3:2668:ad22:0:ff:fe00:7c00  [mesh-local RLOC]
I thread_diag: Address: fd12:3456:789a:1:abcd:...  [OMR (reachable from Wi-Fi/LAN)]
I thread_diag: On-mesh prefix: fd12:3456:789a:1::/64 (border router RLOC16 0x2c00, SLAAC)
I thread_diag: SRP host: 744DBDA1B2C3..., state: Registered
I thread_diag: SRP service: 2AB1...-....._matter._tcp, state: Registered
I thread_diag: Summary: OMR address YES, on-mesh prefixes 1, SRP host Registered, SRP service Registered
```

Chỉ cần xem log, không phải gõ lệnh. `OMR address NO` hoặc SRP không `Registered` nghĩa là lỗi phía Border Router / mạng LAN.

### Gõ lệnh OpenThread CLI (tuỳ chọn)

Gõ lệnh trên serial monitor ở **115200 baud**, qua cổng USB-C **UART** của DevKit (chip USB-UART, hiện là `COMx` / `/dev/ttyUSB0`). Cổng USB-C còn lại (USB gốc của ESP32-H2) chỉ xuất log, **không nhận lệnh gõ**. Gõ `matter help` để thử. Gõ trong lúc điện thoại vẫn đang ghép nối:

| Lệnh | Kết quả mong đợi |
|---|---|
| `matter esp ot_cli state` | `router` hoặc `child` |
| `matter esp ot_cli ipaddr` | ngoài `fe80::...` và địa chỉ mesh-local (cùng tiền tố `fdxx:...` của dòng `detected server`), phải có thêm **một địa chỉ có tiền tố khác** (địa chỉ OMR do Border Router cấp). Không có địa chỉ này thì điện thoại không thể tới được thiết bị |
| `matter esp ot_cli netdata show` | mục `Prefixes:` có ít nhất 1 prefix (OMR), mục `Services:` có SRP server |
| `matter esp ot_cli srp client host` | `state:Registered` |
| `matter esp ot_cli srp client service` | `_matter._tcp ... state:Registered` |
| `matter esp ot_cli ping <địa chỉ>` | ping được Border Router / máy trong LAN |

- Không có địa chỉ OMR / `netdata` không có prefix: Border Router chưa định tuyến được giữa Wi-Fi và Thread (lỗi phía Border Router hoặc router Wi-Fi).
- SRP `state:Registered` và có địa chỉ OMR mà điện thoại vẫn không thấy: Border Router không quảng bá dịch vụ lên LAN, hoặc mạng Wi-Fi chặn mDNS.

## Cấu trúc

```
CMakeLists.txt             project esp-matter (esp32h2, device_hal esp32h2_devkit_c)
sdkconfig.defaults         cấu hình chung Matter/BLE, tắt cluster không dùng
sdkconfig.defaults.esp32h2 OpenThread (FTD, SRP client), relay GPIO4, LED trạng thái
partitions.csv             4 MB, 2 phân vùng OTA
main/app_main.cpp          tạo node + endpoint On/Off Plug-in Unit, cấu hình OpenThread
main/app_driver.cpp        điều khiển GPIO relay + nút + LED
main/serial_number.cpp     SerialNumber = MAC (custom Device Instance Info provider)
main/thread_diag.cpp       log chẩn đoán Thread/SRP khi chưa ghép nối
main/Kconfig.projbuild     tuỳ chọn relay, LED
```

## Sơ đồ đấu nối

```
ESP32-H2 GPIO4 ──► IN  (module relay 3.3V, hoặc qua transistor NPN/MOSFET + diode)
ESP32-H2 3V3   ──► VCC (module relay 5V: cấp 5V, IN vẫn dùng mức 3.3V)
ESP32-H2 GND   ──► GND
```

⚠️ Phía tải dùng điện 220 V: cách ly và đi dây an toàn. Không cắm USB vào máy tính khi mạch đang nối điện lưới mà không có cách ly.
