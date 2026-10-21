# Firmware Matter Presence Sensor (HLK-LD2410C)

Firmware chung cho mọi bo của `matter-presence-sensor`: cùng mã, cùng thuật toán; mỗi bo chỉ khác file cấu hình `boards/<bo>.defaults` (chân radar, LED, nút, cổng log). Hiện có các bo **ESP32-C3, Matter over Wi-Fi**: `c3-supermini`, `c3-wroom-devkit`, `c3-radar-r45`, và bo **ESP32-H2, Matter over Thread**: `h2-mini` (mục **Các bo**).

Cảm biến hiện diện chuẩn Matter (device type **Occupancy Sensor 0x0107**) với radar 24 GHz **HLK-LD2410C**. Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa...: bo ESP32-C3 qua Wi-Fi nhà, không cần Thread Border Router; bo ESP32-H2 qua mạng Thread (cần Thread Border Router). Radar LD2410 phát hiện được cả người ngồi yên (thở, cử động nhẹ), khác cảm biến PIR chỉ thấy chuyển động. **Có người hay không do radar quyết định** từ ngưỡng của nó; ESP32 chỉ học phòng trống và đặt ngưỡng cho radar khi được yêu cầu (mục **Học phòng trống**).

Mã nguồn là bản ổn định `ESP32-C3/matter-presence-ld2410` (viết lại từ đầu firmware 1.0.132 `ESP32-C3/matter-wifi-presence`) cộng cách học phòng trống của biến thể `esplearn` 1.0.175; phần Thread lấy từ bản ESP32-H2 `ESP32-H2/matter-presence-ld2410-learning` (mục **Bo ESP32-H2 mini**). Endpoint 1–4, cluster, khoá lưu trữ, bảng phân vùng, VID/PID giữ nguyên như các bản trước của từng chip, nên **cập nhật OTA từ các bản trước lên bản này giữ nguyên ghép nối và các entity Home Assistant** (mục **Nâng cấp từ các bản trước**; mới kiểm chứng bằng so sánh cấu hình và build, chưa thử trên bo).

Trong Home Assistant có thêm 5 điều khiển (Apple Home không hiện): **Hold time**, **Learn empty room** (học phòng trống, hoặc bấm nút 3 lần; *Factory thresholds*, hoặc bấm 4 lần trên bo ESP32-C3, để ghi lại ngưỡng mặc định), **Radar Bluetooth**, **Radar resolution**, **Range**. Tên thiết bị là **Presence-** + 4 ký tự cuối của số serial, ví dụ **Presence-B2C4**. Các entity chẩn đoán **Reboot count**, **Uptime**, **Wi-Fi RSSI** bị Home Assistant tắt sẵn: bật chúng trong Home Assistant (xem **Entity chẩn đoán**); trên bo ESP32-C3 firmware gửi lại Uptime và Wi-Fi RSSI để giá trị không bị đứng. Entity **nhiệt độ chip ESP32-C3** (endpoint 5) chỉ có khi build với `PRESENCE_CHIP_TEMPERATURE` (mặc định tắt, xem **Nhiệt độ chip**).

## Khác biệt so với bản cũ (`ESP32-C3/matter-wifi-presence`, 1.0.132)

| | 1.0.132 (`matter-wifi-presence`) | Bản này (`matter-presence-sensor/firmware`) |
|---|---|---|
| Mã nguồn | `main/`, bảng phân vùng, sdkconfig của thư mục ESP32-H2 (chung với bản Thread) | tự chứa; hiện chỉ build cho ESP32-C3 (bo H2 thêm sau) |
| Chức năng, thuật toán, log | — | giống hệt, trừ các dòng dưới (Wi-Fi modem sleep, watchdog kết nối, báo cáo Wi-Fi RSSI và Uptime, cách học phòng trống) |
| Học phòng trống | radar tự đo (lệnh 0x000B) 60 s; radar không có lệnh này thì ESP32 học theo phân vị 95 % | ESP32 ghi phòng trống 120 s, ngưỡng = mức cao nhất + biên (`LD2410_LEARN_PEAK`, mục **Học phòng trống**); cách cũ còn trong `menuconfig` |
| Wi-Fi sau khi ghép nối (BLE đã tắt) | tắt modem sleep (`WIFI_PS_NONE`): radio luôn nghe, chip nóng tới 80–96 °C (đo trên bo bằng 1.0.143) | giữ modem sleep `min_modem`, mặc định của ESP-IDF, esp-matter và ESPHome trên ESP32: bo nóng ít hơn; gói từ controller tới bo trễ tối đa một chu kỳ DTIM (thường 100–300 ms). Bật `PRESENCE_WIFI_STAY_AWAKE` thì như cũ |
| Mất kết nối với controller | chờ controller hoặc Matter SDK tự thử lại (5–10 phút, thưa dần) | watchdog kết nối: không có subscription 5 phút thì thông báo lại DNS-SD, 10 phút nối lại Wi-Fi, 30 phút khởi động lại; task Matter treo 3 phút thì khởi động lại (`menuconfig`; đặt 0 thì như cũ) |
| Wi-Fi RSSI, Uptime trong Home Assistant | chỉ gửi lúc Home Assistant subscribe: RSSI đứng giá trị, Uptime lệch sau khi Home Assistant khởi động lại | gửi lại RSSI khi đổi từ 3 dB (kiểm tra mỗi phút), cả RSSI và uptime 10 phút một lần (`menuconfig`; đặt 0 thì như cũ) |
| VID / PID, endpoint, cluster, tên entity, mã mode | `0xFFF1` / `0x8001`, endpoint 1–4 | giống hệt; endpoint 5 (Temperature Sensor: nhiệt độ chip, cluster Temperature Measurement) chỉ có khi bật `PRESENCE_CHIP_TEMPERATURE` (mặc định tắt từ 1.0.147; bật ở 1.0.143–1.0.146) |
| Bảng phân vùng, khoá NVS/KVS | — | giống hệt |
| sdkconfig sinh ra | — | giống hệt, trừ: đường dẫn file, tên project, các tuỳ chọn mới (`BOARD_NAME`, `DIAG_REPORT_*`, `NET_WATCHDOG_*`, `PRESENCE_CHIP_TEMPERATURE` tắt, `LD2410_LEARN_PEAK` bật, ghi 120 s), `PRESENCE_WIFI_STAY_AWAKE` tắt, và phần riêng của từng bo |
| LED | chọn được WS2812 / LED GPIO / không có | như cũ: LED GPIO một màu, WS2812 (màu như 1.0.132) hoặc không có LED (`c3-radar-r45`) |
| Biến thể build | mặc định, `unique_setup`, `factory` | mỗi bo một file `boards/<bo>.defaults`, tính năng thêm `features/unique_setup.defaults`; `tools/build.sh <bo> [tính năng]`. `factory` bỏ: PAI test chỉ cấp DAC cho PID `0x8000` |
| Mã Thread/OpenThread (`thread_diag`, phần OpenThread của `id_diag`, OT CLI) | có, tắt khi build C3 | bỏ |
| Tên file build | `matter_presence_c3_wifi*` | `matter_presence_sensor*` trong `build/<bo>/`, chép sang `dist/presence-<bo>-<phiên bản>*` (bản `matter-presence-ld2410` vẫn dùng tên cũ) |
| Kích thước (`idf.py size`, cùng Docker image) | 1 478 KB, RAM tĩnh 147,0 KB | xem **RAM và flash** |

Thay đổi cấu trúc mã (không đổi hành vi):
- `radar_control.cpp` tách thành phần Matter (`radar_control.cpp`: 4 Mode Select) và phần logic (`radar_worker.cpp`: task `radar_cfg`, các yêu cầu, "lựa chọn cuối cùng thắng", Bluetooth của radar), để chạy được test trên máy tính.
- Task presence chuyển từ `app_main.cpp` sang `presence.cpp`; log đổi tag từ `app_main` thành `presence`.
- Lệnh khởi động lại radar (0x00A3) không có ACK được driver coi là thành công (trước đây phía gọi tự xử lý); không còn log cảnh báo `No ACK for command 0x00A3`.
- `Learn empty room` từ chối mode ngoài 0/1/2 (`ESP_ERR_INVALID_ARG`), như các select khác.
- Yêu cầu học phòng trống và yêu cầu ngưỡng mặc định được đổi cùng nhau trong một critical section. Bản 1.0.132 đổi hai biến riêng lẻ: nếu bấm nút 3 lần đúng lúc Home Assistant gửi *Factory thresholds*, có thể không việc nào chạy và entity kẹt ở `Factory thresholds` cho tới lựa chọn sau (rất hiếm, nhưng có thể xảy ra). "Lựa chọn cuối cùng thắng" giữ nguyên.
- Bỏ mã chết: `identity_diag_request_dump()`, trường *no-one duration* trong yêu cầu đổi thông số (lệnh 0x0060 luôn ghi lại đúng giá trị radar đang giữ).

## Các bo

Mỗi bo là một file `boards/<bo>.defaults`, đặt sau `sdkconfig.defaults` khi build (`tools/build.sh <bo>`). File bo chỉ chứa tên bo (log), chân radar, LED, nút và cổng log; mọi thứ khác dùng chung.

| Bo | Module | Radar: ESP RX ← radar TX, ESP TX → radar RX | LED | Nút | Log, nạp |
|---|---|---|---|---|---|
| `c3-supermini` | ESP32-C3 SuperMini | GPIO3, GPIO4 | GPIO8 một màu, sáng khi mức thấp | GPIO9 (BOOT) | USB-C (USB-Serial-JTAG) |
| `c3-wroom-devkit` | ESP32-C3-WROOM-02-N4, kiểu DevKitC-02 | GPIO3, GPIO4 | WS2812 GPIO8, độ sáng 20/255 | GPIO9 (BOOT) | USB (USB-Serial-JTAG) |
| `c3-radar-r45` | ESP32-C3-WROOM-02-N4 trên mạch `../hardware/c3-radar-r45` | IO10, IO3 | không có | pad IO9 | pad RX0 / TX0 (UART0) |
| `h2-mini` | ESP32-H2 mini (ESP32-H2 SuperMini), **Thread** | GPIO11, GPIO12 | WS2812 GPIO8, độ sáng 20/255 | GPIO9 (BOOT) | UART0 (GPIO23/24) và USB-C (USB-Serial-JTAG) |

Chip lấy từ tên bo: `c3-…` build cho ESP32-C3 (Matter over Wi-Fi, PID `0x8001`), `h2-…` cho ESP32-H2 (Matter over Thread, PID `0x8000`). Cấu hình riêng của từng chip nằm trong `sdkconfig.defaults.esp32c3` và `sdkconfig.defaults.esp32h2` (ESP-IDF tự nạp sau `sdkconfig.defaults`). Bo khác cùng chip: chép một file trong `boards/`, đổi tên (giữ tiền tố `c3-` hoặc `h2-`) và chân. `main/board.h` báo lỗi lúc build nếu chân trùng nhau hoặc là chân flash (và USB trên ESP32-C3).

- **Flash 4 MB** cho mọi bo (bảng phân vùng chung).
- Radar **HLK-LD2410C** (LD2410, LD2410B cũng được, kể cả firmware cũ không có lệnh 0x000B, 0x00AB, 0x00A5; nối theo chữ in trên mạch radar).

## Bo ESP32-C3 SuperMini (`c3-supermini`)
- Radar **HLK-LD2410C** (LD2410, LD2410B cũng được, kể cả firmware cũ không có lệnh 0x000B, 0x00AB, 0x00A5; nối theo chữ in trên mạch radar).

### Sơ đồ nối dây (SuperMini)

| LD2410C | ESP32-C3 SuperMini |
|---|---|
| TX | **GPIO3** (UART1 RX) |
| RX | **GPIO4** (UART1 TX) |
| VCC | **5V** |
| GND | GND |

LD2410C cần nguồn **5V** (chân 5V của SuperMini khi cắm USB); mức logic UART là 3,3 V nên nối thẳng. Radar dùng **UART1**, 256000 baud 8N1.

| Chức năng | Chân |
|---|---|
| Radar LD2410C (UART1) | RX = GPIO3 (nối TX của radar), TX = GPIO4 (nối RX của radar) |
| LED xanh dương trên board | GPIO8, sáng khi chân ở mức thấp |
| Nút BOOT | GPIO9, tích cực mức thấp, pull-up trong |
| USB-C: nạp firmware, log (USB-Serial-JTAG) | GPIO18 (D−), GPIO19 (D+) |

- **GPIO3, GPIO4** không phải chân strapping, flash, USB hay UART0. GPIO4 là chân JTAG MTMS, nhưng JTAG đi qua cổng USB nên GPIO4 dùng tự do.
- **Chân strapping GPIO2, GPIO8, GPIO9**: GPIO8 phải ở mức cao và GPIO9 ở mức thấp để vào chế độ nạp; LED kéo GPIO8 lên 3V3 nên giữ được mức cao. Không nối thêm gì vào GPIO8, GPIO9.
- **Chân flash GPIO11–GPIO17** (GPIO11 là VDD_SPI) và **chân USB GPIO18/GPIO19**: build báo lỗi (`static_assert` trong `main/board.h`) nếu chọn các chân này cho radar, nút hoặc LED, hoặc khi hai chức năng trùng chân. Áp dụng cho mọi bo.
- **UART0 GPIO20 (RX), GPIO21 (TX)**: không dùng cho log, nhưng ROM vẫn in log khởi động ra GPIO21.
- Radar và Wi-Fi cùng lấy điện từ cổng USB: dùng nguồn USB khoẻ. Khi ESP32 khởi động lại bằng phần mềm (OTA, lỗi, factory reset), radar không mất điện và vẫn chạy tiếp; lần khởi động nào firmware cũng mở một phiên cấu hình với radar, nên radar bị bỏ lại ở chế độ cấu hình cũng gửi báo cáo trở lại.

### Đèn LED

LED của SuperMini chỉ có một màu, mọi màu ở cột cuối đều là "sáng". Với LED WS2812 (`STATUS_LED_TYPE_WS2812`, bo `c3-wroom-devkit`), LED hiện đúng màu ở cột cuối, như bản ESP32-H2 và 1.0.132. Bo không có LED (`STATUS_LED_TYPE_NONE`, `c3-radar-r45`): xem trạng thái trong log hoặc Home Assistant. Thứ tự ưu tiên: Identify, rồi học phòng trống / ngưỡng mặc định, rồi chế độ ghép nối.

| LED | Trạng thái | Màu (WS2812) |
|---|---|---|
| Nháy rất nhanh (5 lần mỗi giây) | Ứng dụng yêu cầu *Identify* | trắng |
| Sáng liên tục 20 s | Vừa bắt đầu học phòng trống: rời khỏi phòng | cyan |
| Nháy chậm (1 s tắt, 1 s sáng) trong 120 s | Đang ghi năng lượng nền của phòng trống | cyan nháy |
| Sáng liên tục 10 s | Học xong **hoặc** học thất bại, hoặc ghi ngưỡng mặc định thất bại; xem log `quiet_learn` | xanh lá / đỏ |
| Nháy nhanh (0,2 s sáng, 0,2 s tắt) trong 10 s | Đã ghi ngưỡng mặc định của radar | tím |
| Nháy chậm (1 s sáng, 1 s tắt) | Chế độ ghép nối: đang chờ thêm vào ứng dụng Matter | xanh dương |
| Tắt | Hoạt động bình thường (đã ghép nối), hoặc chế độ ghép nối đã hết giờ | |

Huỷ việc học thì LED trở lại bình thường ngay. Đặt lại cùng một kết quả (ví dụ thất bại lần hai) thì tính lại 10 s. Trạng thái radar (có người, mất tín hiệu) không hiện trên LED. LED trên board khác sáng ngược: `menuconfig` → *Presence Sensor Configuration* → bật *Status LED is on when the pin is high*.

### Nút BOOT (GPIO9; pad IO9 trên `c3-radar-r45`)

| Thao tác | Kết quả |
|---|---|
| **Bấm đúp** | Mở **chế độ ghép nối** 15 phút. Chưa ghép nối: thêm lại khi cửa sổ lúc khởi động đã hết giờ. Đã ghép nối: thêm vào ứng dụng thứ hai (Multi-Admin), chỉ qua mạng vì BLE đã tắt |
| **Bấm 3 lần** | **Học phòng trống**, như chọn *Learning* trong Home Assistant |
| **Bấm 4 lần** | **Ngưỡng mặc định** của radar, như chọn *Factory thresholds* (huỷ việc học đang chạy nếu có) |
| Bấm 5 lần trở lên | Không làm gì (log `5 clicks, ignored`) |
| **Giữ 5 giây** | **Factory reset**: xoá thông tin Wi-Fi và ghép nối Matter, khởi động lại; sau đó LED nháy chờ ghép nối. Giữ được 1 s thì log nhắc |

Một lần bấm dài tối đa 0,5 s; các lần bấm cách nhau tối đa 0,4 s, nên bấm đúp có tác dụng khoảng 0,4 s sau khi nhả nút (để phân biệt với bấm 3 lần). Một lần giữ quá 0,5 s huỷ các lần bấm trước nó. Nút được đọc 20 ms một lần, chống dội 40 ms.

## Bo ESP32-C3-WROOM-02-N4 (`c3-wroom-devkit`, LED WS2812 GPIO8)

Bo dùng module **ESP32-C3-WROOM-02-N4** (flash 4 MB) với LED RGB **WS2812** nối GPIO8, như ESP32-C3-DevKitC-02. Trước đây là biến thể `wroom_n4` (1.0.149–1.0.150) và `esplearn` (1.0.175) của `ESP32-C3/matter-presence-ld2410`.
- **Khác SuperMini chỉ ở cấu hình**, mã nguồn chung:
  - `BOARD_NAME` = `ESP32-C3-WROOM-02-N4` (log khởi động);
  - LED `STATUS_LED_TYPE_WS2812` trên GPIO8, độ sáng 20/255 (`STATUS_LED_BRIGHTNESS`).
- **Không có entity nhiệt độ chip** (như `esplearn`; `wroom_n4` có). Muốn có thì build với `PRESENCE_CHIP_TEMPERATURE` và giới hạn endpoint động 6.
- **Các chân khác như SuperMini:** radar TX → GPIO3, radar RX → GPIO4, nút GPIO9 (BOOT). Kiểm tra bo có đưa các chân này ra không.
- **Log** ra cổng USB-Serial-JTAG (GPIO18/GPIO19) như SuperMini. Nếu cổng USB của bo đi qua chip USB-UART (CP2102, CH340...), vẫn nạp được nhưng không thấy log; khi đó cần build với console UART0 (như `c3-radar-r45`).

WS2812 trên SuperMini: chọn *Status LED* → *Addressable WS2812 RGB LED* trong `menuconfig`. Dây tín hiệu WS2812 nằm ở mức thấp khi rảnh, nên nếu dùng GPIO8 thì LED xanh có sẵn của SuperMini (sáng khi GPIO8 ở mức thấp) sẽ sáng mãi; dùng chân khác cho WS2812 thì tốt hơn.

## Bo c3-radar r45 (`c3-radar-r45`, mạch Ø45 mm)

Mạch in trong `../hardware/c3-radar-r45` (KiCad): ESP32-C3-WROOM-02-N4 và LD2410C trên một bo tròn Ø45 mm. Chân lấy từ sơ đồ của mạch đó (bảng *Gán chân* trong README của mạch):

| Chức năng | ESP32-C3 | Dùng trong firmware |
|---|---|---|
| LD2410C TX → ESP RX | IO10 | `LD2410_UART_RX_PIN=10` (UART1, 256000 8N1) |
| ESP TX → LD2410C RX | IO3 | `LD2410_UART_TX_PIN=3` |
| LD2410C OUT | IO1 (100 kΩ kéo xuống) | không dùng: firmware đọc trạng thái qua UART, như mọi bo |
| BOOT | IO9 (10 kΩ kéo lên, chỉ có pad) | nút: `RESET_BUTTON_GPIO=9` |
| LED | không có | `STATUS_LED_TYPE_NONE` |
| Log, nạp | RXD0 / TXD0 (pad RX0 / TX0) | console UART0 (`ESP_CONSOLE_UART_DEFAULT`), 115200 baud |

- **Không có nút bấm** ngoài RESET: thao tác nút (bấm đúp, 3 lần, 4 lần, giữ 5 s) làm bằng cách chập pad IO9 với pad GND bên cạnh. Học phòng trống, ngưỡng mặc định và mở ghép nối làm được từ Home Assistant; **factory reset** chỉ có cách giữ IO9 với GND 5 s (hoặc xoá flash qua pad nạp).
- **Không có LED**: firmware vẫn chạy các trạng thái LED (không đổi gì khác), chỉ không xuất ra chân nào. Trạng thái xem trong log UART0 hoặc entity *Learn empty room* của Home Assistant.
- **USB-C chỉ cấp nguồn.** Log và nạp qua pad RX0 / TX0 với mạch USB-UART 3,3 V (Linux `/dev/ttyUSB0`). Vào chế độ nạp: giữ IO9 nối GND rồi bấm RESET. Sau lần nạp đầu, cập nhật bằng OTA.
- IO9 lúc khởi động phải ở mức cao (kéo lên 10 kΩ trên mạch): không chập IO9 khi cắm điện hay bấm RESET, trừ khi muốn vào chế độ nạp.
- **Chưa thử trên bo.** Chân lấy từ README và sơ đồ của mạch; build kiểm chứng được cấu hình, chưa kiểm chứng được mạch thật.

## Bo ESP32-H2 mini (`h2-mini`, Matter over Thread)

Bo **ESP32-H2 mini** (ESP32-H2 SuperMini, flash 4 MB) của bản ESP32-H2 trước đây (`ESP32-H2/matter-presence-ld2410-learning`): cùng chân, cùng PID `0x8000`, cùng cấu hình Thread, cùng bảng phân vùng và khoá lưu trữ, nên **OTA từ bản H2 cũ lên bản này giữ ghép nối** (mục **Nâng cấp từ các bản trước**; mới kiểm chứng bằng so sánh cấu hình và build, chưa thử trên bo).

![Sơ đồ đấu dây: ESP32-H2 SuperMini và radar HLK-LD2410C (VCC 5V, GND, TX → GPIO11, RX ← GPIO12), LED RGB GPIO8 và nút BOOT GPIO9 có sẵn trên board](docs/h2-mini-wiring.png)

| LD2410C | ESP32-H2 mini |
|---|---|
| TX | **GPIO11** (UART1 RX) |
| RX | **GPIO12** (UART1 TX) |
| VCC | **5V** |
| GND | GND |

- Trên SuperMini, 5V, GND, GPIO12 và GPIO11 nằm cùng một hàng chân bên phải. Chân 8 và 9 đã nối sẵn LED RGB WS2812 và nút BOOT trên bo: không nối gì thêm vào.
- Tránh các chân strapping (GPIO2, GPIO3, GPIO8, GPIO9, GPIO25), chân flash GPIO15–GPIO21 (build báo lỗi), và UART0 GPIO23/24 (log, console).
- **Log và console:** log ra UART0 (GPIO23/24) và cổng USB-C (USB-Serial-JTAG), như bản H2 cũ. Console Matter (`matter esp ...`) và lệnh OpenThread (`matter esp ot_cli state`, `ipaddr`, `srp client service`...) gõ qua UART0, 115200 baud, bằng mạch USB-UART nối GPIO23/24; cổng USB-C chỉ xuất log, không nhận lệnh (theo README bản H2 cũ).
- **Mạng:** Thread, cần Thread Border Router (mục **Yêu cầu mạng**); thiết bị là Full Thread Device, làm router được để mở rộng mạng Thread.

**Giống mã chung (như ba bo C3):** radar quyết định có người; học phòng trống 120 s từ mức cao nhất (mục **Học phòng trống**, thay cho cách radar tự đo 60 s của bản H2 cũ); Hold time mặc định 30 s, bo đang chạy giữ Hold time đã lưu (cùng khoá `app/hold-init`); Range, Radar resolution, Radar Bluetooth, *Factory thresholds* từ Home Assistant; tên thiết bị, số serial, Model, phiên bản `1.0.<số commit>`.

**Nút và đèn giữ như bản H2 cũ** (`PRESENCE_BUTTON_LED_H2`, bật sẵn cho bo ESP32-H2):

| | Bo ESP32-C3 | `h2-mini` (như bản H2 cũ) |
|---|---|---|
| Bấm 4 lần | ghi ngưỡng xuất xưởng | **không làm gì** (log `4 clicks, ignored`) |
| Đèn sau khi ghi ngưỡng xuất xưởng | tím nháy nhanh 10 s | **xanh lá 10 s**, như học xong |

Các thao tác và màu còn lại giống hệt nhau: bấm đúp mở ghép nối, bấm 3 lần học phòng trống, giữ 5 s factory reset (xoá mạng Thread và ghép nối Matter); xanh dương nháy chờ ghép nối, trắng nháy *Identify*, cyan khi học (nháy chậm trong 120 s ghi), xanh lá học xong, đỏ thất bại.

**Khác bản H2 cũ** (ngoài cách học): mã viết lại của bản C3 (log tag `presence`, `radar_ctl`, ... đổi chỗ), tên bo trong log là `ESP32-H2 mini`, cảnh báo heap thấp chỉ in một lần cho mỗi mức thấp mới, `Learn empty room` từ chối mode ngoài 0/1/2. Endpoint, cluster, entity và khoá NVS không đổi.

## Học phòng trống

Trong phòng không có ai, radar vẫn nhận "năng lượng" từ quạt, rèm bay, cây cảnh, phản xạ đồ vật... Ở gate nào năng lượng nền vượt ngưỡng, radar báo nhầm có người. Học phòng trống ghi năng lượng nền rồi đặt ngưỡng từng gate trên mức đó. Có người hay không vẫn do **radar** quyết định từ ngưỡng của nó.

Mọi bo mặc định học **từ mức cao nhất** (`LD2410_LEARN_PEAK`, bật trong `sdkconfig.defaults`): ESP32 ghi năng lượng phòng trống và đặt ngưỡng cho radar; không dùng lệnh tự đo 0x000B của radar (lệnh này chỉ được trả lời bằng ACK, không gửi số đo về, và có radar LD2410C không đổi ngưỡng sau khi đo).

**Cách bắt đầu:** chọn **Learning** trên Home Assistant hoặc bấm nút 3 lần (không cần ghép nối hay mạng), rồi **rời khỏi phòng** trong 20 s. Radar nhìn xa khoảng 6 m và xuyên cửa gỗ, vách mỏng. Quạt thường chạy thì để chạy: đó là nhiễu cần học.

| Thời điểm | Việc | LED |
|---|---|---|
| 0–20 s | rời khỏi phòng | sáng (cyan với WS2812) |
| 20–140 s | ESP32 ghi năng lượng 18 kênh (9 vùng × chuyển động / tĩnh, engineering mode, khoảng 1 200 báo cáo) | nháy chậm |
| khoảng 140 s | tính ngưỡng, ghi vào radar **một lần** (0x0064 cho 9 vùng), đọc lại (0x0061), tắt engineering mode | sáng 10 s (xanh lá; đỏ: thất bại) |

**Ngưỡng của mỗi kênh**, từ 120 s phòng trống:
- **avg**: trung bình năng lượng của mọi báo cáo; **peak**: báo cáo cao nhất (số "Maximum" của HLKRadarTool);
- **ngưỡng = peak + biên**, biên là số lớn hơn giữa **8** và **peak − avg**: kênh dao động mạnh có biên rộng hơn;
- **không thấp hơn nhiễu + 4**; nhiễu là phân vị 99, hoặc trung vị + 3 × độ dao động (1,4826 × MAD) nếu lớn hơn;
- **không thấp hơn 75 % ngưỡng xuất xưởng** (làm tròn lên): phòng rất yên thì ngưỡng không xuống sát nhiễu;
- **không cao hơn ngưỡng xuất xưởng + 30** (và 99): kênh có người hay vật chuyển động lúc học không bị "điếc"; log cảnh báo `threshold limited to ...`;
- sàn và trần tính từ ngưỡng **xuất xưởng**, không từ ngưỡng radar đang giữ: học lại cùng phòng cho cùng ngưỡng;
- năng lượng tĩnh vùng 0–1 không chỉnh được, giữ nguyên; kênh không có báo cáo giữ nguyên;
- **vì sao dùng peak:** radar so từng báo cáo với ngưỡng (theo tài liệu, không lọc). Ngưỡng đặt dưới một xung đơn lẻ đã thấy lúc phòng trống thì radar sẽ báo nhầm vì xung đó; ngưỡng = peak + biên luôn ở trên mọi báo cáo của phòng trống.

**Ví dụ với số trong ảnh HLKRadarTool của bạn** (radar giữ ngưỡng xuất xưởng; có trong host test):

| Vùng | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| Chuyển động peak / avg | 26 / 13 | 17 / 7 | 8 / 2 | 9 / 4 | 6 / 2 | 7 / 4 | 4 / 2 | 7 / 3 | 5 / 3 |
| peak + biên | 39 | 27 | 16 | 17 | 14 | 15 | 12 | 15 | 13 |
| sàn 75 % | 38 | 38 | 30 | 23 | 15 | 12 | 12 | 12 | 12 |
| **Ngưỡng chuyển động** (xuất xưởng) | **39** (50) | **38** (50) | **30** (40) | **23** (30) | **15** (20) | **15** (15) | **12** (15) | **15** (15) | **13** (15) |
| Tĩnh peak / avg | – | – | 12 / 3 | 12 / 4 | 6 / 3 | 4 / 2 | 3 / 2 | 3 / 2 | 3 / 2 |
| peak + biên | | | 21 | 20 | 14 | 12 | 11 | 11 | 11 |
| sàn 75 % | | | 30 | 30 | 23 | 23 | 15 | 15 | 15 |
| **Ngưỡng tĩnh** (xuất xưởng) | giữ | giữ | **30** (40) | **30** (40) | **23** (30) | **23** (30) | **15** (20) | **15** (20) | **15** (20) |

Phòng này yên nên phần lớn ngưỡng dừng ở sàn 75 %: radar nhạy hơn ngưỡng xuất xưởng, nhưng mọi ngưỡng vẫn cách báo cáo cao nhất của phòng trống ít nhất 8.

**Log** (host test, phòng giả lập, không phải radar thật: vùng 3 có quạt, vùng 6 có xung đơn lẻ tới 15):

```
I quiet_learn: Empty room learning: leave the room, recording starts in 20 s and lasts 120 s
I quiet_learn: Recording the empty room for 120 s
I quiet_learn: 1200 reports. Threshold = highest + the larger of 8 and (highest - average), at least the noise + 4, from 75 % of the factory default to factory + 30
I quiet_learn: Gate 0 (0 m to 0.75 m): moving avg 3 peak 3 noise 6 -> 38 floor (was 50) | still not settable
I quiet_learn: Gate 3 (2.25 m to 3 m): moving avg 18 peak 18 noise 21 -> 26 (was 30) | still avg 75 peak 75 noise 78 -> 70 limit (was 40)
I quiet_learn: Gate 6 (4.5 m to 5.25 m): moving avg 2 peak 15 noise 15 -> 28 (was 15) | still avg 3 peak 3 noise 6 -> 15 floor (was 20)
W quiet_learn: Gate 3 (2.25 m to 3 m): still highest 75, average 75: threshold limited to 70 (factory 40 + 30). Something kept moving there while recording (person, fan, curtain) and may be reported as presence: learn again without it
I quiet_learn: Learning done: new thresholds saved in the LD2410 (1200 reports)
```
`floor`: nâng lên sàn 75 %; `noise`: nâng lên nhiễu + 4; `limit`: chặn ở xuất xưởng + 30. Radar đã giữ đúng các ngưỡng đó thì `The radar holds these thresholds already, nothing written` (flash của radar không bị ghi).

**Thất bại** (LED đỏ, không ghi gì): ít hơn 5 báo cáo có năng lượng mỗi giây (dưới 600 trong 120 s), hoặc radar không trả lời lệnh. Radar không giữ ngưỡng vừa ghi (`Gate N reads back ...`): radar có thể giữ lẫn ngưỡng cũ và mới; học lại hoặc chọn *Factory thresholds*. Chọn Off trong lúc học thì huỷ, không ghi gì; chọn Factory thresholds thì huỷ học rồi ghi ngưỡng xuất xưởng.

**Tuỳ chọn** (`menuconfig` → *Empty Room Learning*):

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `LD2410_LEARN_PEAK` | bật (`sdkconfig.defaults`; Kconfig mặc định tắt) | Học theo cách này (thay cho radar tự đo và cách phân vị 95 %) |
| `LD2410_LEARN_DELAY_SEC` / `_DURATION_SEC` | 20 / 120 (`sdkconfig.defaults`; Kconfig mặc định 60) | Thời gian chờ rời phòng / thời gian ghi |
| `LD2410_LEARN_FLOOR_PERCENT` | 75 | Ngưỡng không thấp hơn chừng này % ngưỡng xuất xưởng (100: không bao giờ thấp hơn xuất xưởng) |
| `LD2410_LEARN_MIN_MARGIN` | 8 | Biên nhỏ nhất trên peak |
| `LD2410_LEARN_MAX_RAISE` | 30 | Trần: ngưỡng xuất xưởng + chừng này (và 99) |

### Cách cũ (tắt `LD2410_LEARN_PEAK`)

Tắt `LD2410_LEARN_PEAK` thì firmware học như bản 1.0.132 (ghi 60 s nếu đặt lại `LD2410_LEARN_DURATION_SEC`), theo một trong hai cách:

**Cách A, radar tự đo (`LD2410_LEARN_RADAR_AUTO=y`, mặc định khi tắt `LD2410_LEARN_PEAK`):** sau 10 s đầu, firmware đọc phiên bản firmware radar (`LD2410 firmware V2.44.25070917`) và ngưỡng hiện tại, rồi gửi lệnh **0x000B** (giao thức LD2410C V1.09 mục 2.2.20) với thời gian đo bằng thời gian ghi. Radar tự chờ 10 s, đo 60 s rồi tự lưu ngưỡng chuyển động và đứng yên của mọi gate; firmware chỉ theo dõi trạng thái trong báo cáo (4 đang đo, 5 xong, 6 thất bại):

```
I quiet_learn: The LD2410 measures the empty room itself: 10 s more to leave, then 60 s
I quiet_learn: Thresholds before: moving 50,50,40,30,20,15,15,15,15 | still -,-,40,40,30,30,20,20,20
I quiet_learn: Thresholds set by the radar: moving 45,40,30,25,18,12,10,10,10 | still -,-,35,30,25,22,15,15,15
I quiet_learn: Learning done: thresholds set by the LD2410 background noise detection
```
(số minh hoạ từ test trên máy tính, không phải từ radar thật)

- Hi-Link không ghi radar tính ngưỡng thế nào. Ngưỡng radar đặt **không bị giới hạn** bởi mức 95 %, +10 hay +30; gate cao hơn mặc định + 30 có cảnh báo `W quiet_learn`. Thấy báo nhầm nhiều hơn thì chọn *Factory thresholds*, hoặc tắt tuỳ chọn để ESP32 tự học.
- Radar nhận lệnh nhưng không đo (không báo trạng thái 4 trong 15 s), báo trạng thái 6, hoặc chưa xong sau 10 + 60 + 30 s: thất bại (trường hợp cuối thì firmware khởi động lại radar).
- Huỷ trong lúc radar đo (chọn Off hoặc Factory thresholds): firmware khởi động lại radar và chờ 1 s cho radar sẵn sàng. Hi-Link không ghi radar giữ ngưỡng cũ hay đã đổi một phần (chưa kiểm chứng): nếu nghi ngờ, chọn *Factory thresholds* hoặc học lại.
- **Radar từ chối lệnh 0x000B** (firmware cũ) nhưng trả lời lệnh khác: log `This LD2410 firmware has no background noise detection: learning on the ESP32`, chuyển sang cách B, ghi bắt đầu đúng lúc hết 20 s. Radar không trả lời gì: thất bại, log nhắc kiểm tra dây **ESP TX → chân RX của radar** (GPIO4; IO3 trên `c3-radar-r45`).

**Cách B, ESP32 tự học:** firmware bật *engineering mode* (năng lượng từng gate trong mỗi báo cáo, khoảng 600 báo cáo trong 60 s), ghi bảng đếm năng lượng (3,6 KB, chỉ cấp phát trong lúc ghi), rồi:
- **mức nhiễu** mỗi gate = năng lượng mà **95 %** số báo cáo không vượt quá (hạng ceil(frames × 95 / 100)). Đợt nhiễu ngắn, tổng dưới khoảng 3 s (người đi ngoài hành lang, cửa đóng mở), bị bỏ qua; nguồn nhiễu chuyển động hơn 5 % thời gian (quạt, rèm, máy lạnh) vẫn được học;
- **ngưỡng mới = mức nhiễu + 10**, không thấp hơn **mặc định nhà sản xuất** (chuyển động `50,50,40,30,20,15,15,15,15`, đứng yên `–,–,40,40,30,30,20,20,20`: học chỉ làm radar bớt nhạy ở gate có nhiễu), không cao hơn **mặc định + 30** (một gate không bị "điếc" vì có người đi qua lúc ghi) và tối đa 100. Ngưỡng đứng yên của gate 0, 1 không chỉnh được, giữ nguyên;
- ghi cả 9 gate (0x0064), đọc lại (0x0061) và so sánh. Không khớp thì thất bại; khi đó radar có thể giữ lẫn ngưỡng cũ và mới (log nói rõ): học lại hoặc chọn *Factory thresholds*.

```
I quiet_learn: 600 reports. Noise = energy of 95 % of them, threshold = noise + 10, from the factory default to factory + 30
I quiet_learn: Gate (0.75 m each): moving noise (highest) -> threshold (was) | still noise (highest) -> threshold (was)
I quiet_learn: Gate 3:   5 (  5) ->  30 ( 30) |  75 ( 75) ->  70 ( 40)
I quiet_learn: Gate 5:  15 ( 40) ->  25 ( 15) |   5 (  5) ->  30 ( 30)
W quiet_learn: Gate 3 (2.25 m to 3 m): still noise 75, threshold limited to 70 (factory 40 + 30). Something kept moving there while recording ...
I quiet_learn: Learning done: new thresholds saved in the LD2410 (600 reports)
```
(log của test trên máy tính: gate 3 có quạt, gate 5 có người đi ngang 2 s và bị bỏ qua)

Ít hơn 2 báo cáo mỗi giây (`Only N engineering reports in 60 s`): thất bại, không ghi gì.

### Factory thresholds và lựa chọn cuối cùng thắng (mọi cách học)

**Factory thresholds** (chọn trong Home Assistant hoặc bấm 4 lần): ghi lại ngưỡng mặc định nhà sản xuất (ngưỡng đứng yên của gate 0, 1 giữ nguyên) rồi đọc lại. Range, độ phân giải, Bluetooth, *no-one duration* giữ nguyên, radar không khởi động lại. Log ghi cả ngưỡng cũ (`Thresholds until now: ...`) để chép lại nếu cần; radar đã giữ ngưỡng mặc định thì không ghi gì (vẫn tính là thành công). Trong lúc ghi, entity hiện `Factory thresholds`, xong trở về `Off`.

**Lựa chọn cuối cùng thắng:**
- Learning rồi chọn Off: huỷ, không ghi gì, LED trở lại bình thường ngay.
- Factory thresholds (hoặc bấm 4 lần) khi đang học: huỷ học, rồi ghi ngưỡng mặc định.
- Learning hoặc Off (hoặc bấm 3 lần) khi Factory thresholds chưa bắt đầu: bỏ Factory thresholds.
- Bấm 3 lần khi đang học: bỏ qua (log `Empty room learning already requested or running`).
- Task radar không chạy được: bấm nút thì LED báo thất bại; chọn từ Home Assistant thì bị từ chối.

Firmware **không bao giờ tự ghi ngưỡng** vào radar khi không ai yêu cầu. Thời gian chờ, thời gian ghi và các mức của từng cách chỉnh trong `menuconfig` → *Empty Room Learning*.

### Nâng cấp từ các bản trước

Mọi bản dưới đây cùng VID `0xFFF1` và PID của chip (`0x8001` ESP32-C3, `0x8000` ESP32-H2), endpoint 1–4, bảng phân vùng và khoá lưu trữ, nên OTA lên bản này giữ ghép nối, Hold time, lựa chọn Radar Bluetooth và các entity (chưa thử trên bo). Chọn file của đúng bo (mục **Cập nhật qua mạng**).

| Đang chạy | Bo, file | Khác biệt sau khi cập nhật |
|---|---|---|
| `matter-presence-ld2410` bản mặc định (1.0.134 trở lên) | SuperMini: `c3-supermini` | chỉ khác cách học phòng trống (lần học sau: ESP32 ghi 120 s, ngưỡng từ mức cao nhất). Ngưỡng đang có trong radar được giữ và dùng tiếp |
| `matter-presence-ld2410` biến thể `wroom_n4` (1.0.149–1.0.150) | WROOM: `c3-wroom-devkit` | cách học như trên; **không còn entity nhiệt độ chip** và *Identify (5)*: chúng chuyển sang không khả dụng, xoá trong Home Assistant |
| biến thể `esplearn` (1.0.175) | WROOM: `c3-wroom-devkit` | không khác gì (cùng mã và cấu hình; chỉ khác tên file, số phiên bản) |
| biến thể `esppresence` (1.0.153–1.0.172) | WROOM: `c3-wroom-devkit` | như từ `esplearn` lên; thêm các điểm dưới đây |
| `matter-wifi-presence` (1.0.132) | SuperMini: `c3-supermini` | như bản mặc định ở trên, cộng các khác biệt ở đầu README |
| bản ESP32-H2 `matter-presence-ld2410-learning` (`matter_presence_h2_learn.ota`, PID `0x8000`) | H2 mini: `h2-mini` | cách học phòng trống (ESP32 ghi 120 s, thay cho radar tự đo 60 s); ngưỡng đang có trong radar được giữ và dùng tiếp. Nút, đèn, Thread, endpoint, entity giữ nguyên (mục **Bo ESP32-H2 mini**) |

Từ `esppresence` (bộ xử lý hiện diện trên ESP32, đã bỏ từ 1.0.175):
- **có người / không** lại do radar quyết định: thời gian về trống = *no-one duration* của radar + Hold time;
- **Hold time** giữ giá trị đang có trên bo (khoá `app/hold-init2` của bản cũ được tính là đã đặt); mặc định 30 s chỉ áp dụng sau factory reset;
- **ngưỡng đang có trong radar** được dùng ngay. Nên **học lại một lần**: các bản 1.0.169–1.0.172 đặt ngưỡng theo mức cao nhất đã lọc trung vị (bộ xử lý tự lọc), có thể thấp hơn xung đơn lẻ mà radar sẽ thấy;
- không còn console (`LEARN_EMPTY`, `STATUS`, `DIAG`, `THRESHOLDS`) và không còn khôi phục radar khi im lặng; số đo nhiễu trong NVS (khoá `noise`) để nguyên, không dùng;
- từ 1.0.169 trở về trước lên thẳng bản này thì entity nhiệt độ chip và *Identify (5)* chuyển sang không khả dụng: xoá trong Home Assistant.

**Quay lại bản cũ:** OTA không xuống được số phiên bản nhỏ hơn. Nạp qua USB (hoặc pad nạp) file app của bản cũ ở `0x20000` cùng `ota_data_initial` ở `0x1d000`: giữ ghép nối. File merged xoá ghép nối.

## Hold time, Radar Bluetooth, Radar resolution và Range

Trên trang thiết bị trong Home Assistant (mục *Cấu hình*), các entity xếp theo chữ cái: **Hold time**, **Learn empty room**, **Radar Bluetooth**, **Radar resolution**, **Range**. Tên được chọn để ra đúng thứ tự này, nên không đổi. Mỗi điều khiển radar là một cluster Matter *Mode Select*; Apple Home không hỗ trợ cluster này (theo tài liệu, chưa thử trên thiết bị Apple).

| Entity | Endpoint | Ý nghĩa | Lựa chọn |
|---|---|---|---|
| **Hold time** | 1 | Thời gian giữ "có người" sau khi radar hết thấy người (Matter HoldTime, lưu trong ESP32) | 1–300 s, mặc định 30 s |
| **Learn empty room** | 1 | Học phòng trống | `Off` (0), `Learning` (1), `Factory thresholds` (2) |
| **Range** | 2 | Gate xa nhất radar còn xét, đặt cùng lúc cho mục tiêu chuyển động và đứng yên | mode = số gate 2–8: `1.5 m` … `6 m` (0,75 m mỗi gate, mặc định 6 m); ở độ phân giải 0,2 m là `0.4 m` … `1.6 m` |
| **Radar resolution** | 3 | Độ dài mỗi gate | `0.75 m` (0, mặc định), `0.2 m` (1, chỉ LD2410B/C) |
| **Radar Bluetooth** | 4 | Bluetooth của LD2410B/C (app HLKRadarTool) | `Off` (0, mặc định), `On` (1); radar không có Bluetooth: chỉ `Not available` (100) |

- **Thời gian tới khi báo không có người ≈ *no-one duration* của radar + Hold time.** Firmware không đổi *no-one duration* (mặc định nhà sản xuất 5 s; xem trong log `radar_ctl: LD2410 settings: no-one duration ...`). Muốn báo nhanh thì giảm Hold time; hay bị báo "không có người" khi ngồi yên thì tăng Hold time.
- Range, Radar resolution và Radar Bluetooth được ghi vào **bộ nhớ của LD2410** (giữ khi mất điện) rồi đọc lại; Home Assistant luôn hiện giá trị radar thực sự giữ. Firmware không ghi đè giá trị radar đang giữ khi không ai yêu cầu.
- Radar giữ gate xa nhất khác nhau cho chuyển động và đứng yên (đặt bằng HLKRadarTool): Range có thêm đúng giá trị đó, ví dụ `Moving 4.5 m, still 3 m` (mode 100); chọn nó không làm gì.
- Đổi **Radar resolution** hoặc **Radar Bluetooth**: firmware ghi vào radar rồi **khởi động lại radar** một lần (mất tín hiệu khoảng 1–3 s) và đọc lại. Nhãn của Range đổi theo độ phân giải. Ngưỡng đã học là cho độ dài gate cũ: **học lại phòng trống** sau khi đổi độ phân giải. Radar không trả lời lệnh độ phân giải (LD2410 đời đầu, firmware cũ) thì chỉ có `0.75 m`.
- Radar chưa đọc được (chưa nối dây ESP TX → chân RX của radar: GPIO4, IO3 trên `c3-radar-r45`): thay đổi trong Home Assistant bị từ chối, entity giữ giá trị cũ; firmware đọc lại radar sau 15 s, 30 s, 1 min... tối đa 5 min một lần.
- Yêu cầu radar không làm được (đổi độ phân giải trên radar cố định 0,75 m, đổi Bluetooth trên radar không có Bluetooth): log `Changing the radar settings failed`, Home Assistant hiện lại giá trị thật.
- Đang học phòng trống: thay đổi Range, độ phân giải, Bluetooth được áp dụng ngay sau khi học xong.

**Radar Bluetooth:** radar xuất xưởng bật Bluetooth (mật khẩu `HiLink`). Firmware **tắt Bluetooth của radar** cho tới khi chọn `On`. Lựa chọn lưu trong NVS của ESP32 (namespace `ld2410`, key `bt_on`): giữ qua khởi động lại, OTA và factory reset bằng nút; mất khi `erase-flash` hoặc nạp file merged. Mỗi lần khởi động, nếu radar khác lựa chọn đã lưu (ví dụ thay radar mới) thì firmware đặt lại theo lựa chọn đó. Đổi không thành công thì không thử lại cho tới lần khởi động sau hoặc lần chọn kế tiếp, để radar không bị khởi động lại liên tục. Firmware **không đổi mật khẩu Bluetooth** của radar (radar từng chạy bản 1.0.111–1.0.115 có thể vẫn giữ mật khẩu `solana`; key NVS cũ `bt_password` được để nguyên). Trạng thái Bluetooth đọc qua lệnh hỏi MAC (0x00A5): khi tắt, radar trả địa chỉ giả `08:05:04:03:02:01` (cách ESPHome xử lý; trên radar thật mới kiểm chứng chiều bật). Bật Bluetooth thì log có MAC để biết radar nào gắn với bo nào: `New LD2410 settings: ... Bluetooth on, MAC 8F:27:2E:B8:0F:65`.

Với chip-tool:

```bash
chip-tool occupancysensing write hold-time 60 0x7283 1
chip-tool modeselect read supported-modes 0x7283 2
chip-tool modeselect change-to-mode 6 0x7283 2    # Range gate 6 = 4.5 m
chip-tool modeselect change-to-mode 1 0x7283 3    # Radar resolution 0.2 m
chip-tool modeselect change-to-mode 0 0x7283 4    # Radar Bluetooth Off
chip-tool modeselect change-to-mode 1 0x7283 1    # Learn empty room: Learning
```

## Entity chẩn đoán: Reboot count, Uptime, Wi-Fi RSSI (gửi lại: ESP32-C3)

Home Assistant tự tạo ba entity này (mục *Diagnostic* trên trang thiết bị) cho **mọi** thiết bị Matter có các cluster chẩn đoán chuẩn, nhưng **tắt sẵn**: integration Matter của Home Assistant khai báo chúng với `entity_registry_enabled_default=False`. Thiết bị không bật thay được, phải bật trong Home Assistant:

- **Một thiết bị:** trang thiết bị → mục *Diagnostic* → *+N entities not shown* → bấm vào entity → bánh răng (*Settings*) → bật **Enabled** → **Update**. Khoảng 30 s sau Home Assistant tải lại integration Matter, lúc đó entity mới có giá trị. Mỗi lần tải lại, **mọi** entity Matter (của mọi thiết bị) hiện *Unavailable* khoảng 1 s trong lịch sử rồi trở lại trạng thái cũ: không phải lỗi thiết bị.
- **Nhiều thiết bị:** *Settings* → *Devices & services* → tab *Entities* → lọc để hiện cả entity bị tắt (*disabled*) → tìm `RSSI`, `Uptime`, `Reboot count` → chọn nhiều → **Enable selected**.

| Entity | Thuộc tính Matter (endpoint 0) | Ý nghĩa, cập nhật |
|---|---|---|
| **Reboot count** | General Diagnostics `RebootCount` | Số lần khởi động. Matter SDK tăng ở mỗi lần khởi động (OTA, lỗi, mất điện), lưu trong NVS (namespace `chip-counters`): giữ qua OTA, đếm lại từ 1 sau factory reset, `erase-flash` hoặc nạp file merged. Chỉ đổi lúc khởi động, khi đó Home Assistant subscribe lại và nhận số mới |
| **Uptime** | General Diagnostics `UpTime` | Home Assistant hiện **thời điểm khởi động** (lúc nhận − uptime). Firmware gửi lại uptime 10 phút một lần |
| **Wi-Fi RSSI** | Wi-Fi Network Diagnostics `RSSI` | Cường độ sóng Wi-Fi nhận từ router (dBm; từ −70 trở lên là tốt, dưới −80 là yếu). Firmware đọc mỗi phút, gửi khi đổi từ 3 dB so với lần gửi trước, và 10 phút một lần |

**Vì sao firmware phải gửi lại:** trong đặc tả Matter, `RSSI` và `UpTime` có thuộc tính *changes omitted*. Matter SDK chỉ gửi chúng khi controller bắt đầu subscription, sau đó không báo khi giá trị đổi. Theo mã nguồn, Matter Server của Home Assistant cũng không tự đọc lại hai thuộc tính này. Điều đó đúng cho cả python-matter-server lẫn bản mới viết bằng matter.js (gói `matter-server` 1.4.0); hai bản này chỉ hỏi lại thuộc tính riêng của một số hãng, và bản matter.js thêm thông tin Thread. Với bản 1.0.132 (và mọi firmware chỉ dùng mặc định của SDK):
- Wi-Fi RSSI đứng nguyên ở giá trị lúc Home Assistant subscribe.
- Uptime được tính từ uptime cũ: sau khi Home Assistant khởi động lại, thời điểm khởi động bị dời muộn đi đúng bằng khoảng thời gian từ lúc subscribe. Subscription thường bắt đầu ngay sau khi bo khởi động, nên trông như bo vừa khởi động lại.

Bản này đánh dấu hai thuộc tính là đã đổi (`MatterReportingAttributeChangeCallback`, `main/diag_report.cpp`), nên các subscription gửi giá trị hiện tại trong lần báo cáo kế tiếp. Khi không có controller nào subscribe thì không gửi gì.

- Home Assistant bản mới bỏ qua lệch Uptime dưới 60 s, nên gửi lại 10 phút một lần không làm entity Uptime đổi trạng thái liên tục.
- Đổi trong `menuconfig` → *Wi-Fi RSSI and Uptime Reports*:
  - `DIAG_REPORT_RSSI_STEP_DB`: mặc định 3 dB; 0 = chỉ gửi theo chu kỳ.
  - `DIAG_REPORT_REFRESH_MIN`: mặc định 10 phút; 0 = không gửi theo chu kỳ.
  - Cả hai bằng 0 thì như bản 1.0.132.
- Không thêm hay đổi endpoint, cluster, thuộc tính nào: ba entity đã có từ 1.0.132 (cùng cluster của Matter SDK), bản này chỉ gửi thêm báo cáo.
- *Data version* của cluster không tăng khi gửi lại, vì cluster của Matter SDK không cho ứng dụng tăng. Theo mã nguồn, cả hai bản Matter Server vẫn nhận giá trị mới: python-matter-server so sánh theo giá trị, bản matter.js ghi mọi giá trị nhận được bất kể *data version* (chưa thử trên bo).

## Nhiệt độ chip ESP32-C3 (endpoint 5, tuỳ chọn)

**Mặc định tắt từ 1.0.147** (bật ở 1.0.143–1.0.146 để tìm nguyên nhân bo nóng; người dùng bỏ đi khi đã đo xong). Khi bật, firmware đọc cảm biến nhiệt bên trong chip ESP32-C3 mỗi phút và báo lên **endpoint 5** (device type *Temperature Sensor*, cluster *Temperature Measurement*). Giá trị chỉ được gửi khi lệch từ 0,5 °C so với lần gửi trước.
- Home Assistant hiện thành một entity nhiệt độ của thiết bị, tên mặc định *Temperature*. Nên đổi tên thành "Nhiệt độ chip" để không nhầm với nhiệt độ phòng. Apple Home hiện thành một cảm biến nhiệt độ.
- Đây là **nhiệt độ lõi chip**, luôn cao hơn nhiệt độ phòng: chip, mạch hạ áp 5 V → 3,3 V của SuperMini và radar đều toả nhiệt. Số đo dùng để xem bo có nóng dần lên hay quá nóng không (ví dụ khi đặt trong hộp kín), không dùng làm cảm biến nhiệt độ phòng.
- Driver ESP-IDF tự đổi dải đo khi nhiệt độ ra khỏi dải đang dùng. Theo bảng của ESP-IDF, sai số dưới 1 °C ở dải −10…80 °C, dưới 2 °C ở 20…100 °C, dưới 3 °C ở 50…125 °C. Bảng này áp dụng cho chip có hiệu chuẩn trong eFuse. Nếu log có `W temperature_sensor: No calibration param in eFuse` thì chip không có hiệu chuẩn, số đo có thể lệch nhiều hơn. Giá trị báo được giới hạn trong −40…125 °C.
- LD2410 không có lệnh nào đọc nhiệt độ của nó, nên firmware không báo được nhiệt độ radar.
- Bật bằng `menuconfig` → *Presence Sensor Configuration* → *Chip temperature in Home Assistant*, kèm `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=6` (thiếu thì build báo lỗi). Tuỳ chọn này tự bật lại cluster Temperature Measurement; `sdkconfig` sinh ra khi đó giống hệt 1.0.144.
- Khi tắt, endpoint 5 không được tạo: không có entity nhiệt độ và không có nút *Identify (5)* (Identify bắt buộc theo device type Temperature Sensor). Bản mặc định nhỏ hơn và dùng ít RAM hơn chút (bảng **RAM và flash**), nhưng gần như không đổi nhiệt độ: cảm biến chỉ được đọc mỗi phút, và RAM dùng nhiều hay ít không làm chip nóng thêm.
- Cập nhật OTA từ 1.0.143–1.0.146 lên bản mặc định: endpoint 5 biến mất. Nếu Home Assistant còn giữ entity nhiệt độ và *Identify (5)* ở trạng thái không khả dụng, xoá chúng trên trang thiết bị (mở entity → *Settings* → *Delete*). Chưa thử trên bo.
- Endpoint 5 được tạo sau các endpoint 1–4, nên số của chúng không đổi. Nếu không tạo được các điều khiển radar (endpoint 2–4) thì endpoint 5 cũng không được tạo, để nó không bị đổi số.

### Chip quá nóng: 80 °C ngoài hộp, 96 °C trong hộp (1.0.143)

Người dùng đo bằng entity này trên bo chạy 1.0.143: **80 °C** khi để bo ngoài hộp, **96 °C** khi đặt trong hộp. Mức này quá nóng cho bo SuperMini.

**Đã kiểm chứng bằng tài liệu và mã nguồn (không đo trên bo):**
- Số đo là nhiệt độ lõi chip. Ở 80–100 °C, sai số dưới 2 °C nếu chip có hiệu chuẩn (xem trên). Nếu số đo đúng thì vỏ chip cũng rất nóng, chạm tay không giữ được lâu; có thể kiểm tra bằng nhiệt kế hồng ngoại.
- Bản 1.0.132–1.0.143 tắt modem sleep sau khi ghép nối, nên radio Wi-Fi nghe liên tục (bản 1.0.122–1.0.131 còn dùng modem sleep). Theo datasheet ESP32-C3, radio đang thu tốn khoảng 84 mA. Ở modem sleep, radio chỉ thức lúc có beacon, trung bình cả chip còn khoảng 20–30 mA (ước tính). Dòng này đi qua mạch hạ áp 5 V → 3,3 V trên bo 18 × 22 mm. Ước tính bo toả khoảng 0,45 W khi radio luôn nghe, và khoảng 0,15 W ở modem sleep. Radar LD2410C toả thêm khoảng 0,4 W (5 V × khoảng 80 mA).
- ESP32-C3FN4 chịu nhiệt độ môi trường tối đa 85 °C, ESP32-C3FH4 tối đa 105 °C. Đây là nhiệt độ không khí quanh chip; lõi chip còn nóng hơn thế.

**Giả thuyết (chưa kiểm chứng):** chip nóng làm Wi-Fi yếu đi. Độ nhạy thu và công suất phát giảm khi nóng, và thạch anh 40 MHz nằm sát chip có thể lệch tần số. Bo mất beacon (sự kiện `disconnection` với `reasonCode 200`) và Home Assistant báo *Unavailable*. Giả thuyết khớp với những gì đã biết:
- bản Thread (ESP32-H2, không có Wi-Fi, tốn ít điện) chạy ổn định nhiều ngày;
- ESPHome và Tasmota ở cùng chỗ không mất kết nối (chưa rõ chạy trên bo nào), mà ESPHome mặc định dùng modem sleep trên ESP32.

**Bản 1.0.144 để Wi-Fi ở modem sleep** (`PRESENCE_WIFI_STAY_AWAKE` mặc định tắt). Người dùng đo với 1.0.144: **63 °C khi để bo ngoài hộp** (1.0.143: 80 °C), tức giảm 17 °C; trong hộp chưa đo. Ước tính từ hai lần đo này và các số dòng điện ở trên (chưa kiểm chứng): kể cả khi chip không tự toả nhiệt, chỗ đặt chip vẫn khoảng 50–55 °C. Nghĩa là phần nóng còn lại chủ yếu đến từ xung quanh, có thể là radar đặt sát, chứ không phải từ chip. Cách kiểm chứng:
1. So sánh entity nhiệt độ trước và sau khi cập nhật, cùng chỗ đặt, cả ngoài hộp và trong hộp.
2. So sánh lịch sử nhiệt độ với các lần *Unavailable* trong vài ngày.
3. So sánh với bo còn lại. Nếu một bo nóng hơn hẳn bo kia dù cùng firmware và cùng điều kiện, bo đó có thể bị lỗi (mạch hạ áp, mối hàn).

Từ 1.0.147 entity nhiệt độ mặc định tắt. Muốn đo tiếp (ví dụ trong hộp, hay vào mùa nóng), build với `PRESENCE_CHIP_TEMPERATURE`.

**Phần cứng (giảm nhiệt thêm):**
- Khoan lỗ thông gió ở đáy và đỉnh hộp cho không khí lưu thông. Trong nhà tắm, tránh mặt hướng về chỗ có nước.
- Không chồng hay dán SuperMini lên radar. Đặt bên cạnh hoặc thấp hơn radar, cách 1–2 cm, vì khí nóng của radar bốc lên.
- Đặt xa bình nóng lạnh, đèn sưởi và nắng trực tiếp.
- Đầu anten của SuperMini (đầu đối diện cổng USB) không chạm dây hay vật kim loại.

## Yêu cầu mạng

**ESP32-H2 (Thread):** cần một **Thread Border Router** trong nhà, ví dụ Apple HomePod mini / HomePod 2 / Apple TV 4K (bản có Ethernet hoặc 2022), Google Nest Hub (2nd gen) / Nest Hub Max / Nest Wifi Pro, Amazon Echo 4th gen, Home Assistant SkyConnect / ZBT-1 + OpenThread Border Router add-on. Điện thoại gửi thông tin mạng Thread cho thiết bị qua BLE lúc ghép nối. Điện thoại và Border Router cùng mạng LAN, router bật IPv6 và không chặn mDNS/multicast.

**ESP32-C3 (Wi-Fi):**

- Wi-Fi **2,4 GHz** (ESP32-C3 không có 5 GHz), WPA/WPA2/WPA3 Personal (không có Enterprise, OWE).
- Điện thoại, controller (HomePod, Home Assistant, Nest...) và thiết bị **cùng một mạng LAN**; không dùng mạng khách.
- Router bật **IPv6** (ít nhất link-local), không chặn **mDNS/multicast**, tắt "AP/client isolation".

## Ghép nối (commissioning)

Firmware dùng chứng chỉ test của esp-matter (VID `0xFFF1`, PID `0x8001` cho ESP32-C3, `0x8000` cho ESP32-H2; SDK có DAC, PAI và Certification Declaration test cho hai PID này), mã ghép nối mặc định của SDK:

- Mã nhập tay: **`3497-011-2332`**
- QR bo ESP32-C3: `MT:-24J042C00KA0648G00` ([hiện QR](https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:-24J042C00KA0648G00))
- QR bo ESP32-H2: `MT:Y.K9042C00KA0648G00` ([hiện QR](https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00))
- Passcode 20202021, discriminator 3840

Khi chưa ghép nối, log in QR, link ảnh QR và mã nhập tay của chính firmware (`setup_code: Setup code of this board (BLE), SDK TEST CODE shared by every board`). Mã cũng được in khi bấm đúp và khi cửa sổ mở lại sau khi xoá thiết bị khỏi mọi ứng dụng, không in khi cửa sổ do controller mở (Multi-Admin). Đây là công cụ phát triển: sản phẩm thật thì tắt `CONFIG_PRINT_ONBOARDING_CODES` vì QR chứa passcode.

**Apple Home / Google Home / Home Assistant:** thêm thiết bị Matter, quét QR; ứng dụng gửi SSID và mật khẩu Wi-Fi (bo ESP32-C3) hoặc thông tin mạng Thread (bo ESP32-H2) cho thiết bị qua BLE. Apple Home báo *Uncertified Accessory*: chọn **Vẫn thêm**.

```bash
chip-tool pairing ble-wifi 0x7283 <SSID> <MẬT_KHẨU> 20202021 3840          # ESP32-C3, mỗi bo một Node ID
chip-tool pairing ble-thread 0x7284 hex:<ACTIVE_DATASET> 20202021 3840     # ESP32-H2 (dataset: ot-ctl dataset active -x)
chip-tool occupancysensing read occupancy 0x7283 1
```

- Khởi động mà chưa ghép nối: cửa sổ ghép nối mở 15 phút; BLE quảng bá nhanh 30 s rồi chuyển sang chậm (500 ms). Hết giờ thì **bấm đúp** để mở lại.
- Trong gói quảng bá trước khi ghép nối (DNS-SD), tên là `Presence Sensor` cho mọi bo, device type 263 (Occupancy Sensor). Tên Bluetooth (GAP) là tên thiết bị `Presence-XXXX`, chỉ thấy sau khi kết nối BLE.
- Sau khi ghép nối, BLE được tắt để trả RAM (`BLE deinitialized and memory reclaimed`). Bấm đúp khi đó mở cửa sổ chỉ qua mạng (DNS-SD), nên ứng dụng thứ hai phải ở cùng LAN.
- Xoá thiết bị khỏi mọi ứng dụng: cửa sổ ghép nối tự mở lại 5 phút, giữ thông tin Wi-Fi hoặc mạng Thread để ghép lại qua mạng.
- Tên thiết bị `Presence-XXXX` được ghi vào **NodeLabel** lúc khởi động nếu chưa có nhãn nào (bo mới, sau factory reset); nhãn đặt từ ứng dụng được giữ. Home Assistant đặt tên thiết bị theo NodeLabel.

**Nhiều bo cùng firmware:** bản mặc định cho mọi bo chung mã test. Ghép từng bo một (các bo chưa ghép khác tắt nguồn, không bấm đúp hai bo cùng lúc), rồi kiểm tra đúng bo bằng **Identify** hoặc so **Serial** trong ứng dụng với dòng `id_diag: dev serial=`. Thêm vào hệ thứ hai: ưu tiên chức năng chia sẻ của hệ đang quản lý (mã do ứng dụng tạo ngẫu nhiên).

### Mã ghép nối riêng cho từng bo (tuỳ chọn, chưa thử với firmware này)

Tính năng `features/unique_setup.defaults` (`tools/build.sh <bo> unique_setup`): mỗi bo tự sinh passcode, discriminator, salt và verifier SPAKE2+ ở lần dùng đầu (ngẫu nhiên từ DRBG của CHIP băm cùng MAC eFuse; không bao giờ sinh discriminator 3840 của mã test), lưu ở phân vùng `fctry` (namespace `setup-code`). Mã giữ qua khởi động lại, OTA, factory reset và nạp file merged; chỉ `erase-flash` mới sinh mã mới.

```bash
tools/build.sh c3-supermini unique_setup      # build/c3-supermini-unique_setup, dist/presence-c3-supermini-unique_setup-*
idf.py -B build/c3-supermini-unique_setup -p /dev/ttyACM0 flash monitor
```

```
W setup_code: New per-board setup code generated and stored in fctry/setup-code, discriminator 2294
I setup_code: Setup code of this board (BLE):
I setup_code:   QR code: MT:...
I setup_code:   Manual pairing code: ...
```

Bo đã ghép vẫn giữ các hệ hiện có; mã mới chỉ dùng cho lần ghép sau. Biến thể này build không lỗi, không cảnh báo, nhưng chưa chạy trên bo ESP32-C3 nào.

## Build

Yêu cầu: **ESP-IDF v6.0.2** + **esp-matter 1.6.1**, giống CI (Docker image `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`).

```bash
cd matter-presence-sensor/firmware
tools/build.sh c3-supermini           # một bo: boards/<bo>.defaults
tools/build.sh c3-wroom-devkit
tools/build.sh c3-radar-r45
tools/build.sh h2-mini                # ESP32-H2, Matter over Thread
tools/build.sh c3-radar-r45 unique_setup   # thêm tính năng: features/<tính năng>.defaults
tools/build.sh h2-mini factory        # dữ liệu nhà máy (DAC riêng từng bo), chỉ bo ESP32-H2
```

`tools/build.sh` chọn target theo tên bo (`c3-` → `esp32c3`, `h2-` → `esp32h2`), build trong `build/<bo>[-<tính năng>]` với `SDKCONFIG_DEFAULTS="sdkconfig.defaults;boards/<bo>.defaults;features/<tính năng>.defaults"` (file sau ghi đè file trước), rồi chép vào `dist/`:

| File | Dùng để |
|---|---|
| `presence-<bo>-<phiên bản>.ota` | OTA qua Matter Server (Home Assistant) |
| `presence-<bo>-<phiên bản>_app_0x20000.bin` | nạp USB giữ ghép nối, cùng file dưới |
| `presence-<bo>-<phiên bản>_ota_data_initial_0x1d000.bin` | |
| `presence-<bo>-<phiên bản>_merged.bin` | nạp lần đầu ở `0x0`; xoá ghép nối |

Bằng Docker, chạy từ thư mục gốc của repo (phiên bản firmware lấy từ số commit nên cần cả thư mục `.git`; image tự `cd` vào thư mục esp-matter nên lệnh phải `cd` lại):

```bash
docker run --rm -v $PWD:/project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c 'cd /project/matter-presence-sensor/firmware && git config --global --add safe.directory "*" \
           && tools/build.sh c3-wroom-devkit'
```

- Phiên bản firmware `1.0.<git rev-list --count HEAD>` (SoftwareVersionString), số commit là SoftwareVersion dùng cho OTA; CMake chạy lại sau mỗi commit. Không phải git checkout thì số là 0 (có cảnh báo).
- Mỗi bo có `sdkconfig` riêng trong `build/<bo>/sdkconfig`. File này giữ giá trị cũ: sau khi đổi `sdkconfig.defaults` hoặc file bo, xoá `build/<bo>` rồi build lại.
- `idf.py menuconfig` cho một bo: `idf.py -B build/<bo> -D SDKCONFIG=build/<bo>/sdkconfig menuconfig`.
- Chỉ build cho `esp32c3` và `esp32h2`; target khác thì CMake báo lỗi.
- Tính năng `factory` (từ `sdkconfig.defaults.factory` của bản H2 cũ): DAC, PAI, Certification Declaration và mã ghép nối lấy từ phân vùng `fctry` riêng từng bo, làm bằng `ESP32-H2/tools/matter-mfg`. Chỉ cho bo ESP32-H2: PAI test của SDK chỉ cấp DAC cho PID `0x8000`. Nạp phân vùng `fctry` của bo **trước** firmware này, nếu không bo vẫn chạy với các controller đang có nhưng không ghép nối lại được.
- CI (`.github/workflows/matter-presence-sensor.yml`) chạy host test rồi build mọi bo mỗi khi thư mục `firmware/` hoặc file workflow thay đổi; Artifacts `presence-<bo>` chứa các file trong `dist/`.

### Nạp firmware và xem log

SuperMini và `c3-wroom-devkit`: cổng USB gốc của ESP32-C3 (USB-Serial-JTAG): Linux `/dev/ttyACM0`, macOS `/dev/cu.usbmodem*`, Windows `COMx`.

```bash
idf.py -B build/c3-supermini -p /dev/ttyACM0 erase-flash flash monitor
```

`c3-radar-r45`: mạch USB-UART 3,3 V nối pad RX0 / TX0 / GND (Linux `/dev/ttyUSB0`); giữ IO9 nối GND rồi bấm RESET để vào chế độ nạp, nạp xong bấm RESET (không chập IO9) để chạy.

`h2-mini`: cổng USB-C (Linux `/dev/ttyACM0`), log ra cả cổng này lẫn UART0; dùng `--chip esp32h2` với `esptool.py`.

File merged (`dist/` hoặc Artifacts của CI), nạp một lần ở địa chỉ `0x0`. File này ghi đè vùng NVS (`0x10000`), tức là **xoá ghép nối**:

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 presence-c3-supermini-1.0.<n>_merged.bin
```

Nạp lại mà giữ ghép nối:

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x1d000 presence-<bo>-1.0.<n>_ota_data_initial_0x1d000.bin \
  0x20000 presence-<bo>-1.0.<n>_app_0x20000.bin
```

Không vào được chế độ nạp: giữ **BOOT** (pad IO9 với GND trên `c3-radar-r45`), cắm điện (hoặc bấm RESET), thả rồi nạp lại. Thoát monitor: `Ctrl+]`.

### Cập nhật qua mạng (OTA)

File **`presence-<bo>-<phiên bản>.ota`** (`dist/` hoặc Artifacts của CI): app kèm header Matter OTA với VID `0xFFF1`, PID của chip (`0x8001` ESP32-C3, `0x8000` ESP32-H2) và phiên bản `1.0.<số commit>`.

1. Home Assistant → Settings → Add-ons → **Matter Server** → **Open Web UI**.
2. Chọn thiết bị → **Upload Firmware** → chọn file `.ota` **của đúng bo** → **Start Update**.
3. Thiết bị tải qua Wi-Fi hoặc Thread (khoảng 1,8 MB qua Thread, có thể mất vài chục phút), tự khởi động lại vào bản mới và giữ ghép nối.

- **Mọi bo C3 cùng VID/PID và cùng số phiên bản**: Matter Server không phân biệt được. Bo H2 có PID riêng (`0x8000`), nên file `.ota` của C3 và của H2 không lẫn được với nhau. Nạp nhầm file của bo khác thì chân radar, LED, nút, cổng log sai (chưa thử trên bo); nạp lại đúng file qua USB / pad nạp.
- Phiên bản trong file phải **lớn hơn** bản đang chạy. Nhánh của thư mục này chứa đủ lịch sử các bản 1.0.132–1.0.175 nên số commit lớn hơn. Khi merge vào `main`, dùng **merge commit**, không squash: squash làm số commit trên `main` nhỏ hơn bản đang chạy trên bo và bo sẽ từ chối bản OTA đó.
- Sau khi đã cập nhật OTA, thiết bị chạy từ phân vùng `ota_1`: nạp USB thì dùng file merged hoặc ghi cả `ota_data_initial` (như trên), không ghi riêng `0x20000`.

## Cấu hình (`idf.py menuconfig`)

Tên các tuỳ chọn giữ như bản 1.0.132; menu *Connection Watchdog* và *Wi-Fi RSSI and Uptime Reports* là mới; `PRESENCE_CHIP_TEMPERATURE` mới, mặc định tắt; `PRESENCE_WIFI_STAY_AWAKE` đổi mặc định thành tắt. Cột *Mặc định* là giá trị sau `sdkconfig.defaults` và file bo; giá trị riêng của Kconfig ghi trong ngoặc. Lệnh `menuconfig` cho một bo: mục **Build**.

| Menu | Tuỳ chọn | Mặc định | Khoảng |
|---|---|---|---|
| Device Information | `DEVICE_VENDOR_NAME` (Manufacturer) | `Haxeko` | ≤ 32 ký tự |
| | `DEVICE_PRODUCT_NAME` (Model, ProductName = ProductLabel) | `Presence LD2410` | ≤ 32 |
| | `DEVICE_NAME_PREFIX` (tên thiết bị = prefix + 4 ký tự cuối serial; rỗng: NodeLabel `Presence Sensor`, tên BLE `Presence-<discriminator>`) | `Presence-` | ≤ 11 |
| | `DEVICE_DISPLAY_NAME` (tên DNS-SD trước khi ghép nối) | `Presence Sensor` | ≤ 32 |
| | `DEVICE_HARDWARE_VERSION_STRING` | `ESP32C3-LD2410` | ≤ 64 |
| Presence Sensor Configuration | `LD2410_UART_RX_PIN` / `LD2410_UART_TX_PIN` | theo file bo (Kconfig: 3 / 4) | 0–21 |
| | `PRESENCE_MAX_DISTANCE_CM` (chỉ tính mục tiêu gần hơn) | 0 (tắt) | 0–800 |
| | `PRESENCE_HOLD_TIME_SEC` (Hold time lúc đầu, áp dụng một lần) | 30 | 1–300 |
| | `RADAR_TIMEOUT_SEC` (radar im lặng thì coi là không có người) | 10 | 1–300 |
| | `BOARD_NAME` (chỉ để in trong log) | theo file bo | |
| | `STATUS_LED_TYPE` (LED GPIO một màu / WS2812 / không có LED) | theo file bo (Kconfig: LED GPIO) | |
| | `STATUS_LED_GPIO` (không có khi không có LED) / `STATUS_LED_ACTIVE_HIGH` (chỉ LED GPIO) / `STATUS_LED_BRIGHTNESS` (chỉ WS2812) | 8 / tắt / 20 | 0–21 / – / 1–255 |
| | `RESET_BUTTON_GPIO` | 9 | −1 (không có nút) – 21 (27 trên ESP32-H2) |
| | `PRESENCE_BUTTON_LED_H2` (nút và đèn như bản H2 cũ: bấm 4 lần không làm gì, ngưỡng xuất xưởng xanh lá 10 s) | bật cho ESP32-H2, tắt cho ESP32-C3 | |
| | `PRESENCE_CHIP_TEMPERATURE` (endpoint 5: nhiệt độ chip, mới; bật thì cần `ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=6`) | tắt (bật ở 1.0.143–1.0.146) | |
| | `PRESENCE_WIFI_STAY_AWAKE` (tắt modem sleep sau khi BLE tắt; 1.0.132 bật) | tắt | |
| Empty Room Learning | `LD2410_LEARN_DELAY_SEC` / `LD2410_LEARN_DURATION_SEC` | 20 / 120 s (Kconfig: 60) | 0–120 / 10–300 |
| | `LD2410_LEARN_PEAK` (học từ mức cao nhất) | bật (Kconfig: tắt) | |
| | `LD2410_LEARN_FLOOR_PERCENT` / `_MIN_MARGIN` (chỉ với `LD2410_LEARN_PEAK`) | 75 / 8 | 0–100 / 1–50 |
| | `LD2410_LEARN_RADAR_AUTO` (không có khi bật `LD2410_LEARN_PEAK`) | bật | |
| | `LD2410_LEARN_PERCENTILE` / `_MARGIN` (không có khi bật `LD2410_LEARN_PEAK`) / `_MAX_RAISE` | 95 / 10 / 30 | 50–100 / 0–50 / 0–100 |
| Thread Diagnostics (chỉ ESP32-H2) | `THREAD_DIAG_LOG` (khối `thread_diag` mỗi 10 s cho tới khi ghép nối xong) | bật | |
| Identity Diagnostics and Setup Code | `IDENTITY_DIAG_PERIOD_SEC` | 300 | 0 (tắt) – 86400 |
| | `IDENTITY_DIAG_SUBSCRIPTION_LOG` / `PRINT_ONBOARDING_CODES` | bật / bật | |
| | `UNIQUE_SETUP_CODE`, `UNIQUE_SETUP_CODE_PARTITION` | bật với tính năng `unique_setup`, `fctry` | |
| Connection Watchdog (chỉ ESP32-C3) | `NET_WATCHDOG_RECONNECT_MIN` (nối lại Wi-Fi mỗi, khi không có subscription) | 10 phút | 0 (không) – 1440 |
| | `NET_WATCHDOG_RESTART_MIN` (khởi động lại sau, khi không có subscription; 0 cũng tắt việc khởi động lại khi task Matter treo) | 30 phút | 0 (không) – 1440 |
| Wi-Fi RSSI and Uptime Reports (chỉ ESP32-C3) | `DIAG_REPORT_RSSI_STEP_DB` (gửi lại Wi-Fi RSSI khi đổi từng này dB) | 3 | 0 (chỉ theo chu kỳ) – 30 |
| | `DIAG_REPORT_REFRESH_MIN` (gửi lại Wi-Fi RSSI và uptime mỗi) | 10 phút | 0 (không) – 1440 |
| Memory Monitor | `MEM_MONITOR_PERIOD_SEC` | 600 | 0 (chỉ khi khởi động) – 86400 |

Console Matter (gõ lệnh trong monitor) tắt để có thêm khoảng 10 KB RAM: bật lại bằng *Component config* → *CHIP Core* → *General Options* → *Use the CHIP shell library* (`CONFIG_ENABLE_CHIP_SHELL`). Khi đó có lệnh của esp-matter (diagnostics, factoryreset, attribute) và `matter wifi ...` của Matter SDK; mật khẩu gõ vào lệnh `matter wifi connect` hiện trên màn hình và nằm trong lịch sử lệnh.

## Wi-Fi và BLE (ESP32-C3)

- **Wi-Fi station** (không SoftAP). Thông tin Wi-Fi đến từ lúc ghép nối; kết nối và kết nối lại do Matter SDK lo, firmware không tự gọi `esp_wifi_connect`.
- **Lúc ghép nối**, BLE và Wi-Fi dùng chung anten 2,4 GHz (*coexistence* của ESP-IDF); firmware giữ mặc định của ESP-IDF (Wi-Fi có modem sleep `min_modem`).
- **Sau khi BLE tắt, Wi-Fi vẫn ở modem sleep** (từ 1.0.144; `id_diag` ghi `power_save=min_modem`):
  - Radio chỉ thức ở mỗi beacon DTIM để nhận gói router giữ hộ. Gói từ controller tới bo (lệnh, xác nhận) trễ tối đa một chu kỳ DTIM, thường 100–300 ms. Gói từ bo đi (báo hiện diện) được gửi ngay.
  - Đây là mặc định của ESP-IDF và esp-matter: không phần nào của SDK đổi chế độ này. Đây cũng là mặc định `power_save_mode: LIGHT` của ESPHome trên ESP32.
- **Tuỳ chọn `PRESENCE_WIFI_STAY_AWAKE`** (mặc định tắt từ 1.0.144; bản 1.0.132–1.0.143 bật):
  - Khi bật: lúc có sự kiện `kBLEDeinitialized`, và ở mỗi lần Wi-Fi kết nối sau đó, firmware gọi `esp_wifi_set_ps(WIFI_PS_NONE)` (log `BLE released: Wi-Fi power save off`, `id_diag` ghi `power_save=none`).
  - Lý do ban đầu: log 1.0.128 có `Msg Retransmission`; hiệu quả chưa từng được kiểm chứng. Cái giá thì đã đo được: chip 80 °C khi để ngoài hộp, 96 °C trong hộp (xem **Nhiệt độ chip**).
  - Chỉ bật lại nếu modem sleep gây lỗi (ví dụ điều khiển từ Home Assistant chậm hay thất bại), và khi đó build kèm `PRESENCE_CHIP_TEMPERATURE` để theo dõi nhiệt độ chip.
- **Không giới hạn công suất phát Wi-Fi.** Bản 1.0.125–1.0.127 đã thử 8,5 dBm, không giúp được; nguyên nhân lỗi kết nối là Beamforming của router TP-Link Deco X10 (tắt đi thì ghép nối được).
- **BLE thu gọn** (NimBLE, chỉ để ghép nối): vai trò peripheral, 1 kết nối, không bảo mật BLE (Matter tự bảo mật bằng PASE), không có tính năng BLE 5, bộ đệm nhỏ (mục *BLE Optimizations* của hướng dẫn tối ưu RAM của esp-matter).
- Mã Wi-Fi nằm trong flash thay vì IRAM (`ESP_WIFI_IRAM_OPT=n`, `ESP_WIFI_RX_IRAM_OPT=n`): trên ESP32-C3, IRAM và DRAM dùng chung SRAM, nên có thêm heap.

### Watchdog kết nối (`net_watchdog.cpp`, mới)

Khi thiết bị mất liên lạc với controller, cả hai phía đều chờ lâu mới thử lại (theo mã nguồn, chưa đo trên bo):
- **Matter Server bản matter.js** của Home Assistant:
  - báo *Unavailable* sau 3 phút mất kết nối;
  - nếu không nối lại được ở địa chỉ cũ thì chờ thông báo DNS-SD (mDNS) của thiết bị, hoặc 10 phút thử lại một lần;
  - thông báo multicast qua Wi-Fi hay bị mất.
- **Matter SDK trên thiết bị:**
  - chỉ tự nối lại subscription sau 5 phút, rồi thưa dần tới 6 giờ;
  - bản matter.js lại từ chối subscription cũ đã bị thay thế.

Vì vậy, sau khi Wi-Fi đã ổn định lại, mỗi lần mất kết nối vẫn có thể kéo dài 3 tới hơn 30 phút.

Watchdog kiểm tra 30 s một lần. Khi thiết bị **đã ghép nối** mà **không còn subscription nào** từ bất kỳ controller nào (Home Assistant, Apple Home…):

| Không có subscription trong | Việc làm | Log |
|---|---|---|
| 5 phút, rồi 5 phút một lần | Thông báo lại thiết bị trên DNS-SD, để controller tìm lại ngay khi liên lạc được | `W net_wd: No controller subscribed for 5 min: announcing the device again (DNS-SD)` |
| 10 phút, rồi 10 phút một lần (`NET_WATCHDOG_RECONNECT_MIN`) | Ngắt Wi-Fi. Matter SDK nối lại ngay: quét mọi kênh, chọn điểm phát mạnh nhất (node Deco gần nhất), xin lại địa chỉ, thông báo lại DNS-SD | `W net_wd: ... reconnecting Wi-Fi` |
| 30 phút (`NET_WATCHDOG_RESTART_MIN`) | Khởi động lại thiết bị | `E net_wd: ... restarting`, lần khởi động sau: `W net_wd: Restarted by the connection watchdog: no controller subscribed` |

- Task Matter không chạy được 3 phút (bị treo hoặc bị task khác chiếm CPU): khởi động lại ngay, log `E net_wd: Matter task stuck ...`. Cấu hình ESP-IDF hiện tại không có cơ chế nào khác làm việc này: task watchdog chỉ cảnh báo, không khởi động lại.
- Có subscription trở lại thì mọi bộ đếm về 0. Thiết bị chưa ghép nối thì watchdog không làm gì.
- Còn **một** controller giữ subscription (ví dụ Apple Home khi Home Assistant mất kết nối) thì watchdog không làm gì. Lúc đó thiết bị vẫn liên lạc được, nên lỗi nằm ở phía controller kia; khởi động lại thiết bị chỉ làm controller còn lại mất kết nối theo.
- Mỗi lần ngắt Wi-Fi, Home Assistant nhận một sự kiện `wiFiNetworkDiagnostics.disconnection` với `reasonCode: 8` (thiết bị tự rời mạng).
- **Ảnh hưởng:** nếu tất cả controller đều tắt lâu hơn 30 phút (ví dụ Home Assistant bảo trì), thiết bị sẽ khởi động lại 30 phút một lần cho tới khi có controller subscribe trở lại. Việc này vô hại: radar chạy tiếp, Hold time và lựa chọn được giữ.
- Không nên dùng watchdog với controller không bao giờ subscribe. **Tắt hoặc quay lại như 1.0.140:** đặt cả hai tuỳ chọn về 0 (không ngắt Wi-Fi, không khởi động lại; chỉ còn thông báo DNS-SD, vô hại).
- Watchdog **không sửa được nguyên nhân gốc** của việc mất kết nối (sóng yếu, router). Nó chỉ rút ngắn thời gian mất kết nối.

## RAM và flash

Đo bằng `idf.py size`, cấu hình mặc định, cả hai bản build trong cùng Docker image `release-v1.6.1_idf_v6.0.2` (image được build lại ngày 2026-10-15 với connectedhomeip `b1902b39`):

| | 1.0.132 (`matter-wifi-presence`) | Bản này |
|---|---|---|
| Firmware (phân vùng app 1 920 KB) | 1 513 104 B (1 478 KB), trống 23 % | 1 514 544 B (1 479 KB), trống 23 % |
| RAM tĩnh (DRAM 313,8 KB) | 150 565 B (147,0 KB, 46,86 %) | 150 543 B (147,0 KB, 46,85 %) |
| Còn cho heap | 170 731 B (166,7 KB) | 170 753 B (166,8 KB) |
| Biến thể `unique_setup` (1.0.147) | | 1 516 528 B |
| Bật `PRESENCE_CHIP_TEMPERATURE` (giới hạn endpoint 6; 1.0.147) | | 1 521 536 B, RAM tĩnh 150 983 B (như 1.0.144) |
| Biến thể `wroom_n4` (WS2812 và nhiệt độ chip) | | 1 539 824 B (trống 22 %), RAM tĩnh 153 427 B, heap 167 869 B |
| Biến thể `esplearn` (`wroom_n4` không nhiệt độ chip, học từ mức cao nhất; nhánh `claude/c3-wroom-esp-presence`, 1.0.175) | | 1 530 673 B (trống 22 %), RAM tĩnh 152 987 B, heap 168 309 B; lúc học mượn thêm 3,6 KB heap trong 120 s |
| Bản mặc định của nhánh `claude/c3-wroom-esp-presence` (1.0.175) | | 1 514 215 B (trống 23 %), RAM tĩnh 150 551 B, heap 170 745 B |
| `matter-presence-sensor` 1.0.180, `c3-supermini` | | 1 512 285 B (trống 23 %), RAM tĩnh 150 543 B, heap 170 753 B (RAM tĩnh bằng bản mặc định 1.0.147) |
| `matter-presence-sensor` 1.0.180, `c3-wroom-devkit` | | 1 530 527 B (trống 22 %), RAM tĩnh 152 987 B, heap 168 309 B (RAM tĩnh bằng `esplearn` 1.0.175) |
| `matter-presence-sensor` 1.0.180, `c3-radar-r45` | | 1 512 297 B (trống 23 %), RAM tĩnh 150 465 B, heap 170 831 B (không có LED) |
| `matter-presence-sensor` 1.0.185, `h2-mini` (ESP32-H2, Thread) | | 1 805 480 B (phân vùng app trống 8 %), DIRAM tĩnh 175 186 B, còn 82 814 B cho heap |
| Bản ESP32-H2 cũ `matter-presence-ld2410-learning` build từ `main` 1.0.184 (cùng image, để so sánh) | | 1 806 880 B (trống 8 %), DIRAM tĩnh 175 178 B, còn 82 822 B |
| `matter-presence-sensor` 1.0.185, ba bo ESP32-C3 | | `c3-supermini` 1 512 313 B, `c3-wroom-devkit` 1 530 547 B, `c3-radar-r45` 1 512 317 B (lớn hơn 1.0.180 từ 20 tới 28 byte), RAM tĩnh không đổi |
| Lúc học phòng trống (mọi bo) | | mượn thêm 3,6 KB heap trong 120 s |

Task và stack (byte; số "còn trống" đo trên bo với bản 1.0.128–1.0.131, mã các task này không đổi):

| Task | Priority | Stack | Còn trống |
|---|---|---|---|
| CHIP (Matter) | 1 | 8192 | 2320 |
| wifi (ESP-IDF) | – | – | 4652 |
| esp_timer (LED 100 ms, nút 20 ms, log `mem`) | – | 3584 | 3120 |
| presence | 2 | 4096 | 3052 |
| radar_cfg | 2 | 4096 | 2852 |
| ld2410 | 5 | 2560 | 1856 |
| id_diag | 1 | 4096 | 2804 |

Heap đo trên bo (log của người dùng, firmware cũ): bản 1.0.122 lúc ghép nối còn 30,7 KB, thấp nhất 25,9 KB; bản 1.0.128 thấp nhất 15,7 KB trong 10 phút đầu; bản 1.0.131 lúc ghép nối còn 49,0 KB, thấp nhất 40,0 KB. Heap của bản này cần đo trên bo bằng log `mem`:

```
I mem: Heap: <còn trống> bytes free, lowest <thấp nhất> since boot, largest block <khối lớn nhất>
I mem: Stack unused bytes: CHIP <n> wifi <n> esp_timer <n> presence <n> ld2410 <n> radar_cfg <n> id_diag <n>
```

Log in lúc khởi động rồi 10 phút một lần. Heap thấp nhất xuống dưới 16 KB: cảnh báo một lần cho mỗi mức thấp mới, kèm khoảng thời gian (`W mem: Heap low: lowest N bytes free since boot, reached between A s and B s; now ... free, largest block ...`). Stack còn dưới 512 byte: cảnh báo.

## Log khởi động và chẩn đoán

```
I app_main: Haxeko Presence LD2410, firmware 1.0.<số commit>
I app_main: Target esp32c3, board ESP32-C3 SuperMini, Matter over Wi-Fi
I app_main: Pins: LD2410 RX GPIO3 (radar TX), TX GPIO4 (radar RX), button GPIO9, LED GPIO8 (single colour, on when low)
I ld2410: LD2410 on UART1, RX=GPIO3 TX=GPIO4
I device_info: Serial number: 58CF79A1B2C4, device name: Presence-B2C4
I radar_ctl: Endpoints: Learn empty room 1, Range 2, Radar resolution 3, Radar Bluetooth 4
I app_main: Hold time 30 s (limits 1-300 s)
I diag_report: Wi-Fi RSSI reported again on a 3 dB change, RSSI and uptime every 10 min
I net_wd: Connection watchdog: without any controller subscription, DNS-SD again every 5 min, Wi-Fi reconnect every 10 min, restart after 30 min (0 = never)
I radar_ctl: LD2410 settings: no-one duration 5 s, range moving 6 m (gate 8), still 6 m (gate 8), 75 cm per gate, Bluetooth off
```

Với `PRESENCE_CHIP_TEMPERATURE` có thêm `I c3_temp: Chip temperature on endpoint 5` và, khi đã chạy, `I c3_temp: Chip temperature 41.3 C`.

Số serial là MAC gốc trong eFuse (6 byte, 12 ký tự hex in hoa), cũng là MAC Wi-Fi router hiển thị. Hiện diện: `presence: Radar: presence (moving yes 120 cm, still no 0 cm)`, `presence: Radar: no one (...)`. Radar không gửi gì 10 s: `E presence: No data from the LD2410 for 10 s, check the wiring (radar TX -> GPIO3, RX -> GPIO4)` (chân của bo đang chạy) và báo không có người; có dữ liệu lại thì `Radar data received`. Sự kiện Matter: `Commissioning complete`, `Commissioning failed, fail safe timer expired`, `Commissioning window opened / closed`, `Fabric removed successfully`, `Wi-Fi connectivity changed`, `Interface IP Address changed`.

**Khối `id_diag`** (lúc khởi động, 5 phút một lần, và sau sự kiện Wi-Fi, IP, ghép nối, fabric, gộp lại cách nhau ít nhất 5 s):

```
I id_diag: hw   efuse_mac=58CF79A1B2C4 wifi_mac=58CF79A1B2C4 reset=power-on up=300s heap=... min_heap=...
I id_diag: dev  vid=0xFFF1 pid=0x8001 serial=58CF79A1B2C4 unique_id=... discriminator=3840 test_setup_code=yes
I id_diag: mtr  commissioned=yes fabrics=2 window_open=no failsafe=no sessions: case=2 pase=0, subscriptions=3 reads=0
I id_diag: fab  index=1 vendor=0x.... node_id=... fabric_id=... compressed_fabric_id=... case=1 subscriptions=2 label="..."
I id_diag: wifi connected ssid="..." bssid=... ch=6 rssi=-55 power_save=min_modem
I id_diag: ip   ipv4=192.168.1.20 ipv6: link-local=1 unique-local=1 global=1
I id_diag: nvs  entries used=... free=... available=... total=... namespaces=...
```

- Mỗi lần vào hoặc rời Wi-Fi: `EV wifi joined bssid=... ch=... security=WPA2`, hoặc `EV wifi failed or left: reason N (...), bssid=... rssi=-55 dBm` (xem **Xử lý sự cố**).
- Subscription: `SUB request`, `SUB established`, `SUB terminated` kèm fabric, peer và chu kỳ báo cáo (chỉ quan sát, không đổi subscription).
- `reset=` là `BROWNOUT`, `PANIC` hoặc `WATCHDOG`: bo bị khởi động lại bất thường (nguồn yếu, lỗi firmware). `SUB terminated` lặp lại: controller mất subscription.
- Đã kết nối mà chưa có IPv6 link-local: cảnh báo `No IPv6 link-local address yet`. `power_save=min_modem` là modem sleep (mặc định); `none` khi bật `PRESENCE_WIFI_STAY_AWAKE` và BLE đã tắt.
- Dữ liệu Matter lấy với khoá CHIP chờ tối đa 2 s; task Matter bị kẹt thì bỏ lượt (`mtr  Matter stack lock busy`).
- Không bao giờ in passcode, khoá riêng DAC hay khoá vận hành, IPK, mật khẩu Wi-Fi. NVS chỉ in số mục theo namespace.

## Xử lý sự cố

**Bo ESP32-H2 (Thread), điện thoại không tìm thấy thiết bị:** ghép nối Matter over Thread có 2 giai đoạn. Qua BLE, điện thoại gửi thông tin mạng Thread (log `Role detached -> child`, `advertising srp service: ...._matter._tcp`); rồi qua mạng IP, điện thoại tìm dịch vụ `_matter._tcp` của thiết bị qua Border Router (log `Commissioning complete`). Giai đoạn 1 xong mà ứng dụng báo không thấy thiết bị thường là do mạng nhà: điện thoại và Border Router cùng LAN (không dùng mạng khách, tắt VPN), router bật IPv6 và không chặn mDNS/multicast, Border Router là của hệ đang dùng. Khi chưa ghép nối, cứ 10 s firmware in khối `thread_diag`:

```
I thread_diag: Role: router, RLOC16: 0x7c00
I thread_diag: Address: fd12:3456:789a:1:abcd:...  [OMR (reachable from Wi-Fi/LAN)]
I thread_diag: Summary: OMR address YES, on-mesh prefixes 1, SRP host Registered, SRP service Registered
```

`OMR address NO` hoặc SRP không `Registered`: lỗi phía Border Router hoặc mạng LAN. Khối `id_diag` của bo H2 có thêm các dòng `ot`, `mle`, `br`, `srp` (vai trò Thread, mạng, Border Router, đăng ký SRP); giữa các bo, `ext_panid` và `partition` phải giống nhau, `ext_addr`, `ml_eid`, `srp host` phải khác nhau.

**Bo ESP32-C3 (Wi-Fi):**

- **Không thấy log:** chọn đúng cổng USB của SuperMini; một số cáp USB-C chỉ có dây nguồn. `c3-radar-r45`: log chỉ có ở pad TX0 (115200 baud), cổng USB-C không có dây dữ liệu.
- **Ghép nối dừng ở bước Wi-Fi**, log lặp lại `wifi:state: auth -> init`, `Connecting -> Connecting_Failed`: router không trả lời bước xác thực. Mỗi lần thử có một dòng `W id_diag: EV wifi failed or left: reason ...`:
  - **reason 2**: router không trả lời bước xác thực (hoặc báo xác thực không còn hiệu lực);
  - **reason 4**: router không trả lời bước kết nối, hoặc ngắt bo vì không hoạt động;
  - **reason 15**: hết thời gian trao khoá, thường do sai mật khẩu;
  - **reason 36**: chính bo dừng lần thử (ví dụ điện thoại đã hết thời gian chờ bước Wi-Fi);
  - **rssi** từ −70 dBm trở lên là đủ mạnh, dưới −80 dBm là quá yếu.
- **TP-Link Deco X10 (mesh):** tắt **Beamforming** trong app Deco (người dùng đã sửa được lỗi trên bằng cách này); cập nhật firmware Deco có thể bật lại. Vẫn lỗi thì bật **IoT Network** (mạng 2,4 GHz riêng), tạm tắt *Fast Roaming*, đặt WPA2. Router khác: WPA2-PSK (AES) hoặc WPA2/WPA3, chế độ b/g/n (không chỉ ax), tắt lọc MAC, thử tên mạng 2,4 GHz riêng nếu đang gộp band.
- **Ghép nối xong phần Wi-Fi nhưng ứng dụng không tìm thấy thiết bị:** điện thoại và thiết bị cùng LAN, router không chặn mDNS/multicast, có IPv6 (dòng `ip` của `id_diag`).
- **Radar không đọc được** (`Could not read the LD2410 settings (check the wire from GPIO4 to the radar RX pin), next try in 15 s`, số chân theo bo): kiểm tra dây ESP TX → RX của radar. Cảm biến vẫn báo hiện diện nếu dây ESP RX ← TX của radar còn tốt, nhưng các điều khiển radar bị từ chối.
- **Home Assistant báo *Unavailable*:**
  - **Chỉ khoảng 1 s, cùng lúc ở mọi thiết bị Matter:** Home Assistant đang tải lại integration Matter, ví dụ 30 s sau khi bật/tắt một entity hay khi khởi động lại. Thiết bị không có lỗi. Log Matter Server lúc đó có `WebSocket connection closed` rồi `WebSocket connection established`.
  - **Mất kết nối thật:** Matter Server bản matter.js chỉ báo *Unavailable* khi thiết bị mất kết nối liên tục 3 phút (log `offline grace period expired, marking unavailable`). Các dòng `Peer is no longer responding to active session` nghĩa là thiết bị không trả lời. Trong log Matter Server, `@1:5a` là node 0x5a = 90.
  - **Tìm nguyên nhân mà không cần cắm USB:** sau khi nối lại, thiết bị gửi các sự kiện đã lưu. Matter Server ghi chúng vào log dạng `Received event ... on ...@1:5a.ep0`:
    - `wiFiNetworkDiagnostics.disconnection` với `reasonCode`: thiết bị mất Wi-Fi. `200` là mất beacon, tức sóng yếu hoặc router; `8` là thiết bị tự rời mạng, ví dụ watchdog nối lại. `wiFiNetworkDiagnostics.associationFailure` là nối lại thất bại.
    - `basicInformation.startUp` và `generalDiagnostics.bootReason`: thiết bị đã khởi động lại. `BrownOutReset` là nguồn yếu hoặc sụt áp; `SoftwareReset` là lỗi firmware hoặc watchdog; `PowerOnReboot` là mất điện.
    - Không có sự kiện nào mà vẫn mất kết nối: thiết bị vẫn nối Wi-Fi nhưng không nhận được gói. Thử `ping <IP>` lúc đang *Unavailable*, xem node Deco và mức sóng trong app của router.
  - Entity **Wi-Fi RSSI** (bản 1.0.140 trở lên): dưới −75 dBm là sóng yếu, hay mất kết nối. Đặt thêm node mesh hoặc dời bo ra xa tường, gương, gạch men.
  - Entity **nhiệt độ chip** (bản 1.0.143–1.0.146, hoặc build với `PRESENCE_CHIP_TEMPERATURE`): nếu mất kết nối lúc chip nóng nhất, xem **Chip quá nóng**.
  - **Phân biệt lỗi mạng với khởi động lại:** nếu sau khi nối lại vẫn dùng cùng số phiên (`@1:5a•580e`), thiết bị **không** khởi động lại (khởi động lại thì mất mọi phiên), mà là mất Wi-Fi hoặc không nhận được gói. Kiểm tra *Wi-Fi RSSI*, *Reboot count*, và log USB (`EV wifi failed or left: reason ...`, `W mem: Heap low`).
- Mỗi lần thử lại ghép nối: factory reset (giữ BOOT 5 s; pad IO9 với GND trên `c3-radar-r45`) hoặc `erase-flash`.

## Mức độ kiểm tra

**Đã kiểm chứng bằng build và test trên máy tính (thư mục `matter-presence-sensor/firmware`):**
- Build 1.0.180 (commit `e1792a5`) cả ba bo bằng `tools/build.sh` từ một bản clone sạch, Docker `release-v1.6.1_idf_v6.0.2`: **0 lỗi, 0 cảnh báo**. Các commit sau đó chỉ đổi chú thích và README (CI build lại với số commit mới).
- `sdkconfig` sinh ra (bỏ dòng chú thích):
  - `c3-wroom-devkit` giống hệt `esplearn` 1.0.175, chỉ thêm dòng `STATUS_LED_TYPE_NONE` (không bật);
  - `c3-supermini` so với bản mặc định 1.0.175 chỉ khác các tuỳ chọn học: `LD2410_LEARN_PEAK`, ghi 120 s, `_FLOOR_PERCENT` 75, `_MIN_MARGIN` 8 thay cho `_RADAR_AUTO`, `_PERCENTILE`, `_MARGIN`;
  - `c3-radar-r45` so với `c3-supermini` chỉ khác: chân radar RX 10 / TX 3, tên bo, không có LED, console UART0 115200 baud thay cho USB-Serial-JTAG.
- Header file OTA của cả ba bo: VID `0xFFF1`, PID `0x8001`, phiên bản 180 / `1.0.180`. File app của mỗi bo chứa đúng tên bo của nó.
- Kích thước: bảng **RAM và flash**.
- Host test (`test/host/run.sh`, 17 chương trình, đều qua): parser và lệnh radar, phiên thông số, học trên ESP32 (từ mức cao nhất và phân vị 95 %) và radar tự đo, điều phối (mọi thứ tự Learning/Off/Factory, kể cả lúc radar đang đo), presence, nút, LED (một màu, WS2812, không có LED: không ghi chân nào, trạng thái vẫn chạy), tên thiết bị, lúc nào gửi lại Wi-Fi RSSI và Uptime, watchdog kết nối, nhiệt độ chip.

- Build 1.0.185 (commit `1bcacd6`) cả bốn bo (`h2-mini` và ba bo C3) bằng `tools/build.sh` từ một bản clone sạch, Docker `release-v1.6.1_idf_v6.0.2`: **0 lỗi, 0 cảnh báo**.
- `sdkconfig` của `h2-mini` so với bản ESP32-H2 cũ (`ESP32-H2/matter-presence-ld2410-learning`, build từ `main` 1.0.184 trong cùng image) chỉ khác: `BOARD_NAME` và `PRESENCE_BUTTON_LED_H2` (tuỳ chọn mới), và các tuỳ chọn học (`LD2410_LEARN_PEAK`, ghi 120 s, `_FLOOR_PERCENT` 75, `_MIN_MARGIN` 8 thay cho `_RADAR_AUTO`, `_PERCENTILE`, `_MARGIN`, ghi 60 s). OpenThread (FTD, SRP, CLI), console Matter, BLE, PID `0x8000`, danh sách cluster, bảng phân vùng, chân radar, LED WS2812 GPIO8, nút GPIO9 giống hệt.
- `sdkconfig` của ba bo C3 1.0.185 giống hệt 1.0.180: việc tách `sdkconfig.defaults` theo chip không đổi gì cho C3.
- Header file OTA `h2-mini`: VID `0xFFF1`, PID `0x8000`, phiên bản 185 / `1.0.185`, cùng dạng file `matter_presence_h2_learn-ota.bin` của bản H2 cũ. Chuỗi Hardware `ESP32H2-LD2410` như cũ.
- Mã Thread (cấu hình OpenThread, `thread_diag`, phần OpenThread của `id_diag`, OpenThread CLI) chép từ bản H2 cũ; số serial trên ESP32-H2 tính như cũ (EUI-64 bỏ hai byte FF FE).
- Host test 18 chương trình đều qua, thêm: bấm 4 lần khi không có hành động (bo H2) thì log `4 clicks, ignored` và không làm gì; đèn WS2812 sau khi ghi ngưỡng xuất xưởng với `PRESENCE_BUTTON_LED_H2` là xanh lá 10 s.

**Đã kiểm chứng ở các bản trước của cùng mã** (`ESP32-C3/matter-presence-ld2410`, nhánh `claude/c3-wroom-esp-presence`):
- Build `esp32c3` trong Docker `release-v1.6.1_idf_v6.0.2`: **0 lỗi, 0 cảnh báo**, cả bản mặc định, biến thể `unique_setup` và bản bật `PRESENCE_CHIP_TEMPERATURE` (giới hạn endpoint 6) ở 1.0.147; bản mặc định và biến thể `wroom_n4` ở 1.0.149; bản mặc định và biến thể `esplearn` ở 1.0.175.
- `sdkconfig` sinh ra giống hệt bản 1.0.132 build trong cùng image, trừ đường dẫn file, mục chọn loại LED đã bỏ, 5 tuỳ chọn mới (`DIAG_REPORT_RSSI_STEP_DB`, `DIAG_REPORT_REFRESH_MIN`, `NET_WATCHDOG_RECONNECT_MIN`, `NET_WATCHDOG_RESTART_MIN`, `PRESENCE_CHIP_TEMPERATURE` tắt) và `PRESENCE_WIFI_STAY_AWAKE` tắt: Wi-Fi bật, OpenThread tắt, BLE chỉ để ghép nối, minimal mDNS, cửa sổ ghép nối 900 s, VID `0xFFF1` / PID `0x8001`, WPA3 SAE, coexistence, stack CHIP 8192, CPU 160 MHz, brownout bật, đúng 21 cluster và giới hạn endpoint động 5 như 1.0.132. So với 1.0.144 chỉ khác 3 điểm: `PRESENCE_CHIP_TEMPERATURE` tắt, cluster Temperature Measurement tắt, giới hạn endpoint động 5. Build với `PRESENCE_CHIP_TEMPERATURE` và giới hạn 6 thì `sdkconfig` giống hệt 1.0.144 (22 cluster: tuỳ chọn tự bật lại cluster Temperature Measurement).
- Header file OTA (bản build 1.0.149, biến thể `wroom_n4`): VID `0xFFF1`, PID `0x8001`, phiên bản 149 / `1.0.149` (lớn hơn 132). 1.0.175: `esplearn` và bản mặc định phiên bản 175 / `1.0.175`, cùng VID/PID.
- Thứ tự tạo endpoint 0–4 giống 1.0.132; esp-matter (`esp_matter_data_model.cpp`) cấp số endpoint theo thứ tự tạo trước `esp_matter::start()`, nên số endpoint giữ nguyên. Endpoint 5 (nhiệt độ chip, chỉ khi bật `PRESENCE_CHIP_TEMPERATURE`) được tạo sau cùng.
- Kích thước so với 1.0.132 (bảng **RAM và flash**): lớn hơn 1 440 byte flash, RAM tĩnh ít hơn 22 byte. Phần thêm vào và bớt đi:
  - `diag_report` (báo cáo lại Wi-Fi RSSI và Uptime): 416 byte flash, 16 byte RAM tĩnh (so với 1.0.139);
  - `net_watchdog`: 1 808 byte flash, 32 byte RAM tĩnh (so với 1.0.140);
  - nhiệt độ chip (cluster Temperature Measurement, driver cảm biến, endpoint 5): 7 136 byte flash, 440 byte RAM tĩnh (so với 1.0.142);
  - Wi-Fi modem sleep (`PRESENCE_WIFI_STAY_AWAKE` tắt, mã tắt modem sleep không được build): bớt 672 byte flash, RAM tĩnh không đổi (so với 1.0.143);
  - nhiệt độ chip thành tuỳ chọn tắt sẵn (1.0.147): bớt 7 152 byte flash và 440 byte RAM tĩnh (so với 1.0.144). Bật lại thì kích thước đúng bằng 1.0.144;
  - WS2812 và tên bo (1.0.149): bản mặc định thêm 160 byte flash, RAM tĩnh không đổi (driver RMT không được build). Biến thể `wroom_n4` (WS2812 và nhiệt độ chip) lớn hơn 1.0.144 18 288 byte flash và 2 444 byte RAM tĩnh: driver RMT và thư viện `led_strip`.
- Làm hỏng có chủ ý (mỗi lần một chỗ): 19 chỗ trong firmware, test bắt được 18; chỗ còn lại (đổi thứ tự ghi ngưỡng mặc định và học trong vòng lặp) không đổi hành vi vì hai yêu cầu đó không bao giờ cùng chờ. Thêm 10 chỗ trong `diag_report.h`, 12 chỗ trong `net_watchdog.h`, 7 chỗ trong `c3_temperature.h`, 6 chỗ ở phần WS2812 của `status_led.cpp` (màu, độ sáng, chân, chỉ gửi khi đổi màu) và 29 chỗ của cách học từ mức cao nhất: test bắt được tất cả.
- Phần gửi lại Wi-Fi RSSI và Uptime: chỉ phần quyết định được test trên máy tính. Lời gọi Matter (`MatterReportingAttributeChangeCallback`, timer của Matter) mới kiểm chứng bằng build và bằng đọc mã nguồn Matter SDK, esp-matter, Home Assistant (đánh dấu đã đổi → subscription gửi lại; Home Assistant cập nhật theo giá trị).

**Đã thấy chạy trên bo, nhưng với firmware cũ** (log của người dùng; theo README cũ là các bản 1.0.128–1.0.131, cùng thuật toán và mã radar): ghép nối với Apple Home và Home Assistant (3 controller cùng lúc), đọc radar, radar tự đo phòng trống (lệnh 0x000B), *Factory thresholds* từ Home Assistant; bật Bluetooth của radar (0x00A4 + 0x00A3, đọc MAC bằng 0x00A5) trên radar V2.44.25070917.

**Chưa thử trên bo với bản này (cần thử, ghi kết quả vào đây):**
1. Log khởi động đúng; radar được đọc (`LD2410 settings: ...`).
2. Ghép nối BLE + Wi-Fi với Apple Home và Home Assistant; thêm controller thứ hai bằng bấm đúp (chỉ DNS-SD).
3. `BLE deinitialized ...`, rồi `id_diag` báo `power_save=min_modem`. Điều khiển từ Home Assistant (Hold time, các select) vẫn có tác dụng trong khoảng 1 s. Log không có nhiều `Msg Retransmission` hơn bản 1.0.143.
4. Hiện diện: bước vào báo có người ngay; ra khỏi phòng báo không có người sau *no-one duration* + Hold time; ngồi im vẫn giữ có người; đổi Hold time từ Home Assistant có tác dụng và giữ sau khi khởi động lại.
5. Rút dây TX của radar: sau 10 s có `No data ...` và báo không có người; cắm lại có `Radar data received`.
6. Range, Radar resolution, Radar Bluetooth từ Home Assistant: radar khởi động lại, giá trị đọc lại đúng, giữ sau khi rút điện; Bluetooth mặc định tắt.
7. Học phòng trống (bấm 3 lần và từ Home Assistant), huỷ bằng Off, Factory thresholds (bấm 4 lần và từ Home Assistant, kể cả lúc đang học), đúng mẫu LED. Cách ESP32 tự học và bấm 4 lần mới kiểm chứng bằng test.
8. Factory reset (giữ 5 s) rồi ghép nối lại; xoá thiết bị khỏi mọi ứng dụng thì cửa sổ ghép nối tự mở lại 5 phút.
9. **OTA từ 1.0.132 lên bản này**: giữ ghép nối, endpoint và entity; sau đó OTA tiếp lên một bản mới hơn.
10. Log `mem` lúc ghép nối: không có `Heap low`; mục tiêu là heap thấp nhất không thấp hơn 1.0.131 (40,0 KB).
11. Chạy 24 h: không khởi động lại bất thường (không `reset=BROWNOUT`, `PANIC`, `WATCHDOG`), subscription ổn định (không `SUB terminated` lặp lại).
12. Bật **Reboot count**, **Uptime**, **Wi-Fi RSSI** trong Home Assistant:
    - Wi-Fi RSSI đổi theo khi dời bo ra xa router (trong khoảng 1 phút khi lệch từ 3 dB);
    - Uptime đúng thời điểm khởi động, và đúng lại trong 10 phút sau khi khởi động lại Home Assistant;
    - Reboot count tăng 1 sau mỗi lần khởi động lại, giữ qua OTA.
13. Watchdog kết nối:
    - tắt router (hoặc chặn thiết bị trong app Deco) 12 phút: log `net_wd` thông báo DNS-SD ở phút 5 và nối lại Wi-Fi ở phút 10; bật router lại thì Home Assistant thấy thiết bị trong vài phút;
    - để quá 30 phút thì thiết bị khởi động lại và log `Restarted by the connection watchdog`;
    - bình thường (có subscription) không có dòng `net_wd` nào ngoài dòng lúc khởi động.
14. Nhiệt độ chip (endpoint 5, build với `PRESENCE_CHIP_TEMPERATURE`):
    - sau khi OTA, Home Assistant có thêm entity nhiệt độ; giá trị hợp lý (cao hơn nhiệt độ phòng) và đổi theo khi bo nóng lên hoặc nguội đi;
    - log `c3_temp: Chip temperature ... C`;
    - Hold time, các select và Occupancy vẫn ở endpoint 1–4;
    - Apple Home hiện thêm một cảm biến nhiệt độ.

    Người dùng báo với 1.0.143: entity có trong Home Assistant, đo được 80 °C khi bo để ngoài hộp và 96 °C trong hộp.
15. Wi-Fi modem sleep (1.0.144), trên bo đã đo bằng 1.0.143, cùng chỗ đặt:
    - nhiệt độ chip ngoài hộp và trong hộp, so với 80 °C / 96 °C của 1.0.143;
    - số lần *Unavailable* và sự kiện `disconnection` trong vài ngày, so với 1.0.143;
    - nhiệt độ của bo còn lại, cùng firmware.

    Người dùng báo với 1.0.144: 63 °C khi để ngoài hộp. Trong hộp và số lần *Unavailable* chưa có kết quả.
16. Bản mặc định không có nhiệt độ chip (1.0.147), OTA từ 1.0.144:
    - Home Assistant không còn entity nhiệt độ và *Identify (5)*, hoặc chúng hiện không khả dụng và xoá được;
    - Occupancy, Hold time, các select, *Identify* của endpoint 1 vẫn chạy;
    - log `mem`: heap còn trống nhiều hơn 1.0.144 một chút.
17. Bo `c3-wroom-devkit` (ESP32-C3-WROOM-02-N4):
    - log `Target esp32c3, board ESP32-C3-WROOM-02-N4` và `LED GPIO8 (WS2812)`;
    - LED WS2812: xanh dương nháy chậm khi chờ ghép nối, trắng nháy nhanh khi *Identify*, cyan / xanh lá / đỏ / tím khi học phòng trống và ghi ngưỡng mặc định;
    - radar trên GPIO3/GPIO4 và nút GPIO9 chạy như trên SuperMini.
18. Bo `c3-radar-r45` (mạch Ø45 mm), log qua pad TX0:
    - nạp lần đầu bằng file merged qua pad RX0 / TX0 (giữ IO9 với GND khi bấm RESET); bấm RESET lần nữa thì chạy;
    - log `board c3-radar r45` và `Pins: LD2410 RX GPIO10 (radar TX), TX GPIO3 (radar RX), button GPIO9`, không có LED;
    - radar được đọc (`LD2410 settings: ...`), hiện diện đúng; Range, Radar resolution, Radar Bluetooth đổi được từ Home Assistant;
    - chập IO9 với GND: bấm đúp mở ghép nối, 3 lần học phòng trống, giữ 5 s factory reset;
    - Wi-Fi ổn định khi radar nằm cạnh ESP trên cùng mạch (entity Wi-Fi RSSI, vài ngày).
19. Học phòng trống từ mức cao nhất (mọi bo), cắm USB / pad TX0 xem log:
    - Home Assistant hiện Model `Presence LD2410`, firmware `1.0.<n>`; Hold time giữ giá trị cũ khi OTA từ `esppresence`;
    - học (Learning trên Home Assistant hoặc bấm 3 lần): `Recording the empty room for 120 s`, không có `The LD2410 measures the empty room itself`; LED sáng 20 s, nháy 120 s, rồi sáng 10 s; select về Off;
    - bảng `Gate N ...: moving avg ... peak ... noise ... -> ...`: so peak với "Maximum" và avg với "Average" của HLKRadarTool trong cùng phòng (hai lần đo khác nhau nên chỉ gần giống); HLKRadarTool thấy đúng ngưỡng mới;
    - học lại ngay trong cùng phòng: ngưỡng gần như giữ nguyên, hoặc `The radar holds these thresholds already, nothing written`;
    - phòng trống vài giờ: không báo nhầm có người; ngồi yên ở xa: vẫn có người; ra khỏi phòng: hết người sau no-one duration + Hold time.

20. Bo `h2-mini` (ESP32-H2 mini), cắm USB xem log:
    - OTA từ bản H2 cũ (`matter_presence_h2_learn`, PID `0x8000`) lên `presence-h2-mini-<phiên bản>.ota`: giữ ghép nối, Hold time, lựa chọn Radar Bluetooth, các entity; số serial và tên thiết bị như cũ;
    - log `Target esp32h2, board ESP32-H2 mini, Matter over Thread` và `Pins: LD2410 RX GPIO11 (radar TX), TX GPIO12 (radar RX), button GPIO9, LED GPIO8 (WS2812)`;
    - mạng Thread: `id_diag` dòng `ot role=child/router`, `srp ... Registered`; chưa ghép nối thì có khối `thread_diag` mỗi 10 s, ghép xong thì hết;
    - bấm 4 lần: log `4 clicks, ignored`, không làm gì; *Factory thresholds* từ Home Assistant: đèn xanh lá 10 s;
    - học phòng trống (bấm 3 lần): cyan 20 s, cyan nháy 120 s, xanh lá 10 s; ngưỡng mới đúng như log `quiet_learn`;
    - console: `matter esp ot_cli state` qua UART0 (GPIO23/24);
    - log `mem`: heap thấp nhất lúc ghép nối không thấp hơn bản H2 cũ.

Cũng chưa kiểm chứng: trả lời của lệnh 0x001B trên radar thật, nhận biết Bluetooth tắt qua địa chỉ giả, Apple Home không hiện Mode Select, biến thể `unique_setup` trên ESP32-C3.

## Host test

```bash
matter-presence-sensor/firmware/test/host/run.sh   # cần g++ (C++17), không cần ESP-IDF
```

API ESP-IDF/FreeRTOS được thay bằng stub (`test/host/stubs/`); thời gian giả lập từng mili giây (`sim.h`); `radar_sim.cpp` giả lập LD2410: trả lời lệnh cấu hình, gửi báo cáo 100 ms một lần qua đúng driver của firmware, và tạo lỗi theo yêu cầu (lệnh bị bỏ lỡ, trả lời trễ, bị từ chối, không trả lời, khởi động lại không ACK, không giữ ngưỡng, radar cũ không có lệnh độ phân giải, Bluetooth hay đo nhiễu). Chạy một chương trình test với `-v` để in log giả lập.

## Cấu trúc thư mục

```
firmware/
├── CMakeLists.txt                  # phiên bản theo số commit, component esp-matter, esp32c3 hoặc esp32h2
├── sdkconfig.defaults              # chung mọi bo: phân vùng, BLE, Matter, học từ mức cao nhất, cluster bỏ đi
├── sdkconfig.defaults.esp32c3      # ESP32-C3: Wi-Fi, BLE thu gọn, log USB, PID 0x8001, thêm cluster bỏ đi
├── sdkconfig.defaults.esp32h2      # ESP32-H2: OpenThread FTD, CLI, console Matter, PID 0x8000
├── boards/                         # một file mỗi bo: tên, chân radar, LED, nút, cổng log
│   ├── c3-supermini.defaults
│   ├── c3-wroom-devkit.defaults
│   ├── c3-radar-r45.defaults
│   └── h2-mini.defaults
├── features/
│   ├── unique_setup.defaults       # mã ghép nối riêng từng bo (tuỳ chọn)
│   └── factory.defaults            # dữ liệu nhà máy từng bo (tuỳ chọn, chỉ ESP32-H2)
├── docs/h2-mini-wiring.png         # sơ đồ nối dây bo ESP32-H2 mini
├── tools/build.sh                  # build một bo (+ tính năng), chép file vào dist/
├── partitions.csv                  # 4 MB, 2 phân vùng OTA (giống hệt 1.0.132)
├── README.md
├── main/
│   ├── app_main.cpp                # Matter node, endpoint Occupancy Sensor, sự kiện, nút bấm, khởi động
│   ├── board.h                     # kiểm tra chân lúc build, tên bo trong log
│   ├── idf_component.yml           # espressif/led_strip (LED WS2812)
│   ├── presence.cpp                # task presence: radar -> Occupancy Sensing
│   ├── radar_control.cpp           # 4 Mode Select (Learn empty room, Range, Radar resolution, Radar Bluetooth)
│   ├── c3_temperature.cpp          # endpoint 5: nhiệt độ chip ESP32-C3 (tuỳ chọn, mặc định tắt)
│   ├── radar_worker.cpp            # task radar_cfg: yêu cầu, lựa chọn cuối cùng thắng, Bluetooth của radar (NVS)
│   ├── radar_settings.cpp          # một phiên thông số với radar, định dạng nhãn
│   ├── quiet_learn.cpp             # học phòng trống (ESP32 mức cao nhất / radar tự đo / ESP32 phân vị), ngưỡng mặc định
│   ├── device_info.cpp, device_name.cpp  # Basic Information, serial, tên thiết bị, NodeLabel, tên BLE
│   ├── setup_code.cpp              # in mã QR; mã ghép nối riêng từng bo (unique_setup)
│   ├── identity_diag.cpp           # log id_diag: danh tính, Wi-Fi hoặc Thread, IP, fabric, subscription
│   ├── thread_diag.cpp             # log thread_diag mỗi 10 s cho tới khi ghép nối (chỉ ESP32-H2)
│   ├── diag_report.cpp             # gửi lại Wi-Fi RSSI và Uptime (chỉ ESP32-C3)
│   ├── net_watchdog.cpp            # watchdog kết nối: DNS-SD, nối lại Wi-Fi, khởi động lại (chỉ ESP32-C3)
│   ├── mem_monitor.cpp             # log heap / stack
│   ├── chip_project_config.h       # tên DNS-SD trước khi ghép nối
│   ├── Kconfig.projbuild
│   └── drivers/
│       ├── ld2410.cpp              # radar LD2410 (UART1, 256000 baud): báo cáo, lệnh cấu hình
│       ├── status_led.cpp          # LED GPIO một màu, WS2812 hoặc không có LED
│       └── user_button.cpp         # nút BOOT: bấm 2/3/4 lần (4 lần: không làm gì trên ESP32-H2), giữ lâu
└── test/host/                      # host test (run.sh), giả lập thời gian và radar, stub ESP-IDF
```
