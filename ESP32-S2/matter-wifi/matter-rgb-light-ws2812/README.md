# Matter RGB Light WS2812 (ESP32-S2, Wi-Fi)

Đèn RGB chuẩn **Matter over Wi-Fi** chạy trên ESP32-S2, dùng dải **64 LED WS2812** nối vào **GPIO8**. Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa...

Thiết bị dùng device type **Extended Color Light (0x010D)**: bật/tắt, độ sáng, màu (Hue/Saturation, XY) và nhiệt độ màu.

## Cách đèn hoạt động

- **Mỗi LED đang sáng luôn sáng ở mức 30 %** (`CONFIG_LED_FIXED_BRIGHTNESS_PERCENT`). Độ sáng chỉnh từ ứng dụng **không làm LED mờ đi**.
- **Độ sáng 0–100 % quyết định số LED sáng** trên tổng 64 LED, sáng dần từ LED đầu tiên (gần chân DIN):

| Độ sáng trong app | Số LED sáng |
|---|---|
| 1 % | 1 |
| 10 % | 6 |
| 25 % | 16 |
| 50 % | 32 |
| 75 % | 48 |
| 100 % | 64 |

  Công thức: `số LED = round(CurrentLevel × 64 / 254)`, tối thiểu 1 LED khi đèn bật.
- **Tắt** trong app thì tắt hết 64 LED.
- **Màu** chọn trong app được áp cho mọi LED đang sáng, sau đó nhân với 30 %. Ví dụ đỏ = RGB(77, 0, 0), trắng = RGB(77, 77, 77).
- Lần đầu khởi động: đèn bật, 100 % (64 LED), màu trắng. Sau đó trạng thái bật/tắt, độ sáng và màu được lưu lại và khôi phục khi mất điện.

Mức 30 %, số LED, chân GPIO và chiều sáng (từ đầu hay từ cuối dải) đều đổi được trong `idf.py menuconfig` → **WS2812 RGB Light**.

## Phần cứng

- Board ESP32-S2, flash 4 MB (LOLIN/Wemos S2 Mini, ESP32-S2-Saola-1, ESP32-S2-DevKitM-1...). Nếu board có PSRAM (S2 Mini có 2 MB), firmware tự dùng thêm; không có cũng chạy.
  ESP32-S2 chỉ có 320 KB RAM. Firmware để code Wi-Fi/lwIP chạy từ flash thay vì IRAM, nên RAM trong còn khoảng 140 KB heap lúc khởi động. Board có PSRAM sẽ dư dả hơn.
- Dải 64 LED WS2812 / WS2812B.
- Nguồn 5V cho LED. 64 LED ở 30 %, màu trắng, ăn khoảng **1,2 A**, nên dùng nguồn **5V 2A** trở lên.

### Sơ đồ nối dây

| WS2812 | Nối vào |
|---|---|
| DIN | **GPIO8** (nên nối tiếp điện trở 330 Ω) |
| 5V | 5V nguồn |
| GND | GND nguồn **và** GND của ESP32-S2 |

Nên đặt tụ 470–1000 µF giữa 5V và GND ở đầu dải LED. Mức logic 3.3V của ESP32-S2 thường đủ cho WS2812; nếu LED nhấp nháy sai màu, thêm IC chuyển mức (74AHCT125) hoặc cấp cho LED đầu tiên 4.5V qua một diode.

### Nút bấm

Nút **BOOT (GPIO0)**:
- **Nhấn nhanh**: bật/tắt đèn (trạng thái được báo về app).
- **Giữ 5 giây**: factory reset (xóa Wi-Fi và thông tin ghép nối Matter).

Đổi chân nút trong menuconfig → **Button** (`-1` để tắt).

## Lưu ý quan trọng: ESP32-S2 không có Bluetooth

Thiết bị Matter Wi-Fi thông thường nhận tên và mật khẩu Wi-Fi qua **Bluetooth** lúc ghép nối. ESP32-S2 **không có Bluetooth**, nên:

1. **Thiết bị phải vào Wi-Fi trước**, bằng một trong hai cách:
   - **Cách 1 (khuyên dùng):** ghi tên/mật khẩu Wi-Fi vào firmware trước khi build, trong `sdkconfig.defaults`:
     ```
     CONFIG_DEFAULT_WIFI_SSID="TenWiFi"
     CONFIG_DEFAULT_WIFI_PASSWORD="MatKhau"
     ```
     hoặc `idf.py menuconfig` → Component config → CHIP Device Layer → WiFi Station Options.
     Thiết bị chỉ dùng thông tin này khi chưa lưu mạng Wi-Fi nào. Wi-Fi phải là **2.4 GHz**.
   - **Cách 2:** qua cổng serial (UART0, xem bên dưới) gõ lệnh:
     ```
     matter esp wifi connect TenWiFi MatKhau
     ```
2. **Ghép nối "on-network"**: ứng dụng tìm thiết bị ngay trên mạng Wi-Fi (DNS-SD), giống cách ghép các hub/bridge Matter dùng dây mạng. Điện thoại phải cùng mạng Wi-Fi với đèn.

> Mã QR mặc định trong tài liệu esp-matter (`MT:Y.K9042C00KA0648G00`) khai báo ghép qua Bluetooth nên **không dùng được** với ESP32-S2. Hãy dùng **mã QR được in ra log** khi thiết bị khởi động (có dòng `SetupQRCode`), hoặc dùng **mã số thủ công**:
>
> **`3497-011-2332`**

## Phần mềm

Yêu cầu: **ESP-IDF v6.0.x** và **esp-matter `release/v1.6`** (giống image Docker CI `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`).

```bash
cd ESP32-S2/matter-wifi/matter-rgb-light-ws2812
. $IDF_PATH/export.sh
. $ESP_MATTER_PATH/export.sh

idf.py set-target esp32s2
idf.py menuconfig          # (tùy chọn) Wi-Fi, số LED, mức 30 %, GPIO
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Hoặc build bằng Docker, không cần cài gì:

```bash
docker run --rm -v "$PWD":/project -w /project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c '. $IDF_PATH/export.sh && . $ESP_MATTER_PATH/export.sh && cd /project && idf.py set-target esp32s2 && idf.py build'
```

Mỗi lần push, GitHub Actions build sẵn firmware (artifact `matter_rgb_light_ws2812-esp32s2`, có file `*_merged.bin` để nạp một lần tại địa chỉ `0x0`).

Đổi Wi-Fi mặc định trong `sdkconfig.defaults` sau khi đã build thì cần xóa file `sdkconfig` (hoặc `idf.py fullclean`) để giá trị mới có hiệu lực.

### Nạp firmware cho board chỉ có cổng USB (S2 Mini)

S2 Mini không có chip USB-UART. Để nạp: **giữ nút 0 (BOOT), nhấn RST, nhả BOOT**, board hiện cổng USB mới, rồi `idf.py -p <cổng> flash`. Sau khi nạp, nhấn RST để chạy.

Log và console Matter chạy trên **UART0 (TX = GPIO43, RX = GPIO44)**, vì Matter shell không hỗ trợ USB-CDC của ESP32-S2. Với S2 Mini, muốn xem log hoặc gõ lệnh `matter esp wifi connect` thì nối thêm một adapter USB-UART vào GPIO43/44. Nếu dùng **Cách 1** (Wi-Fi ghi sẵn trong firmware) thì không cần console.

## Ghép nối

Thông tin ghép nối mặc định (chứng chỉ test của esp-matter):

| | |
|---|---|
| Passcode | `20202021` |
| Discriminator | `3840` |
| Mã thủ công | `34970112332` |

- **Apple Home**: Thêm phụ kiện → Thêm tùy chọn khác → nhập mã `3497-011-2332` (hoặc quét mã QR trong log). iOS báo "phụ kiện chưa được chứng nhận" → Vẫn thêm.
- **Home Assistant**: Settings → Devices & services → Add Matter device → "No, it's new" → nhập mã. Hoặc trong Matter Server dùng `commission_on_network` với passcode `20202021`.
- **Google Home / SmartThings / Alexa**: thêm thiết bị Matter, nhập mã thủ công. Cần bật chế độ developer/test cho thiết bị chưa chứng nhận theo hướng dẫn của từng hãng.
- **chip-tool**:
  ```bash
  chip-tool pairing onnetwork 1 20202021
  chip-tool onoff on 1 1
  chip-tool levelcontrol move-to-level 127 0 0 0 1 1   # 50 % = 32 LED
  chip-tool colorcontrol move-to-hue-and-saturation 0 254 0 0 0 1 1   # đỏ
  ```

Khi xóa thiết bị khỏi tất cả ứng dụng, thiết bị tự mở lại chế độ ghép nối 5 phút. Muốn ghép lại sau đó, giữ nút BOOT 5 giây để factory reset (Wi-Fi mặc định trong firmware sẽ được dùng lại).

## Cấu trúc mã nguồn

| File | Nội dung |
|---|---|
| `main/app_main.cpp` | Tạo node Matter, endpoint Extended Color Light, in mã ghép nối on-network |
| `main/app_driver.cpp` | Nhận thay đổi OnOff / LevelControl / ColorControl từ Matter, nút BOOT |
| `main/led_bar.c` | Điều khiển WS2812 qua RMT: số LED sáng theo độ sáng, cường độ cố định 30 % |
| `main/color_convert.c` | Đổi Hue/Saturation, XY, nhiệt độ màu sang RGB |
| `main/Kconfig.projbuild` | Tùy chọn: GPIO, số LED, mức 30 %, chiều sáng, chân nút |
