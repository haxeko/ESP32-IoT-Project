# Kịch bản kiểm tra: cắm cảm biến thì không commission được

Mục tiêu: tìm xem lỗi "cắm PMS5003 + SCD40 thì quạt chạy liên tục và không ghép nối (commission) được, tháo ra thì ghép bình thường" nằm ở **nguồn**, **sóng (anten)**, **đấu dây cảm biến** hay **firmware**, bằng cách chạy lần lượt các trường hợp dưới đây và ghi lại cùng một bộ số liệu cho mỗi trường hợp.

## 1. Chuẩn bị

### Firmware

| File | Dùng cho | Khác bản thường |
|---|---|---|
| `haxeko_pm5003_pcb_diag_v<ver>_esp32c3_update.bin` | Case A–E (kể cả D2), H | Bật chẩn đoán (`CONFIG_APP_DIAG`), console ra UART0, có lệnh `matter ...`, tắt factory reset bằng 5 lần cấp nguồn |
| `haxeko_pm5003_pcb_diag_nosensor_v<ver>_esp32c3_update.bin` | Case F | Như trên + không chạy driver PMS5003 và SCD40 |
| `haxeko_pm5003_pcb_diag_tx10_v<ver>_esp32c3_update.bin` | Case G | Như trên + Wi-Fi 10 dBm (thay vì 20), BLE 0 dBm (thay vì +9) |
| `haxeko_pm5003_pcb_diag_bod4_v<ver>_esp32c3_update.bin` | Case I | Như bản diag + reset do sụt áp ngay từ khoảng **2,92 V** (mức 4) thay vì 2,51 V (mức 7, mặc định) |

Nạp file `_update.bin` ở địa chỉ `0x1D000`: giữ nguyên bootloader, bảng phân vùng và NVS (ghép nối, bộ đếm chẩn đoán). Bảng phân vùng của bản chẩn đoán giống hệt bản thường. Bootloader cũ được giữ lại nên các dòng log của bootloader (`I (..) boot: ...`) vẫn ra cổng USB chứ không ra UART0; dòng của ROM và toàn bộ log của firmware thì có trên UART0. Xong việc, nạp lại `haxeko_pm5003_pcb_v<ver>_esp32c3_update.bin` (bản thường) cũng ở `0x1D000`.

```bash
esptool.py --chip esp32c3 -p /dev/ttyUSB0 -b 460800 write_flash 0x1d000 haxeko_pm5003_pcb_diag_v<ver>_esp32c3_update.bin
```

Nạp qua UART0 (cùng bộ chuyển USB-UART dùng làm console bên dưới): chip phải ở chế độ nạp, tức GPIO9 ở mức thấp lúc cấp nguồn hoặc reset.

Tự build:

```bash
idf.py -B build_diag -D SDKCONFIG=build_diag/sdkconfig \
       -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.diag" build
# case F: thêm ;sdkconfig.diag.nosensor   (thư mục build_diag_nosensor)
# case G: thêm ;sdkconfig.diag.lowtx      (thư mục build_diag_lowtx)
# case I: thêm ;sdkconfig.diag.bod        (thư mục build_diag_bod)
```

Bản chẩn đoán tốn thêm khoảng 10 KB RAM (khoảng 3 KB tĩnh, còn lại là stack của task ghi log và của shell, bộ đệm UART0). Log ra UART0 cũng chậm hơn log bị bỏ đi ở cổng USB không nối, nên các bước ghép nối chạy chậm hơn bản thường một chút. Nếu **case A cũng lỗi với bản chẩn đoán** mà bản thường thì không, xem dòng `heap: ... min=`: đó là thiếu RAM do chính bản chẩn đoán, không phải lỗi đang tìm.

### Console (log)

Cổng USB của PCB chỉ cấp nguồn, nên bản chẩn đoán đưa log ra **UART0**:

| ESP32-C3-WROOM-02 | Bộ chuyển USB-UART 3.3 V |
|---|---|
| TXD0 = **GPIO21** | RX |
| RXD0 = **GPIO20** | TX |
| GND | GND |

115200 baud, 8N1. Không nối chân 3.3 V/5 V của bộ chuyển (thiết bị vẫn lấy nguồn như lúc dùng thật, để phép thử nguồn có nghĩa). Ghi log ra file: `idf.py -p /dev/ttyUSB0 monitor | tee caseA.log`, hoặc `minicom -C caseA.log`, PuTTY *Session logging*.

Dòng khởi động của ROM (`ESP-ROM:... rst:0x.. (...),boot:0x.. (...)`) luôn ra UART0, kể cả với bản thường: chỉ riêng dòng này đã cho biết chip có reset lặp không và vì sao.

### Không có console: đọc màn hình OLED

Bản chẩn đoán thay dòng dưới cùng của màn hình (mức TỐT/KHÁ...) bằng, ví dụ, `N3 B2 W0 P0 S4`:

| Ký hiệu | Ý nghĩa |
|---|---|
| `N` | Số lần khởi động (reset) từ lúc xóa bộ đếm |
| `B` | Trong đó do **sụt áp** (brown-out) |
| `W` | Trong đó do **watchdog** (firmware treo) |
| `P` | Trong đó do **panic** (firmware lỗi: exception, tràn stack) |
| `S` | Bước commissioning xa nhất từ lúc khởi động: 0 chưa gì, 1 đang phát BLE, 2 điện thoại đã nối BLE, 3 PASE xong (mã ghép nối đúng), 4 đã vào Wi-Fi, 5 có địa chỉ IP, 6 CASE xong (nối qua mạng), 7 hoàn tất |

Bộ đếm lưu trong NVS nên vẫn còn sau chính lần reset đó, và cả sau factory reset (giữ nút 5 giây). **Bấm nút 3 lần** để xóa bộ đếm trước mỗi case; nạp file `_merged.bin` cũng xóa (cùng với ghép nối).

### Dụng cụ nên có

- Đồng hồ vạn năng; **oscilloscope** nếu có (đo sụt áp ngắn mà đồng hồ không thấy).
- Điện thoại có ứng dụng **nRF Connect** (xem RSSI của gói quảng bá BLE tên `AirQuality-3840`).
- Tùy chọn: máy Linux có `chip-tool` (mục 6).

## 2. Hành vi đúng để so sánh

- Sau khi cấp nguồn, quạt PMS5003 chạy **khoảng 35 giây rồi dừng**, và cứ 5 phút chạy lại 35 giây (đo theo chu kỳ, mặc định). Quạt chạy liên tục nhiều phút là **bất thường**, trừ case F (driver tắt nên cảm biến không bao giờ được lệnh ngủ).
- Màn hình sáng ngay khi cấp nguồn; LED đổi màu sau khoảng 35 giây (khi có số PM2.5 đầu tiên); số CO2 có sau khoảng 30 giây.
- Thiết bị chưa ghép nối phát BLE trong 15 phút sau khi cấp nguồn; hết giờ thì bấm đúp nút để mở lại 5 phút.
- **Không cắm/rút cảm biến khi đang có điện**: rút nguồn, cắm, rồi cấp nguồn lại. (Từ bản này, PMS5003 cắm nóng sẽ được cho ngủ lại sau vài giây thay vì chạy quạt tới chu kỳ sau, nhưng phép thử nguồn cần trạng thái khởi động thật.)

## 3. Quy trình cho mỗi case

Làm đúng thứ tự, mỗi case **3 lần**:

1. Rút nguồn. Cắm/tháo cảm biến theo case. Nối console, bắt đầu ghi log.
2. Cấp nguồn. Bấm nút 3 lần (xóa bộ đếm, màn hình hiện `N0 ...`), rồi rút và cấp nguồn lại: bộ đếm bắt đầu từ `N1`.
3. **Để yên 90 giây**, không mở ứng dụng. Ghi: quạt có dừng sau ~35 giây không; `N` có tăng không (reset khi chưa làm gì).
4. Nếu thiết bị đang ghép với ứng dụng nào: xóa khỏi ứng dụng, rồi giữ nút 5 giây (factory reset). `N` tăng 1 do khởi động lại: bình thường (loại "software").
5. Ghép nối bằng ứng dụng (Apple Home / Google Home / Home Assistant) hoặc `chip-tool`. Ghi giờ bắt đầu.
6. Chờ tới khi ứng dụng báo xong hoặc lỗi. Để thêm 2 phút.
7. Ghi vào bảng ở mục 8: dòng OLED cuối cùng, kết quả, thông báo lỗi của ứng dụng, và lưu file log.

## 4. Ma trận

| Case | PMS5003 | SCD40 | Firmware | Mục tiêu |
|---|---|---|---|---|
| A | Không | Không | diag | Mốc: ghép nối được |
| B | Có | Không | diag | Cô lập PMS5003 |
| C | Không | Có | diag | Cô lập SCD40 |
| D | Có | Có | diag | Tái hiện lỗi |
| D2 | Có | Có | diag | Như D, nhưng **chờ quạt dừng hẳn** (khoảng 40 giây sau khi cấp nguồn) rồi mới bắt đầu ghép: lúc đó PMS5003 ngủ, gần như không lấy dòng. Không cần dụng cụ gì, làm được cả với bản thường. Nếu quạt không bao giờ dừng: xem mục 9 |
| E | Có, **cấp 5 V riêng** (GND chung) | Có | diag | Loại trừ nguồn 5 V / sụt áp do quạt |
| F | Có | Có | **diag_nosensor** | Loại trừ driver cảm biến của firmware |
| G | Có | Có | **diag_tx10** | Giảm dòng đỉnh phát sóng: loại trừ nguồn 3.3 V lúc phát Wi-Fi/BLE. Để điện thoại và router trong vòng vài mét |
| H | Có, **nối dây dài ≥ 15 cm, đặt xa module** | Có, đặt xa module | diag | Vẫn tải điện như D nhưng vỏ kim loại và dây không nằm cạnh anten: loại trừ che/nhiễu anten |
| I | Có | Có | **diag_bod4** | "Vôn kế" không cần oscilloscope: module cần tối thiểu 3,0 V, bản này reset (và đếm `B`) ngay khi 3.3 V tụt dưới khoảng 2,92 V. Làm khi D lỗi mà `B` của bản diag không tăng; chạy thêm A với bản này để so sánh |

Lưu ý khi đọc case F: driver tắt nên **SCD40 không đo** (không có xung dòng 175–205 mA mỗi 5 giây), **quạt PMS5003 chạy liên tục** (tải 5 V lớn hơn bình thường) và **LED không sáng** (không có số PM2.5). F tách được lỗi phần mềm của driver, nhưng tải điện cũng khác D.

LED WS2812 chỉ sáng khi đã có số PM2.5: ở case A và C (không có PMS5003) LED tắt, ở B và D LED sáng từ khoảng giây thứ 35. Với 6 LED ở độ sáng 50 % (mặc định), dòng trung bình của LED chỉ khoảng 20–35 mA ở các màu chất lượng không khí, khoảng 60 mA khi nháy trắng (Identify): nhỏ so với PMS5003 và phần phát sóng, nhưng cũng là một tải chỉ có khi cắm PMS5003.

Thử nhanh không cần ứng dụng (console, bản diag): `matter esp wifi connect <ssid> <mật khẩu>` cho thiết bị vào Wi-Fi ngay, không qua BLE. So sánh giữa A và D: có vào được không, `rssi` bao nhiêu, có reset không. Đây là phép thử trực tiếp "phát Wi-Fi khi đang cắm cảm biến". Lệnh này lưu Wi-Fi vào thiết bị: thử xong phải factory reset (giữ nút 5 giây hoặc `matter esp factoryreset`) rồi mới thử ghép nối qua BLE, nếu không thiết bị đã ở sẵn trong mạng và ứng dụng sẽ đi đường khác.

## 5. Số liệu thu thập và cách đọc

### 5.1 Reset

| Nguồn | Dòng log |
|---|---|
| ROM (luôn có, UART0) | `rst:0x1 (POWERON)`, `rst:0xf (BROWNOUT_RST)`, `rst:0x7 (TG0WDT_SYS_RST)`, `rst:0xc (RTC_SW_CPU_RST)`, `rst:0x3 (RTC_SW_SYS_RST)`; `boot:0x.. (SPI_FAST_FLASH_BOOT)` là khởi động thường (số sau `boot:` tùy mức các chân strapping, thường là `0xc`), có chữ `DOWNLOAD` là chip vào chế độ nạp (chân GPIO9 bị kéo thấp lúc reset) |
| Firmware | `app_main: Reset reason: brown-out (9)` |
| Bản diag | `app_diag: resets: total=3 power-on=1 brown-out=2 watchdog=0 panic=0 software=0 other=0 \| last: brown-out` |
| Trước khi reset | `Brownout detector was triggered`; `task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:` + tên task; `Guru Meditation Error` / `Stack protection fault` + bản ghi thanh ghi |

- **`brown-out` tăng**: điện áp 3.3 V tụt dưới khoảng 2,51 V (ngưỡng mặc định của ESP-IDF; khoảng 2,92 V với bản `diag_bod4`). Các ngưỡng là ước lượng, lệch đôi chút giữa các chip. Lỗi nguồn.
- **`power-on` tăng mà không ai rút điện**: điện áp tụt sâu hơn nữa, hoặc nguồn 5 V bị ngắt. Lỗi nguồn.
- **`watchdog` hoặc `panic` tăng**: lỗi firmware. Gửi lại toàn bộ log quanh thời điểm đó (tên task trong dòng `task_wdt`, địa chỉ `MEPC`/`RA`).
- `software` tăng 1 sau factory reset hoặc OTA: bình thường.

### 5.2 Commissioning dừng ở bước nào

Mỗi bước có một dòng `app_diag: EVENT ... | stage=N heap free=... min=...` (bản diag) nằm cạnh các dòng của Matter SDK:

| Bước | `app_diag: EVENT` | Dòng của Matter SDK / ESP-IDF |
|---|---|---|
| 1 | `BLE advertising started` | `chip[DL]: CHIPoBLE advertising started` |
| 2 | `BLE connection from the commissioner` | `chip[DL]: BLE GAP connection established (con 1)` |
| 3 | `Commissioning session started (PASE done)` | `chip[SVR]: Commissioning completed session establishment step` |
| | (nhận Wi-Fi) | `chip[NP]: ESP NetworkCommissioningDelegate: SSID: ...`, `chip[DL]: Changing ESP WiFi mode`, `chip[DL]: WIFI_EVENT_STA_START` |
| 4 | `Wi-Fi connected` | `chip[DL]: WIFI_EVENT_STA_CONNECTED` |
| 5 | `IPv4 address assigned` / `IPv6 address assigned` | `chip[DL]: IP_EVENT_STA_GOT_IP`, `chip[DL]: IP_EVENT_GOT_IP6` |
| 6 | `CASE session established` | `chip[SC]: Received Sigma1 msg` |
| 7 | `Commissioning complete` | `chip[FS]: GeneralCommissioning: Received CommissioningComplete`, `chip[SVR]: Commissioning completed successfully` |
| Lỗi | `Fail-safe timer expired: the commissioning failed`, `BLE connection closed`, `Commissioning session stopped`, `Wi-Fi connection lost` | `chip[SVR]: Failsafe timer expired`, `chip[SC]: PASESession timed out while waiting for a response from the peer`, `app_main: Wi-Fi disconnected: reason N, RSSI R` |

Mã lý do mất Wi-Fi (`wifi_err_reason_t` của ESP-IDF): 2 hết hạn xác thực, 15 hết giờ bắt tay 4 bước (thường là sai mật khẩu hoặc sóng yếu), 200 mất beacon, 201 không thấy AP (sai tên mạng, mạng chỉ có 5 GHz, hoặc không thu được sóng), 202 xác thực thất bại, 203 kết hợp thất bại, 204 hết giờ bắt tay, 205 kết nối thất bại.

### 5.3 RAM và CPU

Mỗi 10 giây (bản diag):

```
app_diag: ---- up 120 s, stage 3 ----
app_diag: heap: free=41236 min=18520 largest=28672
app_diag: tasks (name:stack free/cpu%): IDLE:1100/91.2 CHIP:3320/3.1 local_ui:1480/0.8 pm_sensor:1900/0.1 ...
```

- `heap min` dưới khoảng 10000 trong lúc ghép nối, hoặc log có `alloc failed` / `No memory`: thiếu RAM.
- `stack free` của một task dưới khoảng 300: sắp tràn stack.
- `cpu%` của một task khác `IDLE` trên 50 % kéo dài nhiều chu kỳ: task đó chiếm CPU. Bình thường `IDLE` trên 80 % khi không ghép nối; `local_ui`, `pm_sensor`, `scd4x` mỗi task dưới 2 %.
- Bản thường cũng in `local_ui: heap: ...` và `local_ui: stack min free: ...` mỗi 30 giây.
- Cuối lúc khởi động: `app_main: main task stack min free: N`.

### 5.4 Cảm biến và màn hình

```
app_diag: pm: rx=1136 frames=34 checksum_err=0 other=1 dropped=8 gap_max=1012 ms reports=1 invalid=0 wake=1 sleep_retry=0
app_diag: co2: xfer=212 fail=0 max=1840 us crc_err=0 meas=12 starts=1 start_fail=0 reports=2 invalid=0
app_diag: oled: frames=118 write_err=0 inits=1 init_fail=0 frame_max=31 ms
```

| Trường | Khỏe mạnh | Bất thường nghĩa là |
|---|---|---|
| `pm: frames` | Tăng đều khi quạt chạy (cảm biến gửi một khung mỗi 0,2–2,3 giây), đứng yên khi ngủ | Không tăng khi quạt chạy: dây TX cảm biến → GPIO6 |
| `pm: checksum_err` | 0 | Nhiễu trên dây UART |
| `pm: other`, `dropped` | Mỗi chu kỳ đo: `other` +1 hoặc +2, `dropped` +8 tới +16 (dự kiến: cảm biến trả lời mỗi lệnh bằng một khung 8 byte, làm lệch một khung đo lúc vừa thức) | Tăng liên tục trong lúc quạt chạy: nhiễu, sai tốc độ |
| `pm: gap_max` | Dưới 2500 ms | Lớn hơn: cảm biến ngắt quãng (nguồn 5 V) |
| `pm: sleep_retry` | 0 | Tăng mãi (kèm `pm_sensor: Sensor still sends data after the sleep command`): lệnh ngủ không tới cảm biến, kiểm tra dây GPIO7 → RX cảm biến. Đây là một nguyên nhân làm quạt chạy liên tục |
| `pm: wake` | 1 lúc khởi động, +1 mỗi 5 phút | |
| `co2: fail`, `start_fail` | 0 | SCD40 không trả lời: dây, nguồn, địa chỉ |
| `co2: crc_err` | 0 | Nhiễu trên bus I2C |
| `co2: starts` | 1 | Lớn hơn 1: SCD40 bị reset (sụt nguồn 3.3 V) rồi được khởi động lại |
| `co2: max` | Thường 1–2 ms; tới khoảng 35 ms khi trùng lúc màn hình đang nhận một khung hình | Gần 100000 µs (kèm `fail` tăng): bus I2C bị giữ, hết giờ chờ |
| `oled: write_err`, `inits` | 0, 1 | Tăng: màn hình mất liên lạc/bị reset (nguồn, bus I2C) |

Thời gian đọc: UART được đọc theo khối với thời gian chờ 200 ms nên "thời gian một lần đọc" không có nghĩa; số đo hữu ích là khoảng cách giữa hai khung hợp lệ (`gap_max`). Với I2C, `max` là thời gian dài nhất của một lần truyền (tính cả lúc chờ bus do màn hình đang dùng).

### 5.5 Sóng

- `app_diag: wifi: connected, rssi=-58 dBm, channel 6`: so sánh A với D. Chênh trên 10 dB là anten bị ảnh hưởng.
- nRF Connect: RSSI của `AirQuality-3840` ở cùng khoảng cách, có và không có cảm biến.

## 6. chip-tool

```bash
# Ghép nối qua BLE + Wi-Fi (node id 1), passcode và discriminator mặc định của firmware thử nghiệm
chip-tool pairing ble-wifi 1 "<ssid>" "<mật khẩu>" 20202021 3840
# hoặc bằng mã ghép nối trên màn hình (bấm đúp nút)
chip-tool pairing code-wifi 1 "<ssid>" "<mật khẩu>" 34970112332
# thiết bị đã ở trong Wi-Fi (sau "matter esp wifi connect ..." hoặc bấm đúp nút)
chip-tool pairing onnetwork 1 20202021

# Đọc giá trị
chip-tool airquality read air-quality 1 1
chip-tool pm25concentrationmeasurement read measured-value 1 1
chip-tool carbondioxideconcentrationmeasurement read measured-value 1 1
chip-tool basicinformation read software-version-string 1 0
chip-tool generaldiagnostics read reboot-count 1 0
chip-tool generaldiagnostics read boot-reason 1 0
chip-tool wifinetworkdiagnostics read rssi 1 0

# Xóa
chip-tool pairing unpair 1
```

`chip-tool` in ra từng bước của phía commissioner (`CHIP:CTL: Performing next commissioning step '...'`): bước cuối cùng trước khi lỗi, đặt cạnh `stage` của thiết bị, cho biết bên nào dừng.

## 7. Lệnh trên console của thiết bị (bản diag)

```
matter help
matter config                          # VID/PID, discriminator, passcode
matter onboardingcodes ble             # mã QR và mã ghép nối thủ công
matter ble adv state                   # đang phát BLE không; "adv start" / "adv stop"
matter wifi mode                       # chế độ Wi-Fi
matter esp wifi connect <ssid> <pass>  # vào Wi-Fi không cần ứng dụng
matter esp diagnostics mem-dump        # RAM trống hiện tại, khối lớn nhất, mức thấp nhất
matter esp diagnostics up-time
matter stat peak                       # mức dùng tài nguyên cao nhất của Matter
matter esp attribute get 0x1 0x42A 0x0 # PM2.5 MeasuredValue
matter esp attribute get 0x1 0x40D 0x0 # CO2 MeasuredValue
matter esp attribute get 0x1 0x5B 0x0  # AirQuality
matter esp factoryreset
```

## 8. Bảng ghi kết quả

| Case | Lần | Quạt dừng sau ~35 s? | OLED lúc để yên 90 s | OLED sau khi ghép | Kết quả | Bước dừng (`stage`) | `heap min` | Lý do Wi-Fi / RSSI | Ghi chú |
|---|---|---|---|---|---|---|---|---|---|
| A | 1 | | | | | | | | |
| A | 2 | | | | | | | | |
| ... | | | | | | | | | |

## 9. Suy ra nguyên nhân

| Quan sát | Kết luận | Việc tiếp theo |
|---|---|---|
| `B` tăng (hoặc `rst:0xf`), nhất là ngay sau khi cấp nguồn hoặc đúng lúc vào Wi-Fi | Nguồn 3.3 V sụt | Mục 10; E và G cho biết sụt từ phía 5 V hay phía 3.3 V |
| `N` tăng với `power-on` mà không rút điện | Nguồn tụt rất sâu hoặc ngắt | Như trên; kiểm tra cáp, cầu chì, mạch RC ở chân EN |
| ROM in `DOWNLOAD` / `waiting for download` | Chân GPIO9 thấp lúc reset | Điện trở kéo lên GPIO9 (và GPIO2, GPIO8), dây cảm biến đi gần |
| Màn hình tối, không có gói BLE, quạt chạy mãi | Chip không chạy firmware | Đo 3.3 V; dòng ROM trên UART0 |
| `W` hoặc `P` tăng | Firmware | Gửi log; so với F |
| `N` không tăng, dừng ở `S1` | Điện thoại không nối được BLE: sóng | nRF Connect; case H |
| Dừng ở `S2`–`S3`, `PASESession timed out` | Liên kết BLE chập chờn (sóng), hoặc thiếu CPU/RAM | `heap min`, `cpu%`; case H, G |
| Dừng ở `S3`, có `Wi-Fi disconnected: reason 201/15/205/2` | Không vào được Wi-Fi: sóng hoặc sụt áp khi phát (cũng có thể sai mật khẩu, mạng 5 GHz) | So với A cùng mạng; `matter esp wifi connect`; case G, H |
| Dừng ở `S4`–`S5` | Đã có mạng nhưng controller không tìm thấy thiết bị (mDNS/IPv6 của mạng) | Không liên quan cảm biến nếu A cũng vậy |
| D lỗi, **D2 được** (quạt đã dừng mới ghép) | Tải hoặc nhiễu của PMS5003 lúc quạt chạy | E để tách nguồn 5 V với nhiễu/che anten (H) |
| Quạt không bao giờ dừng, `N` tăng dần. Không cần bản diag cũng thấy: màn hình cứ quay lại màn hình khởi động, không bao giờ hiện số PM2.5, LED không sáng | Chip reset lặp lại trước khi kịp gửi lệnh ngủ (35 giây sau mỗi lần khởi động) | Loại reset trong dòng `resets:` cho biết vì sao |
| Quạt không bao giờ dừng, `N` đứng yên, màn hình vẫn hiện số PM2.5 và LED sáng | Lệnh ngủ không tới cảm biến (`pm: sleep_retry` tăng) | Dây GPIO7 → RX cảm biến |
| D lỗi, **E được** | Sụt áp phía 5 V do PMS5003 | Sửa đường 5 V (mục 10) |
| D và E lỗi, **G được** | Dòng đỉnh khi phát sóng vượt khả năng mạch 3.3 V | LDO, tụ (mục 10) |
| D, E, G lỗi, **H được** | Anten bị che hoặc nhiễu bởi cảm biến/dây | Bố trí lại, xem mục 10 |
| D lỗi với `B` = 0, **I có `B` tăng** (và A với bản bod4 thì không) | Nguồn 3.3 V tụt xuống khoảng 2,5–2,9 V khi cắm cảm biến: dưới mức tối thiểu của module nhưng chưa tới ngưỡng reset mặc định, nên bản thường không reset mà chỉ lỗi sóng/ghép nối | LDO, tụ (mục 10) |
| D lỗi, **I không có `B`** | 3.3 V không tụt dưới khoảng 2,9 V: ít khả năng là nguồn 3.3 V | Sóng (H), đấu dây (B, C) |
| D lỗi, **F được**, không có reset | Driver cảm biến, hoặc dòng đo của SCD40 (F không cho SCD40 đo) | Tách bằng B và C; gửi log `pm:`/`co2:`/`tasks` |
| B được, **C lỗi** | SCD40: dòng đỉnh trên 3.3 V, dây/điện trở kéo lên I2C, thứ tự chân giắc | Đo 3.3 V khi cắm SCD40; `co2:` và `oled:` |
| **B lỗi**, C được | PMS5003: tải 5 V, nhiễu quạt, vỏ kim loại gần anten | E, H |
| `pm: sleep_retry` tăng mãi, mọi thứ khác bình thường | Dây GPIO7 → RX cảm biến hở: quạt không bao giờ dừng, không liên quan ghép nối | Kiểm tra giắc J2B chân 4 |

## 10. Đo phần cứng

1. **3.3 V tại chân 3V3 của module**, oscilloscope, trigger sườn xuống ở 3,0 V, trong lúc cấp nguồn và lúc ghép nối. Module cần 3,0–3,6 V và nguồn cấp được ít nhất 0,5 A (datasheet ESP32-C3-WROOM-02, bảng 6-2); dòng phát Wi-Fi đỉnh 345 mA (bảng 6-4). SCD40 lấy thêm đỉnh 175 mA, tối đa 205 mA ở 3.3 V (datasheet SCD4x, bảng 4).
2. **5 V tại giắc PMS5003** lúc quạt khởi động: PMS5003 cần 4,5–5,5 V, dòng tới 100 mA.
3. **5 V tại đầu vào LDO** lúc phát Wi-Fi: với LDO loại 1117 (sụt áp khoảng 1,1 V) cần trên 4,4 V.
4. Điện áp nghỉ của **SDA/SCL**: phải là 3,3 V. Nếu thấy cao hơn, module SCD40 đang kéo lên 5 V.
5. Chân **EN**, **GPIO9**, **GPIO8**, **GPIO2** khi cảm biến đã cắm: đều phải ở mức cao.
