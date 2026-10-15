# Matter Presence Sensor (ESP32-H2 + HLK-LD2410, Thread)

Cảm biến hiện diện chuẩn **Matter over Thread** chạy trên **ESP32-H2** với radar 24 GHz **HLK-LD2410**. Đây là bản chuyển từ Zigbee (`ESP32-H2/zigbee-presence-occupancy`) sang Matter: cùng chip ESP32-H2, cùng radio 802.15.4, nhưng thay Zigbee bằng Thread + Matter. Thêm trực tiếp vào **Apple Home**, Google Home, Home Assistant, SmartThings, Alexa... mà không cần Zigbee coordinator / Zigbee2MQTT.

Radar LD2410 phát hiện được cả người đang ngồi yên (thở, cử động nhẹ), khác với cảm biến PIR chỉ thấy chuyển động.

## Từ Zigbee sang Matter

| Zigbee (Occupancy Sensing 0x0406) | Matter (bản này) |
|---|---|
| Thiết bị Occupancy Sensor | Device type **Occupancy Sensor (0x0107)** |
| `Occupancy` (bit 0: có người) | `Occupancy` của cluster **Occupancy Sensing (0x0406)** |
| `OccupancySensorType` | `FeatureMap` = **Radar**, `OccupancySensorType` / `OccupancySensorTypeBitmap` do cluster tự tính |
| `PIROccupiedToUnoccupiedDelay` (thời gian giữ) | **`HoldTime`** (1–300 s, mặc định 30 s), có `HoldTimeLimits`, lưu lại sau khi mất điện |
| Ghép nối qua Zigbee coordinator | Ghép nối bằng **BLE** (quét QR), dữ liệu đi qua **Thread Border Router** |
| Router Zigbee (cấp nguồn lưới) | **Thread FTD**: có thể làm Thread Router, mở rộng mạng mesh |

Logic phát hiện giữ nguyên như các bản LD2410 khác trong repo:

- **Có người** ngay khi radar thấy mục tiêu đang di chuyển hoặc đứng yên.
- **Không có người** sau khi radar không thấy ai trong khoảng **HoldTime**. Bộ đếm do cluster Matter xử lý, nên chỉnh HoldTime trong app có tác dụng ngay.
- Nếu radar không gửi dữ liệu quá **10 giây** (lỏng dây, mất nguồn), thiết bị báo **không có người** và ghi lỗi vào log. Có dữ liệu lại thì tự hoạt động bình thường.
- Tuỳ chọn chỉ tính người ở gần hơn một khoảng cách (*Only count targets closer than*). Độ nhạy từng vùng (gate) dùng cấu hình sẵn của LD2410, chỉnh bằng app **HLKRadarTool** qua Bluetooth của LD2410B/C.

## Phần cứng

- **ESP32-H2-DevKitM-1** (hoặc board ESP32-H2 khác, flash 4 MB, ví dụ ESP32-H2 Super Mini)
- Radar hiện diện **HLK-LD2410** (LD2410, LD2410B, LD2410C)

### Sơ đồ nối dây

| LD2410 | ESP32-H2 |
|---|---|
| TX | **GPIO11** |
| RX | **GPIO12** |
| VCC | **5V** |
| GND | GND |

LD2410 cần nguồn **5V**, nhưng mức logic UART là 3.3V nên nối thẳng vào ESP32-H2 được. Radar dùng **UART1** ở 256000 baud; UART0 (GPIO23/24) để dành cho log và console. Đổi chân trong `menuconfig` → *Presence Sensor Configuration* nếu cần, tránh các chân strapping (GPIO2, GPIO3, GPIO8, GPIO9, GPIO25) và chân flash (GPIO15–GPIO21).

| Chức năng | Chân |
|---|---|
| Radar LD2410 (UART1) | RX = GPIO11, TX = GPIO12 |
| LED RGB WS2812 trên board | GPIO8 |
| Nút BOOT: bấm đúp = chế độ ghép nối, giữ 5 s = factory reset | GPIO9 |

### Đèn LED

| LED | Trạng thái |
|---|---|
| Xanh dương nháy chậm (1 s sáng, 1 s tắt) | Chế độ ghép nối: đang chờ thêm vào ứng dụng Matter |
| Tắt | Hoạt động bình thường (đã ghép nối), hoặc chế độ ghép nối đã hết giờ |
| Trắng nháy nhanh | Ứng dụng yêu cầu *Identify* (xác định thiết bị) |

Trạng thái radar (có người / mất tín hiệu) chỉ xem được trong app và trong log, LED không báo.

Board không có LED WS2812: chọn *Single-colour LED on a GPIO* hoặc *None* trong `menuconfig` → *Status LED*.

### Nút BOOT (GPIO9)

| Thao tác | Kết quả |
|---|---|
| **Bấm đúp** (2 lần nhanh) | Vào **chế độ ghép nối** trong 15 phút, LED nháy xanh. Chưa ghép nối: thêm lại thiết bị khi chế độ ghép nối lúc khởi động đã hết giờ. Đã ghép nối: thêm thiết bị vào ứng dụng thứ hai (Multi-Admin) bằng chính mã ghép nối của thiết bị |
| **Giữ 5 giây** | **Factory reset**: xoá mạng Thread và toàn bộ thông tin ghép nối Matter, khởi động lại. Sau đó LED nháy xanh chờ ghép nối |

Sau khi đã ghép nối, BLE được tắt để tiết kiệm RAM. Khi đó chế độ ghép nối bằng nút bấm chỉ quảng bá qua mạng Thread (Border Router), nên ứng dụng thứ hai cần dùng cùng mạng Wi-Fi/LAN với Border Router.

## Yêu cầu mạng

Matter over Thread cần một **Thread Border Router** trong nhà, ví dụ:

- Apple: HomePod mini / HomePod 2 / Apple TV 4K (bản có Ethernet hoặc 2022)
- Google: Nest Hub (2nd gen) / Nest Hub Max / Nest Wifi Pro
- Amazon: Echo 4th gen
- Home Assistant: SkyConnect / ZBT-1 + OpenThread Border Router add-on
- Tự làm: ESP Thread Border Router (ESP32-S3 + ESP32-H2)

Điện thoại gửi thông tin mạng Thread cho thiết bị qua BLE trong lúc ghép nối, nên không cần nhập SSID/mật khẩu.

## Ghép nối (commissioning)

Firmware dùng chứng chỉ test của esp-matter (VID `0xFFF1`, PID `0x8000`), mã ghép nối mặc định:

- Mã nhập tay: **`3497-011-2332`**
- QR: `MT:Y.K9042C00KA0648G00` (mở [link này](https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00) để hiện QR)
- Discriminator 3840, passcode 20202021

**Apple Home:** app **Nhà → + → Thêm phụ kiện** → quét QR (hoặc *Thêm tùy chọn khác... → Nhập mã*). Khi gặp cảnh báo *Uncertified Accessory*, chọn **Vẫn thêm**. Phụ kiện hiện là **Cảm biến chiếm chỗ** (*Occupancy Sensor*), dùng được trong tự động hoá, ví dụ bật đèn khi có người, tắt đèn khi không còn ai.

Google Home, Home Assistant, SmartThings làm tương tự: chọn thêm thiết bị Matter rồi quét QR. Home Assistant hiện thêm mục **Hold time** để chỉnh thời gian giữ trạng thái.

Với chip-tool (lấy dataset từ Border Router, ví dụ `ot-ctl dataset active -x`):

```bash
chip-tool pairing ble-thread 0x7283 hex:<ACTIVE_DATASET> 20202021 3840   # mỗi bo một Node ID
chip-tool occupancysensing read occupancy 0x7283 1
chip-tool occupancysensing write hold-time 60 0x7283 1
```

- Sau khi bật nguồn, thiết bị quảng bá BLE nhanh trong 30 s rồi chuyển sang chậm (log `Start slow advertisement`). Đây là bình thường, thiết bị vẫn chờ ghép nối. Cửa sổ ghép nối mở 15 phút; hết thời gian thì **bấm đúp nút BOOT** để mở lại (hoặc rút điện cắm lại).
- Nếu xoá thiết bị khỏi tất cả ứng dụng, chế độ ghép nối tự mở lại trong 5 phút, giữ nguyên mạng Thread (ghép nối qua mạng, không cần BLE).
- Firmware dùng chứng chỉ test nên chỉ hợp với mục đích DIY. Để làm sản phẩm thật, hãy tạo DAC/passcode riêng bằng `esp-matter-mfg-tool`.

### Nhiều bo cùng firmware

Bản build mặc định cho mọi bo **chung một mã ghép nối** (mã test của SDK ở trên). Mã này chỉ dùng lúc ghép nối, không ảnh hưởng các bo đã ghép, nhưng:

- Ghép **từng bo một**: các bo chưa ghép khác để tắt nguồn, không bấm đúp hai bo cùng lúc. Nếu không, app có thể ghép nhầm bo (mọi bo quảng bá cùng tên BLE, cùng discriminator).
- Sau khi ghép, kiểm tra đúng bo bằng **Identify** hoặc so **Serial** trong app với dòng `id_diag: dev serial=` trong log.
- Thêm vào hệ thứ hai (Multi-Admin): ưu tiên chức năng chia sẻ của hệ đang quản lý (Apple Home: *Bật chế độ ghép nối*; HA: *Share device*), vì mã do app tạo ngẫu nhiên. Bấm đúp nút thì mở bằng mã chung.
- chip-tool: mỗi bo một Node ID (`0x7283`, `0x7284`, ...). Hai bo cùng Node ID trong cùng fabric sẽ trùng tên dịch vụ `_matter._tcp`.
- Muốn mỗi bo một mã riêng: xem mục **Mã ghép nối riêng cho từng bo**.
- Khi có người đi qua cảm biến mà Apple Home vẫn cập nhật nhưng lệnh từ Apple không tới bo, thì lỗi nằm ở đường controller → thiết bị (Border Router, SRP, định tuyến), không phải danh tính bo. Xem mục **Log chẩn đoán danh tính**.

## Mã ghép nối riêng cho từng bo

| Bản build | Mã ghép nối | DAC (chứng thực) |
|---|---|---|
| Mặc định | mã test chung `20202021` / `3840` | DAC test của SDK (chung mọi bo) |
| `sdkconfig.defaults.unique_setup` | mỗi bo tự sinh mã ngẫu nhiên lần đầu, lưu ở phân vùng `fctry` | DAC test của SDK (chung) |
| `sdkconfig.defaults.factory` | nạp từ file factory riêng từng bo (`ESP32-H2/tools/matter-mfg`) | DAC riêng từng bo |

```bash
idf.py -B build-unique -D SDKCONFIG=sdkconfig.unique \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.unique_setup" build
idf.py -B build-unique -p /dev/ttyUSB0 flash monitor
```

Khi bo có thể được ghép (lần đầu khởi động, hoặc sau khi bấm đúp), log in mã của chính bo đó:

```
W setup_code: New per-board setup code generated and stored in fctry/setup-code, discriminator 2294
I setup_code: Setup code of this board (BLE):
I setup_code:   QR code: MT:Y.K90...
I setup_code:   QR image: https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3AY.K90...
I setup_code:   Manual pairing code: 21137749112
```

- Mã giữ nguyên qua khởi động lại, OTA, factory reset và nạp file merged. Chỉ `erase-flash` mới làm bo sinh mã mới (và in lại).
- Bo đã ghép vẫn giữ các hệ hiện có; mã mới chỉ dùng cho lần ghép sau.
- Bản mặc định cũng in mã (mã test chung). Làm sản phẩm thật thì tắt `CONFIG_PRINT_ONBOARDING_CODES`, vì QR chứa passcode.
- Factory data và chứng chỉ test: `ESP32-H2/tools/matter-mfg/README.md` và `ESP32-H2/docs/multi-device-audit.md` (mục G, H).

## Build

Yêu cầu: **ESP-IDF v6.0.2** + **esp-matter release/v1.6** (giống CI). Cách nhanh nhất là dùng Docker image của Espressif:

```bash
cd ESP32-H2/matter-thread/matter-presence-occupancy
docker run --rm -it -v $PWD/../../..:/project -w /project/ESP32-H2/matter-thread/matter-presence-occupancy \
    espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
    bash -c 'git config --global --add safe.directory "*" && idf.py set-target esp32h2 && idf.py build'
```

Hoặc cài local:

```bash
. ~/esp/esp-idf/export.sh
. ~/esp/esp-matter/export.sh
idf.py set-target esp32h2
idf.py menuconfig          # (tuỳ chọn)
idf.py build
idf.py -p /dev/ttyUSB0 erase-flash flash monitor
```

Mỗi lần push, GitHub Actions (`.github/workflows/matter-presence-h2-thread.yml`) build firmware và đính kèm file `matter_presence_h2_merged.bin`, nạp một lần ở địa chỉ `0x0`:

```bash
esptool.py --chip esp32h2 -p /dev/ttyUSB0 write_flash 0x0 matter_presence_h2_merged.bin
```

Vào tab **Actions** → lần chạy mới nhất → **Artifacts**.

> Nếu thiết bị đã từng được thêm vào Home với firmware khác, hãy xoá phụ kiện, `erase-flash`, nạp lại rồi thêm lại.

## Cấu hình (`idf.py menuconfig`)

*Presence Sensor Configuration*:

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| ESP32-H2 UART RX pin | 11 | Nối với TX của LD2410 |
| ESP32-H2 UART TX pin | 12 | Nối với RX của LD2410 |
| Only count targets closer than | 0 (tắt) | Bỏ qua mục tiêu xa hơn khoảng cách này (cm) |
| Default hold time | 30 s | HoldTime lúc mới nạp / sau factory reset. Đổi trong app thì giá trị mới được lưu lại |
| Report unoccupied when the radar sends nothing for | 10 s | Coi radar mất kết nối sau khoảng thời gian này |
| Status LED | WS2812 | WS2812 / LED một màu trên GPIO / không có |
| Status LED pin | 8 | Chân LED |
| WS2812 brightness | 20 | Độ sáng LED WS2812 (1–255) |
| Button pin | 9 | Nút BOOT: bấm đúp = chế độ ghép nối, giữ 5 giây = reset. `-1` để tắt |

Build sẽ báo lỗi nếu hai chức năng dùng trùng một chân, hoặc nếu chọn chân flash (GPIO15–GPIO21).

*Device Information*:

| Mục | Giá trị mặc định |
|---|---|
| Nhà sản xuất (Manufacturer) | `Haxeko` |
| Kiểu máy (Model) | `H2 Occupancy` |
| Phần cứng (Hardware) | `ESP32H2-LD2410` |
| Tên thiết bị khi ghép nối | `Occupancy Sensor` (cũng là tên DNS-SD; tên Bluetooth là `Occupancy-<discriminator>`, ví dụ `Occupancy-3840`) |
| Số serial | Địa chỉ MAC gốc trong eFuse, ví dụ `744DBDA1B2C3`. Tự động, riêng cho từng chip |
| Firmware | `1.0.<số commit>`, tự tăng sau mỗi commit; cũng là số phiên bản Matter dùng cho OTA |

*Thread Diagnostics*: `THREAD_DIAG_LOG` (bật mặc định) in khối chẩn đoán Thread ra log mỗi 10 s cho tới khi ghép nối xong.

## Xử lý sự cố: điện thoại không tìm thấy thiết bị

Ghép nối Matter over Thread có 2 giai đoạn:

1. **Qua BLE:** điện thoại tìm thấy thiết bị, kiểm tra chứng chỉ, gửi thông tin mạng Thread. Log: `Role detached -> child`, `advertising srp service: ...._matter._tcp`.
2. **Qua mạng IP:** điện thoại tìm dịch vụ `_matter._tcp` của thiết bị thông qua Border Router rồi mở phiên bảo mật. Log thành công: `Commissioning complete`.

Nếu giai đoạn 1 xong mà app báo không tìm thấy thiết bị, thường là do mạng nhà, không phải do firmware:

- Điện thoại và Border Router phải cùng một mạng Wi-Fi/LAN (không dùng mạng khách, tắt VPN trên điện thoại).
- Router Wi-Fi phải bật **IPv6** và không chặn **mDNS/multicast**; tắt "AP/client isolation".
- Border Router phải là của hệ đang dùng (Apple Home → HomePod/Apple TV; Google Home → Nest Hub/Nest Wifi; Home Assistant → OTBR add-on). Thử khởi động lại Border Router.
- Mỗi lần thử lại: factory reset (giữ nút BOOT 5 s) hoặc `erase-flash` để xoá thông tin mạng Thread cũ.

Khi chưa được ghép nối, cứ 10 s firmware in khối `thread_diag` ra log:

```
I thread_diag: Role: router, RLOC16: 0x7c00
I thread_diag: Address: fd12:3456:789a:1:abcd:...  [OMR (reachable from Wi-Fi/LAN)]
I thread_diag: Summary: OMR address YES, on-mesh prefixes 1, SRP host Registered, SRP service Registered
```

`OMR address NO` hoặc SRP không `Registered` nghĩa là lỗi phía Border Router / mạng LAN.

Có thể gõ lệnh OpenThread trên serial monitor (115200 baud, cổng USB **UART** của DevKit): `matter esp ot_cli state`, `matter esp ot_cli ipaddr`, `matter esp ot_cli srp client service`... Cổng USB gốc của ESP32-H2 chỉ xuất log, không nhận lệnh.

### Log chẩn đoán danh tính (`id_diag`)

Firmware in một khối log lúc khởi động, sau đó 5 phút một lần (`CONFIG_IDENTITY_DIAG_PERIOD_SEC`, 0 = tắt), và in thêm khi có sự kiện Thread, fabric hoặc phiên. Log không bao giờ chứa passcode, khóa riêng hay network key.

```
I id_diag: hw   efuse_mac=744DBDA1B2C3 eui64=744DBDFFFEA1B2C3 reset=power-on up=300s heap=... min_heap=...
I id_diag: dev  vid=0xFFF1 pid=0x8000 serial=... unique_id=... discriminator=3840 test_setup_code=yes
I id_diag: mtr  commissioned=yes fabrics=2 window_open=no failsafe=no sessions: case=2 pase=0, subscriptions=3 reads=0
I id_diag: fab  index=1 vendor=0x.... node_id=... fabric_id=... compressed_fabric_id=... case=1 subscriptions=2 label="..."
I id_diag: ot   role=router rloc16=0x5400 ext_addr=8E7D3F1A2B4C6D9E eui64=... ml_eid=fd...
I id_diag: ot   dataset=yes net="..." ext_panid=... panid=0x.... ch=15 partition=0x... leader_rid=...
I id_diag: mle  roles: ... attach_attempts=1 partition_changes=0 better_partition=0 parent_changes=0
I id_diag: br   on-mesh prefixes=1 (default route 1), OMR address=yes
I id_diag: srp  server=[fd...]:53 host=8E7D3F1A2B4C6D9E state=Registered services=2/2 registered
I id_diag: nvs  entries used=... free=... available=... total=... namespaces=...
```

Khi thêm bo thứ hai mà các bo chập chờn, so log giữa các bo:

- `efuse_mac`, `serial`, `unique_id`, `ext_addr`, `ml_eid`, `srp host` phải **khác nhau**. Trùng nghĩa là bo bị chép nguyên NVS từ bo khác: factory reset hoặc `erase-flash` bo đó rồi ghép lại.
- `ext_panid`, `partition` phải **giống nhau** giữa các bo và với Border Router. Khác nghĩa là các Border Router (Apple, HA, Aqara) không cùng một mạng Thread.
- `srp state` phải là `Registered`. `server=` cho biết Border Router nào đang giữ bản ghi của bo.
- `reset=` là `BROWNOUT`, `PANIC` hoặc `WATCHDOG` nghĩa là bo bị khởi động lại (nguồn yếu, lỗi firmware).
- Có dòng `SUB request` mà không có `SUB established`, hoặc `SUB terminated` lặp lại: controller mất subscription.

Phân tích đầy đủ, bảng định danh và bộ test nhiều thiết bị: `ESP32-H2/docs/multi-device-audit.md`.

## Cấu trúc thư mục

```
matter-presence-occupancy/
├── CMakeLists.txt              # project esp-matter, phiên bản firmware theo số commit
├── partitions.csv              # 4 MB, 2 phân vùng OTA
├── sdkconfig.defaults          # Matter/BLE, tắt các cluster không dùng
├── sdkconfig.defaults.esp32h2  # OpenThread (FTD, SRP client), chân radar, LED
├── sdkconfig.defaults.unique_setup  # Mã ghép nối riêng từng bo (tùy chọn)
├── sdkconfig.defaults.factory       # Factory data riêng từng bo (tùy chọn)
└── main/
    ├── app_main.cpp            # Matter node, endpoint Occupancy Sensor, radar -> occupancy, OpenThread
    ├── device_info.cpp         # Manufacturer / Model / Serial (Basic Information)
    ├── chip_project_config.h   # Tên thiết bị trong DNS-SD khi chưa ghép nối
    ├── thread_diag.cpp         # Log chẩn đoán Thread/SRP khi chưa ghép nối
    ├── identity_diag.cpp       # Log danh tính / Thread / SRP / fabric / subscription (id_diag)
    ├── setup_code.cpp          # In mã QR; mã ghép nối riêng từng bo (unique_setup)
    ├── Kconfig.projbuild       # Cấu hình chân và tham số
    └── drivers/
        ├── ld2410.cpp          # Radar LD2410 (UART1, 256000 baud)
        ├── status_led.cpp      # LED trạng thái (WS2812 hoặc LED thường)
        └── user_button.cpp     # Nút BOOT: bấm đúp, giữ lâu
```
