# ESP32-C3 · Matter over Wi-Fi · 5 cảm biến hiện diện từ iBeacon

> Bản **ESP32-C3** của [`ESP32-C6/matter-ble-presence`](../../ESP32-C6/matter-ble-presence), dùng **chung mã nguồn**:
> thư mục này chỉ có `CMakeLists.txt`, `sdkconfig.defaults` (và README này). Thư mục `main`, trang web, bảng phân vùng,
> phần cấu hình chung và test lấy từ thư mục ESP32-C6 (`ble_presence.cmake`). Khác biệt chính: **Matter over Wi-Fi**
> thay vì Thread, vì ESP32-C3 không có radio 802.15.4.

Chức năng giống hệt bản C6: một node Matter với **5 endpoint Occupancy Sensor cố định** (Presence 1–5), mỗi endpoint
theo một iBeacon (địa chỉ MAC + loại địa chỉ). **Bấm BOOT 3 lần** → chế độ cấu hình web (SoftAP `VN-Presence-XXXX`,
`http://192.168.4.1`); trên trang bấm **"Khởi động lại về chế độ Matter"** (hoặc bấm BOOT 3 lần, hoặc để 10 phút không
thao tác) → thiết bị khởi động lại vào **Matter over Wi-Fi**, tự nối lại Wi-Fi nhà và fabric cũ, không phải ghép lại.

Cách dùng trang web, thuật toán hiện diện, lưu trữ và xử lý lỗi NVS, các module: xem
[README bản C6](../../ESP32-C6/matter-ble-presence/README.md). Tài liệu này chỉ ghi phần khác.

> **Trạng thái:** **build thành công** (ESP-IDF v6.0.2 + esp-matter v1.6.1, số liệu bên dưới), host test và test trang
> web đạt (chung với bản C6). **Chưa chạy trên phần cứng**: Wi-Fi + BLE trên một radio, heap còn lại khi giữ BLE sau
> khi ghép, ghép với Apple Home / Google Home / Home Assistant phải đo theo [quy trình](#kiểm-thử-phần-cứng).

---

## Khác biệt so với bản ESP32-C6

| | ESP32-C6 | ESP32-C3 (thư mục này) |
|---|---|---|
| Mạng Matter | Thread (Minimal End Device), cần Thread Border Router | **Wi-Fi 2,4 GHz** (station), nối thẳng router nhà |
| Ghép nối | BLE, điện thoại gửi dataset Thread | BLE, điện thoại gửi **SSID và mật khẩu Wi-Fi** |
| Tìm thấy trên mạng | DNS-SD qua SRP của Border Router | mDNS trên mạng LAN (minimal mDNS của Matter SDK) |
| BLE scan cùng mạng (bảng coexistence ESP-IDF) | Thread End Device: **C1** (hỗ trợ, không ổn định) | Wi-Fi station đã kết nối: **Y** (hỗ trợ, ổn định) |
| Scanner tạm dừng khi | Thread mất kết nối (`network_down`) | Wi-Fi mất kết nối (`network_down`) |
| Thông tin mạng lưu ở | dataset Thread trong `nvs` (OpenThread) | SSID/mật khẩu trong `nvs` (driver Wi-Fi, `CONFIG_ESP_WIFI_NVS_ENABLED=y`) |
| VID / PID | `0xFFF1` / `0x8002` | `0xFFF1` / **`0x8003`** |
| Tên file build | `matter_ble_presence` | **`matter_ble_presence_c3`** |
| Console Matter (`matter esp ...`) | bật | **tắt** (tiết kiệm ~10 KB heap), log vẫn ra cổng serial |
| Log chẩn đoán mạng (60 s) | vai trò Thread, RSSI parent, bộ đếm MAC/MLE | RSSI, kênh Wi-Fi, số lần nối/mất, mã lý do mất kết nối |
| SRAM | 512 KB | **400 KB**: xem [RAM](#ram) |

PID riêng giúp controller không nhầm hai loại firmware.

## Phần cứng

* **ESP32-C3 SuperMini** (flash 4 MB) hoặc **ESP32-C3-DevKitM-1** (module ESP32-C3-MINI-1, flash 4 MB). Board ESP32-C3
  khác có flash ≥ 4 MB cũng chạy được (bảng phân vùng chung: app `factory` 3,6 MB).
* Không cần linh kiện ngoài: chỉ dùng nút **BOOT** có sẵn.

### Nút BOOT

| | |
|---|---|
| Chân | **GPIO9**, mức tích cực **thấp** (`CONFIG_PRESENCE_BOOT_GPIO=9`, `PRESENCE_BOOT_ACTIVE_LOW=y`) |
| Kéo lên | **pull-up nội** của GPIO9 bật trong firmware (`PRESENCE_BOOT_PULLUP=y`) |
| Chống dội | phần mềm 40 ms, đọc mỗi 10 ms bằng `esp_timer` (không ngắt) |
| 3 lần bấm | 3 lần bấm-nhả, **bấm chậm được**: lần sau chỉ cần bắt đầu trong 2 s kể từ lần nhả trước (`PRESENCE_BOOT_CLICK_GAP_MS`), mỗi lần bấm ≤ 1,5 s (giữ lâu hơn không tính); log `BOOT click 1 of 3`… cho từng lần; **không phải factory reset** |

Đã đối chiếu **sơ đồ nguyên lý chính thức ESP32-C3-DevKitM-1 V1** (`SCH_ESP32-C3-DEVKITM-1_V1_20200915A.pdf` của
Espressif): nút Boot **SW1 nối GPIO9 xuống GND**, **không có điện trở kéo lên ngoài** trên nút, tụ chống dội C13
0,1 µF **không lắp (NC)** → cần pull-up nội và chống dội phần mềm như trên. Mạch auto-download (DTR/RTS của CP2102N →
transistor → EN và IO9) giống bản C6. ESP32-C3 SuperMini không có sơ đồ chính thức; các board phổ biến cũng nối nút
BOOT vào GPIO9 xuống GND (như các firmware C3 khác trong repo này).

**Khi reset phần cứng:** GPIO9 là chân strapping. Nếu GPIO9 ở mức thấp lúc chip khởi động (đang giữ BOOT khi bấm RESET
hoặc cắm điện, hoặc DTR/RTS của cổng serial kéo xuống), ROM vào **chế độ nạp firmware** và firmware không chạy: nhả
BOOT rồi bấm RESET. Bấm BOOT 3 lần chỉ có tác dụng khi firmware đang chạy. GPIO8 (LED trên SuperMini) cũng là chân
strapping, firmware không dùng.

### Cổng serial

Log ra **UART0** (DevKitM-1: cổng micro-USB qua CP2102N, `/dev/ttyUSB0`) và **USB-Serial-JTAG** (SuperMini: cổng
USB-C, `/dev/ttyACM0`) cùng lúc (mặc định của ESP-IDF cho ESP32-C3: console chính UART0, phụ USB-Serial-JTAG). SSID,
**mật khẩu SoftAP**, mã QR/mã ghép thủ công đều in ở đây khi khởi động.

---

## Wi-Fi và BLE trên một radio

ESP32-C3 có một radio 2,4 GHz; Wi-Fi và BLE chia thời gian (coexistence phần mềm, `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`).
Bảng *Supported Features of Wi-Fi and BLE Coexistence* trong hướng dẫn RF Coexistence của ESP-IDF v6.0.2:

| Wi-Fi | BLE Scan | Dùng ở |
|---|---|---|
| Station: scan / connecting / **connected** | **Y** – hỗ trợ, hiệu năng ổn định | chế độ NORMAL |
| SoftAP: TX beacon | Y | CONFIG, chưa có điện thoại nối |
| SoftAP: connecting / connected | **C1** – hỗ trợ, hiệu năng không ổn định | CONFIG, điện thoại đang mở trang |

Ở trạng thái Wi-Fi CONNECTED, chu kỳ coexistence bắt đầu tại thời điểm beacon của router (TBTT); Wi-Fi ở chế độ modem
sleep (mặc định, bắt buộc khi có Bluetooth) nên giữa các beacon radio dành cho BLE. Firmware vẫn:

* quét **thụ động**, mặc định cửa sổ 40 ms / chu kỳ 160 ms (25%), chỉnh được trên trang web (trang cảnh báo khi > 50%);
  xác suất nghe beacon và cách chọn timeout: bảng ở README bản C6;
* tạm dừng quét khi chưa ghép, đang ghép (PASE), có kết nối BLE từ controller, và khi **Wi-Fi mất kết nối**
  (`network_down`), chạy lại **10 s sau khi Wi-Fi nối lại** (`PRESENCE_SCAN_RESUME_DELAY_SEC`) để thiết bị kịp lấy địa
  chỉ IPv6, quảng bá mDNS và controller dựng lại phiên. Thời gian vắng mặt tính trên "đồng hồ quét" nên tạm dừng không
  gây báo vắng sai.

---

## RAM

ESP32-C3 có 400 KB SRAM chung cho code (IRAM) và dữ liệu, và firmware này **giữ BLE sau khi ghép** (để quét beacon).
Theo tài liệu tối ưu RAM của esp-matter (`docs/en/optimizations.rst`, ví dụ `light` trên ESP32-C3, ESP-IDF 5.4.1):
D/IRAM tĩnh 195 080 byte, heap trống **35 976 byte khi khởi động** (BLE đang chạy) và **101 580 byte sau khi ghép**
(BLE đã được giải phóng) – tức BLE chiếm khoảng 65 KB heap.

Bản C3 áp dụng các tùy chọn trong tài liệu đó không làm đổi hành vi (`sdkconfig.defaults` thư mục này): tắt console
Matter, `ESP_MATTER_MAX_DEVICE_TYPE_COUNT=2`, buffer sự kiện critical 1 KB, controller BLE tối đa 3 hoạt động, giảm một
nửa buffer MSYS của NimBLE, buffer Wi-Fi 6/16/16, toàn bộ code Wi-Fi ra flash (kèm phần chung: `ESP_WIFI_IRAM_OPT=n`,
`ESP_WIFI_RX_IRAM_OPT=n`). Kết quả build: **DRAM tĩnh 149 959 / 321 296 byte, còn 171 337 byte cho heap lúc khởi
động**, ít hơn ví dụ `light` khoảng 45 KB.

Heap thực tế khi Matter + Wi-Fi + BLE + 5 slot cùng chạy **chưa đo được** (cần phần cứng). Log `diag` in mỗi 60 s:
`heap free ... min ... largest ...`; xem giá trị `min` sau khi ghép, sau khi controller subscribe và sau vài giờ. Nếu
`min` xuống dưới khoảng 20 KB, thêm vào `sdkconfig.defaults` (theo thứ tự):

1. `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y`: đưa code controller BLE từ ROM/IRAM ra flash, **thêm hơn 20 KB**; BLE chậm hơn
   và có thể trục trặc lúc xóa flash (ghi NVS) nếu chip flash không hỗ trợ auto-suspend (`CONFIG_SPI_FLASH_AUTO_SUSPEND`).
2. Buffer Wi-Fi nhỏ hơn như cấu hình ESP32-C2 của esp-matter: `STATIC_RX 3`, `DYNAMIC_RX 6`, `DYNAMIC_TX 16`.

---

## Build

```bash
docker run --rm -it -v "$PWD":/project -w /project/ESP32-C3/matter-ble-presence \
    espressif/esp-matter:release-v1.6.1_idf_v6.0.2 bash
# trong container:
git config --global --add safe.directory '*'
idf.py set-target esp32c3
idf.py build
```

Phiên bản đã ghim (ESP-IDF v6.0.2 `7101770dc6`, esp-matter release/v1.6.1 `4c9dd7aa80`, connectedhomeip `b1902b39ac`,
image Docker theo digest): bảng ở README bản C6. Cấu hình: `ESP32-C6/matter-ble-presence/sdkconfig.defaults` (chung),
`sdkconfig.defaults.matter` (Matter) rồi `sdkconfig.defaults` thư mục này (Wi-Fi, PID, RAM);
`sdkconfig.defaults.esp32c6` của bản C6 **không** áp dụng.
Sau khi sửa các file này chạy lại `idf.py set-target esp32c3` (hoặc xóa `sdkconfig`). Tùy chọn dự án:
`idf.py menuconfig` → **BLE Presence x5**.

CI: `.github/workflows/matter-ble-presence-c3.yml` build bản này mỗi khi thư mục này hoặc mã nguồn chung thay đổi.

## Nạp firmware

```bash
idf.py -p /dev/ttyACM0 flash monitor      # SuperMini: cổng USB-C
idf.py -p /dev/ttyUSB0 flash monitor      # DevKitM-1: cổng micro-USB (CP2102N)
```

Hoặc `build/flash_args`: `0x0 bootloader.bin`, `0xc000 partition-table.bin`, `0x20000 matter_ble_presence_c3.bin`
(`--flash-mode dio --flash-freq 80m --flash-size 4MB`). `idf.py flash` không xóa `nvs` (ghép Matter, Wi-Fi) và
`app_cfg` (cấu hình beacon). Nếu chip trước đó chạy firmware khác (bảng phân vùng khác): `idf.py -p PORT erase-flash`
một lần.

## Ghép Matter qua Wi-Fi

Cần: Wi-Fi **2,4 GHz** (ESP32-C3 không có 5 GHz), WPA2/WPA3-Personal; router cho multicast IPv6/mDNS đi giữa
controller và thiết bị (không bật client isolation/AP isolation); controller Matter (Apple Home, Google Home, Home
Assistant với Matter Server, `chip-tool`).

1. Nạp firmware, mở console: lần đầu thiết bị in `Not commissioned: commissioning window open (BLE)` cùng
   `SetupQRCode: [MT:...]` và `Manual pairing code: [...]` (thử nghiệm: passcode `20202021`, discriminator `3840`).
2. Ghép bằng ứng dụng (quét QR, chọn mạng Wi-Fi 2,4 GHz) hoặc:
   ```bash
   chip-tool pairing ble-wifi 0x12 "<SSID>" "<mật khẩu Wi-Fi>" 20202021 3840
   chip-tool occupancysensing read occupancy 0x12 1      # endpoint 1..5
   chip-tool descriptor read parts-list 0x12 0           # [1, 2, 3, 4, 5]
   ```
3. Sau `Commissioning complete` → `Wi-Fi up (RSSI ... dBm, channel ...)` → địa chỉ IPv6 → scanner chạy lại sau 10 s.
   Khởi động lại sau đó: tự nối Wi-Fi đã lưu và dùng lại fabric.

**Gỡ ghép (factory reset)** khi console Matter tắt: xóa thiết bị khỏi controller (khi không còn fabric nào, thiết bị
mở lại cửa sổ ghép 5 phút và in lại mã), hoặc chỉ xóa phân vùng `nvs` (fabric, Wi-Fi, hiệu chuẩn RF) mà giữ cấu hình
beacon trong `app_cfg`:

```bash
python -m esptool --chip esp32c3 -p PORT erase-region 0x10000 0xC000
```

Muốn có lại console Matter (`matter esp factoryreset`, `matter esp wifi ...`): bật `CONFIG_ENABLE_CHIP_SHELL` trong
menuconfig (tốn khoảng 10 KB heap; nhập lệnh qua UART0).

## Cấu hình beacon (chế độ CONFIG)

Giống bản C6: bấm BOOT 3 lần → nối Wi-Fi `VN-Presence-XXXX` bằng mật khẩu in trên console → mở `http://192.168.4.1` →
quét, gán beacon cho Presence 1–5, **Lưu cấu hình** → **Khởi động lại về chế độ Matter**.

Riêng bản C3:

* Ở CONFIG, Wi-Fi chỉ chạy SoftAP: thiết bị **rời mạng Wi-Fi nhà**, controller báo "không phản hồi" cho tới khi thiết bị
  về NORMAL. Dữ liệu ghép và SSID/mật khẩu Wi-Fi nhà **không bị đụng tới**: SoftAP khởi tạo driver Wi-Fi với
  `nvs_enable = 0` (cấu hình chỉ ở RAM), nên không ghi gì vào `nvs`.
* SoftAP + điện thoại đang nối + quét BLE là mức **C1**: firmware chỉ quét BLE khi có điện thoại nối SoftAP, bắt đầu
  5 s sau khi điện thoại vào, 40 ms mỗi 160 ms (chung với bản C6). Danh sách beacon trống vài giây đầu.
* SoftAP phát ở **8,5 dBm** (`PRESENCE_AP_LIMIT_TX_POWER`, mặc định bật trên ESP32-C3): board ESP32-C3 SuperMini hay
  rớt/không nối được Wi-Fi ở công suất tối đa 20 dBm; điện thoại ở gần lúc cấu hình nên công suất thấp hơn không ảnh
  hưởng. Chỉ SoftAP của CONFIG; Wi-Fi station của Matter (NORMAL) vẫn dùng mặc định của driver. Kênh 20 MHz.
* Kênh SoftAP cố định (mặc định 1, `PRESENCE_AP_CHANNEL`), không phụ thuộc kênh Wi-Fi nhà.

---

## Kiểm thử

**Host test** (10 bộ, ASan/UBSan) và **test trang web** (Playwright, 43 kiểm tra, gồm phần chữ "Wi-Fi"/mức Y của bản
C3): chung với bản C6, xem mục *Kiểm thử* trong README bản C6. Kết quả trong phiên này: tất cả đạt.

### Kết quả build thực tế

Trong `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`, đúng hai lệnh ở mục [Build](#build) (`set-target esp32c3` tạo
`sdkconfig` mới từ các file defaults):

```
matter_ble_presence_c3.bin binary size 0x1889e0 bytes. Smallest app partition is 0x3a0000 bytes. 0x217620 bytes (58%) free.
Project build complete.
```

| Hạng mục | Giá trị |
|---|---|
| App `matter_ble_presence_c3.bin` | 1 608 160 byte (0x1889E0) trong phân vùng `factory` 3 712 KB: **còn trống 58%** |
| Bootloader | 21 088 byte (0x5260) |
| Flash (`idf.py size`) | code 1 254 864 byte, dữ liệu 267 676 byte |
| DRAM tĩnh | 149 959 / 321 296 byte (46,7%): IRAM .text 69 918, .bss 65 224, .data 14 817 → **171 337 byte cho heap** lúc khởi động |
| RTC SLOW | `.rtc_noinit` 16 byte = bản ghi yêu cầu CONFIG (giữ qua reset phần mềm), `.rtc_reserved` 24 byte |
| Cảnh báo biên dịch | 0 |
| Thành phần | cjson 1.7.19~2, mdns 1.14.1, esp_secure_cert_mgr 2.9.3, esp_encrypted_img 2.7.0, esp_delta_ota 1.1.4 (`dependencies.lock`) |

Bản C6 build lại sau khi tách mã chung: `sdkconfig` sinh mới từ các file defaults **giống hệt từng giá trị** cấu hình đã
kiểm chứng trước đó.

### Kiểm thử phần cứng

Theo quy trình 10 bước của README bản C6, thay phần Thread bằng Wi-Fi:

| # | Bước | Kỳ vọng / đo |
|---|---|---|
| 1 | Ghép `ble-wifi`, đọc parts-list, occupancy 1–5, feature-map | `[1,2,3,4,5]`, occupancy 0, feature-map 0x240 |
| 4 | Khởi động lại | `Commissioned (1 fabric(s)), Wi-Fi RSSI ...`, không cần ghép lại |
| 8 | Tắt/bật router Wi-Fi (hoặc đổi mật khẩu rồi đổi lại) | `Wi-Fi down (reason N)` → scanner `paused by network_down` → `Wi-Fi up` → quét lại sau 10 s; không OFF giả trong thời gian dừng |
| 10 | Chạy ≥ 24 giờ, đặt cách router ở khoảng cách thường dùng | ghi: báo vắng sai, số lần `Wi-Fi down` và mã lý do, RSSI, độ trễ lệnh/subscription của controller, **heap min**, `unexpected stops`, `queue drops` |

Thêm: ghép thêm controller thứ hai (multi-admin) để kiểm tra heap khi có nhiều phiên CASE và subscription.

## Đã xác minh / cần thử trên phần cứng

**Đã xác minh:** API Wi-Fi của Matter SDK đúng phiên bản (`kWiFiConnectivityChange`, `IsWiFiStationConnected`, driver
Wi-Fi tự lưu thông tin mạng vào NVS), bảng coexistence Wi-Fi + BLE của ESP-IDF v6.0.2, Kconfig controller BLE của C3
(`BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD`, `BT_CTRL_BLE_MAX_ACT`), sơ đồ nút BOOT của ESP32-C3-DevKitM-1, build bản C3
và bản C6 thành công, host test và test trang web đạt.

**Cần thử:** heap còn lại khi giữ BLE (quan trọng nhất), tỉ lệ nghe beacon và độ ổn định Wi-Fi khi quét 25%, ghép thật
qua BLE + Wi-Fi, tương thích Apple Home / Google Home / Home Assistant, nút BOOT trên SuperMini.

## Giới hạn đã biết

* **RAM sát** nếu nhiều controller cùng subscribe: xem [RAM](#ram).
* Chỉ Wi-Fi 2,4 GHz; mạng chặn multicast/mDNS sẽ làm controller không tìm thấy thiết bị sau khi ghép.
* Matter tạm ngừng trong lúc ở CONFIG (thiết bị rời Wi-Fi nhà).
* Console Matter tắt mặc định; DAC/PAI thử nghiệm (VID 0xFFF1) – không dùng cho sản phẩm; không OTA.
* Các giới hạn chung (beacon đổi địa chỉ RPA, RSSI không phải khoảng cách, cửa sổ ghép 15 phút lần đầu): README bản C6.
