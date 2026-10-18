# Matter Presence Sensor + học phòng trống (ESP32-C3 SuperMini + HLK-LD2410C, Wi-Fi)

Cảm biến hiện diện chuẩn Matter (device type **Occupancy Sensor 0x0107**) với radar 24 GHz **HLK-LD2410C**, chạy **Matter over Wi-Fi** trên **ESP32-C3 SuperMini**. Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa... qua Wi-Fi nhà, không cần Thread Border Router. Radar LD2410 phát hiện được cả người ngồi yên (thở, cử động nhẹ), khác cảm biến PIR chỉ thấy chuyển động.

Đây là bản **viết lại từ đầu** của firmware ESP32-C3 1.0.132 (`ESP32-C3/matter-wifi-presence`, dùng chung mã với thư mục ESP32-H2). Project này **tự chứa**: không dùng file nào của thư mục ESP32-H2, mã chỉ cho ESP32-C3. Chức năng, thuật toán, ngưỡng, thời gian, endpoint và khoá lưu trữ giữ nguyên như 1.0.132, nên **cập nhật OTA từ 1.0.132 lên bản này giữ nguyên ghép nối và các entity Home Assistant** (xem **Mức độ kiểm tra**: điều này mới kiểm chứng bằng so sánh cấu hình và build, chưa thử trên bo).

Trong Home Assistant có thêm 5 điều khiển (Apple Home không hiện): **Hold time**, **Learn empty room** (học phòng trống, hoặc bấm nút BOOT 3 lần; *Factory thresholds* hoặc bấm 4 lần để ghi lại ngưỡng mặc định), **Radar Bluetooth**, **Radar resolution**, **Range**. Tên thiết bị là **Presence-** + 4 ký tự cuối của số serial, ví dụ **Presence-B2C4**. Các entity chẩn đoán **Reboot count**, **Uptime**, **Wi-Fi RSSI** bị Home Assistant tắt sẵn: bật chúng trong Home Assistant (xem **Entity chẩn đoán**); firmware gửi lại Uptime và Wi-Fi RSSI để giá trị không bị đứng.

## Khác biệt so với bản cũ (`ESP32-C3/matter-wifi-presence`, 1.0.132)

| | 1.0.132 (`matter-wifi-presence`) | Bản này (`matter-presence-ld2410`) |
|---|---|---|
| Mã nguồn | `main/`, bảng phân vùng, sdkconfig của thư mục ESP32-H2 (chung với bản Thread) | tự chứa, chỉ ESP32-C3 |
| Chức năng, thuật toán, log | — | giống hệt, thêm báo cáo Wi-Fi RSSI và Uptime (dòng dưới) |
| Wi-Fi RSSI, Uptime trong Home Assistant | chỉ gửi lúc Home Assistant subscribe: RSSI đứng giá trị, Uptime lệch sau khi Home Assistant khởi động lại | gửi lại RSSI khi đổi từ 3 dB (kiểm tra mỗi phút), cả RSSI và uptime 10 phút một lần (`menuconfig`; đặt 0 thì như cũ) |
| VID / PID, endpoint, cluster, tên entity, mã mode | `0xFFF1` / `0x8001`, endpoint 1–4 | giống hệt |
| Bảng phân vùng, khoá NVS/KVS | — | giống hệt |
| sdkconfig sinh ra | — | giống hệt (chỉ khác đường dẫn file, mục chọn loại LED đã bỏ và 2 tuỳ chọn mới `DIAG_REPORT_*`) |
| LED | chọn được WS2812 / LED GPIO / không có | chỉ LED GPIO một màu (thư viện `espressif/led_strip` bỏ) |
| Biến thể build | mặc định, `unique_setup`, `factory` | mặc định, `unique_setup` (`factory` bỏ: PAI test chỉ cấp DAC cho PID `0x8000`) |
| Mã Thread/OpenThread (`thread_diag`, phần OpenThread của `id_diag`, OT CLI) | có, tắt khi build C3 | bỏ |
| Tên file build | `matter_presence_c3_wifi*` | giống hệt |
| Kích thước (`idf.py size`, cùng Docker image) | 1 478 KB, RAM tĩnh 147,0 KB | 1 478 KB, RAM tĩnh 147,0 KB (lớn hơn 160 byte flash vì có thêm `diag_report`, nhỏ hơn 54 byte RAM) |

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

LED của SuperMini chỉ có một màu, mọi "màu" của bản ESP32-H2 đều là "sáng". Thứ tự ưu tiên: Identify, rồi học phòng trống / ngưỡng mặc định, rồi chế độ ghép nối.

| LED | Trạng thái | Bản H2 |
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

## Học phòng trống

Trong phòng không có ai, radar vẫn nhận "năng lượng" từ quạt, rèm bay, cây cảnh, phản xạ đồ vật... Ở gate nào năng lượng nền vượt ngưỡng, radar báo nhầm có người. Học phòng trống đo năng lượng nền rồi đặt ngưỡng từng gate trên mức đó.

1. Chọn **Learning** trong Home Assistant, hoặc **bấm nút BOOT 3 lần** (không cần ghép nối hay mạng). Log: `Empty room learning: leave the room, recording starts in 20 s and lasts 60 s`.
2. **Rời khỏi phòng** trong 20 s (LED sáng liên tục). Radar nhìn xa khoảng 6 m và xuyên cửa gỗ, vách mỏng. Quạt thường chạy thì để chạy: đó là nhiễu cần học.
3. Đo 60 s (LED nháy chậm), theo một trong hai cách bên dưới.
4. Xong: LED sáng 10 s, entity trở về **Off** (khoảng 80 s sau khi chọn). Kết quả và lý do thất bại xem trong log `quiet_learn`.

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
- **Từ 1.0.132 lên bản này:** cùng VID/PID, endpoint, bảng phân vùng và khoá lưu trữ, nên ghép nối, Hold time, lựa chọn Radar Bluetooth và các entity được giữ. Chưa thử trên bo; nếu entity hiện sai sau khi cập nhật, dùng **Re-interview** trên trang thiết bị trong Home Assistant.
- Sau khi đã cập nhật OTA, thiết bị chạy từ phân vùng `ota_1`: nạp USB thì dùng file merged hoặc `idf.py flash` (ghi cả `otadata`), không ghi riêng `0x20000`.

## Cấu hình (`idf.py menuconfig`)

Tên các tuỳ chọn giữ như bản 1.0.132; menu *Wi-Fi RSSI and Uptime Reports* là mới.

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
| | `STATUS_LED_GPIO` / `STATUS_LED_ACTIVE_HIGH` | 8 / tắt | 0–21 |
| | `RESET_BUTTON_GPIO` | 9 | −1 (không có nút) – 21 |
| | `PRESENCE_WIFI_STAY_AWAKE` | bật | |
| Empty Room Learning | `LD2410_LEARN_DELAY_SEC` / `LD2410_LEARN_DURATION_SEC` | 20 / 60 s | 0–120 / 10–300 |
| | `LD2410_LEARN_RADAR_AUTO` | bật | |
| | `LD2410_LEARN_PERCENTILE` / `_MARGIN` / `_MAX_RAISE` | 95 / 10 / 30 | 50–100 / 0–50 / 0–100 |
| Identity Diagnostics and Setup Code | `IDENTITY_DIAG_PERIOD_SEC` | 300 | 0 (tắt) – 86400 |
| | `IDENTITY_DIAG_SUBSCRIPTION_LOG` / `PRINT_ONBOARDING_CODES` | bật / bật | |
| | `UNIQUE_SETUP_CODE`, `UNIQUE_SETUP_CODE_PARTITION` | bật khi build `unique_setup`, `fctry` | |
| Wi-Fi RSSI and Uptime Reports (mới) | `DIAG_REPORT_RSSI_STEP_DB` (gửi lại Wi-Fi RSSI khi đổi từng này dB) | 3 | 0 (chỉ theo chu kỳ) – 30 |
| | `DIAG_REPORT_REFRESH_MIN` (gửi lại Wi-Fi RSSI và uptime mỗi) | 10 phút | 0 (không) – 1440 |
| Memory Monitor | `MEM_MONITOR_PERIOD_SEC` | 600 | 0 (chỉ khi khởi động) – 86400 |

Console Matter (gõ lệnh trong monitor) tắt để có thêm khoảng 10 KB RAM: bật lại bằng *Component config* → *CHIP Core* → *General Options* → *Use the CHIP shell library* (`CONFIG_ENABLE_CHIP_SHELL`). Khi đó có lệnh của esp-matter (diagnostics, factoryreset, attribute) và `matter wifi ...` của Matter SDK; mật khẩu gõ vào lệnh `matter wifi connect` hiện trên màn hình và nằm trong lịch sử lệnh.

## Wi-Fi và BLE

- **Wi-Fi station** (không SoftAP). Thông tin Wi-Fi đến từ lúc ghép nối; kết nối và kết nối lại do Matter SDK lo, firmware không tự gọi `esp_wifi_connect`.
- **Lúc ghép nối**, BLE và Wi-Fi dùng chung anten 2,4 GHz (*coexistence* của ESP-IDF); firmware giữ mặc định của ESP-IDF (Wi-Fi có modem sleep `min_modem`).
- **Sau khi BLE tắt, Wi-Fi tắt modem sleep** (`PRESENCE_WIFI_STAY_AWAKE`, mặc định bật): khi có sự kiện `kBLEDeinitialized`, và ở mỗi lần Wi-Fi kết nối sau đó, firmware gọi `esp_wifi_set_ps(WIFI_PS_NONE)` (log `BLE released: Wi-Fi power save off`, `id_diag` ghi `power_save=none`). Lý do: log 1.0.128 có `Msg Retransmission`. Cách giải thích (ở modem sleep, router giữ gói cho bo tới beacon DTIM kế tiếp) là phân tích, chưa đo; hiệu quả của việc tắt modem sleep chưa được kiểm chứng.
- **Không giới hạn công suất phát Wi-Fi.** Bản 1.0.125–1.0.127 đã thử 8,5 dBm, không giúp được; nguyên nhân lỗi kết nối là Beamforming của router TP-Link Deco X10 (tắt đi thì ghép nối được).
- **BLE thu gọn** (NimBLE, chỉ để ghép nối): vai trò peripheral, 1 kết nối, không bảo mật BLE (Matter tự bảo mật bằng PASE), không có tính năng BLE 5, bộ đệm nhỏ (mục *BLE Optimizations* của hướng dẫn tối ưu RAM của esp-matter).
- Mã Wi-Fi nằm trong flash thay vì IRAM (`ESP_WIFI_IRAM_OPT=n`, `ESP_WIFI_RX_IRAM_OPT=n`): trên ESP32-C3, IRAM và DRAM dùng chung SRAM, nên có thêm heap.

## RAM và flash

Đo bằng `idf.py size`, cấu hình mặc định, cả hai bản build trong cùng Docker image `release-v1.6.1_idf_v6.0.2` (image được build lại ngày 2026-10-15 với connectedhomeip `b1902b39`):

| | 1.0.132 (`matter-wifi-presence`) | Bản này |
|---|---|---|
| Firmware (phân vùng app 1 920 KB) | 1 513 104 B (1 478 KB), trống 23 % | 1 513 264 B (1 478 KB), trống 23 % |
| RAM tĩnh (DRAM 313,8 KB) | 150 565 B (147,0 KB, 46,86 %) | 150 511 B (147,0 KB, 46,84 %) |
| Còn cho heap | 170 731 B (166,7 KB) | 170 785 B (166,8 KB) |
| Biến thể `unique_setup` | | 1 515 408 B |

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
I radar_ctl: LD2410 settings: no-one duration 5 s, range moving 6 m (gate 8), still 6 m (gate 8), 75 cm per gate, Bluetooth off
```

Số serial là MAC gốc trong eFuse (6 byte, 12 ký tự hex in hoa), cũng là MAC Wi-Fi router hiển thị. Hiện diện: `presence: Radar: presence (moving yes 120 cm, still no 0 cm)`, `presence: Radar: no one (...)`. Radar không gửi gì 10 s: `E presence: No data from the LD2410 for 10 s, check the wiring (radar TX -> GPIO3, RX -> GPIO4)` và báo không có người; có dữ liệu lại thì `Radar data received`. Sự kiện Matter: `Commissioning complete`, `Commissioning failed, fail safe timer expired`, `Commissioning window opened / closed`, `Fabric removed successfully`, `Wi-Fi connectivity changed`, `Interface IP Address changed`.

**Khối `id_diag`** (lúc khởi động, 5 phút một lần, và sau sự kiện Wi-Fi, IP, ghép nối, fabric, gộp lại cách nhau ít nhất 5 s):

```
I id_diag: hw   efuse_mac=58CF79A1B2C4 wifi_mac=58CF79A1B2C4 reset=power-on up=300s heap=... min_heap=...
I id_diag: dev  vid=0xFFF1 pid=0x8001 serial=58CF79A1B2C4 unique_id=... discriminator=3840 test_setup_code=yes
I id_diag: mtr  commissioned=yes fabrics=2 window_open=no failsafe=no sessions: case=2 pase=0, subscriptions=3 reads=0
I id_diag: fab  index=1 vendor=0x.... node_id=... fabric_id=... compressed_fabric_id=... case=1 subscriptions=2 label="..."
I id_diag: wifi connected ssid="..." bssid=... ch=6 rssi=-55 power_save=none
I id_diag: ip   ipv4=192.168.1.20 ipv6: link-local=1 unique-local=1 global=1
I id_diag: nvs  entries used=... free=... available=... total=... namespaces=...
```

- Mỗi lần vào hoặc rời Wi-Fi: `EV wifi joined bssid=... ch=... security=WPA2`, hoặc `EV wifi failed or left: reason N (...), bssid=... rssi=-55 dBm` (xem **Xử lý sự cố**).
- Subscription: `SUB request`, `SUB established`, `SUB terminated` kèm fabric, peer và chu kỳ báo cáo (chỉ quan sát, không đổi subscription).
- `reset=` là `BROWNOUT`, `PANIC` hoặc `WATCHDOG`: bo bị khởi động lại bất thường (nguồn yếu, lỗi firmware). `SUB terminated` lặp lại: controller mất subscription.
- Đã kết nối mà chưa có IPv6 link-local: cảnh báo `No IPv6 link-local address yet`. `power_save=min_modem` khi BLE còn chạy, `none` sau khi BLE tắt.
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
  - **Phân biệt lỗi mạng với khởi động lại:** nếu sau khi nối lại vẫn dùng cùng số phiên (`@1:5a•580e`), thiết bị **không** khởi động lại (khởi động lại thì mất mọi phiên), mà là mất Wi-Fi hoặc không nhận được gói. Kiểm tra *Wi-Fi RSSI*, *Reboot count*, và log USB (`EV wifi failed or left: reason ...`, `W mem: Heap low`).
- Mỗi lần thử lại ghép nối: factory reset (giữ BOOT 5 s) hoặc `erase-flash`.

## Mức độ kiểm tra

**Đã kiểm chứng bằng build và test trên máy tính (bản này):**
- Build `esp32c3` trong Docker `release-v1.6.1_idf_v6.0.2`: **0 lỗi, 0 cảnh báo**, cả bản mặc định và biến thể `unique_setup`.
- `sdkconfig` sinh ra giống hệt bản 1.0.132 build trong cùng image, trừ đường dẫn file, mục chọn loại LED đã bỏ và 2 tuỳ chọn mới `DIAG_REPORT_RSSI_STEP_DB`, `DIAG_REPORT_REFRESH_MIN` (so với bản 1.0.139 chỉ khác 2 tuỳ chọn này): Wi-Fi bật, OpenThread tắt, BLE chỉ để ghép nối, minimal mDNS, cửa sổ ghép nối 900 s, VID `0xFFF1` / PID `0x8001`, WPA3 SAE, coexistence, stack CHIP 8192, CPU 160 MHz, brownout bật, đúng 21 cluster.
- Header file OTA (bản build 1.0.140): VID `0xFFF1`, PID `0x8001`, phiên bản 140 / `1.0.140` (lớn hơn 132).
- Thứ tự tạo endpoint giống 1.0.132; esp-matter (`esp_matter_data_model.cpp`) cấp số endpoint theo thứ tự tạo trước `esp_matter::start()`, nên số endpoint giữ nguyên.
- Kích thước so với 1.0.132 (bảng **RAM và flash**): lớn hơn 160 byte flash, nhỏ hơn 54 byte RAM tĩnh. `diag_report` (báo cáo lại Wi-Fi RSSI và Uptime) chiếm 416 byte flash và 16 byte RAM tĩnh so với bản 1.0.139 chưa có nó.
- Host test (`test/host/run.sh`, 12 chương trình): parser và lệnh radar, phiên thông số, học trên ESP32 và radar tự đo, điều phối (mọi thứ tự Learning/Off/Factory, kể cả lúc radar đang đo), presence, nút, LED, tên thiết bị, lúc nào gửi lại Wi-Fi RSSI và Uptime. Thử làm hỏng 19 chỗ trong firmware (mỗi lần một chỗ): test bắt được 18; chỗ còn lại (đổi thứ tự ghi ngưỡng mặc định và học trong vòng lặp) không đổi hành vi vì hai yêu cầu đó không bao giờ cùng chờ. Làm hỏng thêm 10 chỗ trong `diag_report.h`: test bắt được cả 10.
- Phần gửi lại Wi-Fi RSSI và Uptime: chỉ phần quyết định được test trên máy tính. Lời gọi Matter (`MatterReportingAttributeChangeCallback`, timer của Matter) mới kiểm chứng bằng build và bằng đọc mã nguồn Matter SDK, esp-matter, Home Assistant (đánh dấu đã đổi → subscription gửi lại; Home Assistant cập nhật theo giá trị).

**Đã thấy chạy trên bo, nhưng với firmware cũ** (log của người dùng; theo README cũ là các bản 1.0.128–1.0.131, cùng thuật toán và mã radar): ghép nối với Apple Home và Home Assistant (3 controller cùng lúc), đọc radar, radar tự đo phòng trống (lệnh 0x000B), *Factory thresholds* từ Home Assistant; bật Bluetooth của radar (0x00A4 + 0x00A3, đọc MAC bằng 0x00A5) trên radar V2.44.25070917.

**Chưa thử trên bo với bản này (cần thử, ghi kết quả vào đây):**
1. Log khởi động đúng; radar được đọc (`LD2410 settings: ...`).
2. Ghép nối BLE + Wi-Fi với Apple Home và Home Assistant; thêm controller thứ hai bằng bấm đúp (chỉ DNS-SD).
3. `BLE deinitialized ...`, `BLE released: Wi-Fi power save off`, `id_diag` báo `power_save=none`; ít `Msg Retransmission` hơn bản 1.0.131.
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

Cũng chưa kiểm chứng: trả lời của lệnh 0x001B trên radar thật, nhận biết Bluetooth tắt qua địa chỉ giả, Apple Home không hiện Mode Select, biến thể `unique_setup` trên ESP32-C3.

## Host test

```bash
ESP32-C3/matter-presence-ld2410/test/host/run.sh   # cần g++ (C++17), không cần ESP-IDF
```

API ESP-IDF/FreeRTOS được thay bằng stub (`test/host/stubs/`); thời gian giả lập từng mili giây (`sim.h`); `radar_sim.cpp` giả lập LD2410: trả lời lệnh cấu hình, gửi báo cáo 100 ms một lần qua đúng driver của firmware, và tạo lỗi theo yêu cầu (lệnh bị bỏ lỡ, trả lời trễ, bị từ chối, không trả lời, khởi động lại không ACK, không giữ ngưỡng, radar cũ không có lệnh độ phân giải, Bluetooth hay đo nhiễu). Chạy một chương trình test với `-v` để in log giả lập.

## Cấu trúc thư mục

```
matter-presence-ld2410/
├── CMakeLists.txt                  # phiên bản theo số commit, component esp-matter, chỉ esp32c3
├── sdkconfig.defaults              # Wi-Fi, BLE thu gọn, log USB, PID 0x8001, chân SuperMini, 21 cluster
├── sdkconfig.defaults.unique_setup # mã ghép nối riêng từng bo (tuỳ chọn)
├── partitions.csv                  # 4 MB, 2 phân vùng OTA (giống hệt 1.0.132)
├── README.md
├── main/
│   ├── app_main.cpp                # Matter node, endpoint Occupancy Sensor, sự kiện, nút bấm, khởi động
│   ├── board.h                     # ESP32-C3 SuperMini: kiểm tra chân lúc build
│   ├── presence.cpp                # task presence: radar -> Occupancy Sensing
│   ├── radar_control.cpp           # 4 Mode Select (Learn empty room, Range, Radar resolution, Radar Bluetooth)
│   ├── radar_worker.cpp            # task radar_cfg: yêu cầu, lựa chọn cuối cùng thắng, Bluetooth của radar (NVS)
│   ├── radar_settings.cpp          # một phiên thông số với radar, định dạng nhãn
│   ├── quiet_learn.cpp             # học phòng trống (radar tự đo / ESP32), ngưỡng mặc định
│   ├── device_info.cpp, device_name.cpp  # Basic Information, serial, tên thiết bị, NodeLabel, tên BLE
│   ├── setup_code.cpp              # in mã QR; mã ghép nối riêng từng bo (unique_setup)
│   ├── identity_diag.cpp           # log id_diag: danh tính, Wi-Fi, IP, fabric, subscription
│   ├── diag_report.cpp             # gửi lại Wi-Fi RSSI và Uptime (entity chẩn đoán của Home Assistant)
│   ├── mem_monitor.cpp             # log heap / stack
│   ├── chip_project_config.h       # tên DNS-SD trước khi ghép nối
│   ├── Kconfig.projbuild
│   └── drivers/
│       ├── ld2410.cpp              # radar LD2410 (UART1, 256000 baud): báo cáo, lệnh cấu hình
│       ├── status_led.cpp          # LED GPIO một màu
│       └── user_button.cpp         # nút BOOT: bấm 2/3/4 lần, giữ lâu
└── test/host/                      # host test (run.sh), giả lập thời gian và radar, stub ESP-IDF
```
