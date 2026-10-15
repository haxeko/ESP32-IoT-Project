# Haxeko Vibe Light: Matter RGB Light WS2812 (ESP32-C3, Wi-Fi)

Đèn RGB chuẩn **Matter over Wi-Fi** chạy trên ESP32-C3 (SuperMini), dùng dải **30 LED WS2812** nối vào **GPIO4**. Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa...

Thiết bị dùng device type **Extended Color Light (0x010D)**: bật/tắt, độ sáng, màu (Hue/Saturation, XY) và nhiệt độ màu.

## Cách đèn hoạt động

- **Mỗi LED đang sáng luôn sáng ở mức 30 %** (`CONFIG_LED_FIXED_BRIGHTNESS_PERCENT`). Độ sáng chỉnh từ ứng dụng **không làm LED mờ đi**.
- **Độ sáng 0–100 % quyết định số LED sáng** trên tổng 30 LED, sáng dần từ LED đầu tiên (gần chân DIN):

| Độ sáng trong app | Số LED sáng |
|---|---|
| 1 % | 1 |
| 10 % | 3 |
| 25 % | 8 |
| 50 % | 15 |
| 75 % | 23 |
| 100 % | 30 |

  Công thức: `số LED = round(CurrentLevel × 30 / 254)`, tối thiểu 1 LED khi đèn bật.
- **Tắt** trong app thì tắt hết 30 LED.
- **Màu** chọn trong app được áp cho mọi LED đang sáng, sau đó nhân với 30 %. Ví dụ đỏ = RGB(77, 0, 0), trắng = RGB(77, 77, 77).
- **Tắt rồi bật lại** (từ app hoặc nút BOOT): đèn sáng lại ngay đúng số LED và màu như trước khi tắt. Hiệu ứng "sáng dần từ mức thấp nhất" của Matter khi Bật được bỏ qua, vì trên dải LED này mức thấp nhất là 1 LED và sẽ trông như đèn nháy tắt một lần. Khi Tắt, dải LED thu ngắn rất nhanh về 1 LED rồi tắt.
- **Mất điện / rút nguồn**: có điện lại thì đèn khôi phục bật/tắt, độ sáng và màu như trước khi mất điện. Bật/tắt được lưu vào flash ngay; độ sáng và màu được lưu khi giá trị đã đứng yên **3 giây** (giống ví dụ `light` của Espressif, tránh ghi flash ở mỗi bước chuyển sáng/màu). Nếu rút điện trong vòng 3 giây sau khi vừa chỉnh độ sáng/màu thì lần chỉnh đó không được nhớ.
- **Nhận dạng (Identify)**: nút "Nhận dạng" trong Apple Home / Home Assistant làm cả dải LED nháy (0,5 s sáng, 0,5 s tắt) bằng màu hiện tại, kể cả khi đèn đang tắt.
- Lần đầu khởi động (chưa có gì lưu): đèn bật, 100 % (30 LED), màu trắng.

Mức 30 %, số LED, chân GPIO và chiều sáng (từ đầu hay từ cuối dải) đều đổi được trong `idf.py menuconfig` → **WS2812 RGB Light**.

## Phần cứng

- ESP32-C3 SuperMini (hoặc ESP32-C3-DevKitM-1...), flash 4 MB.
- Dải 30 LED WS2812 / WS2812B.
- Nguồn 5V cho LED. 30 LED ở 30 %, màu trắng, ăn khoảng **0,55 A**, nên dùng nguồn **5V 1A** trở lên.

### Sơ đồ nối dây

| WS2812 | Nối vào |
|---|---|
| DIN | **GPIO4** (nên nối tiếp điện trở 330 Ω) |
| 5V | 5V nguồn |
| GND | GND nguồn **và** GND của ESP32-C3 |

Nên đặt tụ 470–1000 µF giữa 5V và GND ở đầu dải LED. Mức logic 3.3V của ESP32-C3 thường đủ cho WS2812; nếu LED nhấp nháy sai màu, thêm IC chuyển mức (74AHCT125) hoặc cấp cho LED đầu tiên 4.5V qua một diode.

Có thể cấp nguồn cho cả mạch từ chân **5V** của SuperMini khi cắm USB, nhưng cổng USB máy tính thường chỉ cho 0,5 A. Nếu chỉnh mức sáng cao hơn 30 % hoặc dùng nhiều LED hơn, hãy dùng nguồn 5V riêng.

### Nút bấm

Nút **BOOT (GPIO9)**:
- **Nhấn nhanh**: bật/tắt đèn (trạng thái được báo về app).
- **Giữ 5 giây rồi nhả ra**: factory reset (xóa Wi-Fi và thông tin ghép nối Matter). Reset chạy lúc nhả nút, nên giữ quá 5 giây vẫn được.

Đổi chân nút trong menuconfig → **Button** (`-1` để tắt).

## Phần mềm

Yêu cầu: **ESP-IDF v6.0.x** và **esp-matter `release/v1.6`** (giống image Docker CI `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`).

```bash
cd ESP32-C3/matter-wifi/matter-rgb-light-ws2812
. $IDF_PATH/export.sh
. $ESP_MATTER_PATH/export.sh

idf.py set-target esp32c3
idf.py menuconfig          # (tùy chọn) số LED, mức 30 %, GPIO
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Hoặc build bằng Docker, không cần cài gì:

```bash
docker run --rm -v "$PWD":/project -w /project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c '. $IDF_PATH/export.sh && . $ESP_MATTER_PATH/export.sh && cd /project && idf.py set-target esp32c3 && idf.py build'
```

### Wi-Fi luôn bật

`idf.py menuconfig` → **Wi-Fi Power Save** → `WIFI_NO_POWER_SAVE` (mặc định bật): đèn là thiết bị cắm điện nên sau khi ghép nối xong (Bluetooth đã tắt), firmware tắt chế độ tiết kiệm điện Wi-Fi (modem sleep, mặc định của ESP-IDF): sóng Wi-Fi luôn bật, nhận lệnh và gói mDNS từ ứng dụng ngay thay vì chờ beacon tiếp theo của router. Tốn thêm khoảng vài chục mA. Trong lúc ghép nối qua Bluetooth vẫn giữ modem sleep, vì ESP-IDF bắt buộc khi Wi-Fi và Bluetooth dùng chung bộ thu phát. Log khi đã tắt: `Wi-Fi power save off (mains powered)`.

### File firmware

Mỗi lần `idf.py build` tạo thêm 2 file trong `build/`, tên có **hậu tố là phiên bản firmware**:

| File | Dùng khi |
|---|---|
| `matter_rgb_light_ws2812_esp32c3_<phiên bản>.bin` | Cập nhật, giữ ghép nối và Wi-Fi: nạp tại `0x20000` (hoặc dùng cho OTA) |
| `matter_rgb_light_ws2812_esp32c3_merged_<phiên bản>.bin` | Nạp mới hoàn toàn: một file tại `0x0` (xóa ghép nối và Wi-Fi) |

Ví dụ: `matter_rgb_light_ws2812_esp32c3_merged_1.0.2610131432.bin`.

Phiên bản có dạng `1.0.<thời điểm build UTC yyMMddHHmm>` và được lấy lúc CMake cấu hình. Khi build lại trong cùng thư mục, chạy `idf.py fullclean` trước để có phiên bản mới (CI luôn build mới nên mỗi lần đều có phiên bản riêng).

Mỗi lần push, GitHub Actions build sẵn firmware, artifact tên `matter_rgb_light_ws2812-esp32c3-<phiên bản>` chứa 2 file trên.

Log và console Matter chạy qua **cổng USB có sẵn** của ESP32-C3 (USB-Serial-JTAG), nạp firmware cũng qua cổng này. Nếu không nạp được, giữ nút BOOT, nhấn RST, rồi nhả BOOT.

## Thông tin thiết bị

Ứng dụng nhà thông minh hiển thị:

| Mục | Giá trị | Sửa ở đâu |
|---|---|---|
| Nhà sản xuất (Manufacturer) | `Haxeko` | `menuconfig` → *Device Information* |
| Kiểu máy (Model) | `Vibe Light` | `menuconfig` → *Device Information* |
| Tên thiết bị khi ghép nối | `Vibe Light` | `menuconfig` → *Device Information* |
| Số serial | Địa chỉ MAC Wi-Fi, ví dụ `8ED0B2A8594B` | Tự động, riêng cho từng chip |
| Tên Bluetooth khi chờ ghép nối | `VibeLight-3840` | `CONFIG_BLE_DEVICE_NAME_PREFIX` trong `sdkconfig.defaults` |
| Firmware | `1.0.<yyMMddHHmm>` | Tự động |

Thiết bị đã ghép nối từ trước sẽ thấy Manufacturer/Model mới sau khi nạp firmware mới. Một số app (Apple Home) lưu tạm thông tin cũ, có thể cần xóa rồi thêm lại đèn mới hiện tên mới.

## Ghép nối

Thiết bị được ghép bằng **Bluetooth** như các thiết bị Matter Wi-Fi thông thường: điện thoại gửi tên/mật khẩu Wi-Fi (2.4 GHz) cho đèn qua BLE.

Thông tin ghép nối mặc định (chứng chỉ test của esp-matter):

| | |
|---|---|
| Mã QR | `MT:Y.K9042C00KA0648G00` |
| Mã thủ công | `3497-011-2332` |
| Passcode / Discriminator | `20202021` / `3840` |

Tạo ảnh QR từ chuỗi trên, ví dụ: `https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00`.

- **Apple Home**: Thêm phụ kiện → quét mã QR (hoặc nhập mã thủ công). iOS báo "phụ kiện chưa được chứng nhận" → Vẫn thêm.
- **Home Assistant**: dùng app Home Assistant trên điện thoại → Settings → Devices & services → Add Matter device.
- **Google Home / SmartThings / Alexa**: thêm thiết bị Matter, quét mã. Cần bật chế độ developer/test cho thiết bị chưa chứng nhận theo hướng dẫn của từng hãng.
- **chip-tool**:
  ```bash
  chip-tool pairing ble-wifi 1 <SSID> <mat-khau> 20202021 3840
  chip-tool onoff on 1 1
  chip-tool levelcontrol move-to-level 127 0 0 0 1 1   # 50 % = 15 LED
  chip-tool colorcontrol move-to-hue-and-saturation 0 254 0 0 0 1 1   # đỏ
  ```

Khi xóa thiết bị khỏi tất cả ứng dụng, thiết bị tự mở lại chế độ ghép nối 5 phút (qua Wi-Fi đã lưu). Muốn ghép lại từ đầu, giữ nút BOOT 5 giây rồi nhả ra để factory reset.

## Cấu trúc mã nguồn

| File | Nội dung |
|---|---|
| `main/app_main.cpp` | Tạo node Matter, endpoint Extended Color Light |
| `main/device_info.cpp` | Manufacturer / Model / tên / số serial (Basic Information) |
| `main/app_driver.cpp` | Nhận thay đổi OnOff / LevelControl / ColorControl từ Matter, Identify, nút BOOT |
| `main/led_bar.c` | Điều khiển WS2812 qua RMT (`espressif/led_strip`): số LED sáng theo độ sáng, cường độ cố định 30 %, nháy Identify; chỉ gửi dữ liệu ra LED khi hình ảnh thay đổi |
| `main/wifi_power.cpp` | Tắt tiết kiệm điện Wi-Fi sau khi ghép nối (thiết bị cắm điện) |
| `main/color_convert.c` | Đổi Hue/Saturation, XY, nhiệt độ màu sang RGB |
| `main/Kconfig.projbuild` | Tùy chọn: Manufacturer/Model, GPIO, số LED, mức 30 %, chiều sáng, chân nút, tiết kiệm điện Wi-Fi |
