# Haxeko Smart Fan: quạt 3 số có đảo gió, Matter over Wi-Fi + mắt hồng ngoại (ESP32-C3)

Firmware ESP-IDF + [esp-matter](https://github.com/espressif/esp-matter) cho **ESP32-C3** (SuperMini hoặc DevKitM-1) biến một quạt đứng/quạt treo **3 số có trục xoay (đảo gió)** thành thiết bị **Quạt Matter** (device type Fan, 0x002B). Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa qua **Wi-Fi**, ghép nối bằng **BLE**.

- **4 relay**: 3 relay chọn số (1 / 2 / 3) và 1 relay bật/tắt động cơ trục xoay (đảo gió).
- **Thanh tốc độ chỉ có 3 vị trí khi bật: 33 %, 66 %, 100 %** (tương ứng số 1, 2, 3).
- **Nút đảo gió bên dưới thanh tốc độ** (giao diện quạt kiểu *Fan v2* của Apple Home) điều khiển relay trục xoay.
- **Mắt thu hồng ngoại**: dùng lại remote gốc của quạt (học lệnh ngay trên thiết bị, nhận mọi loại remote).
- Nút BOOT trên mạch: bật/tắt, ghép nối, học remote, factory reset. Thêm được nút số và nút đảo gió ngoài.

## Cách quạt hoạt động

### Tốc độ: 3 vị trí 33 / 66 / 100 %

| Kéo thanh tốc độ tới | Quạt chạy | Thanh tốc độ nhảy về | Relay đóng | Matter FanMode |
|---|---|---|---|---|
| 0 % | tắt | 0 % | không relay nào | Off |
| 1–49 % | **số 1** | **33 %** | số 1 | Low |
| 50–83 % | **số 2** | **66 %** | số 2 | Medium |
| 84–100 % | **số 3** | **100 %** | số 3 | High |

- Kéo tới vị trí bất kỳ thì quạt chạy ở số gần nhất và thanh trượt tự nhảy về 33 / 66 / 100 %, nên khi quạt bật thanh tốc độ chỉ có 3 vị trí. Ứng dụng đặt tốc độ theo số (`SpeedSetting` 1/2/3, FanMode Low/Medium/High, preset low/medium/high của Home Assistant) cũng ra đúng 3 mức này.
- **Đổi số an toàn cho motor**: relay số cũ nhả ra, chờ **200 ms** (`FAN_RELAY_DEAD_TIME_MS`) rồi relay số mới mới đóng, kể cả khi tắt rồi bật lại thật nhanh. Không bao giờ có 2 relay số cùng đóng. Nên đấu thêm **khoá liên động bằng dây** (xem [Sơ đồ đấu nối](#sơ-đồ-đấu-nối)) để kể cả khi hỏng hóc cũng không thể cấp điện 2 đầu dây motor cùng lúc.
- **Bật lại** (nút nguồn remote, nút trên mạch, hoặc ứng dụng gửi lệnh "On"): quạt chạy lại **số trước khi tắt**. Lần bật đầu tiên (chưa có gì lưu): số 1.

### Đảo gió (trục xoay)

- Relay trục xoay chỉ đóng **khi quạt đang chạy và đảo gió đang bật**. Tắt quạt thì động cơ trục xoay dừng theo, nhưng trạng thái đảo gió được nhớ: bật quạt lại là quạt đảo gió tiếp.
- Trục xoay dừng trước và chạy sau động cơ quạt.
- Trên Matter, đảo gió được đưa ra **hai** điều khiển, cả hai cùng bật/tắt relay trục xoay và luôn hiển thị cùng một trạng thái:

| Điều khiển Matter | Apple Home (dưới thanh tốc độ) | Home Assistant | Đảo gió bật khi |
|---|---|---|---|
| Rocking (`RockSetting`, hỗ trợ trái-phải) | nút dao động (*Oscillate*) | Oscillate | `RockSetting` = trái-phải |
| AirflowDirection | nút hướng quay (*Rotation Direction*) | Direction | `AirflowDirection` = Reverse |

  Nút hướng quay vốn để đảo chiều gió của quạt trần, ở đây được **gán vào bật/tắt trục xoay**: chiều *Reverse* = đang đảo gió, *Forward* = không đảo gió. Tuỳ phiên bản iOS mà Apple Home hiện một hoặc cả hai nút; nhấn nút nào cũng được. Muốn chỉ còn một nút, tắt `FAN_MATTER_ROCKING` hoặc `FAN_MATTER_AIRFLOW_DIRECTION` trong menuconfig, nạp lại rồi xoá và thêm lại quạt trong ứng dụng.

### Mất điện, khởi động lại

- **Mất điện rồi có điện lại**: quạt quay về trạng thái cuối (số và đảo gió), chỉ chậm khoảng 0,5 s sau khi cấp điện, không cần chờ Wi-Fi. Đổi được thành "luôn tắt khi có điện lại" trong menuconfig (`FAN_POWER_ON_STATE`); khi đó số và đảo gió vẫn được nhớ cho lần bật tiếp theo.
- **Khởi động lại mềm** (OTA, factory reset, crash, watchdog): luôn khôi phục trạng thái cuối. Nếu lần khởi động trước bị crash (hoặc sụt áp) trong vòng 30 s sau khi khởi động, quạt khởi động ở trạng thái **tắt**, để một lỗi lặp lúc khởi động (ví dụ nguồn yếu sụt áp mỗi khi relay đóng) không bật tắt motor liên tục.
- Trạng thái quạt được ghi vào flash khi đã đứng yên **3 giây** (nhấn remote liên tục nhiều lần chỉ ghi một lần). Rút điện trong 3 giây sau khi vừa đổi thì lần đổi đó không được nhớ.

## Phần cứng

- **ESP32-C3 SuperMini** (hoặc ESP32-C3-DevKitM-1), flash 4 MB.
- **Module 4 relay** 5 V (3 relay số + 1 relay trục xoay), tiếp điểm chịu được dòng motor quạt (thường 10 A / 250 VAC là dư).
- **Mắt thu hồng ngoại 38 kHz**: VS1838B, TSOP38238, TSOP1838... hoặc module KY-022. Cấp **3,3 V**.
- **Nguồn 220 V → 5 V** (ví dụ HLK-PM01) cho ESP32-C3 và module relay.

### Sơ đồ chân

| Chức năng | Chân ESP32-C3 | Ghi chú |
|---|---|---|
| Relay **số 1** (thấp) | **GPIO3** | |
| Relay **số 2** | **GPIO4** | |
| Relay **số 3** (cao) | **GPIO5** | |
| Relay **trục xoay** (đảo gió) | **GPIO7** | |
| Mắt hồng ngoại, chân OUT | **GPIO10** | VCC 3,3 V, GND |
| Nút BOOT | GPIO9 | có sẵn trên mạch |
| LED trạng thái | GPIO8 | SuperMini: LED xanh dương (sáng ở mức thấp). DevKitM-1: WS2812 |
| Nút số / nút đảo gió ngoài (tuỳ chọn) | ví dụ GPIO0 / GPIO1 | nối xuống GND, bật trong menuconfig |

- Các chân relay được chọn là chân **không có điện trở kéo lên lúc reset** (GPIO0, 1, 3, 4, 5, 7, 10 trên ESP32-C3): từ lúc cấp điện tới khi firmware chạy (~0,3 s) chân relay để hở, mạch kích relay active-high không bị kích nhầm. Không dùng cho relay: GPIO2/8/9 (chân strapping), GPIO6 (có kéo lên lúc reset), GPIO11–17 (flash), GPIO18/19 (USB), GPIO20/21 (UART0, ROM in log ra GPIO21 mỗi lần khởi động). Firmware báo lỗi khi build nếu hai chức năng dùng trùng chân hoặc dùng chân flash/USB.
- **Mức kích relay**: mặc định `FAN_RELAY_ACTIVE_LEVEL = 1` (active-high, mạch kích transistor/MOSFET). **Module relay opto 4 kênh phổ biến thường là active-low**: đặt `FAN_RELAY_ACTIVE_LEVEL = 0`. Đặt sai thì lúc khởi động **cả 4 relay cùng đóng**: luôn thử với nguồn USB, chưa nối motor, nghe/nhìn đèn relay trước (tắt hết khi quạt tắt, chỉ một relay số đóng khi quạt chạy).
- Mạch kích active-high (NPN/MOSFET) nên có điện trở kéo xuống ≤ 10 kΩ từ chân IN / cực G xuống GND.

### Sơ đồ đấu nối

```
                    ESP32-C3 SuperMini
 HLK-PM01 +5V ───► 5V            GPIO3 ───► IN1  (relay số 1)
 HLK-PM01 GND ───► GND           GPIO4 ───► IN2  (relay số 2)
                                 GPIO5 ───► IN3  (relay số 3)
                                 GPIO7 ───► IN4  (relay trục xoay)
 Module relay VCC ◄── 5V, GND ◄── GND
 Mắt IR: OUT ──► GPIO10,  VCC ◄── 3V3,  GND ◄── GND
```

Phía điện lưới, đấu **3 relay số nối tiếp qua tiếp điểm thường đóng (NC)** để khoá liên động: dù có bao nhiêu relay số đóng cùng lúc thì cũng chỉ một đầu dây số của motor có điện (relay số 1 được ưu tiên).

```
 Pha (L) ──► COM relay 1 ── NO ──► dây số 1 của motor
                          └ NC ──► COM relay 2 ── NO ──► dây số 2 của motor
                                                └ NC ──► COM relay 3 ── NO ──► dây số 3 của motor
                                                                      └ NC     (để trống)
 Pha (L) ──► COM relay 4 ── NO ──► dây động cơ trục xoay (đảo gió)

 Trung tính (N) ──► dây chung của motor quạt và dây còn lại của động cơ trục xoay
 Tụ quạt: giữ nguyên như mạch gốc của quạt
```

- Mạch này thay cho phím bấm cơ (hoặc mạch điều khiển) gốc của quạt: các dây số trước đây nối vào phím số thì nay nối vào NO của relay tương ứng. Tháo hẳn phím cơ khỏi mạch, không để song song.
- Quạt có trục xoay bằng núm kéo cơ (không có động cơ đảo gió riêng) thì không dùng relay 4.

⚠️ Mạch dùng điện 220 V: cách ly, đi dây an toàn, cầu chì cho nguồn. Không cắm USB vào máy tính khi mạch đang nối điện lưới (nguồn HLK-PM01 không chung mass với lưới, nhưng phía relay thì có).

## Remote hồng ngoại

### Học lệnh remote

Remote mỗi hãng một mã, nên firmware **học** các nút của remote ngay trên thiết bị (không cần máy tính):

1. **Nhấn nút BOOT 3 lần liên tiếp**. LED bắt đầu nháy **vàng**: số lần nháy trong mỗi chu kỳ = bước đang học.
2. Ở mỗi bước, bấm nút tương ứng trên remote (chĩa vào mắt thu). Học được thì LED chớp **xanh lá** và chuyển sang bước sau.

| Bước | LED nháy | Nút trên remote | Tác dụng |
|---|---|---|---|
| 1 | 1 lần | **Bật/Tắt** (ON/OFF, POWER) | bật (số trước đó) / tắt. Nếu học cả nút Tắt riêng ở bước 4 thì nút này chỉ còn **bật** |
| 2 | 2 lần | **Tốc độ** (SPEED) | quạt tắt: bật lên; đang chạy: số 1 → 2 → 3 → 1 |
| 3 | 3 lần | **Đảo gió** (SWING, OSC) | bật/tắt đảo gió |
| 4 | 4 lần | **Tắt** (OFF) riêng, nếu remote có | tắt |
| 5 | 5 lần | **Số 1**, nếu remote có | chạy số 1 |
| 6 | 6 lần | **Số 2**, nếu remote có | chạy số 2 |
| 7 | 7 lần | **Số 3**, nếu remote có | chạy số 3 |

- **Nhấn BOOT 1 lần**: bỏ qua bước hiện tại (giữ mã đã học trước đó cho nút này, nếu có).
- **Nhấn BOOT 2 lần**: kết thúc và lưu ngay (remote thông thường chỉ có 3 nút đầu: học xong bước 3 thì nhấn đúp).
- Không bấm gì trong **30 giây** (`IR_LEARN_TIMEOUT_S`): tự kết thúc và lưu những nút đã học.
- Một nút remote chỉ làm một việc: học lại nút đó cho chức năng khác thì nó bị bỏ khỏi chức năng cũ.
- Học lại lúc nào cũng được (nhấn BOOT 3 lần); các bước bỏ qua giữ nguyên mã cũ. Mã đã học được lưu trong flash, **factory reset không xoá** (muốn xoá hết: `idf.py erase-flash`).

Trong lúc học, remote không điều khiển quạt. Log in từng bước, ví dụ:

```
I ir_rx: IR learning 1/7: press the POWER button on the remote (not learned yet; click: skip, double click: finish)
I ir_rx: IR NEC address 0x00 command 0x45 (0xBA45FF00)
I ir_rx: Learned POWER
...
I ir_rx: IR learning finished (double click): 3 of 7 remote buttons learned
```

### Remote nào dùng được

- Mắt thu 38 kHz (phần lớn remote quạt, tivi, đầu thu).
- Remote giao thức **NEC** (phổ biến nhất ở remote giá rẻ) được giải mã đầy đủ. Mọi giao thức khác (Panasonic, Sony, Samsung, các loại riêng của hãng quạt...) được nhận theo **hình dạng khung tín hiệu**, nên vẫn học và dùng được.
- Giữ nút trên remote không lặp lệnh: mỗi lần bấm là một lệnh.
- Remote dùng bit đảo (Philips RC5/RC6, hiếm gặp ở quạt) gửi hai mã khác nhau xen kẽ ở mỗi lần bấm, nên chỉ ăn cách một lần bấm.
- Mỗi lần nhận mã, log in `IR NEC ...` hoặc `IR code 0x... (N pulses)`; mã chưa học thì log `Not a learned button`. Lệnh remote được nhận thì LED chớp xanh lá.

## Nút nhấn

| Thao tác (nút BOOT, GPIO9) | Tác dụng |
|---|---|
| Nhấn 1 lần | bật/tắt quạt (bật lại đúng số trước đó) |
| Nhấn 2 lần | mở chế độ ghép nối 15 phút (thêm vào hệ thứ hai), LED nháy xanh dương |
| Nhấn 3 lần | học remote hồng ngoại |
| Giữ 5 s (LED chuyển đỏ) rồi thả | LED nháy đỏ, factory reset: xoá Wi-Fi và ghép nối Matter (giữ trạng thái quạt và mã remote) |

- Có thể nối thêm một nút nhấn ra ngoài vỏ quạt song song với nút BOOT (GPIO9 xuống GND). Đừng giữ nút này lúc cấp điện: chip sẽ vào chế độ nạp firmware.
- **Nút số** (`BUTTON_SPEED_GPIO`) và **nút đảo gió** (`BUTTON_SWING_GPIO`) ngoài, tuỳ chọn: mỗi lần nhấn chuyển số (quạt tắt thì bật lên) / bật-tắt đảo gió.

## LED trạng thái

| LED | Ý nghĩa |
|---|---|
| Nháy xanh dương (0,5 s sáng / 0,5 s tắt) | đang chờ ghép nối / đang ghép nối, hoặc Identify (nút *Nhận dạng* trong Apple Home, *Identify* trong Home Assistant) |
| Nháy vàng N lần, nghỉ | đang học remote, bước N |
| Chớp xanh lá | nhận lệnh remote / học xong một nút |
| Đỏ đứng | giữ nút đủ 5 s: thả ra để factory reset |
| Nháy đỏ nhanh | đang factory reset |
| Tắt | bình thường (để không chói khi ngủ). Muốn LED sáng mờ khi quạt chạy: `STATUS_LED_SHOW_RUNNING` |

SuperMini chỉ có LED một màu: các trạng thái trên chỉ khác nhau ở cách nháy. Mạch không có LED: chọn `STATUS_LED_TYPE_NONE` (khi đó Identify báo "không có đèn").

## Trên ứng dụng

Thiết bị là **Quạt** (Fan, 0x002B) với cluster Fan Control:

| Thuộc tính | Giá trị |
|---|---|
| FanModeSequence | Off / Low / Medium / High (không có Auto) |
| SpeedMax | 3 (MultiSpeed) |
| PercentSetting / PercentCurrent | 0, 33, 66, 100 |
| RockSupport / RockSetting | trái-phải / đảo gió |
| AirflowDirection | Forward = không đảo gió, Reverse = đảo gió |
| Lệnh Step | tăng/giảm một số (Google Home, chip-tool) |

- **Apple Home**: ô quạt bật/tắt; mở ra là thanh tốc độ 3 vị trí, bên dưới là nút đảo gió (xem [Đảo gió](#đảo-gió-trục-xoay)).
- **Home Assistant**: thực thể `fan` có thanh tốc độ bước 33 %, preset low/medium/high, *Oscillate* và *Direction*.
- Ứng dụng ghi **FanMode = On** (bật mà không chọn số): theo chuẩn Matter là số cao nhất, firmware này cho chạy **số trước khi tắt** như nút nguồn của remote (`FAN_ON_RESUMES_LAST_SPEED`, tắt đi để theo đúng chuẩn).
- Điều khiển bằng remote hay nút trên mạch thì ứng dụng cập nhật ngay.

Với chip-tool (node id `0x7283`, endpoint 1):

```bash
chip-tool fancontrol write percent-setting 45 0x7283 1     # -> số 1, PercentSetting tự về 33
chip-tool fancontrol write percent-setting 70 0x7283 1     # -> số 2, 66 %
chip-tool fancontrol write fan-mode 3 0x7283 1             # High -> số 3, 100 %
chip-tool fancontrol write rock-setting 1 0x7283 1         # đảo gió bật (0 = tắt)
chip-tool fancontrol write airflow-direction 1 0x7283 1    # Reverse = đảo gió bật (0 = tắt)
chip-tool fancontrol step 0 0x7283 1 --Wrap false --LowestOff true   # tăng một số
chip-tool fancontrol read percent-current 0x7283 1
```

## Cấu hình

`idf.py menuconfig`:

| Menu | Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|---|
| Fan Relays | `FAN_RELAY_SPEED1_GPIO` / `2` / `3` | 3 / 4 / 5 | chân relay số 1, 2, 3 |
| | `FAN_RELAY_SWING_GPIO` | 7 | chân relay trục xoay |
| | `FAN_RELAY_ACTIVE_LEVEL` | 1 | 1 = active-high, 0 = active-low (module relay opto thường là 0) |
| | `FAN_RELAY_DEAD_TIME_MS` | 200 | thời gian chờ giữa nhả relay số cũ và đóng relay số mới |
| Fan Behavior | `FAN_POWER_ON_STATE` | khôi phục | trạng thái khi có điện lại: khôi phục / luôn tắt |
| | `FAN_ON_RESUMES_LAST_SPEED` | y | FanMode On chạy số trước đó (n = số cao nhất theo chuẩn) |
| | `FAN_MATTER_ROCKING` | y | đảo gió dạng Rocking (nút *Oscillate*) |
| | `FAN_MATTER_AIRFLOW_DIRECTION` | y | đảo gió trên nút hướng quay (*Rotation Direction*) |
| IR Receiver | `IR_RX_GPIO` | 10 | chân OUT của mắt thu, -1 = không có mắt thu |
| | `IR_LEARN_TIMEOUT_S` | 30 | tự kết thúc học remote sau chừng này giây không bấm gì |
| Buttons | `APP_BUTTON_GPIO` | 9 | nút chính (BOOT), -1 = không có |
| | `BUTTON_SPEED_GPIO` / `BUTTON_SWING_GPIO` | -1 | nút số / nút đảo gió ngoài |
| Status LED | `STATUS_LED_TYPE` | GPIO | không có / LED một màu / WS2812 |
| | `STATUS_LED_GPIO` | 8 | |
| | `STATUS_LED_ACTIVE_LEVEL` | 0 | LED một màu: 0 = sáng ở mức thấp (SuperMini) |
| | `STATUS_LED_SHOW_RUNNING` | n | LED sáng mờ khi quạt chạy |
| Device Information | `DEVICE_VENDOR_NAME` | Haxeko | nhà sản xuất |
| | `DEVICE_PRODUCT_NAME` | Smart Fan | kiểu máy |
| | `DEVICE_DISPLAY_NAME` | Smart Fan | tên mặc định khi thêm vào ứng dụng, tên quảng bá trước khi ghép nối |
| | `DEVICE_HARDWARE_VERSION_STRING` | ESP32-C3 SuperMini | phiên bản phần cứng |

Hoặc sửa `sdkconfig.defaults` rồi xoá `sdkconfig` để áp dụng lại. ESP32-C3-DevKitM-1: chọn `STATUS_LED_TYPE_WS2812`.

## Build

Yêu cầu: ESP-IDF v6.0.2 + esp-matter release/v1.6 (đã `export.sh` cả hai). Cách nhanh nhất là dùng Docker. Mount **cả repo** (không chỉ thư mục dự án) để mỗi commit mới tự lấy giờ build mới cho số phiên bản:

```bash
# chạy từ bất kỳ thư mục nào trong repo
docker run --rm -it -v "$(git rev-parse --show-toplevel)":/repo \
  espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c "git config --global --add safe.directory /repo && cd /repo/ESP32-C3/matter-wifi/matter-fan-3-speed && { [ -f sdkconfig ] || idf.py set-target esp32c3; } && idf.py build"
```

Hoặc cài local:

```bash
. ~/esp/esp-idf/export.sh
. ~/esp/esp-matter/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

SuperMini nạp và xem log qua cổng USB có sẵn (`/dev/ttyACM0`, Windows `COMx`). Nếu không nạp được: giữ BOOT, nhấn RST, thả BOOT.

### File firmware

| File trong `build/` | Dùng để |
|---|---|
| `matter_fan_3speed-<phiên bản>.bin` | nạp một lần ở địa chỉ `0x0` (bootloader + bảng phân vùng + app). Xoá cả vùng NVS nên phải ghép nối lại và học lại remote |
| `matter_fan_3speed.bin` | file app, dùng với `idf.py flash` / `write-flash @flash_args` (giữ ghép nối và mã remote) |
| `matter_fan_3speed-ota.bin` | cập nhật OTA qua Matter |

```bash
esptool --chip esp32c3 -p /dev/ttyACM0 write-flash 0x0 build/matter_fan_3speed-1.0.2610141530.bin
```

GitHub Actions (`.github/workflows/matter-fan-3-speed.yml` ở gốc repo) chạy test trên máy tính, build và upload artifact `matter_fan_3speed-<phiên bản>` (các file trên cùng `flash_args`, `bootloader/`, `partition_table/`) và `matter_fan_3speed-<phiên bản>-debug` (`.elf`/`.map` để giải mã địa chỉ khi crash). Vào tab **Actions** → lần chạy mới nhất → **Artifacts**.

### Phiên bản firmware

Mỗi lần build, firmware tự có phiên bản dạng `1.0.<yyMMddHHmm>` (giờ build theo UTC), ví dụ `1.0.2610141530`. Xem ở Apple Home (phụ kiện → cài đặt → **Firmware**) hoặc dòng `App version:` trong log khởi động. Số phiên bản Matter dùng cho OTA (`SoftwareVersion`) là phần `yyMMddHHmm`, nên luôn tăng. Build local: commit mới (hoặc checkout) tự lấy phiên bản mới ở lần build sau; chỉ sửa file thì chạy `idf.py reconfigure`.

### OTA

- Mỗi bản build tạo thêm `build/matter_fan_3speed-ota.bin` để đưa vào một OTA provider (ví dụ `chip-ota-provider-app`).
- **Rollback**: image mới khởi động ở trạng thái chờ xác nhận, và chỉ được xác nhận khi đã kết nối Wi-Fi liên tục 60 s mà vẫn còn các ghép nối. Image mới crash hoặc khởi động lại trước đó thì bootloader tự quay về bản cũ.

## Ghép nối

Firmware dùng chứng chỉ test (DAC test) của esp-matter, mã ghép nối mặc định:

| | |
|---|---|
| Mã QR | `MT:Y.K9042C00KA0648G00` |
| Mã thủ công | `3497-011-2332` |
| Passcode / Discriminator | `20202021` / `3840` |
| Tên Bluetooth khi chờ ghép nối | `SmartFan-3840` |

Tạo ảnh QR: `https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00`.

1. Cấp điện lần đầu (hoặc sau factory reset): LED nháy xanh dương, cửa sổ ghép nối BLE mở 15 phút.
2. Apple Home: **+ → Thêm phụ kiện → quét mã QR** (hoặc nhập mã thủ công). Gặp cảnh báo phụ kiện chưa được chứng nhận thì chọn **Vẫn thêm**. iPhone gửi tên/mật khẩu Wi-Fi 2,4 GHz cho quạt qua Bluetooth.
3. Home Assistant / Google Home / SmartThings / Alexa: *Add Matter device*, quét cùng mã. Google/SmartThings/Alexa cần bật chế độ cho thiết bị chưa chứng nhận (developer/test) theo hướng dẫn của từng hãng.

- **Thêm vào hệ thứ hai** (ví dụ đã có Apple Home, muốn thêm Home Assistant): nhấn đúp BOOT, LED nháy xanh dương, rồi trong 15 phút thêm thiết bị ở hệ mới bằng mã gốc ở trên (điện thoại cùng mạng Wi-Fi với quạt). Hoặc dùng chức năng chia sẻ của hệ đang quản lý (Apple Home: *Bật chế độ ghép nối*; HA: *Share device*).
- **Xoá khỏi hệ cuối cùng**: quạt tự factory reset và mở lại cửa sổ ghép nối BLE 15 phút với mã gốc. Quạt vẫn chạy như trước, mã remote vẫn còn.
- Mọi mạch nạp firmware này dùng **chung mã ghép nối**: có nhiều mạch thì ghép **từng mạch một**.
- Firmware dùng chứng chỉ test (VID `0xFFF1`, PID `0x8000`), chỉ hợp cho DIY.

Với chip-tool:

```bash
chip-tool pairing ble-wifi 0x7283 <SSID> <mật-khẩu> 20202021 3840
chip-tool fancontrol write fan-mode 2 0x7283 1     # Medium = số 2
chip-tool basicinformation read serial-number 0x7283 0
```

## Kiểm thử trên máy tính

`test/host/` chạy logic quạt và bộ giải mã hồng ngoại trên máy tính (chỉ cần `gcc`), không cần ESP32 hay ESP-IDF:

```bash
test/host/run.sh
```

| Test | Kiểm tra |
|---|---|
| `test_fan_logic.c` | % ↔ số (33/66/100, mọi giá trị 0–100), khớp với FanMode/SpeedSetting mà cluster Matter tự tính; bật/tắt nhớ số cũ, chuyển số vòng, đặt số, đảo gió chỉ chạy khi quạt chạy, lệnh Step (wrap, lowest off), dữ liệu lưu bị hỏng |
| `test_ir.c` | chuyển dữ liệu RMT thành xung (bỏ mức nghỉ đầu khung, gộp đoạn cùng mức, cắt khung quá dài); giải mã NEC với méo của mắt thu và nhiễu thời gian, khung lặp NEC, khung hỏng; mã theo hình dạng (Sony, Samsung) ổn định với nhiễu và khác nhau giữa các nút; lọc nhiễu; giữ nút không lặp lệnh; bảng nút đã học (POWER thành ON khi có nút OFF, học lại chuyển chức năng) |

Phần relay, Matter, Wi-Fi và BLE vẫn phải thử trên thiết bị thật.

## Cấu trúc

```
CMakeLists.txt             project esp-matter (esp32c3), phiên bản theo giờ build, file firmware đầy đủ mang tên phiên bản
sdkconfig.defaults         Matter over Wi-Fi, BLE tối giản, OTA + rollback, watchdog, cluster không dùng
partitions.csv             4 MB, 2 phân vùng OTA
main/app_main.cpp          khởi tạo, node Matter, sự kiện ghép nối/LED, Identify, xác nhận OTA, factory reset
main/fan_matter.cpp        endpoint Fan, delegate Fan Control <-> task quạt, kéo thanh tốc độ về 33/66/100 %
main/fan_ctrl.cpp          task quạt: relay (ngắt trước - đóng sau, thời gian chết), lưu/khôi phục trạng thái
main/fan_logic.c           máy trạng thái quạt (thuần C, có test)
main/ir_rx.cpp             mắt thu hồng ngoại (RMT), học remote, lưu mã
main/ir_decode.c           giải mã NEC, mã theo hình dạng khung, lọc lặp phím (thuần C, có test)
main/ir_table.c            bảng nút remote đã học -> lệnh quạt (thuần C, có test)
main/buttons.cpp           nút BOOT (1/2/3 lần, giữ 5 s), nút số / đảo gió ngoài
main/status_led.cpp        LED trạng thái (GPIO hoặc WS2812)
main/device_info.cpp       nhà sản xuất, kiểu máy, tên, phần cứng, số sê-ri (MAC Wi-Fi)
main/chip_project_config.h tên và loại thiết bị khi quảng bá trước ghép nối (DNS-SD)
main/Kconfig.projbuild     menu Fan Relays / Fan Behavior / IR Receiver / Buttons / Status LED / Device Information
test/host/                 test trên máy tính (run.sh)
```
