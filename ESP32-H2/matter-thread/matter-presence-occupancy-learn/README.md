# Matter Presence Sensor + học phòng trống (ESP32-H2 + HLK-LD2410, Thread)

> Bản fork của [`matter-presence-occupancy`](../matter-presence-occupancy) (bản đang chạy ổn định, giữ nguyên). Khác biệt: thêm 3 điều khiển LD2410 trong **Home Assistant** (không hiện trong Apple Home): **học phòng trống** giúp radar bớt báo nhầm có người, **Duration** và **Range** chỉnh thời gian giữ và khoảng cách phát hiện của radar. Phần cứng, chân nối, ghép nối, LED, nút bấm giống hệt bản gốc.

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
- **Không có người** sau khi radar không thấy ai trong khoảng **HoldTime**. Bộ đếm do cluster Matter xử lý, nên chỉnh HoldTime trong app có tác dụng ngay. Bản thân LD2410 cũng giữ trạng thái có người thêm một khoảng (**Duration**, xem bên dưới) trước khi báo mất mục tiêu, nên tổng thời gian = Duration + HoldTime.
- Nếu radar không gửi dữ liệu quá **10 giây** (lỏng dây, mất nguồn), thiết bị báo **không có người** và ghi lỗi vào log. Có dữ liệu lại thì tự hoạt động bình thường.
- Tuỳ chọn chỉ tính người ở gần hơn một khoảng cách (*Only count targets closer than*). Độ nhạy từng vùng (gate) được chỉnh tự động bằng chức năng **học phòng trống** bên dưới, khoảng cách phát hiện bằng entity **Range** (hoặc bằng app **HLKRadarTool** qua Bluetooth của LD2410B/C).

## Học phòng trống (giảm báo nhầm)

Trong phòng không có ai, radar vẫn nhận được "năng lượng" từ quạt, rèm bay, cây cảnh, phản xạ của đồ vật... Ở vùng nào năng lượng nền này vượt ngưỡng của LD2410, radar báo nhầm là có người. Chức năng học đo năng lượng nền của phòng trống rồi nâng ngưỡng từng vùng lên trên mức đó.

**Trong Home Assistant:** trang thiết bị → mục *Cấu hình* (Configuration) có entity **Learn empty room** với hai lựa chọn `Off` / `Learning`. Đây là cluster Matter *Mode Select*; Apple Home không hỗ trợ cluster này nên không hiện gì.

1. Chọn **Learning**.
2. **Rời khỏi phòng** trong 10 giây đầu, đóng cửa, tắt những thứ chuyển động bất thường (không cần tắt quạt nếu quạt vẫn thường chạy, vì đó chính là nhiễu cần học).
3. Thiết bị bật *engineering mode* của LD2410 và ghi lại năng lượng chuyển động / đứng yên **cao nhất** của từng vùng (gate, mỗi gate 0,75 m) trong **30 giây**.
4. Thiết bị tính ngưỡng mới, ghi vào LD2410, đọc lại để kiểm tra. **LD2410 tự lưu ngưỡng vào bộ nhớ của nó**, giữ nguyên khi mất điện hay khi nạp lại firmware ESP32-H2.
5. Entity tự trở về **Off** (khoảng 40 giây sau khi chọn). Kết quả xem trong log `quiet_learn`.

Ngưỡng mới của mỗi gate = **năng lượng nền cao nhất + 10**, nhưng:

- không bao giờ thấp hơn **mặc định của nhà sản xuất** (chuyển động `50,50,40,30,20,15,15,15,15`, đứng yên `–,–,40,40,30,30,20,20,20`): việc học chỉ làm radar **bớt nhạy** ở vùng có nhiễu, không làm nhạy hơn. Học lại trong phòng yên tĩnh sẽ đưa ngưỡng về gần mặc định;
- tối đa **100**: vùng đó bị bỏ qua (người đứng ở khoảng cách đó sẽ không được phát hiện);
- ngưỡng **đứng yên của gate 0 và 1** không chỉnh được trên LD2410, giữ nguyên.

Ví dụ log:

```
I quiet_learn: Gate (0.75 m each): moving noise -> threshold (was) | still noise -> threshold (was)
I quiet_learn: Gate 0:  41 ->  51 ( 50) | not settable
I quiet_learn: Gate 2:  16 ->  40 ( 40) |  51 ->  61 ( 40)
I quiet_learn: Gate 6:   8 ->  18 ( 15) |  36 ->  46 ( 20)
I quiet_learn: Learning done: new thresholds saved in the LD2410 (300 reports)
```

- Chọn **Off** trong lúc đang học: huỷ, không ghi gì vào radar.
- Radar không trả lời lệnh: không ghi gì, log báo kiểm tra dây **GPIO12 → chân RX của LD2410** (bản gốc chỉ đọc radar nên có thể chưa nối dây này).
- Thời gian chờ, thời gian ghi và mức +10 chỉnh được trong `menuconfig` → *Empty Room Learning*.

## Duration và Range (thông số của LD2410)

Cũng trong mục *Cấu hình* của thiết bị trên Home Assistant có thêm hai entity dạng select:

| Entity | Thông số LD2410 | Lựa chọn |
|---|---|---|
| **Duration** | *Unmanned duration* (no-one duration): radar tiếp tục báo có người thêm khoảng này sau khi mất mục tiêu | `0 s`, `5 s` (mặc định nhà sản xuất), `10 s`, `15 s`, `30 s`, `1 min`, `2 min`, `5 min`, `10 min` |
| **Range** | *Detection range*: gate xa nhất radar còn xét, đặt cùng lúc cho mục tiêu chuyển động và đứng yên | `1.5 m`, `2.25 m`, `3 m`, `3.75 m`, `4.5 m`, `5.25 m`, `6 m` (gate 2–8, mỗi gate 0,75 m; mặc định 6 m). LD2410B/C đặt độ phân giải 0,2 m thì là `0.4 m` … `1.6 m` |

- Giá trị được ghi vào **bộ nhớ của LD2410** (giữ khi mất điện, khi nạp lại firmware ESP32-H2) rồi đọc lại để kiểm tra; Home Assistant hiển thị giá trị radar thực sự đang giữ. Lúc khởi động firmware đọc thông số của radar (log `radar_ctl: LD2410 settings: ...`).
- Radar đang giữ một giá trị không có trong danh sách (đặt bằng HLKRadarTool) thì danh sách có thêm đúng giá trị đó, ví dụ `7 s` hoặc `Moving 4.5 m, still 3 m`.
- **Thời gian từ lúc người rời đi tới khi báo "không có người" ≈ Duration + Hold time.** Ví dụ Duration 5 s + Hold time 30 s → khoảng 35 s. Muốn tắt đèn nhanh hơn thì giảm một trong hai; hay bị báo "không có người" khi ngồi yên lâu thì tăng Duration hoặc Hold time.
- **Range** do radar xử lý: mục tiêu xa hơn bị bỏ qua hoàn toàn (ví dụ không bắt người ở phòng bên qua vách mỏng). Tuỳ chọn *Only count targets closer than* trong menuconfig thì lọc trong firmware, sau khi radar đã báo.
- Radar chưa trả lời lệnh (chưa nối dây **GPIO12 → chân RX của LD2410**): thay đổi trong Home Assistant bị bỏ qua, entity giữ giá trị cũ; firmware thử đọc lại radar sau 15 s, 30 s, 1 min... (tối đa 5 min một lần).
- Đang học phòng trống thì thay đổi Duration/Range được áp dụng ngay sau khi học xong.
- Mỗi endpoint Matter chỉ có một cluster *Mode Select*, nên Duration và Range nằm trên 2 endpoint riêng (2 và 3) kiểu thiết bị *Mode Select* (0x0027). Apple Home không hỗ trợ kiểu thiết bị này nên vẫn chỉ hiện cảm biến chiếm chỗ (dự kiến theo tài liệu, chưa thử trên thiết bị Apple thật).
- Thiết bị đã ghép nối với bản fork cũ (chưa có Duration/Range): nếu sau khi nạp firmware mới Home Assistant chưa hiện hai entity này, mở trang thiết bị → *Matter* (⋮) → **Re-interview device**.

Với chip-tool (endpoint 2 = Duration, mode = vị trí trong danh sách trên; endpoint 3 = Range, mode = số gate):

```bash
chip-tool modeselect read supported-modes 0x7283 2
chip-tool modeselect change-to-mode 4 0x7283 2    # Duration 30 s
chip-tool modeselect change-to-mode 6 0x7283 3    # Range gate 6 = 4.5 m
```

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
chip-tool pairing ble-thread 0x7283 hex:<ACTIVE_DATASET> 20202021 3840
chip-tool occupancysensing read occupancy 0x7283 1
chip-tool occupancysensing write hold-time 60 0x7283 1
```

- Sau khi bật nguồn, thiết bị quảng bá BLE nhanh trong 30 s rồi chuyển sang chậm (log `Start slow advertisement`). Đây là bình thường, thiết bị vẫn chờ ghép nối. Cửa sổ ghép nối mở 15 phút; hết thời gian thì **bấm đúp nút BOOT** để mở lại (hoặc rút điện cắm lại).
- Nếu xoá thiết bị khỏi tất cả ứng dụng, chế độ ghép nối tự mở lại trong 5 phút, giữ nguyên mạng Thread (ghép nối qua mạng, không cần BLE).
- Firmware dùng chứng chỉ test nên chỉ hợp với mục đích DIY. Để làm sản phẩm thật, hãy tạo DAC/passcode riêng bằng `esp-matter-mfg-tool`.

## Build

Yêu cầu: **ESP-IDF v6.0.2** + **esp-matter release/v1.6** (giống CI). Cách nhanh nhất là dùng Docker image của Espressif:

```bash
cd ESP32-H2/matter-thread/matter-presence-occupancy-learn
docker run --rm -it -v $PWD/../../..:/project -w /project/ESP32-H2/matter-thread/matter-presence-occupancy-learn \
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

Mỗi lần push, GitHub Actions (`.github/workflows/matter-presence-h2-learn.yml`) build firmware và đính kèm file `matter_presence_h2_learn_merged.bin`, nạp một lần ở địa chỉ `0x0`:

```bash
esptool.py --chip esp32h2 -p /dev/ttyUSB0 write_flash 0x0 matter_presence_h2_learn_merged.bin
```

Vào tab **Actions** → lần chạy mới nhất → **Artifacts**.

> File merged ghi đè cả vùng NVS (`0x10000`), tức là **xoá thông tin ghép nối**: sau khi nạp phải thêm lại thiết bị. Nếu thiết bị đã từng được thêm vào Home với firmware khác, hãy xoá phụ kiện, `erase-flash`, nạp lại rồi thêm lại.
>
> Nâng cấp bản fork mà **giữ ghép nối** (thiết bị chưa từng cập nhật OTA): chỉ nạp phần ứng dụng `esptool.py --chip esp32h2 -p /dev/ttyUSB0 write_flash 0x20000 matter_presence_h2_learn.bin` (hoặc `idf.py flash`).

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
| Tên thiết bị khi ghép nối | `Occupancy Sensor` (cũng là tên DNS-SD; tên Bluetooth là `Occupancy-<discriminator>`, ví dụ `Occupancy-3840`) |
| Số serial | Địa chỉ MAC gốc trong eFuse, ví dụ `744DBDA1B2C3`. Tự động, riêng cho từng chip |
| Firmware | `1.0.<số commit>`, tự tăng sau mỗi commit; cũng là số phiên bản Matter dùng cho OTA |

*Empty Room Learning*:

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| Delay before recording | 10 s | Thời gian để rời phòng sau khi chọn Learning |
| Recording time | 30 s | Thời gian ghi năng lượng nền của phòng trống |
| Margin above the recorded noise | 10 | Ngưỡng mới = năng lượng nền cao nhất + giá trị này |

*Thread Diagnostics*: `THREAD_DIAG_LOG` (bật mặc định) in khối chẩn đoán Thread ra log mỗi 10 s cho tới khi ghép nối xong.

*Memory Monitor*: `MEM_MONITOR_PERIOD_SEC` (mặc định 600 s, `0` = chỉ khi khởi động) in heap và stack còn trống, xem mục **RAM và flash**.

## RAM và flash

Đo bằng `idf.py size` (ESP-IDF v6.0.2, esp-matter 1.6.1, cấu hình mặc định):

| | Bản ổn định | Fork, chỉ học phòng trống | Fork + Duration/Range |
|---|---|---|---|
| Firmware (phân vùng app 1 920 KB) | 1 725 KB, trống 195 KB (10 %) | 1 741 KB, trống 179 KB (9 %) | 1 746 KB, trống 174 KB (9 %) |
| RAM tĩnh (DIRAM 252 KB) | 166,9 KB (66,2 %) | 168,1 KB (66,7 %) | 169,5 KB (67,3 %) |
| DIRAM còn trống (vùng dành cho heap) | 85,1 KB | 83,8 KB | 82,5 KB |

Phần thêm Duration/Range tốn khoảng 5 KB flash và 1,4 KB RAM tĩnh. Khi chạy còn thêm task `radar_cfg` thường trực với stack 4 KB (bản trước chỉ tạo task 4 KB trong lúc học) và dữ liệu của 2 endpoint Matter mới. Flash và RAM tĩnh còn dư nhiều; heap thật khi chạy phụ thuộc Matter/Thread/BLE nên chỉ đo được trên thiết bị, bằng log `mem`:

```
I mem: Heap: <còn trống> bytes free, lowest <thấp nhất> since boot, largest block <khối lớn nhất>
I mem: Stack unused bytes: CHIP <n> ot_task <n> esp_timer <n> console <n> presence <n> ld2410 <n> radar_cfg <n>
```

Log in lúc khởi động rồi 10 phút một lần; con số quan trọng là `lowest ... since boot` (heap thấp nhất kể từ khi bật, thường rơi vào lúc ghép nối khi BLE còn chạy). Log chuyển sang cảnh báo `W mem` khi heap thấp nhất dưới 16 KB hoặc một stack còn dưới 512 byte. Trên console còn có lệnh `matter esp diagnostics mem-dump`.

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

## Cấu trúc thư mục

```
matter-presence-occupancy-learn/
├── CMakeLists.txt              # project esp-matter, phiên bản firmware theo số commit
├── partitions.csv              # 4 MB, 2 phân vùng OTA
├── sdkconfig.defaults          # Matter/BLE, tắt các cluster không dùng
├── sdkconfig.defaults.esp32h2  # OpenThread (FTD, SRP client), chân radar, LED
└── main/
    ├── app_main.cpp            # Matter node, endpoint Occupancy Sensor, OpenThread
    ├── radar_control.cpp       # Mode Select "Learn empty room" / "Duration" / "Range", task cấu hình radar
    ├── radar_settings.cpp      # Danh sách lựa chọn Duration/Range, đọc/ghi thông số LD2410
    ├── quiet_learn.cpp         # Học phòng trống: engineering mode, tính và ghi ngưỡng vào LD2410
    ├── mem_monitor.cpp         # Log heap / stack định kỳ
    ├── device_info.cpp         # Manufacturer / Model / Serial (Basic Information)
    ├── chip_project_config.h   # Tên thiết bị trong DNS-SD khi chưa ghép nối
    ├── thread_diag.cpp         # Log chẩn đoán Thread/SRP khi chưa ghép nối
    ├── Kconfig.projbuild       # Cấu hình chân và tham số
    └── drivers/
        ├── ld2410.cpp          # Radar LD2410 (UART1, 256000 baud): đọc báo cáo, gửi lệnh cấu hình
        ├── status_led.cpp      # LED trạng thái (WS2812 hoặc LED thường)
        └── user_button.cpp     # Nút BOOT: bấm đúp, giữ lâu
```
