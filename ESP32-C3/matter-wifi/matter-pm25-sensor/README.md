# Matter PM2.5 Air Quality Sensor (ESP32-C3, Wi-Fi)

Cảm biến bụi mịn PM2.5 chuẩn **Matter over Wi-Fi** chạy trên ESP32-C3, thêm trực tiếp vào **Apple Home (HomeKit)**, Google Home, Home Assistant, SmartThings...

Thiết bị dùng device type **Air Quality Sensor (0x002C)** với các cluster:

| Cluster | Nội dung |
|---|---|
| Air Quality (0x005B) | Mức chất lượng không khí, tính từ PM2.5 (trạng thái chính của cảm biến) |
| PM2.5 Concentration Measurement (0x042A) | PM2.5, µg/m³ |
| PM10 Concentration Measurement (0x042D) | PM10, µg/m³ |
| PM1 Concentration Measurement (0x042C) | PM1.0, µg/m³ (chỉ có ở cảm biến PMS) |

## Phần cứng

- ESP32-C3 (ESP32-C3 SuperMini, ESP32-C3-DevKitM-1, ...), flash 4 MB
- Một cảm biến bụi UART:
  - **Plantower PMS5003 / PMS7003 / PMSA003** (mặc định)
  - **Nova Fitness SDS011 / SDS021**

### Sơ đồ nối dây

| Cảm biến | ESP32-C3 |
|---|---|
| VCC (5V) | 5V / VBUS |
| GND | GND |
| TX | **GPIO20** (`rx_pin`) |
| RX | **GPIO21** (`tx_pin`) |
| SET (PMS, nếu có) | để hở hoặc 3.3V |
| RESET (PMS, nếu có) | để hở hoặc 3.3V |

Quạt của cảm biến cần nguồn **5V**, nhưng mức logic UART là 3.3V nên nối thẳng vào ESP32-C3 được.

Nút **BOOT (GPIO9)**: giữ 5 giây để factory reset (xóa Wi-Fi và thông tin ghép nối Matter).

> **Lưu ý về GPIO20/GPIO21:** đây là chân UART0 (console) mặc định của ESP32-C3. Firmware này chuyển log sang **USB-Serial-JTAG** có sẵn trên chip (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`), còn UART1 dùng cho cảm biến. Vì vậy:
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

### Cấu hình (`idf.py menuconfig` → *PM2.5 Sensor Configuration*)

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| PM sensor model | PMS5003/7003/A003 | Chọn loại cảm biến |
| UART RX pin | 20 | Nối với chân TX của cảm biến |
| UART TX pin | 21 | Nối với chân RX của cảm biến |
| Report interval | 10 s | Lấy trung bình các mẫu trong khoảng này rồi cập nhật lên Matter |
| SDS011 working period | 0 (liên tục) | Chế độ đo chu kỳ của SDS011 (1–30 phút), giúp kéo dài tuổi thọ laser/quạt |

## Thêm vào Apple Home

1. Mở log (`idf.py monitor`); khi thiết bị chạy lần đầu sẽ in ra link QR code, ví dụ:
   `https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00`
2. Mở link đó để hiện QR code, hoặc dùng mã ghép nối thủ công **`3497-011-2332`** (passcode mặc định `20202021`, discriminator `3840`).
3. iPhone: **Nhà → + → Thêm phụ kiện → quét QR**. Khi gặp cảnh báo phụ kiện chưa được chứng nhận (*Uncertified Accessory*), chọn tiếp tục thêm (*Add Anyway*).
4. iPhone gửi thông tin Wi-Fi cho ESP32-C3 qua BLE. Sau khi thêm xong, phụ kiện hiện là **Cảm biến chất lượng không khí**, gồm mức chất lượng không khí, mật độ PM2.5 và PM10.

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

Muốn đổi ngưỡng thì sửa hàm `pm25_to_air_quality()` trong `main/app_main.cpp`.

Nếu cảm biến không gửi dữ liệu (lỏng dây, mất nguồn), thiết bị báo các giá trị PM là `null` và Air Quality là `Unknown`. Khi có lại dữ liệu, thiết bị tự cập nhật bình thường.

## Cấu trúc thư mục

```
matter-pm25-sensor/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
└── main/
    ├── app_main.cpp          # Matter node, endpoint Air Quality Sensor, cập nhật attribute
    ├── Kconfig.projbuild     # Cấu hình cảm biến / chân UART
    ├── idf_component.yml
    └── drivers/
        ├── pm_sensor.h
        └── pm_sensor.cpp     # Driver UART cho PMS5003/7003/A003 và SDS011
```
