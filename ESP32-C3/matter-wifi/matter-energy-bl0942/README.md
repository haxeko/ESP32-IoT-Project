# ESP32-C3 Matter over Wi-Fi: ổ cắm Relay + đo công suất BL0942

Firmware ESP-IDF + [esp-matter](https://github.com/espressif/esp-matter) cho ESP32-C3 Mini. Thiết bị là một **ổ cắm Matter** (On/Off Plug-in Unit): điều khiển relay, đo công suất bằng **BL0942** qua UART, kết nối qua **Wi-Fi**, commissioning qua **BLE**.

Cấu trúc Matter giống hệt bản BL0937 ([haxeko/ESP32-C3-Matter](https://github.com/haxeko/ESP32-C3-Matter)), là bản đã hiện được công suất trên Apple Home. Chỉ khác driver đọc cảm biến.

| Chức năng | Chân |
|---|---|
| Relay (active-high mặc định) | GPIO5 |
| Nút BOOT, nhấn ngắn: bật/tắt relay | GPIO9 |
| Nút BOOT, giữ 5 s rồi thả: factory reset | GPIO9 |
| BL0942 **TX** → ESP32 RX (UART0) | **GPIO0** |
| BL0942 **RX** ← ESP32 TX (UART0) | **GPIO1** |

Trạng thái relay được lưu NVS và khôi phục sau khi mất điện.

BL0942 dùng **UART0** giống ESPHome (`logger: baud_rate: 0`). Console log chuyển sang UART1 trên đúng chân cũ (TX GPIO21, RX GPIO20), cộng với USB-Serial-JTAG, nên cách xem log không đổi.

## Đo công suất (BL0942)

Cùng endpoint với relay (endpoint 1), firmware thêm device type **Electrical Sensor**:

| Cluster | Thuộc tính | Đơn vị |
|---|---|---|
| Power Topology | Node Topology | |
| Electrical Power Measurement | ActivePower | mW |
| | Voltage | mV |
| | ActiveCurrent | mA |
| Electrical Energy Measurement | CumulativeEnergyImported | mWh (lưu NVS mỗi 10 phút) |

- Khi khởi động, driver cấu hình BL0942 giống ESPHome (mở khoá ghi, ghi thanh ghi MODE, khoá lại). Log in `BL0942 answering` khi đọc được chip, `BL0942 not responding` nếu chip không trả lời (driver tự cấu hình lại định kỳ).
- Khi chưa đọc được BL0942, firmware gửi 0 W lên Matter (giống bản BL0937). Apple Home ẩn ô công suất nếu giá trị là null, nên firmware luôn gửi số.
- Đọc gói đầy đủ của BL0942 (lệnh `0x58 0xAA`, 23 byte, có kiểm tra checksum) mỗi 2 s (`BL0942_UPDATE_INTERVAL_MS`).
- Điện năng tính từ bộ đếm xung phần cứng `CF_CNT`, có xử lý tràn 24 bit và trường hợp BL0942 bị reset. Xung đếm được trong lúc vài lần đọc bị lỗi vẫn được cộng đủ.
- Giá trị gửi lên Matter được làm tròn giống cấu hình ESPHome: công suất 0.1 W, điện áp 1 V, dòng 0.01 A, để dao động nhỏ không gây báo cáo liên tục. Công suất lấy trị tuyệt đối, khớp chế độ đếm điện năng tuyệt đối của chip.

**Hiển thị trên app:** dữ liệu theo chuẩn Matter 1.3+. Home Assistant, SmartThings, Google Home đều đọc được. Apple Home hiển thị công suất tuỳ phiên bản iOS. Nếu không thấy trong Apple Home thì dữ liệu vẫn có trên Matter, đọc được qua Home Assistant hoặc chip-tool:

```bash
chip-tool electricalpowermeasurement read active-power 0x7283 1
chip-tool electricalenergymeasurement read cumulative-energy-imported 0x7283 1
```

### Hiệu chuẩn

`idf.py menuconfig` → **BL0942 Power Meter**:

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `BL0942_UART_BAUD` | 9600 | 9600 khi chân `SCLK_BPS` nối VDD, 4800 khi nối GND |
| `BL0942_ADDRESS` | 0 | địa chỉ chip theo chân A1/A2 |
| `BL0942_VOLTAGE_REF` | 15873.35944 | LSB/V, module phổ biến (phân áp 5 × 390 kΩ + 510 Ω) |
| `BL0942_CURRENT_REF` | 251213.46469 | LSB/A, shunt 1 mΩ |
| `BL0942_POWER_REF` | 596 | LSB/W |
| `BL0942_ENERGY_REF` | 0 | xung CF/kWh; 0 = tự tính từ `POWER_REF` |
| `BL0942_*_CAL_PERMILLE` | 1000 | hệ số hiệu chỉnh ‰ cho công suất (kèm điện năng) / áp / dòng |

Cách hiệu chỉnh: cắm tải thuần trở đã biết (bóng sợi đốt / ấm đun 1000 W), so với đồng hồ chuẩn, rồi đặt `CAL = 1000 × giá_trị_chuẩn / giá_trị_đọc`. Ví dụ đọc 1050 W, thực tế 1000 W thì đặt `POWER_CAL_PERMILLE=952`.

### Lưu ý phần cứng

- BL0942 thường nối GND về dây pha/trung tính của lưới điện, nên **mạch không cách ly**. Không cắm USB vào máy tính khi mạch đang cấp 220 V.
- Cấp BL0942 bằng **3.3 V** để mức UART tương thích ESP32-C3.
- GPIO0/GPIO1 trên ESP32-C3 là chân XTAL_32K, nhưng mặc định không dùng thạch anh ngoài, nên dùng làm UART bình thường.

## Cấu hình

`idf.py menuconfig` → **Relay Configuration**:

- `RELAY_GPIO` (mặc định 5)
- `RELAY_ACTIVE_LEVEL` (1 = active-high, 0 = active-low; module relay opto rẻ tiền thường là active-low)

Hoặc sửa trực tiếp `sdkconfig.defaults.esp32c3` rồi xoá `sdkconfig` để áp dụng lại.

## Build

Yêu cầu: ESP-IDF v6.0.2 + esp-matter release/v1.6 (đã `export.sh` cả hai). Cách nhanh nhất là dùng Docker:

```bash
docker run --rm -it -v $PWD:/project \
  espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c "cd /project && idf.py set-target esp32c3 && idf.py build"
```

Hoặc cài local:

```bash
. ~/esp/esp-idf/export.sh
. ~/esp/esp-matter/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

GitHub Actions (`.github/workflows/matter-energy-bl0942.yml` ở gốc repo) tự build và upload file `.bin`, trong đó có `matter_plug_bl0942_merged.bin` để nạp một lần ở địa chỉ `0x0`. Vào tab **Actions** → lần chạy mới nhất → **Artifacts**.

## Phiên bản firmware

Mỗi lần build, firmware tự có phiên bản mới dạng `1.1.<yyMMddHHmm>` (giờ build theo UTC). Ví dụ `1.1.2610110915` là bản build lúc 09:15 UTC ngày 11/10/2026. Xem phiên bản ở:

- **Apple Home**: mở phụ kiện → cài đặt → kéo xuống mục **Firmware**.
- **Log khi khởi động**: dòng `App version: 1.1.xxxxxxxxxx`.

Khi build local, phiên bản lấy theo lúc CMake cấu hình. Muốn có số mới thì chạy `idf.py fullclean` trước khi build. GitHub Actions luôn build từ đầu nên mỗi lần chạy đều có số mới. Đổi `FW_VERSION_MAJOR_MINOR` trong `CMakeLists.txt` khi muốn tăng số chính.

## Thông tin thiết bị

| Thông tin Matter | Giá trị | Đổi trong `menuconfig` → **Device Information** |
|---|---|---|
| Manufacturer (VendorName) | **Haxeko** | `DEVICE_INFO_VENDOR_NAME` |
| Model (ProductName, ProductLabel) | **Matter Plug** | `DEVICE_INFO_PRODUCT_NAME` |
| Serial Number | **Địa chỉ MAC Wi-Fi**, 12 ký tự hex, ví dụ `A0B1C2D3E4F5` | tự động |
| Tên BLE khi kết nối | `MatterPlug-3840` (tiền tố + discriminator, tối đa 16 ký tự) | `BLE_DEVICE_NAME_PREFIX` |

Apple Home dùng Model làm tên mặc định khi thêm phụ kiện. Với phụ kiện đã thêm từ trước, xoá rồi thêm lại để Home đọc thông tin mới.

## Cập nhật OTA

Mỗi lần build tạo thêm `build/matter_plug_bl0942-ota.bin`: file OTA chuẩn Matter (app firmware kèm header VID `0xFFF1`, PID `0x8000`, số phiên bản). Số phiên bản theo giờ build nên bản mới luôn lớn hơn bản đang chạy, thiết bị mới chịu nhận.

| File | Dùng khi |
|---|---|
| `matter_plug_bl0942-ota.bin` | Cập nhật qua Matter OTA (OTA Provider) |
| `matter_plug_bl0942.bin` | Nạp app qua USB ở `0x20000` (`esptool.py write_flash 0x20000 ...`), giữ nguyên dữ liệu Matter và kWh |
| `matter_plug_bl0942_merged.bin` | Nạp lần đầu / nạp lại toàn bộ ở `0x0` |

Apple Home chỉ cập nhật firmware đã đăng ký trên DCL (cơ sở dữ liệu chứng nhận Matter), nên không đẩy được file OTA tự build với VID test. Dùng một OTA Provider thay thế, ví dụ `chip-tool` + `chip-ota-provider-app` của connectedhomeip:

```bash
# Máy tính chạy OTA Provider (node id 0x1234), phục vụ file OTA
./chip-ota-provider-app -f matter_plug_bl0942-ota.bin

# Ghép OTA Provider vào cùng fabric, cấp quyền cho thiết bị đọc nó
chip-tool pairing onnetwork 0x1234 20202021
chip-tool accesscontrol write acl '[{"fabricIndex": 1, "privilege": 5, "authMode": 2, "subjects": [112233], "targets": null}, {"fabricIndex": 1, "privilege": 3, "authMode": 2, "subjects": null, "targets": [{"cluster": 41, "endpoint": null, "deviceType": null}]}]' 0x1234 0

# Yêu cầu thiết bị (node id 0x7283) tải bản mới từ Provider
chip-tool otasoftwareupdaterequestor announce-otaprovider 0x1234 0 0 0 0x7283 0
```

Thiết bị tải xong sẽ tự khởi động lại vào bản mới. Kiểm tra mục Firmware trong Apple Home (hoặc dòng `App version` trong log) để xác nhận.

## Commissioning

Firmware dùng chứng chỉ test (DAC test) của esp-matter, mã ghép nối mặc định:

- Manual code: **34970112332**
- QR: `MT:Y.K9042C00KA0648G00`
- Discriminator 3840, passcode 20202021

Mở Apple Home / Google Home / SmartThings / Home Assistant → *Add Matter device* → quét QR hoặc nhập mã ở trên. Với chip-tool:

```bash
chip-tool pairing ble-wifi 0x7283 <SSID> <PASSWORD> 20202021 3840
chip-tool onoff toggle 0x7283 1
```

Vì là thiết bị dùng chứng chỉ test, app sẽ cảnh báo "Uncertified accessory". Chọn tiếp tục.

> Nếu thiết bị đã từng được thêm vào Home với firmware khác, hãy xoá phụ kiện, `erase-flash`, nạp lại rồi thêm lại để Home đọc đúng cấu trúc mới.

## Cấu trúc

```
CMakeLists.txt             project esp-matter (esp32c3)
sdkconfig.defaults         cấu hình chung Matter/Wi-Fi/BLE, bật cluster đo điện
sdkconfig.defaults.esp32c3 chân nút, relay, UART BL0942
partitions.csv             4 MB, 2 phân vùng OTA
main/app_main.cpp          tạo node + endpoint On/Off Plug-in Unit
main/app_driver.cpp        điều khiển GPIO relay + nút
main/bl0942.c              driver BL0942 (UART, checksum, CF_CNT -> điện năng)
main/power_meter.cpp       Electrical Sensor: đưa công suất / áp / dòng / điện năng lên Matter
main/Kconfig.projbuild     tuỳ chọn relay, UART, hiệu chuẩn BL0942
```

## Sơ đồ đấu nối

```
BL0942 VCC ◄── 3V3      ESP32-C3
BL0942 GND ◄── GND
BL0942 TX  ──► GPIO0 (RX)
BL0942 RX  ◄── GPIO1 (TX)

ESP32-C3 GPIO5 ──► IN  (module relay 3.3V hoặc qua transistor)
ESP32-C3 3V3   ──► VCC (module relay 5V: cấp 5V, IN vẫn dùng mức 3.3V)
ESP32-C3 GND   ──► GND
```

⚠️ Phía tải dùng điện 220 V: cách ly và đi dây an toàn.
