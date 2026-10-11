# Matter Energy Meter: ESP32-C3 + BL0942

Thiết bị đo điện chuẩn **Matter over Wi-Fi**, viết bằng **ESP-IDF + esp-matter**. Chip dùng là **ESP32-C3 Mini** (SuperMini, DevKitM...), IC đo là **BL0942** giao tiếp UART.

Các giá trị đo được:

| Đại lượng | Matter cluster / attribute | Đơn vị Matter |
|---|---|---|
| Điện áp | Electrical Power Measurement: `Voltage`, `RMSVoltage` | mV |
| Dòng điện | Electrical Power Measurement: `ActiveCurrent`, `RMSCurrent` | mA |
| Công suất tác dụng | Electrical Power Measurement: `ActivePower` | mW |
| Công suất biểu kiến | Electrical Power Measurement: `ApparentPower` | mVA |
| Tần số | Electrical Power Measurement: `Frequency` | mHz |
| Hệ số công suất | Electrical Power Measurement: `PowerFactor` | ×10000 |
| Điện năng tiêu thụ | Electrical Energy Measurement: `CumulativeEnergyImported` (và event `CumulativeEnergyMeasured`) | mWh |
| **Công suất (W) cho Apple Home** | Illuminance Measurement (Light Sensor), giá trị **lux = W** | lux |

## 1. Phần cứng

### Sơ đồ nối dây

```
 BL0942 module              ESP32-C3 Mini
 ┌───────────┐              ┌──────────────┐
 │       VCC ├──────────────┤ 3V3          │
 │       GND ├──────────────┤ GND          │
 │        TX ├──────────────┤ GPIO0 (RX)   │
 │        RX ├──────────────┤ GPIO1 (TX)   │
 └───────────┘              │ GPIO9 = BOOT │ ← giữ 5 s để factory reset
                            └──────────────┘
```

* UART1, **4800 baud**, 8N1 (mặc định khi chân `SCLK_BPS` của BL0942 nối GND; nối VDD thì đổi sang 9600 trong menuconfig).
* Địa chỉ chip (A1/A2) mặc định là 0.
* Có thể đổi chân, baud, địa chỉ trong `idf.py menuconfig` → **BL0942 Energy Meter**.

> ⚠️ **AN TOÀN ĐIỆN:** Hầu hết module BL0942 **không cách ly**, nghĩa là GND của module nối thẳng vào dây pha hoặc dây trung tính 220 V. Khi module đang cắm điện lưới, toàn bộ ESP32-C3 cũng mang điện thế lưới.
> **Không cắm cáp USB vào máy tính khi module đang nối điện lưới.** Nạp firmware và xem log khi đã ngắt điện lưới, hoặc dùng module/bộ cách ly (isolated UART + nguồn cách ly). Lắp trong hộp kín, có cầu chì.

### Lưu ý về GPIO0/GPIO1 trên ESP32-C3

GPIO0/GPIO1 cũng là chân XTAL_32K. Mặc định IDF không bật thạch anh 32 kHz ngoài, nên dùng làm UART bình thường. Đây không phải chân strapping.

## 2. Cài môi trường

Dự án dùng **esp-matter `release/v1.5`** và **ESP-IDF v5.5.x** (bản esp-matter v1.5 khuyến nghị).

```bash
# ESP-IDF
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32c3 && . ./export.sh && cd ..

# esp-matter
git clone -b release/v1.5 --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
cd connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 linux --shallow
cd ../..
./install.sh
cd ..
```

Mỗi lần mở terminal mới:

```bash
. ./esp-idf/export.sh
. ./esp-matter/export.sh
```

## 3. Build và nạp

```bash
cd ESP32-C3/matter-wifi/matter-energy-bl0942
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

> Lần nạp đầu nên `erase-flash` để xoá dữ liệu Matter cũ.

## 4. Kết nối vào Apple Home (HomeKit)

Firmware dùng chứng chỉ test của Matter (VID `0xFFF1`, PID `0x8000`).

* Mã QR in ra trong log khi khởi động (dòng `SetupQRCode`), hoặc dùng mã thủ công mặc định: **`34970112332`**.
* Trên iPhone: **Nhà (Home) → + → Thêm phụ kiện**, quét QR. Khi app báo "Phụ kiện chưa được chứng nhận", chọn **Vẫn thêm**.
* Cần có Home Hub (Apple TV / HomePod) và iPhone ở cùng mạng Wi-Fi 2.4 GHz.

### Xem công suất trên Apple Home

Cluster **Electrical Power/Energy Measurement** là chuẩn Matter 1.3. Tôi không xác minh được ứng dụng Nhà của Apple hiện có hiển thị cluster này hay không; các nguồn nói khác nhau. Để chắc chắn **xem được công suất trên Apple Home**, firmware thêm endpoint thứ 2 là **Light Sensor** (bật mặc định, tắt bằng `CONFIG_HOMEKIT_POWER_AS_LIGHT_SENSOR`):

* Giá trị độ sáng (lux) = **công suất tiêu thụ (W)**. Ví dụ "120 lx" nghĩa là đang tiêu thụ 120 W.
* Dùng được trong **Tự động hoá** (Automation) của Apple Home, ví dụ "khi độ sáng > 1000 lx" tức là công suất > 1000 W thì tắt ổ cắm khác hoặc gửi thông báo.
* Dưới 1 W hiển thị 0 (giới hạn của Illuminance Measurement là ≥ 1 lux).

Để xem đầy đủ điện áp, dòng, kWh, dùng **Home Assistant** (Matter integration). HA đọc đúng các cluster Electrical Power/Energy Measurement. Cũng có thể dùng thêm Google Home, SmartThings hoặc `chip-tool` (Matter multi-admin).

```bash
# Ví dụ đọc bằng chip-tool (node id 1, endpoint 1)
chip-tool electricalpowermeasurement read active-power 1 1
chip-tool electricalpowermeasurement read voltage 1 1
chip-tool electricalenergymeasurement read cumulative-energy-imported 1 1
```

## 5. Hiệu chuẩn

Công thức:

```
U (V)   = V_RMS  / VOLTAGE_REF
I (A)   = I_RMS  / CURRENT_REF
P (W)   = WATT   / POWER_REF
E (kWh) = CF_CNT / ENERGY_REF      (ENERGY_REF = 0 → tự tính = POWER_REF * 3600000 / 419430.4)
```

Giá trị mặc định đúng cho module phổ biến (shunt 1 mΩ, phân áp 5 × 390 kΩ / 510 Ω). Nếu module của bạn khác hoặc cần chính xác hơn:

1. Cắm một tải thuần trở đã biết (ví dụ bóng sợi đốt / ấm đun 1000 W) và một đồng hồ chuẩn.
2. Bật log debug cho tag `energy_meter` (`esp_log_level_set("energy_meter", ESP_LOG_DEBUG)` hoặc menuconfig) để xem `U= I= P=`.
3. Tính lại: `REF_mới = REF_cũ × (giá trị đo được / giá trị chuẩn)`.
4. Nhập vào `idf.py menuconfig` → **BL0942 Energy Meter → Calibration**.

Ngưỡng nhiễu (mặc định 15 mA và 0.5 W) dùng để hiển thị 0 khi không có tải.

## 6. Điện năng tích luỹ

* Tính từ bộ đếm xung phần cứng `CF_CNT` của BL0942 (24 bit, tự xử lý tràn và trường hợp BL0942 bị reset).
* Tổng điện năng được **lưu vào NVS** mỗi khi tăng thêm `CONFIG_ENERGY_NVS_SAVE_DELTA_WH` (mặc định 10 Wh), nên mất điện chỉ mất tối đa phần chưa lưu.
* Factory reset (giữ nút BOOT 5 giây) xoá cấu hình Matter. Tổng điện năng nằm ở namespace NVS `energy_meter`, muốn xoá thì `idf.py erase-flash`.

## 7. Cấu trúc mã nguồn

```
matter-energy-bl0942/
├── CMakeLists.txt
├── partitions.csv              # OTA 2 x 1.875 MB, flash 4 MB
├── sdkconfig.defaults          # Bật BLE commissioning, tắt các cluster không dùng
├── sdkconfig.defaults.esp32c3
└── main/
    ├── app_main.cpp            # Khởi tạo Matter node, nút reset
    ├── energy_meter.cpp/.h     # Endpoint Matter + delegate + task đo
    ├── bl0942.c/.h             # Driver UART BL0942
    ├── Kconfig.projbuild       # Cấu hình chân, hiệu chuẩn, chu kỳ đọc
    └── idf_component.yml
```

Mô hình dữ liệu Matter:

| Endpoint | Device type | Cluster chính |
|---|---|---|
| 0 | Root Node | Basic Information, Network Commissioning, ... |
| 1 | Electrical Sensor (0x0510) | Power Topology (Node), Electrical Power Measurement (AC), Electrical Energy Measurement (Imported + Cumulative) |
| 2 | Light Sensor (0x0106) | Illuminance Measurement (lux = W), tuỳ chọn |

## 8. Xử lý sự cố

| Hiện tượng | Nguyên nhân / cách xử lý |
|---|---|
| Log `BL0942 not responding` / `Timeout` | Kiểm tra TX↔RX có bị đảo không, VCC 3.3 V, baud (4800/9600), địa chỉ A1/A2 |
| `Checksum mismatch` thỉnh thoảng | Nhiễu đường UART: dây ngắn lại, thêm GND chung |
| Giá trị sai tỷ lệ | Hiệu chuẩn lại (mục 5) |
| Apple Home không tìm thấy | Wi-Fi phải là 2.4 GHz; giữ BOOT 5 s để reset rồi thêm lại |
