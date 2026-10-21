# Matter Presence Sensor + học phòng trống (ESP32-C3 SuperMini + HLK-LD2410C, Wi-Fi)

> Bản **ESP32-C3** của [`ESP32-H2/matter-presence-ld2410-learning`](../../ESP32-H2/matter-presence-ld2410-learning), dùng **chung mã nguồn**: thư mục này chỉ có `CMakeLists.txt` và `sdkconfig.defaults` (cùng README này và [`REBUILD_PROMPT.md`](REBUILD_PROMPT.md), bản tổng hợp yêu cầu để viết lại firmware từ đầu), phần còn lại (thư mục `main`, bảng phân vùng, cấu hình) lấy từ thư mục ESP32-H2. Khác biệt chính là chạy **Matter over Wi-Fi** thay vì Matter over Thread, vì ESP32-C3 không có radio 802.15.4; chân nối, LED và PID cũng khác, và bản C3 không có console Matter (bảng bên dưới).

Cảm biến hiện diện chuẩn Matter (device type **Occupancy Sensor 0x0107**) với radar 24 GHz **HLK-LD2410C**. Thêm trực tiếp vào **Apple Home**, Google Home, Home Assistant, SmartThings, Alexa... qua Wi-Fi nhà, **không cần Thread Border Router**.

Mọi chức năng giống hệt bản ESP32-H2, cùng thuật toán, ngưỡng, thời gian và trạng thái cảm biến (kể cả biến thể thử nghiệm `esppresence`, mục **Bộ xử lý hiện diện trên ESP32**): **học phòng trống** (bấm nút BOOT 3 lần hoặc từ Home Assistant), **ngưỡng mặc định của radar** (bấm nút BOOT 4 lần hoặc *Factory thresholds* trong Home Assistant), **Range**, **Radar resolution**, **Radar Bluetooth**, **Hold time**, cập nhật **OTA**. Cách dùng các chức năng này xem [README bản ESP32-H2](../../ESP32-H2/matter-presence-ld2410-learning/README.md); tài liệu này chỉ ghi phần khác.

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
| Log | UART0 (GPIO23/24) + USB-Serial-JTAG | **USB-Serial-JTAG** (cổng USB-C, GPIO18/19) |
| Console Matter (gõ lệnh qua USB) | có | **không** (tắt để có thêm RAM, bật lại được trong `menuconfig`) |
| Wi-Fi sau khi ghép nối | — | luôn thức, không modem sleep (xem **Wi-Fi và BLE**) |
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
| Nút BOOT: bấm đúp = chế độ ghép nối, bấm 3 lần = học phòng trống, bấm 4 lần = ngưỡng mặc định của radar, giữ 5 s = factory reset | GPIO9 |
| USB-C: nạp firmware, log | GPIO18 (D-), GPIO19 (D+) |

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
| Nháy rất nhanh (5 lần mỗi giây) | Ứng dụng yêu cầu *Identify* |
| Sáng liên tục 20 s | Vừa bắt đầu học phòng trống: rời khỏi phòng |
| Nháy chậm (1 s tắt, 1 s sáng) trong 60 s | Đang ghi năng lượng nền của phòng trống |
| Sáng liên tục 10 s | Học xong **hoặc** học thất bại, hoặc ghi ngưỡng mặc định thất bại. Xem kết quả trong log `quiet_learn` |
| Nháy nhanh (0,2 s sáng, 0,2 s tắt) trong 10 s | Đã ghi lại **ngưỡng mặc định** của radar (bấm nút 4 lần hoặc *Factory thresholds*); bản H2 nháy tím |

LED một màu không phân biệt được học xong (bản H2: xanh lá) và thất bại (bản H2: đỏ); ghi ngưỡng mặc định xong thì nháy nhanh nên vẫn nhận ra. Trong lúc học, LED báo việc học thay cho chế độ ghép nối; *Identify* vẫn được ưu tiên.

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
| Keep Wi-Fi awake once BLE is released | bật (xem **Wi-Fi và BLE**) |
| ESP32 Energy Filter (experimental) | tắt. Biến thể `espfilter` mới được build và kiểm tra cho ESP32-H2 (README bản H2) |
| ESP32 Presence Engine (experimental) | tắt. Biến thể `esppresence` build được cho ESP32-C3 SuperMini (mục **Bộ xử lý hiện diện trên ESP32** bên dưới) |
| Hardware (Basic Information) | `ESP32C3-LD2410` |

Chân hợp lệ: GPIO0–GPIO21, trừ chân flash (GPIO11–GPIO17) và USB (GPIO18, GPIO19). Không có menu *Thread Diagnostics* trên ESP32-C3.

## Bộ xử lý hiện diện trên ESP32 (biến thể `esppresence`, thử nghiệm)

Cùng bộ xử lý với bản ESP32-C3-WROOM **1.0.166**: ESP32 tự quyết định có người từ năng lượng từng vùng của radar, với ngưỡng lưu trong radar; tín hiệu mạnh phải vượt cả ngưỡng radar và nhiễu phòng trống + 5 trong 6 trên 8 báo cáo (năng lượng chuyển động rất mạnh 3 báo cáo liền thì kích hoạt ngay); ngưỡng duy trì thấp hơn nhưng trên nhiễu; Hold time là bộ đếm tắt (60 s trong biến thể này). Học xong, ngưỡng radar ở vùng có nhiễu chạm ngưỡng được nâng lên trên nhiễu và ghi vào radar một lần. Thuật toán, việc học phòng trống, lệnh console, hiệu chỉnh, tuỳ chọn và cách quay về bản mặc định: [README bản ESP32-H2](../../ESP32-H2/matter-presence-ld2410-learning/README.md), mục **Bộ xử lý hiện diện trên ESP32**. Bản build mặc định **không** có bộ xử lý này.

```bash
cd ESP32-C3/matter-wifi-presence
idf.py -B build-esppresence -D IDF_TARGET=esp32c3 -D SDKCONFIG=sdkconfig.esppresence \
  -D SDKCONFIG_DEFAULTS=../../ESP32-H2/matter-presence-ld2410-learning/sdkconfig.defaults.esppresence build
```

Kết quả ở `build-esppresence/`: `matter_presence_c3_wifi.bin`, file OTA `matter_presence_c3_wifi-ota.bin` (đổi đuôi thành `.ota`).

Riêng bản ESP32-C3 SuperMini:
- **Console:** log và lệnh ở cổng USB-C như bản mặc định (USB-Serial-JTAG). Dấu nhắc `presence>` (REPL của ESP-IDF, task `console_repl`); gõ `help`, `STATUS`, `DIAG_STREAM on`... Console Matter vẫn tắt như bản mặc định.
- **LED một màu** khi học phòng trống: sáng 20 s (rời phòng), nháy chậm khoảng 100 s (ESP32 ghi nhiễu 30 s, rồi radar tự đo), rồi sáng 10 s cả khi xong lẫn khi thất bại: xem kết quả trong log `quiet_learn` (kể cả các ngưỡng được nâng trên nhiễu).
- Firmware `1.0.<số commit>-esppresence`, Model `Presence LD2410 ESP engine`; VID/PID giữ `0xFFF1`/`0x8001`, chân radar GPIO3/GPIO4 như bản mặc định.
- **RAM khi chạy:** biến thể này thêm task `presence_eng` (stack 4 KB) và `console_repl` (stack 3,5 KB), cùng hàng đợi báo cáo radar và bộ đệm console, nên heap còn ít hơn bản mặc định khoảng 10–11 KB (ước tính từ kích thước stack và bộ đệm, **chưa đo trên bo**). Heap thấp nhất của bản C3 đo được lúc ghép nối là 40,0 KB (1.0.131, mục **RAM và flash**); kiểm tra lại bằng log `mem` với biến thể này, nhất là lúc ghép nối (BLE và Wi-Fi cùng chạy).
- **Kiểm chứng:** host test như bản H2 (chung mã nguồn, mục **Mức độ kiểm chứng** của biến thể ở README bản H2). Build 1.0.138 (Docker `release-v1.6.1_idf_v6.0.2`): **0 lỗi, 0 cảnh báo**; firmware 1 553 728 B (phân vùng app còn trống 21 %), RAM tĩnh 151 595 B, hơn bản mặc định cùng mã 38 448 B flash và 1 022 B RAM tĩnh; `sdkconfig` chỉ khác bản mặc định ở Model, Hold time ban đầu (60) và 15 tuỳ chọn của bộ xử lý; header OTA VID `0xFFF1`, PID `0x8001`, phiên bản 138 / `1.0.138-esppresence`. Bản mặc định C3 vẫn build 0 lỗi, 0 cảnh báo, `sdkconfig` giống hệt bản 1.0.136.
- **Chưa thử trên bo thật:** console `presence>` trên cổng USB-C, bộ xử lý với radar thật, bước nâng ngưỡng radar trên nhiễu với radar thật, RAM khi chạy, OTA lên biến thể này.

## Wi-Fi và BLE

- **Lúc ghép nối**, BLE (kết nối với điện thoại) và Wi-Fi dùng chung một anten 2,4 GHz, chia nhau thời gian phát/thu (*coexistence* của ESP-IDF). Điện thoại giữ kết nối BLE trong lúc bo vào Wi-Fi, rồi chờ kết quả khoảng 30 s. Trong lúc này firmware giữ nguyên cách chia của ESP-IDF (Wi-Fi có modem sleep).
- **Sau khi ghép nối**, BLE được tắt và trả RAM lại (`BLE deinitialized and memory reclaimed`). Từ bản 1.0.132, Wi-Fi lúc đó **tắt modem sleep** (log `BLE released: Wi-Fi power save off`, dòng `wifi` của `id_diag` ghi `power_save=none`).
  - Ở modem sleep, bo ngủ giữa hai beacon DTIM và router giữ gói tin cho bo tới lần thức sau. Phản hồi của controller tới chậm, nên bản tin Matter phải gửi lại (log `Msg Retransmission`) hoặc hết thời gian, như trong log của bản 1.0.128.
  - Bo luôn chạy bằng USB nên tốn thêm điện không đáng kể với cảm biến này. Tắt trong `menuconfig` → *Presence Sensor Configuration* → *Keep Wi-Fi awake once BLE is released*.
- **BLE gọn hơn** (chỉ dùng để ghép nối): một kết nối, không ghép đôi BLE (Matter tự bảo mật bằng PASE), chỉ quảng bá kiểu cũ và PHY 1M, bộ đệm nhỏ. Các giá trị theo mục *BLE Optimizations* trong hướng dẫn tối ưu RAM của esp-matter (giống ví dụ ESP32-C2 của Espressif). Theo hướng dẫn đó, heap lúc ghép nối tăng khoảng 19 KB trên ESP32-C3.
- **Bỏ các kiểu bảo mật Wi-Fi Matter không dùng**: Wi-Fi Enterprise (802.1X/EAP) và OWE. Matter SDK chỉ đặt tên mạng và mật khẩu (WPA/WPA2/WPA3 Personal); WPA3 vẫn giữ.
- **Bỏ console Matter** (khoảng 10 KB heap theo hướng dẫn của esp-matter), bỏ các cluster Matter không endpoint nào dùng, giảm bộ đệm lịch sử sự kiện Matter (sự kiện quan trọng 4 KB → 1 KB, debug 1 KB → 256 byte).
- Bỏ tuỳ chọn giới hạn công suất phát Wi-Fi (bản 1.0.125–1.0.131): không giúp được lỗi kết nối (xem **Xử lý sự cố**).

## RAM và flash

Đo bằng `idf.py size` (ESP-IDF v6.0.2, esp-matter 1.6.1, cấu hình mặc định, firmware 1.0.132):

| | ESP32-H2 (Thread) | ESP32-C3 (Wi-Fi) |
|---|---|---|
| Firmware (phân vùng app 1 920 KB) | 1 766 KB, trống 154 KB (8 %) | 1 478 KB, trống 442 KB (23 %) |
| RAM tĩnh | 171,1 KB / 252,0 KB DIRAM (67,9 %) | 147,0 KB / 313,8 KB DRAM (46,9 %) |
| RAM còn trống cho heap | 80,9 KB | 166,7 KB |

So với bản 1.0.131 (đo bằng build), bản C3 nhỏ hơn 109 KB flash và còn thêm 9,1 KB RAM tĩnh cho heap (166,7 KB so với 157,6 KB): bộ đệm sự kiện Matter, console, các cluster không dùng. Phần lớn RAM tiết kiệm được là RAM cấp phát khi chạy (task và bộ đệm của console, bộ đệm BLE lúc ghép nối), không hiện trong bảng này; con số thật chỉ đo được trên bo bằng log `mem`.

Wi-Fi và BLE cấp phát thêm bộ đệm từ heap khi chạy, nhiều nhất lúc ghép nối khi cả hai cùng bật. Số đo trên bo thật (log của người dùng):
- bản 1.0.122, lúc ghép nối: khi khởi động heap còn 41,0 KB; đang ghép nối (BLE và Wi-Fi cùng bật) còn 30,7 KB, thấp nhất 25,9 KB;
- bản 1.0.128, sau 10 phút chạy: còn 96,6 KB, nhưng thấp nhất từ lúc bật chỉ còn **15,7 KB** (`W mem: Heap low: 96552 bytes free, lowest 16060 since boot`), sát mức báo động;
- bản 1.0.131 (mã Wi-Fi trong flash thay vì IRAM, thêm 19,1 KB heap), đang ghép nối: còn 49,0 KB, thấp nhất 40,0 KB (`id_diag ... heap=50180 min_heap=40940`).

Từ bản 1.0.130, mã Wi-Fi của bản ESP32-C3 nằm trong flash thay vì IRAM (`CONFIG_ESP_WIFI_IRAM_OPT=n`, `CONFIG_ESP_WIFI_RX_IRAM_OPT=n`): trên ESP32-C3, IRAM và DRAM dùng chung SRAM. Đổi lại, Wi-Fi chậm hơn một chút; Matter chỉ gửi rất ít dữ liệu nên không cần tốc độ đó. Heap của bản 1.0.132 cần đo lại trên bo.

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
I id_diag: wifi connected ssid="..." bssid=... ch=6 rssi=-55 power_save=none
I id_diag: ip   ipv4=192.168.1.20 ipv6: link-local=1 unique-local=1 global=1
```

Không bao giờ in mật khẩu Wi-Fi, passcode hay khóa riêng. `rssi` dưới khoảng -75 dBm là sóng yếu. Khối in khi có sự kiện Wi-Fi chờ 5 s cho địa chỉ ổn định; cảnh báo `No IPv6 link-local address yet` khi đã kết nối nghĩa là thiết bị chưa có IPv6, controller chưa tới được thiết bị. Ngay sau khi khởi động, bo đã ghép nối vẫn báo `wifi not connected (connecting, ...)` cho tới khi vào được Wi-Fi. `power_save=min_modem` khi chưa ghép nối (BLE còn chạy), `none` sau khi BLE tắt. Log `mem` liệt kê thêm stack của task `wifi` thay cho `ot_task`.

Từ bản 1.0.132, bản C3 **không có console Matter** (gõ lệnh trong monitor): tắt để có thêm khoảng 10 KB RAM (hướng dẫn tối ưu RAM của esp-matter). Log vẫn ra cổng USB như cũ. Cần console để thử nghiệm: `menuconfig` → *Component config* → *CHIP Core* → *General Options* → bật *Use the CHIP shell library* (`CONFIG_ENABLE_CHIP_SHELL`), rồi build lại. Khi đó có các lệnh của esp-matter như bản H2 (không có `ot_cli`) và lệnh `matter wifi scan`, `matter wifi mode`, `matter wifi connect <SSID> <mật khẩu>` của Matter SDK; mật khẩu gõ vào lệnh connect hiện trên màn hình monitor và nằm trong lịch sử lệnh của console.

## Xử lý sự cố

- **Không thấy log:** chọn đúng cổng USB của SuperMini; một số cáp USB-C chỉ có dây nguồn.
- **Ghép nối dừng ở bước Wi-Fi:** mạng 2,4 GHz, đúng mật khẩu, router không chặn thiết bị mới. Log `id_diag: wifi not connected` sau khi ghép nối nghĩa là thiết bị không vào được Wi-Fi.
- **Thấy mạng Wi-Fi nhưng không kết nối được**, log lặp lại:
  ```
  I wifi:state: init -> auth (0xb0)
  I wifi:state: auth -> init (0x200)
  I chip[DL]: WiFi station state change: Connecting -> Connecting_Failed
  ```
  Bo gửi yêu cầu xác thực nhưng router không trả lời: sau đúng 1 s thì bỏ, khoảng 5 s sau thử lại (gồm một lần quét kênh khoảng 4,7 s). Điện thoại chỉ chờ bước này khoảng 30 s, nên chỉ có khoảng 5 lần thử.
  - Mỗi lần như vậy có một dòng `W id_diag: EV wifi failed or left: reason ...`, ví dụ `reason 2 (authentication not answered, or no longer valid for the access point), bssid=... rssi=-55 dBm` (bản 1.0.126–1.0.131 ghi `authentication timed out, the access point did not answer`). Các lý do hay gặp:
    - **reason 2**: router không trả lời bước xác thực, hoặc router báo xác thực không còn hiệu lực (cũng gặp khi router đá bo ra ngay sau khi vào mạng);
    - **reason 4**: router không trả lời bước kết nối (`assoc -> init`), hoặc ngắt bo vì không hoạt động;
    - **reason 15**: bước trao khoá hết thời gian, thường do sai mật khẩu;
    - **reason 36**: chính bo dừng lại, ví dụ khi điện thoại đã hết thời gian chờ bước Wi-Fi.
    - **rssi** là tín hiệu của router mà bo nhận được. Khoảng −70 dBm trở lên là đủ mạnh. Dưới −80 dBm là quá yếu: đặt bo gần router hoặc nút mesh hơn.
    - Khi vào được mạng, dòng `EV wifi joined ... security=WPA2` cho biết kiểu bảo mật đã dùng.
  - Cộng đồng ESPHome và Arduino báo một số bo ESP32-C3 SuperMini không kết nối được khi phát Wi-Fi hết công suất, và thường sửa bằng cách hạ xuống 8,5 dBm. Bản 1.0.125–1.0.127 đã thử giới hạn này: trên bo đã thử không giúp gì (xem **Mức độ kiểm tra**), nên từ bản 1.0.132 tuỳ chọn giới hạn công suất được bỏ hẳn.
  - **TP-Link Deco X10 (mesh):** người dùng đã sửa được lỗi này bằng cách **tắt Beamforming** trong app Deco; sau đó bo ghép nối được. Lỗi lặp lại sau đó (bản 1.0.131): kiểm tra Beamforming vẫn tắt (cập nhật firmware Deco có thể bật lại), bật **IoT Network** trong app Deco (*More* → *IoT Network*, mạng 2,4 GHz riêng cho thiết bị nhà thông minh) rồi ghép nối C3 vào mạng đó; vẫn lỗi thì tạm tắt *Fast Roaming* và đặt bảo mật WPA2. Router mesh khác có tuỳ chọn tương tự thì thử như vậy.
  - Thử Wi-Fi mà không cần ghép nối: bật console (xem **Log khởi động và chẩn đoán**), rồi gõ `matter wifi connect <tên mạng> <mật khẩu>`.
    - Thử với router, rồi với điểm phát Wi-Fi của điện thoại (2,4 GHz, WPA2) đặt cạnh bo.
    - Được với điện thoại mà không được với router: lỗi ở router. Không được với cả hai: lỗi ở bo (anten, thạch anh).
    - Mạng nhập kiểu này không được lưu lại.
  - Kiểm tra router:
    - bảo mật **WPA2-PSK (AES)** hoặc WPA2/WPA3 hỗn hợp, không dùng WPA3 riêng;
    - chế độ 2,4 GHz b/g/n hoặc b/g/n/ax, không chỉ ax: ESP32-C3 không có Wi-Fi 6;
    - tắt lọc địa chỉ MAC;
    - nếu 2,4 GHz và 5 GHz chung một tên mạng (band steering), thử tạm một tên riêng cho 2,4 GHz;
    - dùng nguồn USB khoẻ, vì radar và Wi-Fi cùng lấy điện từ cổng USB.
- **Ghép nối xong phần Wi-Fi nhưng app báo không tìm thấy thiết bị:** điện thoại và thiết bị phải cùng LAN, router không chặn mDNS/multicast, có IPv6 (xem dòng `ip` của `id_diag`).
- Mỗi lần thử lại: factory reset (giữ BOOT 5 s) hoặc `erase-flash`.

## Mức độ kiểm tra

- **Đã kiểm tra bằng build:** firmware ESP32-C3 build không lỗi, không cảnh báo, từ thư mục này và từ thư mục ESP32-H2 (`set-target esp32c3`); firmware ESP32-H2 vẫn build như trước. Kiểm tra cấu hình sinh ra (Wi-Fi bật, OpenThread tắt, log USB, PID `0x8001`, chân; bản 1.0.132: console tắt, BLE thu gọn, Enterprise/OWE tắt, 18 cluster Matter còn lại đúng là các cluster thiết bị dùng), kích thước firmware so với phân vùng OTA, và test trên máy tính cho phần số serial (MAC 6 byte của ESP32-C3), nút bấm 4 lần và LED.
- **Đã chạy trên thiết bị thật (log của người dùng):** ghép nối với Apple Home qua BLE. Các bước PASE, kiểm tra chứng chỉ DAC/PAI test, CSR và AddNOC đều đạt. Ghép nối dừng ở bước kết nối Wi-Fi, với lỗi xác thực ở trên.
  - Lỗi giống hệt ở bản 1.0.122 (công suất mặc định), ở bản 1.0.125 (`tx_max=8.50dBm`) và ở một firmware đèn Matter khác trên cùng bo, cùng router.
  - Vậy hạ công suất không sửa được lỗi trên bo này.
  - Nguyên nhân là Beamforming của router TP-Link Deco X10: tắt đi thì bo vào được Wi-Fi và ghép nối được (người dùng báo lại, chưa có log của lần thành công). Lần đó bo chạy bản giới hạn 8,5 dBm; công suất mặc định (từ 1.0.128) với Beamforming tắt chưa được thử.
  - Bản 1.0.128–1.0.131 đã ghép nối và chạy cùng lúc với 3 controller (log `fabrics=3`, có Apple Home và Home Assistant), đọc radar, học phòng trống (radar tự đo bằng lệnh 0x000B) và *Factory thresholds* từ Home Assistant. Bản 1.0.131 lại có lúc khó vào Wi-Fi khi ghép nối (router không trả lời bước xác thực ở −55 dBm, xem **Xử lý sự cố**).
- **Cần thử trên thiết bị thật với bản 1.0.132:** ghép nối qua BLE + Wi-Fi (BLE đã thu gọn), Wi-Fi sau khi ghép nối (`power_save=none`, ít `Msg Retransmission` hơn), bấm 4 lần (nháy nhanh 10 s), RAM khi chạy (log `mem`, nhất là lúc ghép nối), OTA qua Wi-Fi, độ ổn định Wi-Fi lâu dài.

## Cấu trúc thư mục

```
matter-wifi-presence/
├── CMakeLists.txt       # project ESP32-C3, dùng presence.cmake và main/ của thư mục ESP32-H2
├── sdkconfig.defaults   # target esp32c3, đường dẫn bảng phân vùng và chip_project_config.h dùng chung
├── README.md
└── REBUILD_PROMPT.md    # tổng hợp yêu cầu của bản C3, dùng làm prompt viết lại firmware từ đầu

../../ESP32-H2/matter-presence-ld2410-learning/   # mã nguồn dùng chung
├── presence.cmake            # phiên bản firmware, component esp-matter, tên project theo chip
├── sdkconfig.defaults        # chung cho hai chip
├── sdkconfig.defaults.esp32c3  # Wi-Fi, BLE và RAM của bản C3, log USB, PID 0x8001, chân radar / LED của SuperMini
├── sdkconfig.defaults.esp32h2  # Thread, chân của bản H2
├── partitions.csv            # 4 MB, 2 phân vùng OTA
└── main/
    ├── board.h, board_esp32c3.h, board_esp32h2.h   # tên board, mạng, chân cấm theo chip
    └── ...                   # cảm biến, radar, LED, nút, Matter (chung)
```
