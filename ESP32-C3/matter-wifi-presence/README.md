# Matter Presence Sensor + học phòng trống (ESP32-C3 SuperMini + HLK-LD2410C, Wi-Fi)

> Bản **ESP32-C3** của [`ESP32-H2/matter-presence-ld2410-learning`](../../ESP32-H2/matter-presence-ld2410-learning), dùng **chung mã nguồn**: thư mục này chỉ có `CMakeLists.txt` và `sdkconfig.defaults` (cùng README này), phần còn lại (thư mục `main`, bảng phân vùng, cấu hình) lấy từ thư mục ESP32-H2. Khác biệt chính là chạy **Matter over Wi-Fi** thay vì Matter over Thread, vì ESP32-C3 không có radio 802.15.4; chân nối, LED, console và PID cũng khác (bảng bên dưới).

Cảm biến hiện diện chuẩn Matter (device type **Occupancy Sensor 0x0107**) với radar 24 GHz **HLK-LD2410C**. Thêm trực tiếp vào **Apple Home**, Google Home, Home Assistant, SmartThings, Alexa... qua Wi-Fi nhà, **không cần Thread Border Router**.

Mọi chức năng giống hệt bản ESP32-H2, cùng thuật toán, ngưỡng, thời gian và trạng thái cảm biến: **học phòng trống** (bấm nút BOOT 3 lần hoặc từ Home Assistant), **Range**, **Radar resolution**, **Radar Bluetooth**, **Hold time**, cập nhật **OTA**. Cách dùng các chức năng này xem [README bản ESP32-H2](../../ESP32-H2/matter-presence-ld2410-learning/README.md); tài liệu này chỉ ghi phần khác.

## Khác biệt so với bản ESP32-H2

| | ESP32-H2 | ESP32-C3 (thư mục này) |
|---|---|---|
| Board | ESP32-H2 SuperMini / DevKitM-1 | **ESP32-C3 SuperMini** (flash 4 MB) |
| Mạng Matter | Thread (802.15.4), cần Thread Border Router | **Wi-Fi 2,4 GHz** (station), nối thẳng router nhà |
| Ghép nối | BLE, điện thoại gửi dataset Thread | BLE, điện thoại gửi **SSID và mật khẩu Wi-Fi** |
| Tìm thấy trên mạng | DNS-SD qua SRP của Border Router | mDNS trên mạng LAN |
| Radar LD2410 (UART1) | TX → GPIO11, RX ← GPIO12 | **TX → GPIO3, RX ← GPIO4** |
| LED | WS2812 RGB, GPIO8 | **LED xanh dương một màu, GPIO8**, sáng khi chân ở mức thấp |
| Nút | BOOT, GPIO9 | BOOT, GPIO9 |
| Log và console | UART0 (GPIO23/24) + USB-Serial-JTAG | **USB-Serial-JTAG** (cổng USB-C, GPIO18/19) |
| VID / PID | `0xFFF1` / `0x8000` | `0xFFF1` / **`0x8001`** |
| Hardware (Basic Information) | `ESP32H2-LD2410` | `ESP32C3-LD2410` |
| Tên file build | `matter_presence_h2_learn` | `matter_presence_c3_wifi` |
| Log chẩn đoán mạng | `thread_diag`, `id_diag` (Thread, SRP) | `id_diag` (Wi-Fi, IPv4/IPv6) |
| CPU | 96 MHz | 160 MHz |

PID riêng giúp app và Home Assistant không nhầm hai loại firmware, nhất là khi cập nhật OTA: file `.ota` của bản H2 không cài được lên bản C3 và ngược lại.

## Phần cứng

- **ESP32-C3 SuperMini** (flash 4 MB). Board ESP32-C3 khác cũng chạy được, nối theo số GPIO in trên mạch và chỉnh LED trong `menuconfig`.
- Radar hiện diện **HLK-LD2410C** (LD2410, LD2410B cũng được; nối theo chữ in trên mạch radar).

### Sơ đồ nối dây

| LD2410C | ESP32-C3 SuperMini |
|---|---|
| TX | **GPIO3** |
| RX | **GPIO4** |
| VCC | **5V** |
| GND | GND |

LD2410C cần nguồn **5V** (lấy từ chân 5V của SuperMini khi cắm USB), mức logic UART là 3.3V nên nối thẳng được. Radar dùng **UART1** ở 256000 baud.

| Chức năng | Chân |
|---|---|
| Radar LD2410C (UART1) | RX = GPIO3 (nối TX của radar), TX = GPIO4 (nối RX của radar) |
| LED xanh dương trên board | GPIO8 (sáng khi mức thấp) |
| Nút BOOT: bấm đúp = chế độ ghép nối, bấm 3 lần = học phòng trống, giữ 5 s = factory reset | GPIO9 |
| USB-C: nạp firmware, log, console | GPIO18 (D-), GPIO19 (D+) |

Kiểm tra chân của ESP32-C3:

- **GPIO3, GPIO4** không phải chân strapping, chân flash, chân USB hay UART0. GPIO4 là chân JTAG MTMS, nhưng JTAG mặc định đi qua cổng USB nên GPIO4 dùng tự do.
- **Chân strapping GPIO2, GPIO8, GPIO9.** GPIO8 (LED) phải ở mức cao và GPIO9 (BOOT) ở mức thấp để vào chế độ nạp. LED nối lên 3V3 nên giữ GPIO8 ở mức cao. Không nối thêm gì vào GPIO8, GPIO9.
- **Chân flash GPIO11–GPIO17** và **chân USB GPIO18/GPIO19**: build báo lỗi nếu chọn các chân này cho radar, nút hoặc LED.
- **UART0 GPIO20 (RX), GPIO21 (TX)**: không dùng cho log (log đi qua USB), nhưng ROM vẫn in thông báo khởi động ra GPIO21.

### Đèn LED

LED của SuperMini chỉ có một màu, nên mọi màu của bản H2 đều thành "sáng":

| LED | Trạng thái |
|---|---|
| Nháy chậm (1 s sáng, 1 s tắt) | Chế độ ghép nối: đang chờ thêm vào ứng dụng Matter |
| Tắt | Hoạt động bình thường (đã ghép nối), hoặc chế độ ghép nối đã hết giờ |
| Nháy nhanh | Ứng dụng yêu cầu *Identify* |
| Sáng liên tục 20 s | Vừa bắt đầu học phòng trống: rời khỏi phòng |
| Nháy chậm (1 s tắt, 1 s sáng) trong 60 s | Đang ghi năng lượng nền của phòng trống |
| Sáng liên tục 10 s | Học xong **hoặc** học thất bại; cũng vậy sau *Factory thresholds* (ghi lại ngưỡng mặc định). Xem kết quả trong log `quiet_learn` |

LED một màu không phân biệt được học xong (bản H2: xanh lá) và học thất bại (bản H2: đỏ). Trong lúc học, LED báo việc học thay cho chế độ ghép nối; *Identify* vẫn được ưu tiên.

Nếu LED trên board của bạn sáng ngược (sáng khi lẽ ra phải tắt): `menuconfig` → *Presence Sensor Configuration* → bật *Status LED is on when the pin is high*.

## Yêu cầu mạng

- Wi-Fi **2,4 GHz** (ESP32-C3 không có 5 GHz), WPA2 hoặc WPA3.
- Điện thoại, controller (HomePod, Home Assistant, Nest...) và thiết bị **cùng một mạng LAN**; không dùng mạng khách.
- Router bật **IPv6** (ít nhất link-local, mặc định trên mọi router), không chặn **mDNS/multicast**, tắt "AP/client isolation".
- Không cần Thread Border Router.

## Ghép nối (commissioning)

Firmware dùng chứng chỉ test của esp-matter (VID `0xFFF1`, PID `0x8001`; SDK có DAC và Certification Declaration test cho PID này), mã ghép nối mặc định:

- Mã nhập tay: **`3497-011-2332`** (giống bản H2: mã nhập tay không chứa PID)
- QR: `MT:-24J042C00KA0648G00` (mở [link này](https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:-24J042C00KA0648G00) để hiện QR). **Khác bản H2** vì QR chứa PID.
- Discriminator 3840, passcode 20202021

Khi chưa được ghép nối, log khởi động in QR và mã nhập tay của chính firmware (`setup_code: Setup code of this board (BLE)`).

**Apple Home / Google Home / Home Assistant:** thêm thiết bị Matter, quét QR. Ứng dụng hỏi (hoặc tự dùng) mạng Wi-Fi của điện thoại rồi gửi SSID và mật khẩu cho thiết bị qua BLE. Apple Home báo *Uncertified Accessory*: chọn **Vẫn thêm**.

Với chip-tool:

```bash
chip-tool pairing ble-wifi 0x7283 <SSID> <MẬT_KHẨU> 20202021 3840   # mỗi bo một Node ID
chip-tool occupancysensing read occupancy 0x7283 1
```

Sau khi ghép nối, BLE được tắt để tiết kiệm RAM. Bấm đúp nút BOOT khi đó mở chế độ ghép nối chỉ qua mạng (mDNS), nên ứng dụng thứ hai phải ở cùng mạng LAN. Factory reset (giữ BOOT 5 s) xoá cả thông tin Wi-Fi lẫn ghép nối Matter. Nhiều bo cùng firmware: như bản H2 (mục *Nhiều bo cùng firmware*).

Mã ghép nối riêng từng bo: biến thể `sdkconfig.defaults.unique_setup` không phụ thuộc chip nhưng mới được build và kiểm tra cho ESP32-H2. Biến thể `sdkconfig.defaults.factory` ký DAC bằng PAI test `FFF1/8000` của `ESP32-H2/tools/matter-mfg`, PAI này chỉ cấp DAC cho PID `0x8000`, nên chưa dùng được cho bản C3 (PID `0x8001`) nếu không đổi PAI.

## Build

Yêu cầu: **ESP-IDF v6.0.2** + **esp-matter 1.6.1**, giống bản H2 và CI.

```bash
cd ESP32-C3/matter-wifi-presence
idf.py set-target esp32c3
idf.py build
```

Bằng Docker (cần cả repo vì mã nguồn nằm ở thư mục ESP32-H2; image tự `cd` vào thư mục esp-matter nên phải `cd` lại trong lệnh):

```bash
cd ESP32-C3/matter-wifi-presence
docker run --rm -it -v $PWD/../..:/project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
    bash -c 'cd /project/ESP32-C3/matter-wifi-presence && git config --global --add safe.directory "*" && idf.py set-target esp32c3 && idf.py build'
```

Cũng build được từ thư mục ESP32-H2 (cùng firmware, cùng tên file). Đổi chip bằng `set-target`; lệnh này đã tự `fullclean` và chuyển `sdkconfig` cũ thành `sdkconfig.old`, nên `fullclean` riêng là không bắt buộc:

```bash
cd ESP32-H2/matter-presence-ld2410-learning
idf.py set-target esp32h2 && idf.py build     # bản Thread: build/matter_presence_h2_learn.bin
idf.py fullclean
idf.py set-target esp32c3 && idf.py build     # bản Wi-Fi:  build/matter_presence_c3_wifi.bin
```

### Nạp firmware và xem log

SuperMini chỉ có cổng USB gốc của ESP32-C3 (USB-Serial-JTAG): Linux `/dev/ttyACM0`, macOS `/dev/cu.usbmodem*`, Windows `COMx`.

```bash
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

Nếu không nạp được: giữ nút **BOOT**, cắm USB (hoặc bấm RESET), thả BOOT rồi nạp lại. Thoát monitor: `Ctrl+]`.

Mỗi lần push, GitHub Actions (`.github/workflows/matter-presence-c3-wifi.yml`) build và đính kèm `matter_presence_c3_wifi_merged.bin`, nạp một lần ở địa chỉ `0x0` (xoá cả thông tin ghép nối, giống bản H2):

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 matter_presence_c3_wifi_merged.bin
```

### Cập nhật qua mạng (OTA)

Giống bản H2, file **`matter_presence_c3_wifi.ota`** (build tại máy: `build/matter_presence_c3_wifi-ota.bin`), mã hãng/sản phẩm `0xFFF1`/`0x8001`, phiên bản `1.0.<số commit>`, tải qua Wi-Fi.

## Cấu hình (`idf.py menuconfig`)

Các tuỳ chọn giống bản H2 (*Presence Sensor Configuration*, *Empty Room Learning*, *Device Information*...), mặc định cho ESP32-C3:

| Tuỳ chọn | Mặc định ESP32-C3 |
|---|---|
| UART RX pin (nối TX của LD2410) | 3 |
| UART TX pin (nối RX của LD2410) | 4 |
| Status LED | *Single-colour LED on a GPIO* |
| Status LED pin | 8 |
| Status LED is on when the pin is high | tắt (LED sáng khi mức thấp) |
| Button pin | 9 |
| Hardware (Basic Information) | `ESP32C3-LD2410` |

Chân hợp lệ: GPIO0–GPIO21, trừ chân flash (GPIO11–GPIO17) và USB (GPIO18, GPIO19). Không có menu *Thread Diagnostics* trên ESP32-C3.

## RAM và flash

Đo bằng `idf.py size` (ESP-IDF v6.0.2, esp-matter 1.6.1, cấu hình mặc định, firmware 1.0.122):

| | ESP32-H2 (Thread) | ESP32-C3 (Wi-Fi) |
|---|---|---|
| Firmware (phân vùng app 1 920 KB) | 1 760 KB, trống 160 KB (8 %) | 1 582 KB, trống 338 KB (18 %) |
| RAM tĩnh | 171,1 KB / 252,0 KB DIRAM (67,9 %) | 175,2 KB / 313,8 KB DRAM (55,8 %) |
| RAM còn trống cho heap | 80,9 KB | 138,6 KB |

Wi-Fi và BLE cấp phát thêm bộ đệm từ heap khi chạy, nhiều nhất lúc ghép nối khi cả hai cùng bật, nên heap thật chỉ đo được trên thiết bị bằng log `mem` (như bản H2). Nếu heap thấp nhất xuống dưới 16 KB (log `W mem`), có thể tắt `CONFIG_ESP_WIFI_IRAM_OPT` và `CONFIG_ESP_WIFI_RX_IRAM_OPT` để có thêm RAM (Wi-Fi chậm hơn một chút); bản này giữ mặc định.

## Log khởi động và chẩn đoán

```
I app_main: Haxeko Presence LD2410, firmware 1.0.<số commit>
I app_main: Target esp32c3, board ESP32-C3 SuperMini, Matter over Wi-Fi
I app_main: Pins: LD2410 RX GPIO3 (radar TX), TX GPIO4 (radar RX), button GPIO9, LED GPIO8 (single colour, on when low)
I ld2410: LD2410 on UART1, RX=GPIO3 TX=GPIO4
I device_info: Serial number: 58CF79A1B2C4, device name: Presence-B2C4
```

Số serial là địa chỉ MAC gốc trong eFuse, cũng là MAC Wi-Fi mà router hiển thị, nên dễ tìm bo trong danh sách thiết bị của router.

Khối `id_diag` (lúc khởi động, 5 phút một lần, khi Wi-Fi kết nối/mất kết nối hoặc đổi địa chỉ IP) có thêm phần Wi-Fi thay cho phần Thread:

```
I id_diag: hw   efuse_mac=58CF79A1B2C4 wifi_mac=58CF79A1B2C4 reset=power-on up=300s heap=... min_heap=...
I id_diag: dev  vid=0xFFF1 pid=0x8001 serial=58CF79A1B2C4 unique_id=... discriminator=3840 test_setup_code=yes
I id_diag: wifi connected ssid="..." bssid=... ch=6 rssi=-55 power_save=min_modem
I id_diag: ip   ipv4=192.168.1.20 ipv6: link-local=1 unique-local=1 global=1
```

Không bao giờ in mật khẩu Wi-Fi, passcode hay khóa riêng. `rssi` dưới khoảng -75 dBm là sóng yếu. Khối in khi có sự kiện Wi-Fi chờ 5 s cho địa chỉ ổn định; cảnh báo `No IPv6 link-local address yet` khi đã kết nối nghĩa là thiết bị chưa có IPv6, controller chưa tới được thiết bị. Ngay sau khi khởi động, bo đã ghép nối vẫn báo `wifi not connected (connecting, ...)` cho tới khi vào được Wi-Fi. Log `mem` liệt kê thêm stack của task `wifi` thay cho `ot_task`.

Console (gõ trong monitor qua USB): lệnh của esp-matter như bản H2 (không có `ot_cli`). Matter SDK có sẵn lệnh `matter wifi scan`, `matter wifi mode`, `matter wifi connect <SSID> <mật khẩu>` để thử nghiệm; mật khẩu gõ vào lệnh connect hiện trên màn hình monitor và nằm trong lịch sử lệnh của console.

## Xử lý sự cố

- **Không thấy log:** chọn đúng cổng USB của SuperMini; một số cáp USB-C chỉ có dây nguồn.
- **Ghép nối dừng ở bước Wi-Fi:** mạng 2,4 GHz, đúng mật khẩu, router không chặn thiết bị mới. Log `id_diag: wifi not connected` sau khi ghép nối nghĩa là thiết bị không vào được Wi-Fi.
- **Ghép nối xong phần Wi-Fi nhưng app báo không tìm thấy thiết bị:** điện thoại và thiết bị phải cùng LAN, router không chặn mDNS/multicast, có IPv6 (xem dòng `ip` của `id_diag`).
- Mỗi lần thử lại: factory reset (giữ BOOT 5 s) hoặc `erase-flash`.

## Mức độ kiểm tra

- **Đã kiểm tra bằng build:** firmware ESP32-C3 build không lỗi, không cảnh báo, từ thư mục này và từ thư mục ESP32-H2 (`set-target esp32c3`); firmware ESP32-H2 vẫn build như trước. Kiểm tra cấu hình sinh ra (Wi-Fi bật, OpenThread tắt, console USB, PID `0x8001`, chân), kích thước firmware so với phân vùng OTA, và test trên máy tính cho phần số serial (MAC 6 byte của ESP32-C3).
- **Cần thử trên thiết bị thật:** ghép nối qua BLE + Wi-Fi với Apple Home / Home Assistant, đọc radar trên GPIO3/GPIO4, LED GPIO8 (đúng chiều sáng khi mức thấp hay không), nút BOOT, học phòng trống, OTA qua Wi-Fi, RAM khi chạy (log `mem`, nhất là lúc ghép nối khi BLE và Wi-Fi cùng chạy), độ ổn định Wi-Fi lâu dài.

## Cấu trúc thư mục

```
matter-wifi-presence/
├── CMakeLists.txt       # project ESP32-C3, dùng presence.cmake và main/ của thư mục ESP32-H2
├── sdkconfig.defaults   # target esp32c3, đường dẫn bảng phân vùng và chip_project_config.h dùng chung
└── README.md

../../ESP32-H2/matter-presence-ld2410-learning/   # mã nguồn dùng chung
├── presence.cmake            # phiên bản firmware, component esp-matter, tên project theo chip
├── sdkconfig.defaults        # chung cho hai chip
├── sdkconfig.defaults.esp32c3  # Wi-Fi, console USB, PID 0x8001, chân radar / LED của SuperMini
├── sdkconfig.defaults.esp32h2  # Thread, chân của bản H2
├── partitions.csv            # 4 MB, 2 phân vùng OTA
└── main/
    ├── board.h, board_esp32c3.h, board_esp32h2.h   # tên board, mạng, chân cấm theo chip
    └── ...                   # cảm biến, radar, LED, nút, Matter (chung)
```
