# Matter Presence Sensor + học phòng trống (ESP32-C3 SuperMini + HLK-LD2410C, Wi-Fi)

> **Nhánh thử nghiệm `claude/c3-wroom-esp-presence`.** Nhánh này thêm **bộ xử lý hiện diện trên ESP32** (mục **Bộ xử lý hiện diện trên ESP32**: ESP32 tự quyết định hiện diện từ năng lượng từng vùng, với hai ngưỡng lấy từ ngưỡng radar tự đo khi học phòng trống), cạnh **bộ lọc năng lượng trên ESP32** của nhánh `claude/c3-wroom-esp-filter`. Firmware build từ nhánh này mang phiên bản `1.0.<số commit>-esppresence` (biến thể bộ lọc: `-espfilter`); biến thể WROOM của bộ xử lý có Model `Presence LD2410 ESP engine`, để không lẫn với bản ổn định `1.0.x` của nhánh `claude/c3-matter-presence-rewrite`.

Cảm biến hiện diện chuẩn Matter (device type **Occupancy Sensor 0x0107**) với radar 24 GHz **HLK-LD2410C**, chạy **Matter over Wi-Fi** trên **ESP32-C3 SuperMini**. Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa... qua Wi-Fi nhà, không cần Thread Border Router. Radar LD2410 phát hiện được cả người ngồi yên (thở, cử động nhẹ), khác cảm biến PIR chỉ thấy chuyển động.

Đây là bản **viết lại từ đầu** của firmware ESP32-C3 1.0.132 (`ESP32-C3/matter-wifi-presence`, dùng chung mã với thư mục ESP32-H2). Project này **tự chứa**: không dùng file nào của thư mục ESP32-H2, mã chỉ cho ESP32-C3. Chức năng, thuật toán, ngưỡng, thời gian, endpoint 1–4 và khoá lưu trữ giữ nguyên như 1.0.132 (từ 1.0.144 bản này để Wi-Fi ở modem sleep cho chip bớt nóng; endpoint 5 báo nhiệt độ chip là tuỳ chọn, mặc định tắt từ 1.0.147), nên **cập nhật OTA từ 1.0.132 lên bản này giữ nguyên ghép nối và các entity Home Assistant** (xem **Mức độ kiểm tra**: điều này mới kiểm chứng bằng so sánh cấu hình và build, chưa thử trên bo).

Trong Home Assistant có thêm 5 điều khiển (Apple Home không hiện): **Hold time**, **Learn empty room** (học phòng trống, hoặc bấm nút BOOT 3 lần; *Factory thresholds* hoặc bấm 4 lần để ghi lại ngưỡng mặc định), **Radar Bluetooth**, **Radar resolution**, **Range**. Tên thiết bị là **Presence-** + 4 ký tự cuối của số serial, ví dụ **Presence-B2C4**. Các entity chẩn đoán **Reboot count**, **Uptime**, **Wi-Fi RSSI** bị Home Assistant tắt sẵn: bật chúng trong Home Assistant (xem **Entity chẩn đoán**); firmware gửi lại Uptime và Wi-Fi RSSI để giá trị không bị đứng. Entity **nhiệt độ chip ESP32-C3** (endpoint 5) chỉ có khi build với `PRESENCE_CHIP_TEMPERATURE` (mặc định tắt từ 1.0.147, xem **Nhiệt độ chip**).

## Khác biệt so với bản cũ (`ESP32-C3/matter-wifi-presence`, 1.0.132)

| | 1.0.132 (`matter-wifi-presence`) | Bản này (`matter-presence-ld2410`) |
|---|---|---|
| Mã nguồn | `main/`, bảng phân vùng, sdkconfig của thư mục ESP32-H2 (chung với bản Thread) | tự chứa, chỉ ESP32-C3 |
| Chức năng, thuật toán, log | — | giống hệt, trừ các dòng dưới (Wi-Fi modem sleep, watchdog kết nối, báo cáo Wi-Fi RSSI và Uptime) |
| Wi-Fi sau khi ghép nối (BLE đã tắt) | tắt modem sleep (`WIFI_PS_NONE`): radio luôn nghe, chip nóng tới 80–96 °C (đo trên bo bằng 1.0.143) | giữ modem sleep `min_modem`, mặc định của ESP-IDF, esp-matter và ESPHome trên ESP32: bo nóng ít hơn; gói từ controller tới bo trễ tối đa một chu kỳ DTIM (thường 100–300 ms). Bật `PRESENCE_WIFI_STAY_AWAKE` thì như cũ |
| Mất kết nối với controller | chờ controller hoặc Matter SDK tự thử lại (5–10 phút, thưa dần) | watchdog kết nối: không có subscription 5 phút thì thông báo lại DNS-SD, 10 phút nối lại Wi-Fi, 30 phút khởi động lại; task Matter treo 3 phút thì khởi động lại (`menuconfig`; đặt 0 thì như cũ) |
| Wi-Fi RSSI, Uptime trong Home Assistant | chỉ gửi lúc Home Assistant subscribe: RSSI đứng giá trị, Uptime lệch sau khi Home Assistant khởi động lại | gửi lại RSSI khi đổi từ 3 dB (kiểm tra mỗi phút), cả RSSI và uptime 10 phút một lần (`menuconfig`; đặt 0 thì như cũ) |
| VID / PID, endpoint, cluster, tên entity, mã mode | `0xFFF1` / `0x8001`, endpoint 1–4 | giống hệt; endpoint 5 (Temperature Sensor: nhiệt độ chip, cluster Temperature Measurement) chỉ có khi bật `PRESENCE_CHIP_TEMPERATURE` (mặc định tắt từ 1.0.147; bật ở 1.0.143–1.0.146) |
| Bảng phân vùng, khoá NVS/KVS | — | giống hệt |
| sdkconfig sinh ra | — | giống hệt, trừ: đường dẫn file, mục chọn loại LED không còn "không có", 6 tuỳ chọn mới (`BOARD_NAME`, `DIAG_REPORT_*`, `NET_WATCHDOG_*`, `PRESENCE_CHIP_TEMPERATURE` tắt), `PRESENCE_WIFI_STAY_AWAKE` tắt |
| LED | chọn được WS2812 / LED GPIO / không có | LED GPIO một màu (mặc định) hoặc WS2812 (từ 1.0.149, màu như 1.0.132); bỏ lựa chọn "không có" |
| Biến thể build | mặc định, `unique_setup`, `factory` | mặc định, `unique_setup`, `wroom_n4` (bo ESP32-C3-WROOM-02-N4, LED WS2812 GPIO8, tính năng như 1.0.144; từ 1.0.149). `factory` bỏ: PAI test chỉ cấp DAC cho PID `0x8000` |
| Mã Thread/OpenThread (`thread_diag`, phần OpenThread của `id_diag`, OT CLI) | có, tắt khi build C3 | bỏ |
| Tên file build | `matter_presence_c3_wifi*` | giống hệt |
| Kích thước (`idf.py size`, cùng Docker image) | 1 478 KB, RAM tĩnh 147,0 KB | 1 479 KB, RAM tĩnh 147,0 KB (lớn hơn 1 440 byte flash vì có thêm `diag_report`, `net_watchdog` và mục chọn loại LED, RAM tĩnh ít hơn 22 byte; mã tắt modem sleep, nhiệt độ chip và WS2812 không được build) |

Thay đổi cấu trúc mã (không đổi hành vi):
- `radar_control.cpp` tách thành phần Matter (`radar_control.cpp`: 4 Mode Select) và phần logic (`radar_worker.cpp`: task `radar_cfg`, các yêu cầu, "lựa chọn cuối cùng thắng", Bluetooth của radar), để chạy được test trên máy tính.
- Task presence chuyển từ `app_main.cpp` sang `presence.cpp`; log đổi tag từ `app_main` thành `presence`.
- Lệnh khởi động lại radar (0x00A3) không có ACK được driver coi là thành công (trước đây phía gọi tự xử lý); không còn log cảnh báo `No ACK for command 0x00A3`.
- `Learn empty room` từ chối mode ngoài 0/1/2 (`ESP_ERR_INVALID_ARG`), như các select khác.
- Yêu cầu học phòng trống và yêu cầu ngưỡng mặc định được đổi cùng nhau trong một critical section. Bản 1.0.132 đổi hai biến riêng lẻ: nếu bấm nút 3 lần đúng lúc Home Assistant gửi *Factory thresholds*, có thể không việc nào chạy và entity kẹt ở `Factory thresholds` cho tới lựa chọn sau (rất hiếm, nhưng có thể xảy ra). "Lựa chọn cuối cùng thắng" giữ nguyên.
- Bỏ mã chết: `identity_diag_request_dump()`, trường *no-one duration* trong yêu cầu đổi thông số (lệnh 0x0060 luôn ghi lại đúng giá trị radar đang giữ).

## Phần cứng

- **ESP32-C3 SuperMini** (flash 4 MB, CPU 160 MHz). Board ESP32-C3 khác cũng chạy được: nối theo số GPIO in trên mạch và chỉnh chân, LED trong `menuconfig`.
- Radar **HLK-LD2410C** (LD2410, LD2410B cũng được, kể cả firmware cũ không có lệnh 0x000B, 0x00AB, 0x00A5; nối theo chữ in trên mạch radar).

### Sơ đồ nối dây

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
- **Chân flash GPIO11–GPIO17** (GPIO11 là VDD_SPI) và **chân USB GPIO18/GPIO19**: build báo lỗi (`static_assert` trong `main/board.h`) nếu chọn các chân này cho radar, nút hoặc LED, hoặc khi hai chức năng trùng chân.
- **UART0 GPIO20 (RX), GPIO21 (TX)**: không dùng cho log, nhưng ROM vẫn in log khởi động ra GPIO21.
- Radar và Wi-Fi cùng lấy điện từ cổng USB: dùng nguồn USB khoẻ. Khi ESP32 khởi động lại bằng phần mềm (OTA, lỗi, factory reset), radar không mất điện và vẫn chạy tiếp; lần khởi động nào firmware cũng mở một phiên cấu hình với radar, nên radar bị bỏ lại ở chế độ cấu hình cũng gửi báo cáo trở lại.

### Đèn LED

LED của SuperMini chỉ có một màu, mọi màu ở cột cuối đều là "sáng". Với LED WS2812 (`STATUS_LED_TYPE_WS2812`, ví dụ biến thể `wroom_n4`), LED hiện đúng màu ở cột cuối, như bản ESP32-H2 và 1.0.132. Thứ tự ưu tiên: Identify, rồi học phòng trống / ngưỡng mặc định, rồi chế độ ghép nối.

| LED | Trạng thái | Màu (WS2812) |
|---|---|---|
| Nháy rất nhanh (5 lần mỗi giây) | Ứng dụng yêu cầu *Identify* | trắng |
| Sáng liên tục 20 s | Vừa bắt đầu học phòng trống: rời khỏi phòng | cyan |
| Nháy chậm (1 s tắt, 1 s sáng) trong 60 s | Đang ghi năng lượng nền của phòng trống | cyan nháy |
| Sáng liên tục 10 s | Học xong **hoặc** học thất bại, hoặc ghi ngưỡng mặc định thất bại; xem log `quiet_learn` | xanh lá / đỏ |
| Nháy nhanh (0,2 s sáng, 0,2 s tắt) trong 10 s | Đã ghi ngưỡng mặc định của radar | tím |
| Nháy chậm (1 s sáng, 1 s tắt) | Chế độ ghép nối: đang chờ thêm vào ứng dụng Matter | xanh dương |
| Tắt | Hoạt động bình thường (đã ghép nối), hoặc chế độ ghép nối đã hết giờ | |

Huỷ việc học thì LED trở lại bình thường ngay. Đặt lại cùng một kết quả (ví dụ thất bại lần hai) thì tính lại 10 s. Trạng thái radar (có người, mất tín hiệu) không hiện trên LED. LED trên board khác sáng ngược: `menuconfig` → *Presence Sensor Configuration* → bật *Status LED is on when the pin is high*.

### Nút BOOT (GPIO9)

| Thao tác | Kết quả |
|---|---|
| **Bấm đúp** | Mở **chế độ ghép nối** 15 phút. Chưa ghép nối: thêm lại khi cửa sổ lúc khởi động đã hết giờ. Đã ghép nối: thêm vào ứng dụng thứ hai (Multi-Admin), chỉ qua mạng vì BLE đã tắt |
| **Bấm 3 lần** | **Học phòng trống**, như chọn *Learning* trong Home Assistant |
| **Bấm 4 lần** | **Ngưỡng mặc định** của radar, như chọn *Factory thresholds* (huỷ việc học đang chạy nếu có) |
| Bấm 5 lần trở lên | Không làm gì (log `5 clicks, ignored`) |
| **Giữ 5 giây** | **Factory reset**: xoá thông tin Wi-Fi và ghép nối Matter, khởi động lại; sau đó LED nháy chờ ghép nối. Giữ được 1 s thì log nhắc |

Một lần bấm dài tối đa 0,5 s; các lần bấm cách nhau tối đa 0,4 s, nên bấm đúp có tác dụng khoảng 0,4 s sau khi nhả nút (để phân biệt với bấm 3 lần). Một lần giữ quá 0,5 s huỷ các lần bấm trước nó. Nút được đọc 20 ms một lần, chống dội 40 ms.

## Bo ESP32-C3-WROOM-02-N4 (biến thể `wroom_n4`, LED WS2812 GPIO8)

Từ 1.0.149 có biến thể build cho bo dùng module **ESP32-C3-WROOM-02-N4** (flash 4 MB) với LED RGB **WS2812** nối GPIO8, như ESP32-C3-DevKitC-02 (file `sdkconfig.defaults.wroom_n4`).
- **Tính năng như bản 1.0.144:** có entity nhiệt độ chip (endpoint 5, cả *Identify (5)*), Wi-Fi ở modem sleep.
- **Khác bản mặc định chỉ ở cấu hình**, mã nguồn chung:
  - `BOARD_NAME` = `ESP32-C3-WROOM-02-N4` (log khởi động);
  - LED `STATUS_LED_TYPE_WS2812` trên GPIO8, độ sáng 20/255 (`STATUS_LED_BRIGHTNESS`);
  - `PRESENCE_CHIP_TEMPERATURE` bật, giới hạn endpoint động 6.
- **Các chân khác giữ như SuperMini:** radar TX → GPIO3, radar RX → GPIO4, nút GPIO9 (BOOT). Kiểm tra bo có đưa các chân này ra không.
- **Log** ra cổng USB-Serial-JTAG (GPIO18/GPIO19) như SuperMini. Nếu cổng USB của bo đi qua chip USB-UART (CP2102, CH340...), vẫn nạp được nhưng không thấy log; khi đó cần build với console UART0.
- **OTA:** file `.ota` của biến thể này cùng VID/PID và cùng dãy số phiên bản với bản mặc định, Matter Server không phân biệt được. Chỉ cập nhật bo WROOM bằng file của biến thể `wroom_n4`. Nạp nhầm thì bo vẫn chạy, nhưng LED sai và entity nhiệt độ xuất hiện hoặc mất.
- Chưa thử trên bo.

```bash
idf.py -B build-wroom -D IDF_TARGET=esp32c3 -D SDKCONFIG=sdkconfig.wroom \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.wroom_n4" build
```

WS2812 trên SuperMini: chọn *Status LED* → *Addressable WS2812 RGB LED* trong `menuconfig`. Dây tín hiệu WS2812 nằm ở mức thấp khi rảnh, nên nếu dùng GPIO8 thì LED xanh có sẵn của SuperMini (sáng khi GPIO8 ở mức thấp) sẽ sáng mãi; dùng chân khác cho WS2812 thì tốt hơn.

## Bộ lọc năng lượng trên ESP32 (thử nghiệm, `PRESENCE_ENERGY_FILTER`)

**Vấn đề:** radar thỉnh thoảng báo có người khi phòng trống, do một xung nhỏ vượt ngưỡng. LD2410 báo có người ngay khi **một** báo cáo (khoảng 100 ms) có năng lượng vượt ngưỡng ở một gate. Sau đó radar tự giữ trạng thái đó thêm **no-one duration** (5 s). Cộng Hold time, một xung nhiễu làm phòng "có người" khoảng 35 s.

**Cách bộ lọc làm:**
- ESP giữ radar ở **engineering mode**: mỗi báo cáo có năng lượng di chuyển và đứng yên của 9 gate.
- ESP so năng lượng với **ngưỡng đang lưu trong radar** (đọc bằng lệnh 0x0061, đọc lại sau mỗi lần học phòng trống hoặc ghi ngưỡng mặc định) và với **Range**.
- Gate đứng yên chỉ tính từ gate 2, như radar (ngưỡng đứng yên của gate 0 và 1 không đặt được).
- **Khi phòng đang trống**, một báo cáo được tính là "vượt" khi radar báo có người **và** có gate vượt ngưỡng:
  - vượt ngưỡng **từ 15 trở lên**: báo có người **ngay**, nhanh như không có bộ lọc;
  - vượt ít hơn: phải có **3 báo cáo vượt liên tiếp** (khoảng 0,3 s). Một báo cáo không vượt thì đếm lại từ đầu.
- Bộ lọc đếm theo **năng lượng của từng báo cáo**, không theo cờ "có người" của radar, vì radar còn giữ cờ đó 5 s sau một xung nhiễu.
- **Khi đã có người:** radar tự quyết định như khi không có bộ lọc (giữ có người khi radar còn thấy, cộng no-one duration và Hold time).
- **Bộ lọc chỉ bỏ bớt, không bao giờ báo có người khi radar không báo.** Không ghi gì vào radar.
- **Không có dữ liệu năng lượng** (engineering mode vừa mất do radar khởi động lại, hoặc ngay sau khi học phòng trống trên ESP32): radar tự quyết định. Task `radar_cfg` kiểm tra mỗi 2 s và bật lại engineering mode, tối đa 10 s một lần.

**Log** (tag `presence`, cần cắm USB):
```
I presence: Energy filter: ignored 1 report above a threshold (highest moving gate 3 energy 35, threshold 30, +5)
I presence: Energy filter: moving gate 3 energy 70, threshold 30 (+40): occupied at once
I presence: Energy filter: 3 reports in a row above a threshold (highest still gate 4 energy 36, threshold 30, +6): occupied
I radar_ctl: Engineering mode on: the energy of every gate for the energy filter
```

**Tuỳ chọn** (`menuconfig` → *ESP32 Energy Filter (experimental)*, không thêm entity nào):

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `PRESENCE_ENERGY_FILTER` | tắt (bật trong biến thể `espfilter`) | Bật bộ lọc |
| `PRESENCE_ENERGY_FILTER_STRONG` | 15 | Vượt ngưỡng từng này trở lên thì báo có người ngay |
| `PRESENCE_ENERGY_FILTER_FRAMES` | 3 | Số báo cáo vượt liên tiếp cần cho xung yếu |

**Biến thể `espfilter`** cho bo ESP32-C3-WROOM-02-N4: gồm mọi thứ của `wroom_n4` (LED WS2812 GPIO8, nhiệt độ chip), cộng bộ lọc và Model `Presence LD2410 ESP filter`.

```bash
idf.py -B build-espfilter -D IDF_TARGET=esp32c3 -D SDKCONFIG=sdkconfig.espfilter \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.wroom_n4;sdkconfig.defaults.espfilter" build
```

**Phiên bản và tên:**
- Phiên bản hiện trong Home Assistant là `1.0.<số commit>-espfilter`. Số dùng cho OTA vẫn là số commit.
- OTA từ bản ổn định lên bản này được, vì số commit lớn hơn.
- Muốn quay về bản ổn định bằng OTA thì bản ổn định phải có số commit lớn hơn bản này. Nếu không, nạp USB.
- Hai dòng firmware cùng VID/PID nên Matter Server không phân biệt được; chỉ cập nhật bo WROOM bằng file `-espfilter`.

**Quay lại bản ổn định:** radar không bị ghi gì (engineering mode mất khi radar khởi động lại), nên chỉ cần nạp lại firmware ổn định `wroom_n4`:
- OTA bản ổn định có số commit lớn hơn bản `-espfilter` đang chạy: giữ ghép nối;
- hoặc nạp USB bằng `idf.py flash` từ bản build ổn định (ghi cả `otadata`): giữ ghép nối;
- file merged ghi đè vùng NVS: phải ghép nối lại.

**Mức độ kiểm chứng:**
- Thuật toán, việc dùng bộ lọc trong task presence, và việc bật lại engineering mode, đọc lại ngưỡng trong task `radar_cfg` đều có host test (radar giả lập).
- Build biến thể `espfilter` 1.0.151: **0 lỗi, 0 cảnh báo**. `sdkconfig` chỉ khác `wroom_n4` của bản ổn định (1.0.150) ở Model và 3 tuỳ chọn của bộ lọc: cùng VID/PID, cluster, endpoint, Wi-Fi. Header file OTA: VID `0xFFF1`, PID `0x8001`, phiên bản 151 / `1.0.151-espfilter`.
- Build lại trên nhánh `claude/c3-wroom-esp-presence` (1.0.156, 1.0.159): **0 cảnh báo**, `sdkconfig` vẫn chỉ khác `wroom_n4` ở Model và 3 tuỳ chọn đó. Mã của bộ lọc không đổi; driver radar thêm bộ đếm khung hỏng, log `mem` có bộ đệm dài hơn.
- **Chưa thử trên bo:** việc đọc năng lượng từ radar thật, các log, và việc bộ lọc có thật sự chặn được xung nhiễu (mục **Mức độ kiểm tra**, bước 18).
- Ngưỡng 15 và 3 báo cáo là giá trị ban đầu, **cần chỉnh theo log trên bo thật**.

## Bộ xử lý hiện diện trên ESP32 (thử nghiệm, `PRESENCE_ESP_ENGINE`)

**Khác với bản ổn định và bộ lọc năng lượng:**
- ESP32 tự quyết định có người hay không, từ năng lượng radar gửi lên ở engineering mode: năng lượng chuyển động và năng lượng tĩnh của 9 vùng khoảng cách (gate), mỗi báo cáo khoảng 100 ms.
- Cờ "có người" của radar không được dùng.
- **Ngưỡng là ngưỡng lưu trong radar.** Radar tự đo khi học phòng trống, như bản ổn định; cũng chỉnh được bằng app HLKRadarTool.
- ESP32 đọc lại ngưỡng sau mỗi lần học hay ghi ngưỡng mặc định, rồi dùng cho cơ chế hai ngưỡng.
- **Từ 1.0.162, tín hiệu mạnh phải vượt cả nhiễu phòng trống của chính vùng đó.** Vùng có nhiễu chạm ngưỡng radar (ngưỡng xuất xưởng, hay ngưỡng hạ bằng app) không tự kích hoạt được nữa. Học xong, ngưỡng radar ở các vùng đó được nâng lên trên nhiễu và ghi vào radar.

(Bản 1.0.153–1.0.156 tự học ngưỡng riêng trên ESP32 và không dùng ngưỡng radar; từ 1.0.159 radar đo lại như trước.)

### Thuật toán

1. **Lọc xung (trung vị 3 báo cáo).**
   - Mỗi kênh lấy trung vị của 3 báo cáo gần nhất trước khi so ngưỡng.
   - Một báo cáo đứng riêng, dù mạnh đến đâu, bị loại; phải có 2 trong 3 báo cáo liên tiếp cùng cao.
   - Đổi lại chậm thêm một báo cáo (0,1 s).
2. **Hai ngưỡng cho từng kênh** (18 kênh: 9 vùng × chuyển động/tĩnh), tính từ ngưỡng radar T và nhiễu phòng trống N của kênh đó:
   - **kích hoạt (mạnh):** năng lượng đã lọc **vượt T** (lớn hơn T), đúng như radar tự tính có người, **và đạt N + 5**. Vùng có nhiễu chạm ngưỡng radar thì ngưỡng kích hoạt nâng lên N + 5. T = 100: radar bỏ vùng đó, ESP32 cũng bỏ;
   - **duy trì:** năng lượng đã lọc **đạt T − 40 % của T, bớt tối đa 10**. Ví dụ T = 20 → 12, T = 15 → 9, T = 50 → 40;
   - ngưỡng duy trì **không thấp hơn N + 2**, và luôn thấp hơn ngưỡng kích hoạt;
   - **nhiễu phòng trống N** của một kênh là số lớn hơn trong hai số: phân vị 99, và trung vị + 3 × độ dao động (1,4826 × MAD). ESP32 đo nó lúc học (mục dưới);
   - N từ 96 trở lên: kênh đó không kích hoạt được; từ 99: cũng không duy trì được (không phân biệt được với người);
   - chưa có số đo nhiễu (chưa học bằng firmware 1.0.159 trở lên, hoặc học với độ phân giải khác): kích hoạt khi vượt T, ngưỡng duy trì = T, không kiểm nhiễu;
   - năng lượng tĩnh của vùng 0 và 1 không được tính: radar không có ngưỡng tĩnh ở đó.
3. **Phòng trống → có người:**
   - một vùng "mạnh" khi năng lượng đã lọc đạt ngưỡng kích hoạt;
   - cần **3 trong 4 báo cáo gần nhất** mạnh ở cùng chỗ: một vùng hoặc vùng kề (người đi qua ranh giới vùng, còn nhiễu thì nhảy lung tung);
   - người bước vào được nhận sau khoảng **0,4 s**; xung lẻ hoặc 2 báo cáo liền nhau không kích hoạt được.
4. **Duy trì:**
   - khi đã có người, một vùng có **tín hiệu duy trì** khi năng lượng đã lọc đạt ngưỡng duy trì trong **3 trong 10 báo cáo** gần nhất (nhịp thở, cử động nhẹ);
   - mỗi báo cáo có tín hiệu duy trì đặt lại bộ đếm.
5. **Tắt hiện diện:**
   - hết tín hiệu duy trì liên tục **Hold time** (60 s trong biến thể này) thì phòng trống, quay về chờ tín hiệu mạnh;
   - tín hiệu duy trì quay lại trong lúc đếm thì bộ đếm 60 s bắt đầu lại;
   - Hold time là entity *Hold time* trên Home Assistant (1–300 s): đổi ở đó là đổi bộ đếm.
6. **Mất dữ liệu không phải phòng trống:**
   - không có báo cáo hợp lệ, hoặc báo cáo không có năng lượng, quá 2 s là **lỗi cảm biến**;
   - trạng thái giữ nguyên (đang có người thì vẫn báo có người), bộ đếm tạm dừng; khi dữ liệu trở lại, bộ đếm chạy lại từ đầu;
   - báo cáo hỏng (sai đuôi, năng lượng trên 100) bị bỏ và được đếm.

**Vì sao ngưỡng duy trì phải ở trên nhiễu:**
- Radar tự đo thường đặt ngưỡng sát nhiễu.
- Hạ ngưỡng duy trì một khoảng cố định dưới ngưỡng đó thì nhiễu có thể chạm ngưỡng duy trì liên tục, và phòng không bao giờ trống.
- Mô phỏng (mô hình nhiễu tự đặt, không phải dữ liệu radar thật), với ngưỡng kiểu radar tự đo (phân vị 99 + 5) và hạ 40 %:
  - hạ mà không biết nhiễu: sau 10 phút phòng vẫn chưa trống, ở mọi lần thử. Vì vậy firmware không hạ khi chưa đo nhiễu;
  - hạ nhưng không dưới nhiễu đã đo: phòng trống sau 61 s.

**Vì sao ngưỡng kích hoạt cũng phải ở trên nhiễu (1.0.162):**
- Báo nhầm thực tế (ảnh HLKRadarTool, radar còn ngưỡng xuất xưởng): năng lượng tĩnh vùng 2 (1,5–2,25 m) của phòng trống trung bình 56, cao nhất 63, trong khi ngưỡng radar là 40. Vùng 3: trung bình 30, cao nhất 34, ngưỡng 40.
- Nhiễu đó nằm trên ngưỡng radar gần như liên tục, nên bản 1.0.159 coi vùng 2 là "mạnh" dù không có ai.
- **Tăng số báo cáo** (3 trong 4 → nhiều hơn) chỉ chặn được xung ngắn: nhiễu nằm trên ngưỡng liên tục thì báo cáo nào cũng mạnh.
- **Cộng một khoảng cho mọi vùng** phải cộng ít nhất 23 mới qua 63, làm các vùng khác kém nhạy hẳn.
- Vì vậy ngưỡng kích hoạt của **từng kênh** phải ở trên nhiễu đo được của chính kênh đó (N + 5). Kênh có nhiễu thấp hơn ngưỡng radar không đổi gì.
- Mô phỏng Monte Carlo, phòng mô phỏng theo số trong ảnh (mô hình nhiễu tự đặt, **không phải dữ liệu radar thật**), ngưỡng xuất xưởng, 100 phòng × 1 giờ trống:
  - bộ xử lý 1.0.159: báo nhầm có người ở cả 100 phòng, và phòng không bao giờ trống lại;
  - bộ xử lý 1.0.162: 0 lần báo nhầm, cả trước và sau khi học nâng ngưỡng radar. Người đi vào vẫn được nhận ở cả 100 lần; ra khỏi phòng thì trống sau 61 s; người ngồi yên ở 3,75–6,75 m được giữ 100 % của 20 phút;
  - người ngồi yên ngay ở vùng 2 (giả định năng lượng tĩnh 75 ± 6, chưa đo thật): được giữ 100 %. Người ở vùng 2 mà năng lượng tĩnh chỉ ngang nhiễu (dưới N + 2) thì vùng 2 không giữ được, chỉ các vùng khác giữ;
  - phòng mô hình cũ (nhiễu thấp hơn ngưỡng radar): kết quả 1.0.162 trùng hệt 1.0.159.

**Thời gian và tài nguyên:**
- Đồng hồ là mili-giây 32 bit, quay vòng sau 49,7 ngày; firmware chỉ dùng hiệu thời gian nên không bị ảnh hưởng (có test).
- Mỗi báo cáo chỉ vài trăm phép tính, không chờ ai. Task đọc UART đưa báo cáo vào hàng đợi (đầy thì bỏ và đếm); task `presence_eng` xử lý.
- Flash của ESP32 chỉ được ghi khi học thành công (nhiễu, 40 byte). Flash của radar chỉ được ghi khi học (thêm một lần ghi nếu có ngưỡng được nâng lên trên nhiễu) hay ghi ngưỡng mặc định; không bao giờ tự ghi lúc đang chạy.

### Học phòng trống

**Cách bắt đầu:** chọn **Learning** trên Home Assistant, bấm BOOT 3 lần, hoặc lệnh `LEARN_EMPTY` trên console. Việc học như bản ổn định (mục **Học phòng trống**):
- radar tự đo và lưu ngưỡng, xem được trong app HLKRadarTool;
- radar firmware cũ không có lệnh tự đo thì ESP32 đo và ghi ngưỡng vào radar.
- Chưa rõ radar có thật sự đổi ngưỡng sau lệnh tự đo 0x000B không: báo lỗi cho thấy học xong ngưỡng vẫn là ngưỡng xuất xưởng (giả thuyết: radar chỉ đo nhiễu). Log `Thresholds set by the radar` sau mỗi lần học cho biết; bước nâng ngưỡng bên dưới không phụ thuộc vào việc đó.

Biến thể này thêm hai bước: **ESP32 ghi nhiễu phòng trống 30 s trước khi radar đo**, và sau khi radar đo, **nâng ngưỡng radar ở các vùng mà nhiễu chạm tới**.

| Thời điểm | Việc | LED |
|---|---|---|
| 0–20 s | rời khỏi phòng | cyan sáng |
| 20–50 s | ESP32 ghi nhiễu (engineering mode) | cyan nháy |
| 50–60 s | ESP32 tắt engineering mode, gửi lệnh tự đo 0x000B; radar tự chờ 10 s | cyan nháy |
| 60–120 s | radar đo và lưu ngưỡng | cyan nháy |
| khoảng 120 s | ESP32 lưu nhiễu vào NVS, nâng ngưỡng radar mà nhiễu chạm tới và ghi vào radar (nếu có), đọc lại ngưỡng, bật lại engineering mode | xanh lá 10 s (đỏ: thất bại) |

- **Ở ngoài phòng đến khi LED thôi nháy** (khoảng 2 phút). Select *Learn empty room* hiện *Learning* trong lúc đó rồi về *Off*.
- **Nâng ngưỡng radar trên nhiễu:** ngưỡng một kênh nhỏ hơn N + 4 thì được nâng lên N + 4 (ESP32 kích hoạt từ N + 5), tối đa ngưỡng xuất xưởng + 30 (`LD2410_LEARN_MAX_RAISE`) và 99. Ngưỡng cao hơn không bao giờ bị hạ; năng lượng tĩnh vùng 0–1 không chỉnh được nên không đụng tới.
  - log: `Gate 2 still: empty room noise 65, radar threshold 40 -> 69`, rồi `Thresholds raised above the noise recorded by the ESP32: moving ... | still ...` và `Raised thresholds saved in the LD2410`; HLKRadarTool thấy ngưỡng mới;
  - không vùng nào cần nâng thì không ghi gì thêm vào radar;
  - nhiễu cao hơn ngưỡng xuất xưởng + 30 (thường do có người hay vật chuyển động lúc học): ngưỡng radar chỉ nâng tới đó, ESP32 vẫn chỉ kích hoạt từ N + 5 ở vùng đó, log cảnh báo `raised at most to factory ...`.
- **Thất bại** (LED đỏ):
  - ít hơn 5 báo cáo có năng lượng mỗi giây lúc ESP32 ghi nhiễu (ngưỡng radar và nhiễu đã đo đều giữ nguyên);
  - hoặc radar đo thất bại (các trường hợp ở mục **Học phòng trống**; ngưỡng radar và nhiễu giữ nguyên);
  - hoặc radar không giữ ngưỡng được nâng (`Gate N reads back ...`): nhiễu mới đã lưu và được dùng; radar có thể giữ lẫn ngưỡng cũ và mới, ESP32 đọc lại và dùng đúng ngưỡng radar đang giữ.
- **Radar firmware cũ** (không có lệnh 0x000B): sau 30 s ghi nhiễu, ESP32 ghi tiếp 60 s, tự tính và ghi ngưỡng vào radar; nhiễu lấy từ lần ghi 60 s đó.
- **Factory thresholds** (Home Assistant, bấm 4 lần, hoặc `FACTORY_THRESHOLDS YES`): ghi ngưỡng mặc định vào radar; nhiễu đã đo giữ nguyên.
- **Trong lúc học:**
  - lúc radar tự đo thì không có năng lượng (lỗi cảm biến, không phải phòng trống), nên Home Assistant giữ trạng thái trước khi học;
  - học xong mà không có ai thì hết người sau Hold time.
- **Chỉnh ngưỡng từng vùng bằng app HLKRadarTool** (bật *Radar Bluetooth* trên Home Assistant trước). ESP32 đọc lại ngưỡng khi:
  - bật hoặc tắt *Radar Bluetooth*;
  - đổi Range hoặc độ phân giải;
  - học xong, ghi ngưỡng mặc định xong;
  - khởi động.
- **Chưa học bằng firmware 1.0.159 trở lên:** log nhắc học phòng trống; kích hoạt khi vượt ngưỡng radar và ngưỡng duy trì = ngưỡng radar, không kiểm nhiễu. Radar còn giữ ngưỡng xuất xưởng thì log cũng nhắc.

### Console

Console ESP-IDF (`esp_console`) trên cổng log: USB-Serial-JTAG (mặc định). Lệnh chỉ xếp hàng yêu cầu, câu trả lời in ra log; gõ `help` để xem danh sách.

**Mở console:**
1. Cắm cổng USB của bo (cổng dùng để nạp) vào máy tính.
2. Mở terminal nối tiếp:
   - có ESP-IDF: `idf.py -p COM5 monitor` (Linux `/dev/ttyACM0`, macOS `/dev/cu.usbmodem*`); thoát bằng `Ctrl+]`;
   - không có ESP-IDF: phần mềm terminal nối tiếp bất kỳ, ví dụ PuTTY (*Serial*, cổng `COMx`) hay *Serial Monitor* của Arduino IDE. Tốc độ baud không quan trọng với USB-Serial-JTAG (chọn 115200).
3. Bấm Enter để thấy dấu nhắc `presence>`, gõ lệnh (ví dụ `DIAG_STREAM on`) rồi Enter. Chữ thường cũng được (`diag_stream on`). Log khác vẫn in xen giữa, lệnh vẫn được nhận.

Mở cổng làm bo khởi động lại ở một số phần mềm: không sao, console chạy sau khi khởi động. Chỉ thấy vài dòng `ESP-ROM:esp32c3...` lúc bấm RESET rồi im: cổng USB của bo đi qua chip USB-UART (CP2102, CH340...), không ra console của bản này (mục **Chân UART và phạm vi**).

| Lệnh | Việc làm |
|---|---|
| `LEARN_EMPTY` | Học phòng trống, như *Learning* trên Home Assistant |
| `LEARN_CANCEL` | Huỷ việc học, giữ ngưỡng cũ |
| `STATUS` | Trạng thái hiện diện, thời gian còn lại trước khi tắt, cảm biến, nguồn ngưỡng (radar, xuất xưởng), nhiễu đã đo, Range |
| `DIAG` | Bảng 9 vùng: năng lượng hiện tại, đã lọc, cao nhất trong 1 s, ngưỡng duy trì, ngưỡng kích hoạt và ngưỡng radar, của chuyển động và tĩnh |
| `DIAG_STREAM on` / `off` | In `DIAG` mỗi giây (để hiệu chỉnh) |
| `THRESHOLDS` | Ngưỡng radar, ngưỡng kích hoạt, ngưỡng duy trì và nhiễu đo được của từng vùng |
| `FACTORY_THRESHOLDS YES` | Ghi ngưỡng mặc định vào radar, như *Factory thresholds* trên Home Assistant |

**Bảng `DIAG`**, mỗi vùng một dòng, cho năng lượng chuyển động (*moving*) và tĩnh (*still*):

| Cột | Nghĩa |
|---|---|
| `now` | năng lượng trong báo cáo mới nhất |
| `filt` | sau lọc trung vị 3 báo cáo: số này được so với ngưỡng |
| `peak` | cao nhất trong khoảng 1 s gần nhất |
| `hold` | ngưỡng duy trì: `filt` đạt nó thì giữ người (`never`: kênh không giữ được) |
| `strong` | ngưỡng kích hoạt: `filt` đạt nó (3 trong 4 báo cáo) thì phòng trống thành có người. Bằng `radar` + 1, hoặc nhiễu + 5 nếu cao hơn (`never`: kênh không kích hoạt được) |
| `radar` | ngưỡng lưu trong radar |
| `(not counted)` | vùng ngoài Range, vùng 0–1 của năng lượng tĩnh, hoặc vùng radar bỏ (ngưỡng 100) |

**Bảng ngưỡng** (khi khởi động, khi ngưỡng đổi, lệnh `THRESHOLDS`):

```
I esp_presence: Thresholds from the radar (75 cm gates): strong from radar + 1 and at least the empty room noise + 5; hold at radar - 40 % (at most 10), at least the noise + 2
I esp_presence:   gate 0 0-0.75 m    moving: radar 50 strong 51 hold 44 (noise 42, hold raised) | still: no radar threshold
I esp_presence:   gate 2 1.5-2.25 m  moving: radar 40 strong 41 hold 30 (noise 21) | still: radar 69 strong 70 hold 67 (noise 65, hold raised)
I esp_presence:   gate 7 5.25-6 m    moving: radar 15 strong 16 hold 10 (noise 8, hold raised) | still: radar 20 strong 21 hold 12 (noise 7)
```
Cùng phòng đó trước khi học lại (vùng 2 còn ngưỡng radar 40): `still: radar 40 strong 70 hold 67 (noise 65, strong and hold raised)`, kèm cảnh báo `Gate 2 still: the empty room noise 65 reaches the radar threshold 40, strong there only from 70. Learn the empty room again ...`.
(số minh hoạ từ test trên máy tính, không phải từ radar thật)

**Log tự động:**
- khi có người (`Present: moving gate 3 energy 70, strong from 31 (radar threshold 30), 3 of the last 4 reports strong`): vùng, loại năng lượng và ngưỡng đã kích hoạt;
- khi phòng trống (`Empty: no hold signal for 60 s ...`);
- khi lỗi cảm biến;
- `STATUS` mỗi 5 phút (`PRESENCE_ENGINE_STATUS_LOG_SEC`).

Log của việc học có tag `quiet_learn`, như bản ổn định.

Console chạy trong task `console_repl` của ESP-IDF. Không cắm máy tính (ví dụ cấp nguồn bằng sạc USB): log và dấu nhắc bị bỏ ngay, không chờ; task chỉ thử đọc lại mỗi 10 ms (mã ESP-IDF 6.0), không làm chậm task khác. Không cần console thì tắt `PRESENCE_ENGINE_CONSOLE`; học phòng trống vẫn chạy được từ Home Assistant hoặc nút BOOT.

### Chân UART và phạm vi

- **Chân UART của radar:** `menuconfig` → *Presence Sensor Configuration* → `LD2410_UART_RX_PIN` (nối chân TX của radar, mặc định GPIO3) và `LD2410_UART_TX_PIN` (nối chân RX của radar, mặc định GPIO4), 256000 baud.
  - Không dùng GPIO11–17 (flash) và GPIO18/19 (USB).
  - Tránh GPIO2, GPIO8, GPIO9 (chân strapping).
  - Build lại sau khi đổi.
- **Phạm vi:**
  - select **Range** trên Home Assistant (vùng xa nhất, ghi vào radar khi chọn);
  - `PRESENCE_MAX_DISTANCE_CM` bỏ các vùng bắt đầu xa hơn khoảng cách đó (tính ở ESP32).
  - Vùng ngoài phạm vi không được tính. Nhiễu được đo ở mọi vùng, nên mở rộng Range sau đó không cần học lại.
- **Console:** mặc định qua USB-Serial-JTAG (GPIO18/19). Bo cắm USB qua chip CH340/CP2102 (UART0) thì đặt `ESP_CONSOLE_UART_DEFAULT` trong menuconfig rồi build lại: log và lệnh chuyển sang UART0.

### Hiệu chỉnh thực tế

1. Lắp radar cố định ở chỗ dùng thật, chọn **Range** vừa đủ phòng (bớt vùng sau tường), rồi **học phòng trống** (ở ngoài đến khi LED thôi nháy).
2. Mở console, gõ `THRESHOLDS`: xem ngưỡng radar, ngưỡng duy trì và nhiễu từng vùng.
   - Cảnh báo `the empty room noise N reaches the radar threshold T, strong there only from S`: vùng đó nhiễu chạm ngưỡng radar; ESP32 đã chỉ kích hoạt từ S. Học lại để ngưỡng radar cũng được nâng.
   - `strong and hold raised`: như trên. `hold raised`: vùng đó nhiễu nên ngưỡng duy trì không hạ được nhiều.
3. `DIAG_STREAM on`, rồi lần lượt:
   - **đi vào:** cột `filt` của vùng có người phải vượt `radar` trong vài báo cáo (log `Present: ...` sau khoảng 0,4 s);
   - **ngồi yên ở chỗ xa nhất cần nhận** (ví dụ 4–6 m): cột `filt` của năng lượng tĩnh ở vùng đó phải thường xuyên đạt `hold`. `STATUS` phải báo `present, hold signal at gate ...`;
   - **ra khỏi phòng:** khoảng Hold time + 1 s sau, log `Empty: ...`.
4. **Ngồi yên mà bị báo trống** (`filt` thấp hơn `hold`):
   - hạ ngưỡng vùng đó trong HLKRadarTool (ngưỡng duy trì hạ theo), rồi tắt *Radar Bluetooth* để ESP32 đọc lại;
   - hoặc tăng `PRESENCE_ENGINE_HOLD_DROP_PERCENT` (40 → 50) hay `PRESENCE_ENGINE_HOLD_DROP_MAX` (10 → 15);
   - hoặc tăng Hold time trên Home Assistant;
   - nếu cột `hold` bị nâng lên vì nhiễu: học lại khi phòng thật sự trống (người trong phòng lúc học làm nhiễu vùng đó cao), hoặc dời radar.
5. **Phòng trống mà bị báo có người** (log `Present: ...` không có ai): xem vùng, năng lượng và ngưỡng `strong from` trong log, rồi:
   - học lại khi nguồn nhiễu chạy như bình thường (quạt, máy lạnh bật như lúc dùng): nhiễu được đo lại, ngưỡng radar ở vùng nhiễu được nâng;
   - hoặc tăng ngưỡng vùng đó trong HLKRadarTool (rồi tắt *Radar Bluetooth* để ESP32 đọc lại);
   - hoặc tăng `PRESENCE_ENGINE_TRIGGER_NOISE_MARGIN` (5 → 8): mọi vùng có nhiễu gần ngưỡng kích hoạt cách nhiễu xa hơn;
   - hoặc `PRESENCE_ENGINE_TRIGGER_MARGIN` (0 → 3–5): mọi vùng kém nhạy đi chừng đó;
   - hoặc tăng `PRESENCE_ENGINE_TRIGGER_REPORTS` / `_TRIGGER_WINDOW` (3 / 4 → 6 / 8, chậm thêm khoảng 0,3 s): chỉ chặn được xung ngắn, không chặn được nhiễu nằm trên ngưỡng liên tục;
   - hoặc giảm Range.
6. **Bị trễ tắt** (người đã ra mà vẫn có người lâu): `DIAG_STREAM` cho thấy vùng nào còn đạt `hold` khi trống. Học lại (đo lại nhiễu), giảm `PRESENCE_ENGINE_HOLD_DROP_PERCENT`, hoặc giảm Range.
7. Môi trường thay đổi (thêm quạt, đổi đồ đạc, đổi chỗ radar) thì học lại.

### Tuỳ chọn (`menuconfig` → *ESP32 Presence Engine (experimental)*)

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `PRESENCE_ESP_ENGINE` | tắt (bật trong biến thể `esppresence`) | Bật bộ xử lý (không dùng cùng `PRESENCE_ENERGY_FILTER`) |
| `PRESENCE_ENGINE_TRIGGER_REPORTS` / `_TRIGGER_WINDOW` | 3 / 4 | Số báo cáo đạt ngưỡng kích hoạt / trong số báo cáo gần nhất để kích hoạt |
| `PRESENCE_ENGINE_TRIGGER_NOISE_MARGIN` | 5 | Ngưỡng kích hoạt ít nhất nhiễu phòng trống + chừng này (3–30); ngưỡng radar được nâng lên nhiễu + chừng này − 1 sau khi học |
| `PRESENCE_ENGINE_TRIGGER_MARGIN` | 0 | Cộng thêm vào ngưỡng kích hoạt của mọi vùng (ngưỡng radar + 1 + chừng này); ngưỡng duy trì không đổi |
| `PRESENCE_ENGINE_HOLD_REPORTS` / `_HOLD_WINDOW` | 3 / 10 | Số báo cáo đạt ngưỡng duy trì / trong số báo cáo gần nhất để duy trì |
| `PRESENCE_ENGINE_HOLD_DROP_PERCENT` | 40 | Ngưỡng duy trì thấp hơn ngưỡng radar bao nhiêu % (0: bằng ngưỡng radar) |
| `PRESENCE_ENGINE_HOLD_DROP_MAX` | 10 | ...nhưng thấp hơn tối đa chừng này |
| `PRESENCE_ENGINE_NOISE_RECORD_SEC` | 30 | ESP32 ghi nhiễu bao lâu lúc học, trước khi radar đo |
| `PRESENCE_ENGINE_DATA_TIMEOUT_MS` | 2000 | Không có dữ liệu bao lâu thì là lỗi cảm biến |
| `PRESENCE_ENGINE_STATUS_LOG_SEC` | 300 | In `STATUS` mỗi (0: không) |
| `PRESENCE_ENGINE_CONSOLE` | bật | Lệnh console (khoảng 5 KB RAM) |
| `PRESENCE_HOLD_TIME_SEC` | 60 trong biến thể này | Hold time ban đầu (bộ đếm tắt hiện diện) |
| `LD2410_LEARN_*` (*Empty Room Learning*) | như bản ổn định | Thời gian chờ, thời gian radar đo, radar tự đo hay ESP32 đo |

**Khôi phục radar:**
- Báo cáo không có năng lượng: task `radar_cfg` bật lại engineering mode, tối đa 10 s một lần.
- Radar im lặng 5 s, và đã 5 s sau phiên cấu hình gần nhất (trong phiên, radar không gửi báo cáo):
  - ESP32 bỏ dữ liệu đang chờ trong UART;
  - gửi lệnh thoát chế độ cấu hình (radar bị bỏ lại trong chế độ đó thì không gửi báo cáo);
  - im lặng tiếp thì khởi động lại radar sau 15 s, 30 s, 1 phút... tối đa 5 phút một lần (không ghi gì vào flash của radar).

**Biến thể `esppresence`** cho bo ESP32-C3-WROOM-02-N4: gồm mọi thứ của `wroom_n4` (LED WS2812 GPIO8, nhiệt độ chip), cộng bộ xử lý, Hold time 60 s, Model `Presence LD2410 ESP engine`.

```bash
idf.py -B build-esppresence -D IDF_TARGET=esp32c3 -D SDKCONFIG=sdkconfig.esppresence \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.wroom_n4;sdkconfig.defaults.esppresence" build
```

**Phiên bản, OTA, quay lại:**
- Phiên bản hiện `1.0.<số commit>-esppresence`; số OTA vẫn là số commit, nên OTA được từ bản ổn định, từ `-espfilter` và từ bản `-esppresence` cũ, và vẫn giữ ghép nối.
- Hold time được đặt về 60 s **một lần** khi bo lần đầu chạy biến thể này. Ghép nối, VID/PID, endpoint và cluster không đổi; không thêm entity.
- **Từ 1.0.159 lên 1.0.162:**
  - nhiễu đã đo bằng 1.0.159 (NVS key `noise`, cùng định dạng) được dùng ngay cho ngưỡng kích hoạt: hết báo nhầm ở vùng nhiễu mà không cần học lại;
  - học lại một lần để ngưỡng radar ở vùng nhiễu cũng được nâng (thấy trong HLKRadarTool, và bản ổn định dùng được nếu quay lại);
  - chưa từng học bằng 1.0.159: phải học một lần thì mới có nhiễu.
- **Từ 1.0.153–1.0.156 lên bản này:**
  - ngưỡng ESP32 tự học của các bản đó (NVS key `profile`) không còn được dùng, để nguyên trong NVS;
  - học phòng trống lại một lần: radar đo ngưỡng, ESP32 đo nhiễu.
- **Quay lại bản ổn định:**
  - OTA một bản ổn định có số commit lớn hơn, hoặc nạp USB bằng `idf.py flash` (cả hai giữ ghép nối); file merged thì phải ghép nối lại;
  - ngưỡng trong radar (kể cả ngưỡng đã được nâng trên nhiễu) giữ nguyên, bản ổn định dùng tiếp; nhiễu trong NVS không được dùng;
  - Hold time giữ giá trị 60 s, chỉnh lại trên Home Assistant nếu muốn.

**Mức độ kiểm chứng:**
- Host test (radar và nhiễu giả lập):
  - thuật toán, 28 test:
    - ngưỡng lấy từ radar: vượt ngưỡng radar mới là mạnh; ngưỡng duy trì hạ 40 %, tối đa 10, không dưới nhiễu + 2; ngưỡng 100 bỏ vùng; năng lượng tĩnh vùng 0–1 không tính;
    - ngưỡng kích hoạt trên nhiễu + 5 ở vùng nhiễu chạm ngưỡng radar, duy trì luôn dưới kích hoạt, nhiễu gần 100, biên cộng thêm cho mọi vùng; 20 000 tổ hợp ngưỡng radar, nhiễu và tuỳ chọn ngẫu nhiên;
    - nâng ngưỡng radar trên nhiễu: nhiễu + 4, tối đa trần, không hạ, bỏ qua vùng 100 và vùng tĩnh 0–1;
    - phòng của báo lỗi (vùng 2 tĩnh 56 / 63, ngưỡng 40): chưa có số đo nhiễu thì báo nhầm ngay; có thì 30 phút không báo nhầm; sau khi nâng ngưỡng radar, người ngồi ở vùng 2 hay đi vào vùng 3 vẫn được nhận, trống sau 60 s;
    - số đo nhiễu và CRC; nhiễu đo với độ phân giải khác;
    - nhiễu không kích hoạt, cả khi ngưỡng radar sát nhiễu; người bước vào được nhận ở báo cáo thứ 4; người đi qua hai vùng;
    - người ngồi yên ở xa được giữ (và bị mất khi chưa đo nhiễu); tắt đúng Hold time; đếm lại;
    - lỗi cảm biến không phải phòng trống; báo cáo hỏng; đồng hồ 32 bit quay vòng; Range;
  - task `presence_eng`, 14 test: ngưỡng radar nhận từ task `radar_cfg`; nhiễu lưu NVS và nạp lại, blob hỏng để nguyên; phòng của báo lỗi từ đầu đến cuối (báo nhầm khi chưa học, học xong ngưỡng vùng 2 được nâng, 30 phút không báo nhầm, người ngồi đó vẫn được nhận); nhiễu trên ngưỡng xuất xưởng + 30; NVS đầy (không nâng gì); cảnh báo, lệnh, log;
  - task `radar_cfg`, 9 test: học qua radar (ESP32 ghi nhiễu, radar tự đo, ngưỡng nâng được ghi vào radar, đọc lại ngưỡng, bật lại engineering mode), huỷ, Factory thresholds, khôi phục radar;
  - học phòng trống (`quiet_learn`), 9 test cho bộ xử lý: ghi nhiễu 30 s rồi tắt engineering mode trước lệnh 0x000B, huỷ, thiếu báo cáo, radar đo thất bại, radar firmware cũ; ngưỡng nâng được ghi vào radar ở cả hai cách học, không ghi khi không có gì để nâng, radar không giữ ngưỡng nâng thì báo thất bại.
- Làm hỏng có chủ ý 77 chỗ trong bộ xử lý, task `presence_eng`, `radar_cfg` và `quiet_learn` (mỗi lần một chỗ, 1.0.162): test bắt được 74. Ba chỗ còn lại không thể đổi kết quả nào (giới hạn ngưỡng duy trì dưới ngưỡng kích hoạt không bao giờ chạm tới với tuỳ chọn hợp lệ; vùng ngưỡng 100 không bao giờ được nâng vì mức nâng tối đa 99), nên không test nào phân biệt được.
- **Mô phỏng Monte Carlo** (bộ xử lý 1.0.159; phòng của báo lỗi với 1.0.162: mục **Vì sao ngưỡng kích hoạt cũng phải ở trên nhiễu**), 100 phòng, mỗi phòng học rồi chạy 1 giờ:
  - mô hình nhiễu tự đặt, **không phải dữ liệu radar thật**;
  - ba bộ ngưỡng radar: xuất xưởng, kiểu radar tự đo (sát nhiễu: phân vị 99 + 5), kiểu ESP32 học của bản ổn định;
  - **có số đo nhiễu:** 0 lần báo nhầm có người. Người ở 10 s rồi ra: trống sau 61 s. Người ngồi yên ở 3,75–6,75 m (năng lượng tĩnh khoảng 14–18 ± 5) được giữ suốt 20 phút với cả ba bộ ngưỡng;
  - **chưa có số đo nhiễu** (ngưỡng duy trì = ngưỡng radar): với ngưỡng xuất xưởng, người ngồi yên đó chỉ được giữ khoảng 74 % thời gian, bị báo trống khoảng một lần mỗi 20 phút;
  - **xung nhiễu nhiều gấp 3 lần lúc học:** ngưỡng xuất xưởng và ngưỡng ESP32 học không báo nhầm. Ngưỡng sát nhiễu báo nhầm 30 lần trong 100 giờ, do ngưỡng radar chứ không do ngưỡng duy trì: học lại.
- Build biến thể `esppresence` 1.0.159: **0 lỗi, 0 cảnh báo**.
  - `sdkconfig` chỉ khác `wroom_n4` ở Model, Hold time ban đầu (60 thay 30) và 11 tuỳ chọn của bộ xử lý: cùng VID/PID, cluster, endpoint, Wi-Fi, BLE.
  - Header file OTA: VID `0xFFF1`, PID `0x8001`, phiên bản 159 / `1.0.159-esppresence`.
- **Chưa thử trên bo** (mục **Mức độ kiểm tra**, bước 19):
  - năng lượng của radar thật qua bộ xử lý; ngưỡng radar tự đo trong phòng thật;
  - radar tự đo ngay sau khi ESP32 tắt engineering mode;
  - console trên bo của bạn, Hold time từ Home Assistant, các bước khôi phục radar, RAM và stack lúc chạy.
- Các tham số mặc định là giá trị ban đầu, **cần chỉnh theo log trên bo thật** (mục **Hiệu chỉnh thực tế**).

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
- Radar chưa đọc được (chưa nối dây GPIO4 → chân RX của radar): thay đổi trong Home Assistant bị từ chối, entity giữ giá trị cũ; firmware đọc lại radar sau 15 s, 30 s, 1 min... tối đa 5 min một lần.
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

## Entity chẩn đoán: Reboot count, Uptime, Wi-Fi RSSI

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

## Học phòng trống

Trong phòng không có ai, radar vẫn nhận "năng lượng" từ quạt, rèm bay, cây cảnh, phản xạ đồ vật... Ở gate nào năng lượng nền vượt ngưỡng, radar báo nhầm có người. Học phòng trống đo năng lượng nền rồi đặt ngưỡng từng gate trên mức đó.

1. Chọn **Learning** trong Home Assistant, hoặc **bấm nút BOOT 3 lần** (không cần ghép nối hay mạng). Log: `Empty room learning: leave the room, recording starts in 20 s and lasts 60 s`.
2. **Rời khỏi phòng** trong 20 s (LED sáng liên tục). Radar nhìn xa khoảng 6 m và xuyên cửa gỗ, vách mỏng. Quạt thường chạy thì để chạy: đó là nhiễu cần học.
3. Đo 60 s (LED nháy chậm), theo một trong hai cách bên dưới.
4. Xong: LED sáng 10 s, entity trở về **Off** (khoảng 80 s sau khi chọn). Kết quả và lý do thất bại xem trong log `quiet_learn`.

Biến thể `esppresence` thêm 30 s ESP32 ghi nhiễu trước khi radar đo, nên học mất khoảng 2 phút, và sau khi radar đo thì nâng ngưỡng ở vùng có nhiễu chạm ngưỡng radar (mục **Bộ xử lý hiện diện trên ESP32**).

**Cách A, radar tự đo (mặc định, `LD2410_LEARN_RADAR_AUTO=y`):** sau 10 s đầu, firmware đọc phiên bản firmware radar (`LD2410 firmware V2.44.25070917`) và ngưỡng hiện tại, rồi gửi lệnh **0x000B** (giao thức LD2410C V1.09 mục 2.2.20) với thời gian đo bằng thời gian ghi. Radar tự chờ 10 s, đo 60 s rồi tự lưu ngưỡng chuyển động và đứng yên của mọi gate; firmware chỉ theo dõi trạng thái trong báo cáo (4 đang đo, 5 xong, 6 thất bại):

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
- **Radar từ chối lệnh 0x000B** (firmware cũ) nhưng trả lời lệnh khác: log `This LD2410 firmware has no background noise detection: learning on the ESP32`, chuyển sang cách B, ghi bắt đầu đúng lúc hết 20 s. Radar không trả lời gì: thất bại, log nhắc kiểm tra dây **GPIO4 → chân RX của radar**.

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

**Factory thresholds** (chọn trong Home Assistant hoặc bấm 4 lần): ghi lại ngưỡng mặc định nhà sản xuất (ngưỡng đứng yên của gate 0, 1 giữ nguyên) rồi đọc lại. Range, độ phân giải, Bluetooth, *no-one duration* giữ nguyên, radar không khởi động lại. Log ghi cả ngưỡng cũ (`Thresholds until now: ...`) để chép lại nếu cần; radar đã giữ ngưỡng mặc định thì không ghi gì (vẫn tính là thành công). Trong lúc ghi, entity hiện `Factory thresholds`, xong trở về `Off`.

**Lựa chọn cuối cùng thắng:**
- Learning rồi chọn Off: huỷ, không ghi gì, LED trở lại bình thường ngay.
- Factory thresholds (hoặc bấm 4 lần) khi đang học: huỷ học, rồi ghi ngưỡng mặc định.
- Learning hoặc Off (hoặc bấm 3 lần) khi Factory thresholds chưa bắt đầu: bỏ Factory thresholds.
- Bấm 3 lần khi đang học: bỏ qua (log `Empty room learning already requested or running`).
- Task radar không chạy được: bấm nút thì LED báo thất bại; chọn từ Home Assistant thì bị từ chối.

Firmware **không bao giờ tự ghi ngưỡng** vào radar khi không ai yêu cầu. Thời gian chờ, thời gian ghi, mức 95 %, +10 và +30 chỉnh trong `menuconfig` → *Empty Room Learning*.

## Yêu cầu mạng

- Wi-Fi **2,4 GHz** (ESP32-C3 không có 5 GHz), WPA/WPA2/WPA3 Personal (không có Enterprise, OWE).
- Điện thoại, controller (HomePod, Home Assistant, Nest...) và thiết bị **cùng một mạng LAN**; không dùng mạng khách.
- Router bật **IPv6** (ít nhất link-local), không chặn **mDNS/multicast**, tắt "AP/client isolation".

## Ghép nối (commissioning)

Firmware dùng chứng chỉ test của esp-matter (VID `0xFFF1`, PID `0x8001`; SDK có DAC, PAI và Certification Declaration test cho PID này), mã ghép nối mặc định của SDK:

- Mã nhập tay: **`3497-011-2332`**
- QR: `MT:-24J042C00KA0648G00` ([hiện QR](https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:-24J042C00KA0648G00))
- Passcode 20202021, discriminator 3840

Khi chưa ghép nối, log in QR, link ảnh QR và mã nhập tay của chính firmware (`setup_code: Setup code of this board (BLE), SDK TEST CODE shared by every board`). Mã cũng được in khi bấm đúp và khi cửa sổ mở lại sau khi xoá thiết bị khỏi mọi ứng dụng, không in khi cửa sổ do controller mở (Multi-Admin). Đây là công cụ phát triển: sản phẩm thật thì tắt `CONFIG_PRINT_ONBOARDING_CODES` vì QR chứa passcode.

**Apple Home / Google Home / Home Assistant:** thêm thiết bị Matter, quét QR; ứng dụng gửi SSID và mật khẩu Wi-Fi cho thiết bị qua BLE. Apple Home báo *Uncertified Accessory*: chọn **Vẫn thêm**.

```bash
chip-tool pairing ble-wifi 0x7283 <SSID> <MẬT_KHẨU> 20202021 3840   # mỗi bo một Node ID
chip-tool occupancysensing read occupancy 0x7283 1
```

- Khởi động mà chưa ghép nối: cửa sổ ghép nối mở 15 phút; BLE quảng bá nhanh 30 s rồi chuyển sang chậm (500 ms). Hết giờ thì **bấm đúp** để mở lại.
- Trong gói quảng bá trước khi ghép nối (DNS-SD), tên là `Presence Sensor` cho mọi bo, device type 263 (Occupancy Sensor). Tên Bluetooth (GAP) là tên thiết bị `Presence-XXXX`, chỉ thấy sau khi kết nối BLE.
- Sau khi ghép nối, BLE được tắt để trả RAM (`BLE deinitialized and memory reclaimed`). Bấm đúp khi đó mở cửa sổ chỉ qua mạng (DNS-SD), nên ứng dụng thứ hai phải ở cùng LAN.
- Xoá thiết bị khỏi mọi ứng dụng: cửa sổ ghép nối tự mở lại 5 phút, giữ thông tin Wi-Fi để ghép lại qua mạng.
- Tên thiết bị `Presence-XXXX` được ghi vào **NodeLabel** lúc khởi động nếu chưa có nhãn nào (bo mới, sau factory reset); nhãn đặt từ ứng dụng được giữ. Home Assistant đặt tên thiết bị theo NodeLabel.

**Nhiều bo cùng firmware:** bản mặc định cho mọi bo chung mã test. Ghép từng bo một (các bo chưa ghép khác tắt nguồn, không bấm đúp hai bo cùng lúc), rồi kiểm tra đúng bo bằng **Identify** hoặc so **Serial** trong ứng dụng với dòng `id_diag: dev serial=`. Thêm vào hệ thứ hai: ưu tiên chức năng chia sẻ của hệ đang quản lý (mã do ứng dụng tạo ngẫu nhiên).

### Mã ghép nối riêng cho từng bo (tuỳ chọn, chưa thử trên ESP32-C3)

Biến thể `sdkconfig.defaults.unique_setup`: mỗi bo tự sinh passcode, discriminator, salt và verifier SPAKE2+ ở lần dùng đầu (ngẫu nhiên từ DRBG của CHIP băm cùng MAC eFuse; không bao giờ sinh discriminator 3840 của mã test), lưu ở phân vùng `fctry` (namespace `setup-code`). Mã giữ qua khởi động lại, OTA, factory reset và nạp file merged; chỉ `erase-flash` mới sinh mã mới.

```bash
cd ESP32-C3/matter-presence-ld2410
idf.py -B build-unique -D IDF_TARGET=esp32c3 -D SDKCONFIG=sdkconfig.unique \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.unique_setup" build
idf.py -B build-unique -p /dev/ttyACM0 flash monitor
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
cd ESP32-C3/matter-presence-ld2410
idf.py set-target esp32c3
idf.py build
```

Bằng Docker, chạy từ thư mục gốc của repo (phiên bản firmware lấy từ số commit nên cần cả thư mục `.git`; image tự `cd` vào thư mục esp-matter nên lệnh phải `cd` lại):

```bash
docker run --rm -v $PWD:/project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c 'cd /project/ESP32-C3/matter-presence-ld2410 && git config --global --add safe.directory "*" \
           && idf.py set-target esp32c3 && idf.py build'
```

- Kết quả: `build/matter_presence_c3_wifi.bin` và `build/matter_presence_c3_wifi-ota.bin`.
- Phiên bản firmware `1.0.<git rev-list --count HEAD>` (SoftwareVersionString), số commit là SoftwareVersion dùng cho OTA; CMake chạy lại sau mỗi commit. Không phải git checkout thì số là 0 (có cảnh báo).
- Chỉ build cho `esp32c3`; target khác thì CMake báo lỗi.
- File `sdkconfig` cũ giữ giá trị cũ: sau khi đổi `sdkconfig.defaults`, chạy lại `idf.py set-target esp32c3`.
- CI (`.github/workflows/matter-presence-ld2410-c3.yml`) chạy host test và build mỗi khi thư mục này hoặc file workflow thay đổi; Artifacts gồm app `.bin`, bootloader, bảng phân vùng, `ota_data_initial.bin`, `flash_args`, file merged và file `.ota`.

### Nạp firmware và xem log

SuperMini chỉ có cổng USB gốc của ESP32-C3 (USB-Serial-JTAG): Linux `/dev/ttyACM0`, macOS `/dev/cu.usbmodem*`, Windows `COMx`.

```bash
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

File merged từ CI, nạp một lần ở địa chỉ `0x0`. File này ghi đè vùng NVS (`0x10000`), tức là **xoá ghép nối**:

```bash
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 matter_presence_c3_wifi_merged.bin
```

Không vào được chế độ nạp: giữ **BOOT**, cắm USB (hoặc bấm RESET), thả BOOT rồi nạp lại. Thoát monitor: `Ctrl+]`.

### Cập nhật qua mạng (OTA)

File **`matter_presence_c3_wifi.ota`** (Artifacts của CI; build tại máy là `build/matter_presence_c3_wifi-ota.bin`, đổi đuôi thành `.ota`): app kèm header Matter OTA với VID/PID `0xFFF1`/`0x8001` và phiên bản `1.0.<số commit>`.

1. Home Assistant → Settings → Add-ons → **Matter Server** → **Open Web UI**.
2. Chọn thiết bị → **Upload Firmware** → chọn file `.ota` → **Start Update**.
3. Thiết bị tải qua Wi-Fi, tự khởi động lại vào bản mới và giữ ghép nối.

- Phiên bản trong file phải **lớn hơn** bản đang chạy. Bản này là 1.0.134 trở lên vì nhánh của nó chứa đủ lịch sử của 1.0.132. Khi merge vào `main`, dùng **merge commit**, không squash: squash làm số commit trên `main` (hiện 129) nhỏ hơn 132 và bo cũ sẽ từ chối bản OTA đó.
- **Từ 1.0.132 lên bản này:** cùng VID/PID, endpoint 1–4, bảng phân vùng và khoá lưu trữ, nên ghép nối, Hold time, lựa chọn Radar Bluetooth và các entity được giữ. Bản mặc định không có endpoint 5 (nhiệt độ chip); từ 1.0.143–1.0.146 lên bản mặc định thì endpoint 5 biến mất (xem **Nhiệt độ chip**). Chưa thử trên bo; nếu entity hiện sai sau khi cập nhật, dùng **Re-interview** trên trang thiết bị trong Home Assistant.
- Sau khi đã cập nhật OTA, thiết bị chạy từ phân vùng `ota_1`: nạp USB thì dùng file merged hoặc `idf.py flash` (ghi cả `otadata`), không ghi riêng `0x20000`.

## Cấu hình (`idf.py menuconfig`)

Tên các tuỳ chọn giữ như bản 1.0.132; menu *Connection Watchdog* và *Wi-Fi RSSI and Uptime Reports* là mới; `PRESENCE_CHIP_TEMPERATURE` mới, mặc định tắt; `PRESENCE_WIFI_STAY_AWAKE` đổi mặc định thành tắt.

| Menu | Tuỳ chọn | Mặc định | Khoảng |
|---|---|---|---|
| Device Information | `DEVICE_VENDOR_NAME` (Manufacturer) | `Haxeko` | ≤ 32 ký tự |
| | `DEVICE_PRODUCT_NAME` (Model, ProductName = ProductLabel) | `Presence LD2410` | ≤ 32 |
| | `DEVICE_NAME_PREFIX` (tên thiết bị = prefix + 4 ký tự cuối serial; rỗng: NodeLabel `Presence Sensor`, tên BLE `Presence-<discriminator>`) | `Presence-` | ≤ 11 |
| | `DEVICE_DISPLAY_NAME` (tên DNS-SD trước khi ghép nối) | `Presence Sensor` | ≤ 32 |
| | `DEVICE_HARDWARE_VERSION_STRING` | `ESP32C3-LD2410` | ≤ 64 |
| Presence Sensor Configuration | `LD2410_UART_RX_PIN` / `LD2410_UART_TX_PIN` | 3 / 4 | 0–21 |
| | `PRESENCE_MAX_DISTANCE_CM` (chỉ tính mục tiêu gần hơn) | 0 (tắt) | 0–800 |
| | `PRESENCE_HOLD_TIME_SEC` (Hold time lúc đầu, áp dụng một lần) | 30 | 1–300 |
| | `RADAR_TIMEOUT_SEC` (radar im lặng thì coi là không có người) | 10 | 1–300 |
| | `BOARD_NAME` (chỉ để in trong log, mới) | `ESP32-C3 SuperMini` | |
| | `STATUS_LED_TYPE` (LED GPIO một màu / WS2812) | LED GPIO | |
| | `STATUS_LED_GPIO` / `STATUS_LED_ACTIVE_HIGH` (chỉ LED GPIO) / `STATUS_LED_BRIGHTNESS` (chỉ WS2812) | 8 / tắt / 20 | 0–21 / – / 1–255 |
| | `RESET_BUTTON_GPIO` | 9 | −1 (không có nút) – 21 |
| | `PRESENCE_CHIP_TEMPERATURE` (endpoint 5: nhiệt độ chip, mới; bật thì cần `ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=6`) | tắt (bật ở 1.0.143–1.0.146) | |
| | `PRESENCE_WIFI_STAY_AWAKE` (tắt modem sleep sau khi BLE tắt; 1.0.132 bật) | tắt | |
| ESP32 Energy Filter (thử nghiệm) | `PRESENCE_ENERGY_FILTER` / `_STRONG` / `_FRAMES` | tắt / 15 / 3 | – / 1–100 / 2–20 |
| ESP32 Presence Engine (thử nghiệm) | `PRESENCE_ESP_ENGINE` và các tuỳ chọn `PRESENCE_ENGINE_*` (mục **Bộ xử lý hiện diện trên ESP32**) | tắt | |
| Empty Room Learning | `LD2410_LEARN_DELAY_SEC` / `LD2410_LEARN_DURATION_SEC` | 20 / 60 s | 0–120 / 10–300 |
| | `LD2410_LEARN_RADAR_AUTO` | bật | |
| | `LD2410_LEARN_PERCENTILE` / `_MARGIN` / `_MAX_RAISE` | 95 / 10 / 30 | 50–100 / 0–50 / 0–100 |
| Identity Diagnostics and Setup Code | `IDENTITY_DIAG_PERIOD_SEC` | 300 | 0 (tắt) – 86400 |
| | `IDENTITY_DIAG_SUBSCRIPTION_LOG` / `PRINT_ONBOARDING_CODES` | bật / bật | |
| | `UNIQUE_SETUP_CODE`, `UNIQUE_SETUP_CODE_PARTITION` | bật khi build `unique_setup`, `fctry` | |
| Connection Watchdog (mới) | `NET_WATCHDOG_RECONNECT_MIN` (nối lại Wi-Fi mỗi, khi không có subscription) | 10 phút | 0 (không) – 1440 |
| | `NET_WATCHDOG_RESTART_MIN` (khởi động lại sau, khi không có subscription; 0 cũng tắt việc khởi động lại khi task Matter treo) | 30 phút | 0 (không) – 1440 |
| Wi-Fi RSSI and Uptime Reports (mới) | `DIAG_REPORT_RSSI_STEP_DB` (gửi lại Wi-Fi RSSI khi đổi từng này dB) | 3 | 0 (chỉ theo chu kỳ) – 30 |
| | `DIAG_REPORT_REFRESH_MIN` (gửi lại Wi-Fi RSSI và uptime mỗi) | 10 phút | 0 (không) – 1440 |
| Memory Monitor | `MEM_MONITOR_PERIOD_SEC` | 600 | 0 (chỉ khi khởi động) – 86400 |

Console Matter (gõ lệnh trong monitor) tắt để có thêm khoảng 10 KB RAM: bật lại bằng *Component config* → *CHIP Core* → *General Options* → *Use the CHIP shell library* (`CONFIG_ENABLE_CHIP_SHELL`). Khi đó có lệnh của esp-matter (diagnostics, factoryreset, attribute) và `matter wifi ...` của Matter SDK; mật khẩu gõ vào lệnh `matter wifi connect` hiện trên màn hình và nằm trong lịch sử lệnh.

## Wi-Fi và BLE

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
| Biến thể `espfilter` (`wroom_n4` và bộ lọc năng lượng; nhánh `claude/c3-wroom-esp-filter`, 1.0.151) | | 1 542 016 B (trống 22 %), RAM tĩnh 153 531 B, heap 167 765 B; build lại trên nhánh này (1.0.159): 1 543 568 B, RAM tĩnh 153 585 B |
| Bản mặc định của nhánh này (1.0.159: driver radar đếm khung hỏng, có thể xả UART; mã ghi nhiễu cho bộ xử lý không chạy) | | 1 516 208 B (trống 23 %), RAM tĩnh 150 605 B, heap 170 691 B |
| Biến thể `esppresence` (`wroom_n4` và bộ xử lý hiện diện; nhánh này, 1.0.159) | | 1 577 040 B (trống 20 %), RAM tĩnh 154 439 B, heap 166 857 B |

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
| presence_eng (biến thể `esppresence`) | 3 | 4096 | chưa đo |
| console_repl (biến thể `esppresence`, của ESP-IDF) | 2 | 3584 | chưa đo |

Heap đo trên bo (log của người dùng, firmware cũ): bản 1.0.122 lúc ghép nối còn 30,7 KB, thấp nhất 25,9 KB; bản 1.0.128 thấp nhất 15,7 KB trong 10 phút đầu; bản 1.0.131 lúc ghép nối còn 49,0 KB, thấp nhất 40,0 KB. Heap của bản này cần đo trên bo bằng log `mem`:

```
I mem: Heap: <còn trống> bytes free, lowest <thấp nhất> since boot, largest block <khối lớn nhất>
I mem: Stack unused bytes: CHIP <n> wifi <n> esp_timer <n> presence <n> ld2410 <n> radar_cfg <n> id_diag <n>
```

Biến thể `esppresence` thêm `presence_eng` và `console_repl` vào dòng stack. Lúc học phòng trống, bộ xử lý mượn thêm 3,6 KB heap trong 60 s ghi rồi trả lại.

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

Số serial là MAC gốc trong eFuse (6 byte, 12 ký tự hex in hoa), cũng là MAC Wi-Fi router hiển thị. Hiện diện: `presence: Radar: presence (moving yes 120 cm, still no 0 cm)`, `presence: Radar: no one (...)`. Radar không gửi gì 10 s: `E presence: No data from the LD2410 for 10 s, check the wiring (radar TX -> GPIO3, RX -> GPIO4)` và báo không có người; có dữ liệu lại thì `Radar data received`. Sự kiện Matter: `Commissioning complete`, `Commissioning failed, fail safe timer expired`, `Commissioning window opened / closed`, `Fabric removed successfully`, `Wi-Fi connectivity changed`, `Interface IP Address changed`.

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

- **Không thấy log:** chọn đúng cổng USB của SuperMini; một số cáp USB-C chỉ có dây nguồn.
- **Ghép nối dừng ở bước Wi-Fi**, log lặp lại `wifi:state: auth -> init`, `Connecting -> Connecting_Failed`: router không trả lời bước xác thực. Mỗi lần thử có một dòng `W id_diag: EV wifi failed or left: reason ...`:
  - **reason 2**: router không trả lời bước xác thực (hoặc báo xác thực không còn hiệu lực);
  - **reason 4**: router không trả lời bước kết nối, hoặc ngắt bo vì không hoạt động;
  - **reason 15**: hết thời gian trao khoá, thường do sai mật khẩu;
  - **reason 36**: chính bo dừng lần thử (ví dụ điện thoại đã hết thời gian chờ bước Wi-Fi);
  - **rssi** từ −70 dBm trở lên là đủ mạnh, dưới −80 dBm là quá yếu.
- **TP-Link Deco X10 (mesh):** tắt **Beamforming** trong app Deco (người dùng đã sửa được lỗi trên bằng cách này); cập nhật firmware Deco có thể bật lại. Vẫn lỗi thì bật **IoT Network** (mạng 2,4 GHz riêng), tạm tắt *Fast Roaming*, đặt WPA2. Router khác: WPA2-PSK (AES) hoặc WPA2/WPA3, chế độ b/g/n (không chỉ ax), tắt lọc MAC, thử tên mạng 2,4 GHz riêng nếu đang gộp band.
- **Ghép nối xong phần Wi-Fi nhưng ứng dụng không tìm thấy thiết bị:** điện thoại và thiết bị cùng LAN, router không chặn mDNS/multicast, có IPv6 (dòng `ip` của `id_diag`).
- **Radar không đọc được** (`Could not read the LD2410 settings (check the wire from GPIO4 to the radar RX pin), next try in 15 s`): kiểm tra dây GPIO4 → RX của radar. Cảm biến vẫn báo hiện diện nếu dây GPIO3 ← TX còn tốt, nhưng các điều khiển radar bị từ chối.
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
- Mỗi lần thử lại ghép nối: factory reset (giữ BOOT 5 s) hoặc `erase-flash`.

## Mức độ kiểm tra

**Đã kiểm chứng bằng build và test trên máy tính (bản này):**
- Build `esp32c3` trong Docker `release-v1.6.1_idf_v6.0.2`: **0 lỗi, 0 cảnh báo**, cả bản mặc định, biến thể `unique_setup` và bản bật `PRESENCE_CHIP_TEMPERATURE` (giới hạn endpoint 6) ở 1.0.147; bản mặc định và biến thể `wroom_n4` ở 1.0.149; biến thể `espfilter` ở 1.0.151 (nhánh `claude/c3-wroom-esp-filter`); trên nhánh này, bản mặc định và các biến thể `espfilter`, `esppresence` ở 1.0.153, 1.0.156 và 1.0.159.
- `sdkconfig` sinh ra giống hệt bản 1.0.132 build trong cùng image, trừ đường dẫn file, mục chọn loại LED đã bỏ, 5 tuỳ chọn mới (`DIAG_REPORT_RSSI_STEP_DB`, `DIAG_REPORT_REFRESH_MIN`, `NET_WATCHDOG_RECONNECT_MIN`, `NET_WATCHDOG_RESTART_MIN`, `PRESENCE_CHIP_TEMPERATURE` tắt) và `PRESENCE_WIFI_STAY_AWAKE` tắt: Wi-Fi bật, OpenThread tắt, BLE chỉ để ghép nối, minimal mDNS, cửa sổ ghép nối 900 s, VID `0xFFF1` / PID `0x8001`, WPA3 SAE, coexistence, stack CHIP 8192, CPU 160 MHz, brownout bật, đúng 21 cluster và giới hạn endpoint động 5 như 1.0.132. So với 1.0.144 chỉ khác 3 điểm: `PRESENCE_CHIP_TEMPERATURE` tắt, cluster Temperature Measurement tắt, giới hạn endpoint động 5. Build với `PRESENCE_CHIP_TEMPERATURE` và giới hạn 6 thì `sdkconfig` giống hệt 1.0.144 (22 cluster: tuỳ chọn tự bật lại cluster Temperature Measurement).
- Header file OTA (bản build 1.0.149, biến thể `wroom_n4`): VID `0xFFF1`, PID `0x8001`, phiên bản 149 / `1.0.149` (lớn hơn 132). Biến thể `espfilter` (1.0.151): cùng VID/PID, phiên bản 151 / `1.0.151-espfilter`. Trên nhánh này (1.0.159): biến thể `esppresence` phiên bản 159 / `1.0.159-esppresence`, `espfilter` phiên bản 159 / `1.0.159-espfilter`, cùng VID/PID.
- Thứ tự tạo endpoint 0–4 giống 1.0.132; esp-matter (`esp_matter_data_model.cpp`) cấp số endpoint theo thứ tự tạo trước `esp_matter::start()`, nên số endpoint giữ nguyên. Endpoint 5 (nhiệt độ chip, chỉ khi bật `PRESENCE_CHIP_TEMPERATURE`) được tạo sau cùng.
- Kích thước so với 1.0.132 (bảng **RAM và flash**): lớn hơn 1 440 byte flash, RAM tĩnh ít hơn 22 byte. Phần thêm vào và bớt đi:
  - `diag_report` (báo cáo lại Wi-Fi RSSI và Uptime): 416 byte flash, 16 byte RAM tĩnh (so với 1.0.139);
  - `net_watchdog`: 1 808 byte flash, 32 byte RAM tĩnh (so với 1.0.140);
  - nhiệt độ chip (cluster Temperature Measurement, driver cảm biến, endpoint 5): 7 136 byte flash, 440 byte RAM tĩnh (so với 1.0.142);
  - Wi-Fi modem sleep (`PRESENCE_WIFI_STAY_AWAKE` tắt, mã tắt modem sleep không được build): bớt 672 byte flash, RAM tĩnh không đổi (so với 1.0.143);
  - nhiệt độ chip thành tuỳ chọn tắt sẵn (1.0.147): bớt 7 152 byte flash và 440 byte RAM tĩnh (so với 1.0.144). Bật lại thì kích thước đúng bằng 1.0.144;
  - WS2812 và tên bo (1.0.149): bản mặc định thêm 160 byte flash, RAM tĩnh không đổi (driver RMT không được build). Biến thể `wroom_n4` (WS2812 và nhiệt độ chip) lớn hơn 1.0.144 18 288 byte flash và 2 444 byte RAM tĩnh: driver RMT và thư viện `led_strip`.
  - bộ lọc năng lượng (biến thể `espfilter`, 1.0.151): lớn hơn `wroom_n4` 1.0.149 2 192 byte flash và 104 byte RAM tĩnh.
  - bộ xử lý hiện diện (biến thể `esppresence`, 1.0.159): lớn hơn `wroom_n4` 1.0.149 37 216 byte flash và 1 012 byte RAM tĩnh (bộ xử lý, console `esp_console`, driver USB-Serial-JTAG). Lúc chạy dùng thêm khoảng 10 KB heap (stack của `presence_eng` 4 096 byte và `console_repl` 3 584 byte, hàng đợi, bộ đệm USB), cộng 3,6 KB khi học. Thiếu heap thì tắt `PRESENCE_ENGINE_CONSOLE` (bớt khoảng 5 KB).
  - mọi bản build của nhánh này lớn hơn 1.0.151 cùng loại khoảng 1,5 KB flash và 54 byte RAM tĩnh: driver radar đếm khung hỏng và có thể xả UART (`uart_flush_input`), và mã ghi nhiễu lúc học; cả hai chỉ chạy khi bật bộ xử lý. Không bật bộ xử lý thì hành vi không đổi.
- Host test (`test/host/run.sh`, 20 chương trình): parser và lệnh radar, phiên thông số, học trên ESP32 và radar tự đo, điều phối (mọi thứ tự Learning/Off/Factory, kể cả lúc radar đang đo), presence, nút, LED (một màu và màu WS2812), tên thiết bị, lúc nào gửi lại Wi-Fi RSSI và Uptime, việc watchdog kết nối làm theo thời gian không có subscription, đổi và gửi nhiệt độ chip, bộ lọc năng lượng trên ESP32 (thuật toán, task presence, task `radar_cfg` bật lại engineering mode và đọc lại ngưỡng), bộ xử lý hiện diện trên ESP32 (thuật toán với nhiễu giả lập, task `presence_eng`, task `radar_cfg`, ghi nhiễu lúc học; mục **Bộ xử lý hiện diện trên ESP32**, **Mức độ kiểm chứng**). Thử làm hỏng 19 chỗ trong firmware (mỗi lần một chỗ): test bắt được 18; chỗ còn lại (đổi thứ tự ghi ngưỡng mặc định và học trong vòng lặp) không đổi hành vi vì hai yêu cầu đó không bao giờ cùng chờ. Làm hỏng thêm 10 chỗ trong `diag_report.h`, 12 chỗ trong `net_watchdog.h`, 7 chỗ trong `c3_temperature.h`, 6 chỗ ở phần WS2812 của `status_led.cpp` (màu, độ sáng, chân, chỉ gửi khi đổi màu), 25 chỗ của bộ lọc năng lượng (`energy_filter`, `presence.cpp`, `radar_worker.cpp`, driver) và 44 chỗ của bộ xử lý hiện diện (`presence_engine`, `esp_presence`, `radar_worker.cpp`, `quiet_learn.cpp`): test bắt được tất cả.
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
17. Biến thể `wroom_n4` trên bo ESP32-C3-WROOM-02-N4 (nạp USB lần đầu):
    - log `Target esp32c3, board ESP32-C3-WROOM-02-N4` và `LED GPIO8 (WS2812)`;
    - LED WS2812: xanh dương nháy chậm khi chờ ghép nối, trắng nháy nhanh khi *Identify*, cyan / xanh lá / đỏ / tím khi học phòng trống và ghi ngưỡng mặc định;
    - radar trên GPIO3/GPIO4 và nút GPIO9 chạy như trên SuperMini; entity nhiệt độ chip có trong Home Assistant.
18. Bộ lọc năng lượng (biến thể `espfilter`), cắm USB xem log:
    - lúc khởi động: `Engineering mode on: the energy of every gate for the energy filter`;
    - Home Assistant hiện Model `Presence LD2410 ESP filter` và firmware `1.0.<n>-espfilter`;
    - phòng trống vài giờ: các xung nhỏ hiện `Energy filter: ignored ...`, phòng không bị báo có người;
    - người đi vào: `occupied at once` và đèn bật nhanh như trước; người vào chậm hoặc ở xa: `3 reports in a row ...`;
    - ngồi im 10 phút: vẫn có người; rời phòng: hết người sau no-one duration + Hold time như trước;
    - đổi Radar resolution hoặc rút điện radar: `Engineering mode on` lại trong vài giây;
    - học phòng trống: ngưỡng mới được dùng (log `ignored ... threshold` theo ngưỡng mới).
19. Bộ xử lý hiện diện (biến thể `esppresence`), cắm USB, mở console (`idf.py monitor`):
    - lúc khởi động: `ESP32 presence engine: strong signal (from radar threshold + 1, at least the empty room noise + 5) ...`, `No noise of the empty room recorded yet ...` (hoặc `Noise of the empty room loaded ...`), `Radar energies received` (hoặc `Engineering mode on: ... for the ESP32 presence engine`), rồi bảng `Thresholds from the radar` đúng với ngưỡng trong HLKRadarTool;
    - Home Assistant hiện Model `Presence LD2410 ESP engine`, firmware `1.0.<n>-esppresence`, Hold time 60 s;
    - `help`, `STATUS`, `DIAG`, `DIAG_STREAM on`, `THRESHOLDS`: lệnh chạy, bảng 9 vùng có năng lượng thay đổi theo người;
    - học phòng trống (Learning trên Home Assistant, bấm BOOT 3 lần, `LEARN_EMPTY`):
      - log `Recording the noise of the empty room on the ESP32 for 30 s`, rồi `The LD2410 measures the empty room itself`;
      - LED cyan sáng 20 s, nháy khoảng 100 s, rồi xanh lá; select về Off;
      - ngưỡng mới hiện trong HLKRadarTool và trong bảng `Thresholds from the radar`; `Noise of the empty room recorded (... reports) and saved`;
      - xem log `Thresholds set by the radar`: radar có thật sự đổi ngưỡng sau lệnh 0x000B không (ảnh báo lỗi cho thấy ngưỡng vẫn là ngưỡng xuất xưởng);
      - vùng có nhiễu chạm ngưỡng radar (ví dụ vùng 2 tĩnh của báo lỗi): `Gate 2 still: empty room noise ..., radar threshold 40 -> ...`, `Raised thresholds saved in the LD2410`; HLKRadarTool thấy ngưỡng đã nâng; bảng ngưỡng không còn `strong and hold raised` ở vùng đó;
      - khởi động lại thì `Noise of the empty room loaded`;
      - lệnh tự đo 0x000B đã chạy trên radar thật với bản ổn định, nhưng **chưa thử ngay sau khi ESP32 tắt engineering mode** như ở biến thể này;
    - học khi có người đi lại trong phòng: radar đặt ngưỡng cao hoặc học thất bại (log `quiet_learn`); vùng có người đi lại có nhiễu cao (`hold raised above it`);
    - phòng trống vài giờ (quạt, máy lạnh chạy như bình thường): không có `Present: ...`; nếu có, log ghi vùng, năng lượng và ngưỡng `strong from` để chỉnh (mục **Hiệu chỉnh thực tế**, bước 5);
    - đi vào: `Present: ...` trong khoảng 0,5 s, Home Assistant báo có người;
    - ngồi yên ở vùng có nhiễu cao (ví dụ 1,5–2,25 m trong báo lỗi): `DIAG_STREAM on`, cột `filt` của năng lượng tĩnh vùng đó phải đạt `hold`, nếu không thì vùng kề hay năng lượng chuyển động phải giữ người;
    - ngồi yên 10 phút ở chỗ xa nhất cần nhận (ví dụ 4–6 m): vẫn có người, `STATUS` báo `hold signal at gate ...`;
    - ra khỏi phòng: `Empty: no hold signal for 60 s` và Home Assistant hết người cùng lúc; quay lại trong lúc đếm thì đếm lại từ đầu;
    - đổi Hold time trên Home Assistant: log `Hold time N s`, thời gian tắt đổi theo;
    - đổi ngưỡng một vùng bằng HLKRadarTool, tắt *Radar Bluetooth*: bảng ngưỡng in lại với ngưỡng mới;
    - rút dây TX của radar khi đang có người: `Sensor fault: no data from the radar ...`, sau 5 s `resynchronising the UART`, khoảng 20 s `restarting the radar`; Home Assistant vẫn có người. Cắm lại: `The LD2410 reports again`, rồi đếm 60 s từ đầu;
    - đổi Radar resolution: ngưỡng duy trì = ngưỡng radar và log nhắc học lại (nhiễu đo với độ phân giải khác); đổi về như cũ thì nhiễu được dùng lại;
    - log `mem`: heap thấp nhất, stack còn trống của `presence_eng` và `console_repl`;
    - không cắm máy tính (nguồn từ sạc): chạy như trên; nhiệt độ chip như biến thể `wroom_n4`.

Cũng chưa kiểm chứng: trả lời của lệnh 0x001B trên radar thật, nhận biết Bluetooth tắt qua địa chỉ giả, Apple Home không hiện Mode Select, biến thể `unique_setup` trên ESP32-C3.

## Host test

```bash
ESP32-C3/matter-presence-ld2410/test/host/run.sh   # cần g++ (C++17), không cần ESP-IDF
```

API ESP-IDF/FreeRTOS được thay bằng stub (`test/host/stubs/`); thời gian giả lập từng mili giây (`sim.h`); `radar_sim.cpp` giả lập LD2410: trả lời lệnh cấu hình, gửi báo cáo 100 ms một lần qua đúng driver của firmware, và tạo lỗi theo yêu cầu (lệnh bị bỏ lỡ, trả lời trễ, bị từ chối, không trả lời, khởi động lại không ACK, không giữ ngưỡng, radar cũ không có lệnh độ phân giải, Bluetooth hay đo nhiễu). Chạy một chương trình test với `-v` để in log giả lập.

Bộ xử lý hiện diện (`test_presence_engine.cpp`) được thử với phòng trống giả lập: mỗi kênh có nền và độ dao động riêng (nhiễu Gauss), cộng xung ngẫu nhiên; người là năng lượng chuyển động và tĩnh ở một vùng. Đây là mô hình tự đặt, không phải dữ liệu LD2410 thật.

## Cấu trúc thư mục

```
matter-presence-ld2410/
├── CMakeLists.txt                  # phiên bản theo số commit, component esp-matter, chỉ esp32c3
├── sdkconfig.defaults              # Wi-Fi, BLE thu gọn, log USB, PID 0x8001, chân SuperMini, 21 cluster
├── sdkconfig.defaults.unique_setup # mã ghép nối riêng từng bo (tuỳ chọn)
├── sdkconfig.defaults.wroom_n4     # bo ESP32-C3-WROOM-02-N4: LED WS2812 GPIO8, nhiệt độ chip (tuỳ chọn)
├── sdkconfig.defaults.espfilter    # bộ lọc năng lượng trên ESP32, Model riêng (dùng cùng wroom_n4)
├── sdkconfig.defaults.esppresence  # bộ xử lý hiện diện trên ESP32, Hold time 60 s, Model riêng (dùng cùng wroom_n4)
├── partitions.csv                  # 4 MB, 2 phân vùng OTA (giống hệt 1.0.132)
├── README.md
├── main/
│   ├── app_main.cpp                # Matter node, endpoint Occupancy Sensor, sự kiện, nút bấm, khởi động
│   ├── board.h                     # kiểm tra chân lúc build, tên bo trong log
│   ├── idf_component.yml           # espressif/led_strip (LED WS2812)
│   ├── presence.cpp                # task presence: radar -> Occupancy Sensing
│   ├── radar_control.cpp           # 4 Mode Select (Learn empty room, Range, Radar resolution, Radar Bluetooth)
│   ├── c3_temperature.cpp          # endpoint 5: nhiệt độ chip ESP32-C3 (tuỳ chọn, mặc định tắt)
│   ├── energy_filter.cpp/.h        # bộ lọc năng lượng trên ESP32 (thử nghiệm, mặc định tắt)
│   ├── presence_engine.cpp/.h      # bộ xử lý hiện diện: lọc, hai ngưỡng từ ngưỡng radar, nhiễu phòng trống (thử nghiệm)
│   ├── esp_presence.cpp/.h         # task presence_eng: báo cáo và ngưỡng radar -> bộ xử lý, nhiễu trong NVS, log
│   ├── esp_presence_console.cpp    # lệnh console LEARN_EMPTY, STATUS, DIAG... (esp_console)
│   ├── radar_worker.cpp            # task radar_cfg: yêu cầu, lựa chọn cuối cùng thắng, Bluetooth của radar (NVS)
│   ├── radar_settings.cpp          # một phiên thông số với radar, định dạng nhãn
│   ├── quiet_learn.cpp             # học phòng trống (radar tự đo / ESP32), ngưỡng mặc định
│   ├── device_info.cpp, device_name.cpp  # Basic Information, serial, tên thiết bị, NodeLabel, tên BLE
│   ├── setup_code.cpp              # in mã QR; mã ghép nối riêng từng bo (unique_setup)
│   ├── identity_diag.cpp           # log id_diag: danh tính, Wi-Fi, IP, fabric, subscription
│   ├── diag_report.cpp             # gửi lại Wi-Fi RSSI và Uptime (entity chẩn đoán của Home Assistant)
│   ├── net_watchdog.cpp            # watchdog kết nối: DNS-SD, nối lại Wi-Fi, khởi động lại
│   ├── mem_monitor.cpp             # log heap / stack
│   ├── chip_project_config.h       # tên DNS-SD trước khi ghép nối
│   ├── Kconfig.projbuild
│   └── drivers/
│       ├── ld2410.cpp              # radar LD2410 (UART1, 256000 baud): báo cáo, lệnh cấu hình
│       ├── status_led.cpp          # LED GPIO một màu hoặc WS2812
│       └── user_button.cpp         # nút BOOT: bấm 2/3/4 lần, giữ lâu
└── test/host/                      # host test (run.sh), giả lập thời gian và radar, stub ESP-IDF
```
