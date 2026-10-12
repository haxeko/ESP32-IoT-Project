# Matter PM2.5 Air Quality Sensor (ESP32-C3, Wi-Fi)

Cảm biến bụi mịn PM2.5 chuẩn **Matter over Wi-Fi** chạy trên ESP32-C3, thêm trực tiếp vào **Apple Home (HomeKit)**, Google Home, Home Assistant, SmartThings...

Thiết bị dùng device type **Air Quality Sensor (0x002C)** và chỉ gửi qua Matter hai giá trị:

| Cluster | Nội dung |
|---|---|
| Air Quality (0x005B) | Mức chất lượng không khí, tính từ PM2.5 (trạng thái chính của cảm biến) |
| PM2.5 Concentration Measurement (0x042A) | PM2.5, µg/m³ |

Màn hình OLED, đèn LED WS2812 và radar LD2410 chạy **tại chỗ trên thiết bị**, không gửi qua Matter (giống bản ESPHome trước đó):

- **LED WS2812** đổi màu theo mức chất lượng không khí: xanh dương, xanh lá, vàng, cam, đỏ.
- **Màn hình OLED** hiện PM2.5 và mức chất lượng không khí:
  - Luôn sáng khi khởi động cho tới khi có kết quả đo đầu tiên, sau đó sáng thêm 5 giây. Nếu cảm biến PM lỗi thì tối đa 120 giây.
  - Sau đó chỉ sáng khi **radar LD2410 thấy người trong khoảng 120 cm**, và tắt sau 60 giây khi không còn ai.

## Phần cứng

- ESP32-C3 (ESP32-C3 SuperMini, ESP32-C3-DevKitM-1, ...), flash 4 MB
- Một cảm biến bụi UART:
  - **Plantower PMS5003 / PMS7003 / PMSA003** (mặc định)
  - **Nova Fitness SDS011 / SDS021**
- Màn hình OLED **SH1106 128x64** (I2C, địa chỉ 0x3C)
- Dải LED **WS2812**, 6 bóng
- Radar hiện diện **HLK-LD2410**

Các phần OLED, LED và LD2410 đều không bắt buộc. Thiếu phần nào thì firmware ghi lỗi vào log rồi chạy tiếp, cảm biến PM2.5 và Matter vẫn hoạt động.

Thiết bị **chạy độc lập với Matter**: cảm biến, màn hình, LED và radar khởi động trước Matter. Chúng vẫn hoạt động khi thiết bị chưa được ghép nối, khi mất Wi-Fi, hoặc khi Matter không khởi động được. Mỗi 30 giây, log in một dòng `local_ui: status: ...` cho biết trạng thái của màn hình, radar và LED.

### Sơ đồ nối dây

| Thiết bị | Chân | ESP32-C3 |
|---|---|---|
| PMS5003 | TX | **GPIO20** |
| PMS5003 | RX | **GPIO21** |
| PMS5003 | VCC / GND | 5V / GND |
| LD2410 | TX | **GPIO3** |
| LD2410 | RX | **GPIO4** |
| LD2410 | VCC / GND | 5V / GND |
| OLED SH1106 | SDA | **GPIO8** |
| OLED SH1106 | SCL | **GPIO9** |
| OLED SH1106 | VCC / GND | 3.3V / GND |
| WS2812 | DIN | **GPIO10** |
| WS2812 | VCC / GND | 5V / GND |

Quạt PMS5003 và radar LD2410 cần nguồn **5V**, nhưng mức logic UART là 3.3V nên nối thẳng vào ESP32-C3 được. Chân SET/RESET của PMS (nếu có) để hở hoặc nối 3.3V.

Việc phân chia UART giống bản ESPHome: PMS5003 dùng UART1, LD2410 dùng UART0 (256000 baud), còn log chạy qua USB.

### Factory reset

GPIO9 (nút BOOT) đang làm chân SCL của màn hình, nên không dùng nút BOOT để reset được. Thay vào đó:

- **Cắm rồi rút nguồn 5 lần liên tiếp**, mỗi lần giữ điện dưới 10 giây. Ở lần cắm thứ 5, thiết bị xóa Wi-Fi và thông tin ghép nối Matter, rồi chờ ghép nối lại.
- Nếu bạn xóa thiết bị khỏi tất cả ứng dụng (Apple Home, Home Assistant...), thiết bị tự mở lại chế độ ghép nối, không cần reset.

> **Lưu ý về GPIO20/GPIO21:** đây là chân UART0 (console) mặc định của ESP32-C3. Firmware này chuyển log sang **USB-Serial-JTAG** có sẵn trên chip (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). Vì vậy:
> - Nạp firmware và xem log qua **cổng USB native** của ESP32-C3 (SuperMini chỉ có cổng này).
> - Với board có chip USB-UART (CP2102/CH340) nối vào GPIO20/21 như ESP32-C3-DevKitM-1, hãy cắm board qua cổng USB native, và tháo dây cảm biến nếu cần nạp qua cổng USB-UART.
> - Khi khởi động, ROM bootloader in vài dòng log ra GPIO21. Cảm biến bỏ qua dữ liệu này, không ảnh hưởng.

## Phần mềm

Yêu cầu: **ESP-IDF v5.4.1** và **esp-matter `release/v1.4`**.

```bash
# ESP-IDF
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32c3 && cd ..

# esp-matter
git clone -b release/v1.4 --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
./connectedhomeip/connectedhomeip/scripts/checkout_submodules.py --platform esp32 linux --shallow
./install.sh
cd ..
```

Build và nạp:

```bash
. ./esp-idf/export.sh
. ./esp-matter/export.sh

cd ESP32-C3/matter-wifi/matter-pm25-sensor
idf.py set-target esp32c3
idf.py menuconfig          # (tùy chọn) PM2.5 Sensor Configuration
idf.py -p /dev/ttyACM0 flash monitor
```

### Cấu hình (`idf.py menuconfig`)

*PM2.5 Sensor Configuration*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| PM sensor model | PMS5003/7003/A003 | Chọn loại cảm biến |
| UART RX / TX pin | 20 / 21 | Chân nối với TX / RX của cảm biến |
| PMS measurement period | 300 s | PMS ngủ giữa các lần đo (tắt quạt và laser). Mỗi chu kỳ: đánh thức, chờ 30 giây, đo 5 giây rồi ngủ lại. Giá trị đầu tiên có sau khoảng 35 giây. Đặt `0` để đo liên tục |
| Report interval | 10 s | Chỉ dùng khi đo liên tục: lấy trung bình các mẫu trong khoảng này |
| SDS011 working period | 0 (liên tục) | Chế độ đo chu kỳ của SDS011 (1–30 phút) |

*Local Display and Indicators*:

| Tùy chọn | Mặc định |
|---|---|
| OLED SDA / SCL pin | 8 / 9 |
| OLED I2C address | 0x3C |
| Rotate OLED 180 degrees | bật |
| WS2812 data pin / số LED / độ sáng | 10 / 6 / 50% |
| LD2410 RX / TX pin | 3 / 4 |
| Khoảng cách bật màn hình | 120 cm |
| Giữ màn hình sáng sau khi người rời đi | 60 s |
| Bỏ qua dữ liệu radar cũ hơn | 30 s |

### Thông tin thiết bị và phiên bản firmware

Ứng dụng nhà thông minh hiển thị các thông tin sau:

| Mục | Giá trị | Sửa ở đâu |
|---|---|---|
| Nhà sản xuất (Manufacturer) | `Haxeko` | `menuconfig` → *Device Information* |
| Kiểu máy (Model) | `PM5003` | `menuconfig` → *Device Information* |
| Firmware | `1.0.<số commit>`, ví dụ `1.0.8` | Tự động |

Phiên bản firmware **tự tăng sau mỗi commit**. Số cuối là số commit của repo (`git rev-list --count HEAD`), và cũng được dùng làm số phiên bản Matter (SoftwareVersion) cho OTA. Khi ra bản lớn, sửa `FIRMWARE_VERSION_BASE` trong `CMakeLists.txt`, ví dụ từ `1.0` thành `1.1`.

- Clone repo đầy đủ, **không dùng `--depth`**. Với clone nông, số commit sẽ sai.
- Nếu build từ thư mục không có git (ví dụ tải file zip), phiên bản sẽ là `1.0.0`.

## Thêm vào Apple Home

1. Mở log (`idf.py monitor`); khi thiết bị chạy lần đầu sẽ in ra link QR code, ví dụ:
   `https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00`
2. Mở link đó để hiện QR code, hoặc dùng mã ghép nối thủ công **`3497-011-2332`** (passcode mặc định `20202021`, discriminator `3840`).
3. iPhone: **Nhà → + → Thêm phụ kiện → quét QR**. Khi gặp cảnh báo phụ kiện chưa được chứng nhận (*Uncertified Accessory*), chọn tiếp tục thêm (*Add Anyway*).
4. iPhone gửi thông tin Wi-Fi cho ESP32-C3 qua BLE. Sau khi thêm xong, phụ kiện hiện là **Cảm biến chất lượng không khí**, gồm mức chất lượng không khí và mật độ PM2.5.

Nên dùng iOS bản mới và có Home hub (HomePod mini/HomePod/Apple TV 4K) để xem từ xa và chạy tự động hóa. Phiên bản iOS cũ có thể không hiển thị đầy đủ cảm biến chất lượng không khí Matter.

> Firmware dùng chứng chỉ test (VID `0xFFF1`, PID `0x8000`) nên chỉ hợp với mục đích DIY. Để làm sản phẩm thật, hãy tạo DAC/passcode riêng bằng `esp-matter-mfg-tool`.

## Cách tính mức chất lượng không khí

Air Quality được tính từ PM2.5 (trung bình theo report interval), dùng các ngưỡng AQI của US EPA (2024):

| PM2.5 (µg/m³) | US AQI | Matter `AirQualityEnum` |
|---|---|---|
| 0 – 9.0 | 0 – 50 | Good |
| 9.1 – 35.4 | 51 – 100 | Fair |
| 35.5 – 55.4 | 101 – 150 | Moderate |
| 55.5 – 125.4 | 151 – 200 | Poor |
| 125.5 – 225.4 | 201 – 300 | VeryPoor |
| > 225.4 | 301+ | ExtremelyPoor |

Apple Home tự quy đổi giá trị này sang thang chất lượng không khí của HomeKit (5 mức, từ Excellent đến Poor).

Trên thiết bị (LED và OLED), mức được chia 5 bậc giống bản ESPHome:

| PM2.5 (µg/m³) | Màn hình | LED |
|---|---|---|
| 0 – 9.0 | RẤT TỐT | Xanh dương |
| 9.1 – 35.4 | TỐT | Xanh lá |
| 35.5 – 55.4 | TRUNG BÌNH | Vàng |
| 55.5 – 125.4 | KÉM | Cam |
| > 125.4 | XẤU | Đỏ |

Muốn đổi ngưỡng thì sửa hàm `pm25_to_air_quality()` trong `main/app_main.cpp` (cho Matter) và `pm25_to_level()` trong `main/local_ui.cpp` (cho LED và màn hình).

Nếu cảm biến không gửi dữ liệu (lỏng dây, mất nguồn), Matter báo PM2.5 là `null` và Air Quality là `Unknown`. Màn hình và LED giữ giá trị cuối cùng, giống bản ESPHome. Khi có lại dữ liệu, thiết bị tự cập nhật bình thường.

## Đổi font màn hình

Font chữ trên OLED là bitmap sinh sẵn từ Roboto (Regular 12 px, Bold 16 px, Bold 28 px), nằm trong `main/ui/fonts.c`. Muốn đổi cỡ chữ hoặc thêm ký tự, sửa `tools/gen_fonts.py` rồi chạy:

```bash
pip install pillow
python3 tools/gen_fonts.py Roboto-Regular.ttf Roboto-Bold.ttf > main/ui/fonts.c
```

## Cấu trúc thư mục

```
matter-pm25-sensor/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── app_main.cpp          # Matter node, endpoint Air Quality Sensor, factory reset
│   ├── device_info.cpp       # Manufacturer / Model (Basic Information)
│   ├── local_ui.cpp          # Logic màn hình / LED / radar chạy tại chỗ
│   ├── Kconfig.projbuild     # Cấu hình chân và tham số
│   ├── idf_component.yml
│   ├── drivers/
│   │   ├── pm_sensor.cpp     # PMS5003/7003/A003 và SDS011 (UART1)
│   │   ├── ld2410.cpp        # Radar LD2410 (UART0)
│   │   ├── sh1106.cpp        # OLED SH1106 I2C + vẽ chữ
│   │   └── status_led.cpp    # WS2812
│   └── ui/
│       └── fonts.c           # Font bitmap Roboto (sinh bởi tools/gen_fonts.py)
└── tools/
    └── gen_fonts.py
```
