# Cảm biến nhiệt độ Matter over Wi-Fi: ESP32-C3 SuperMini + ENS160 / AHT21

Firmware **ESP-IDF + esp-matter** (SDK Matter chính thức của Espressif, không dùng Arduino hay ESPHome) cho **ESP32-C3 SuperMini** đọc module **ENS160 + AHT21** qua I2C. Thiết bị chạy chuẩn **Matter over Wi-Fi**, thêm thẳng vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa... không cần Thread Border Router.

> **ENS160 không đo nhiệt độ.** ENS160 (ScioSense) là cảm biến khí kim loại oxit (MOX): nó cho chỉ số chất lượng không khí **AQI-UBA** (1–5), **TVOC** (ppb) và **eCO2** (ppm). Nó *cần được cung cấp* nhiệt độ và độ ẩm môi trường để bù trừ phép đo, chứ không tự đo được. Module phổ biến "ENS160 + AHT21" có thêm chip **AHT21**: chính chip này đo nhiệt độ và độ ẩm.
>
> Firmware này lấy **nhiệt độ và độ ẩm từ AHT21**, gửi chúng cho ENS160 để bù trừ, và gửi qua Matter cả nhiệt độ, độ ẩm lẫn chất lượng không khí. Nếu module của bạn **chỉ có ENS160** (không có AHT21) thì không có nhiệt độ: gắn thêm một AHT20/AHT21 vào cùng bus I2C (địa chỉ 0x38).

## Thiết bị Matter

| Endpoint | Device type | Cluster | Giá trị | Nguồn |
|---|---|---|---|---|
| 1 | **Temperature Sensor** (0x0302) | Temperature Measurement | nhiệt độ, °C (bước 0,01) | AHT21 |
| 2 | **Humidity Sensor** (0x0307) | Relative Humidity Measurement | độ ẩm, % (bước 0,01) | AHT21 |
| 3 | **Air Quality Sensor** (0x002C) | Air Quality | mức chất lượng không khí | ENS160 (AQI-UBA) |
| | | TVOC Concentration Measurement | TVOC, ppb | ENS160 |
| | | Carbon Dioxide Concentration Measurement | eCO2, ppm (tắt được, xem dưới) | ENS160 |

- Mỗi endpoint có cluster **Identify**: khi ứng dụng gửi lệnh Identify, LED nháy nhanh.
- Trước khi ghép nối, thiết bị quảng bá loại **Temperature Sensor** (DNS-SD `DT` = 0x0302) với tên `ENS160 Sensor`.
- Cảm biến được đọc **10 giây một lần**. Giá trị chỉ được ghi sang Matter khi thay đổi đủ lớn để không gửi nhiễu cho mọi ứng dụng đang theo dõi: **0,1 °C**, **0,5 %RH**, **10 ppb** TVOC, **10 ppm** eCO2; mức chất lượng không khí thì mọi thay đổi.
- Cảm biến không trả lời (chưa cắm, lỏng dây) hoặc ENS160 đang làm nóng: Matter báo giá trị **null** (ứng dụng hiện "không có dữ liệu") và chất lượng không khí **Unknown**.

Mức chất lượng không khí lấy thẳng từ chỉ số AQI-UBA (Cơ quan Môi trường Liên bang Đức) mà ENS160 tính:

| AQI-UBA (ENS160) | Ý nghĩa | Matter AirQuality |
|---|---|---|
| 1 | Rất tốt (excellent) | 1 Good |
| 2 | Tốt (good) | 2 Fair |
| 3 | Trung bình (moderate) | 3 Moderate |
| 4 | Kém (poor) | 4 Poor |
| 5 | Không lành mạnh (unhealthy) | 5 VeryPoor |

Đổi cách quy đổi: sửa `ens160_aqi_to_air_quality()` trong `main/drivers/sensor_math.h` (và các feature Fair/Moderate/VeryPoor trong `main/sensor_endpoints.cpp` nếu dùng thêm mức khác).

## Phần cứng

- **ESP32-C3 SuperMini** (flash 4 MB). Bo ESP32-C3 khác cũng được: đổi chân trong `menuconfig` → *Board*.
- Module **ENS160 + AHT21** (I2C). ENS160 ở địa chỉ **0x53** (chân ADD ở mức cao, thường gặp nhất) hoặc **0x52** (ADD mức thấp): firmware tự tìm cả hai. AHT21 ở **0x38**.

### Nối dây

| Module ENS160 + AHT21 | ESP32-C3 SuperMini |
|---|---|
| VIN | **3V3** |
| GND | **GND** |
| SDA | **GPIO5** |
| SCL | **GPIO6** |
| ADD, CS, INT (nếu module có) | để hở (module đã kéo sẵn) |

- Nếu module có chân **CS** mà không tìm thấy ENS160 ở 0x52/0x53: nối CS lên 3V3 (CS ở mức thấp đưa ENS160 sang chế độ SPI).
- **Cấp 3,3 V** cho module. Một số module có LDO và ghi "VIN 3.3–5V", nhưng nếu điện trở kéo lên của SDA/SCL nối vào VIN thì cấp 5 V sẽ đưa 5 V vào chân ESP32-C3 (chịu tối đa 3,6 V). Cấp 3,3 V thì luôn an toàn.
- Module đã có điện trở kéo lên I2C; firmware bật thêm điện trở kéo lên bên trong (~45 kΩ) của ESP32-C3. Bus chạy 100 kHz nên dây dài 20–30 cm vẫn ổn.
- **Đặt module cách xa ESP32-C3** (dây 10 cm trở lên) và cho không khí lưu thông: chip ESP32-C3 nóng lên khi chạy Wi-Fi, và chính ENS160 có bộ gia nhiệt nằm ngay cạnh AHT21. Xem [Hiệu chỉnh nhiệt độ](#hiệu-chỉnh-nhiệt-độ).

| Chức năng | Chân SuperMini |
|---|---|
| I2C SDA / SCL (module ENS160 + AHT21) | GPIO5 / GPIO6 |
| LED xanh trên bo, sáng khi mức thấp | GPIO8 |
| Nút BOOT, tích cực mức thấp | GPIO9 |
| USB-C: nạp firmware và log (USB-Serial-JTAG) | GPIO18 (D−), GPIO19 (D+) |

- **GPIO2, GPIO8, GPIO9** là chân strapping (GPIO8 phải ở mức cao và GPIO9 mức thấp để vào chế độ nạp): đừng nối module vào đây. GPIO8/GPIO9 đã là LED và nút BOOT của SuperMini.
- **GPIO11–GPIO17** (flash) và **GPIO18/GPIO19** (USB): build báo lỗi (`static_assert` trong `main/board.h`) nếu chọn các chân này cho I2C, nút hoặc LED, hoặc khi hai chức năng trùng chân.
- Chân còn trống trên SuperMini: GPIO0, 1, 3, 4, 7, 10, 20, 21 (GPIO20/21 là UART0, ROM in log khởi động ra GPIO21).

### LED (GPIO8)

| LED | Trạng thái |
|---|---|
| Nháy rất nhanh (5 lần/giây) | Ứng dụng yêu cầu *Identify* |
| Nháy chậm (1 s sáng, 1 s tắt) | Chế độ ghép nối: đang chờ thêm vào ứng dụng Matter |
| Chớp ngắn 2 giây một lần | AHT21 hoặc ENS160 không trả lời: kiểm tra dây (log cho biết chip nào) |
| Tắt | Hoạt động bình thường |

### Nút BOOT (GPIO9)

| Thao tác | Kết quả |
|---|---|
| **Bấm đúp** | Mở **chế độ ghép nối** 15 phút. Chưa ghép nối: thêm lại khi cửa sổ lúc khởi động đã hết giờ. Đã ghép nối: thêm vào ứng dụng thứ hai (Multi-Admin), chỉ qua mạng vì BLE đã tắt |
| **Giữ 5 giây** | **Factory reset**: xóa Wi-Fi và thông tin ghép nối Matter, khởi động lại, rồi chờ ghép nối |

Xóa thiết bị khỏi mọi ứng dụng (Apple Home, Home Assistant...) thì thiết bị tự mở lại chế độ ghép nối 5 phút, không cần reset.

## Build và nạp

Cần **ESP-IDF v6.0.2** và **esp-matter 1.6.1**, như CI của repo. Cách nhanh nhất là Docker image chính thức của Espressif:

```bash
# Từ thư mục gốc của repo. Entrypoint của image tự nạp môi trường ESP-IDF/esp-matter rồi đổi thư mục, nên phải cd lại.
docker run --rm -v "$PWD":/project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 bash -c \
  'git config --global --add safe.directory "*" && cd /project/ESP32-C3/matter-wifi/matter-ens160-aht21 &&
   idf.py set-target esp32c3 && idf.py build && idf.py merge-bin -o matter_ens160_aht21_merged.bin'
```

Hoặc cài trên máy (xem [hướng dẫn esp-matter](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html)):

```bash
. ~/esp/esp-idf/export.sh            # ESP-IDF v6.0.2
. ~/esp/esp-matter/export.sh         # esp-matter release/v1.6.1
cd ESP32-C3/matter-wifi/matter-ens160-aht21
idf.py set-target esp32c3
idf.py menuconfig                    # (tùy chọn) Board, ENS160 + AHT21 Sensor
idf.py -p /dev/ttyACM0 flash monitor
```

`sdkconfig.defaults` chỉ áp dụng khi tạo `sdkconfig` mới: sau khi sửa nó (hoặc kéo code mới về), xóa `sdkconfig` hoặc chạy lại `idf.py set-target esp32c3`.

| File trong `build/` | Dùng khi | Nạp ở địa chỉ | Wi-Fi và ghép nối Matter |
|---|---|---|---|
| `matter_ens160_aht21_merged.bin` | Bo mới, hoặc muốn xóa sạch | `0x0` | **Bị xóa** (file phủ cả vùng NVS) |
| `matter_ens160_aht21.bin` | Cập nhật bo đang dùng qua USB | `0x20000` (cùng `ota_data_initial.bin` ở `0x1d000`) | Giữ nguyên |
| `matter_ens160_aht21-ota.bin` | Cập nhật qua mạng (Matter OTA, ví dụ Home Assistant Matter Server) | | Giữ nguyên |

```bash
# Bo mới (xóa ghép nối)
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 build/matter_ens160_aht21_merged.bin
# Cập nhật, giữ ghép nối (hoặc idf.py flash)
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x1d000 build/ota_data_initial.bin 0x20000 build/matter_ens160_aht21.bin
```

SuperMini không vào được chế độ nạp: giữ nút **BOOT**, bấm **RESET** (hoặc cắm USB trong lúc giữ BOOT), rồi nạp. Log và nạp đều qua cổng USB-C (USB-Serial-JTAG, Linux `/dev/ttyACM0`).

**Phiên bản firmware** = `1.0.<số commit của repo>` (`git rev-list --count HEAD`), hiện trong ứng dụng (mục Firmware), trong log khởi động và là số SoftwareVersion của Matter OTA: commit mới hơn luôn ra số lớn hơn. Clone đầy đủ (không `--depth`) để số đúng.

CI (`.github/workflows/matter-ens160-aht21.yml`) chạy test trên máy tính rồi build bằng đúng Docker image trên, lưu các file nạp vào *Artifacts*.

## Thêm vào ứng dụng

1. Khi chưa ghép nối, thiết bị in trong log (`idf.py monitor`) QR code, link ảnh QR và mã ghép nối thủ công. Với setup code test mặc định (passcode `20202021`, discriminator `3840`) và VID/PID `0xFFF1`/`0x8002` của firmware này:
   ```
   chip[SVR]: SetupQRCode: [MT:06PS042C00KA0648G00]
   chip[SVR]: Copy/paste the below URL in a browser to see the QR Code:
   chip[SVR]: https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3A06PS042C00KA0648G00
   chip[SVR]: Manual pairing code: [34970112332]
   ```
   Ảnh QR: [MT:06PS042C00KA0648G00](https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3A06PS042C00KA0648G00), mã thủ công **`3497-011-2332`** (tính bằng `SetupPayload.py` của Matter SDK; QR này khác QR test thường gặp `MT:Y.K9042C00KA0648G00` vì PID khác).
2. **Apple Home**: *Nhà → + → Thêm phụ kiện* → quét QR (hoặc nhập mã thủ công). Gặp cảnh báo phụ kiện chưa được chứng nhận (*Uncertified Accessory*) thì chọn *Add Anyway*. iPhone gửi thông tin Wi-Fi (2,4 GHz) cho ESP32-C3 qua BLE. Phụ kiện hiện **nhiệt độ**, **độ ẩm** và **chất lượng không khí**.
3. **Home Assistant**: cần integration *Matter* (Matter Server), thêm bằng app Home Assistant trên điện thoại (*Settings → Devices → Add Matter device*). Tên thiết bị là **ENS160-** + 4 ký tự cuối số serial (MAC), ví dụ `ENS160-B2C4`.
4. **Google Home / SmartThings / Alexa**: thêm thiết bị Matter bằng QR. Mỗi hệ có quy định riêng cho thiết bị dùng chứng chỉ test, ví dụ Google Home cần khai báo sản phẩm test (VID `0xFFF1`, PID `0x8002`) trong Google Home Developer Console.

| Thông tin | Giá trị | Sửa ở đâu |
|---|---|---|
| Nhà sản xuất | `Haxeko` | `menuconfig` → *Device Information* |
| Kiểu máy (Model) | `ENS160 AHT21` | `menuconfig` → *Device Information* |
| Phần cứng | `ESP32C3-ENS160-AHT21` | `menuconfig` → *Device Information* |
| Số serial | MAC trong eFuse, ví dụ `58CF79A1B2C4` | tự động |
| VID / PID | `0xFFF1` / `0x8002` | `sdkconfig.defaults` |

> Firmware dùng **chứng chỉ test** của Matter SDK (VID `0xFFF1`) và setup code test dùng chung cho mọi bo: chỉ để tự làm (DIY). Làm sản phẩm thật thì cần DAC/passcode riêng (`esp-matter-mfg-tool`) và chứng nhận CSA. PID `0x8002` khác các firmware khác của repo (`0x8000`, `0x8001`) để bộ điều khiển và file OTA không nhầm lẫn giữa chúng.

## Cấu hình (`idf.py menuconfig`)

*Board*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `SENSOR_I2C_SDA_GPIO` / `SENSOR_I2C_SCL_GPIO` | 5 / 6 | Chân I2C nối module |
| `STATUS_LED_GPIO` | 8 | LED trạng thái, `-1` = không có |
| `STATUS_LED_ACTIVE_HIGH` | tắt | LED sáng khi chân ở mức cao (LED của SuperMini sáng khi mức thấp) |
| `RESET_BUTTON_GPIO` | 9 | Nút ghép nối / factory reset (nối xuống GND), `-1` = không có |

*ENS160 + AHT21 Sensor*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `SENSOR_PERIOD_SEC` | 10 | Chu kỳ đọc cảm biến (2–300 s). AHT21 không nên đo dày hơn 2 s một lần (tự làm nóng) |
| `SENSOR_TEMPERATURE_OFFSET_TENTHS` | 0 | Bù nhiệt độ, đơn vị 0,1 °C (ví dụ `-15` = −1,5 °C) |
| `ENS160_REPORT_WHILE_WARMING_UP` | tắt | Gửi giá trị ENS160 cả khi nó đang làm nóng |
| `SENSOR_ECO2_AS_CO2` | bật | Đưa eCO2 thành cluster CO2. Đổi tùy chọn này làm thay đổi cluster của endpoint 3: xóa rồi thêm lại thiết bị trong ứng dụng |

*Device Information*: tên nhà sản xuất, model, tiền tố tên thiết bị (`ENS160-`), tên quảng bá trước khi ghép nối.

## Hiệu chỉnh nhiệt độ

Trên module ENS160 + AHT21, AHT21 nằm sát bộ gia nhiệt của ENS160 nên thường đọc **cao hơn thực tế**, hay gặp khoảng 1–3 °C (và độ ẩm thấp hơn), tùy module và vỏ hộp. Cách chỉnh:

1. Để thiết bị chạy ít nhất 1 giờ ở chỗ đặt thật (module cách xa ESP32-C3, không bị nắng hay quạt thổi trực tiếp).
2. So với một nhiệt kế tin cậy đặt cạnh module, lấy chênh lệch: ví dụ thiết bị 29,3 °C, nhiệt kế 27,5 °C → bù **−1,8 °C**.
3. `menuconfig` → *ENS160 + AHT21 Sensor* → `SENSOR_TEMPERATURE_OFFSET_TENTHS` = `-18`, build và nạp lại (cập nhật giữ ghép nối).

Độ ẩm được **tự tính lại** theo nhiệt độ đã bù (cùng lượng hơi nước, công thức Magnus), không cần bù riêng. Nhiệt độ và độ ẩm đã bù cũng là giá trị gửi cho ENS160 để bù trừ phép đo khí.

## ENS160: làm nóng, eCO2

- **Mỗi lần cấp điện**, ENS160 cần khoảng **3 phút** làm nóng (*warm-up*). Một **cảm biến mới** cần khoảng **1 giờ** chạy liên tục lần đầu tiên (*initial start-up*), và độ chính xác còn tốt dần lên trong 1–2 ngày đầu. Trong thời gian này, Matter báo chất lượng không khí *Unknown*, TVOC và eCO2 null; log ghi `ENS160: warm-up ...` hoặc `ENS160: initial start-up ...`. Muốn thấy giá trị ngay (ví dụ để thử dây), bật `ENS160_REPORT_WHILE_WARMING_UP`.
- Khi ESP32 khởi động lại mà module không mất điện (cập nhật OTA, reset), ENS160 vẫn đang đo: firmware để nó chạy tiếp, **không làm nóng lại**.
- **eCO2 là CO2 ước lượng từ VOC**, không phải CO2 đo thật như cảm biến NDIR (SCD40, MH-Z19...): nó tăng theo hơi thở, mùi nấu ăn, cồn... nhưng không đo được CO2 từ nguồn không có VOC. Không muốn ứng dụng hiện nó là CO2 thì tắt `SENSOR_ECO2_AS_CO2`.
- ENS160 ngừng đo (không có giá trị mới 3 lần đọc liên tiếp) hoặc báo lỗi: firmware reset nó và chạy lại (làm nóng lại 3 phút).

## Log

Một dòng giá trị ở lần đọc đầu tiên, sau đó mỗi phút một dòng. Log của test giả lập trên máy tính (số đo và phiên bản firmware ENS160 là của cảm biến giả lập, không phải từ bo thật; cột đầu là ms):

```
130 I sensors: AHT21 found at 0x38
280 I ens160: ENS160 at 0x53, firmware 5.4.6
300 I ens160: ENS160 at 0x53 measuring (standard mode)
300 I sensors: 30.00 C, 50.00 %RH | ENS160 starting
10390 I sensors: ENS160: warm-up, about 3 min after power-on, the air quality is sent once the values are valid
10390 I sensors: 30.00 C, 50.00 %RH | AQI 2, TVOC 120 ppb, eCO2 650 ppm (not valid yet, not sent)
181920 I sensors: ENS160: normal operation
```

Rút dây AHT21 trong lúc chạy:

```
202010 W sensors: AHT21 reading failed, err:-1
212010 W sensors: AHT21 reading failed, err:-1
222010 W sensors: AHT21 reading failed, err:-1
222010 W sensors: AHT21 lost after 3 readings without values: temperature and humidity unknown
232010 W sensors: No AHT21 at 0x38 (err:261): no temperature and humidity. The ENS160 measures no temperature, the AHT21 of the module does: check SDA GPIO5, SCL GPIO6, 3V3 and GND
```

Cảm biến mất kết nối thì giá trị cuối được giữ thêm 2 lần đọc, sau đó thành null; firmware tìm lại cảm biến ở mỗi lần đọc, nên cắm lại dây là chạy tiếp, không cần khởi động lại.

## Kiểm thử trên máy tính

`test/host/` chạy driver và task cảm biến trên máy tính (Linux/macOS, chỉ cần `g++`), với bus I2C, AHT21, ENS160 và đồng hồ giả lập theo datasheet. Không cần ESP32 hay ESP-IDF:

```bash
test/host/run.sh          # thêm -v để in log
```

| Test | Kiểm tra |
|---|---|
| `test_sensor_math.cpp` | CRC-8 của AHT21 (giá trị chuẩn của CRC-8/NRSC-5), giải mã khung 7 byte, TEMP_IN/RH_IN của ENS160, quy đổi AQI-UBA, giới hạn giá trị Matter, độ ẩm sau khi bù nhiệt, ngưỡng gửi |
| `test_aht21.cpp` | Khởi tạo thanh ghi 0x1B/0x1C/0x1E khi status khác 0x18, lệnh hiệu chuẩn của AHT20 đời đầu, chờ đủ 80 ms dù tick FreeRTOS là 10 ms, đo lâu, CRC hỏng, mất hiệu chuẩn, rút dây |
| `test_ens160.cpp` | Khởi động từ deep sleep (idle → phiên bản firmware → standard), sai part ID, địa chỉ 0x52, giữ nguyên cảm biến đang đo khi ESP32 khởi động lại, reset, ghi bù trừ, cờ NEWDAT |
| `test_sensors.cpp` (3 bản build) | Làm nóng, rút/cắm lại AHT21, khung lỗi đơn lẻ, AHT21 hỏng không hiệu chuẩn được (giá trị cũ không bị giữ mãi), ENS160 treo hoặc báo lỗi rồi được reset, output không hợp lệ, dòng log, offset −2 °C và gửi khi đang làm nóng, không có AHT21 từ đầu |

Phần Matter, Wi-Fi và BLE phải thử trên bo thật.

## Tình trạng

- Build được với ESP-IDF v6.0.2 + esp-matter 1.6.1 (Docker `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`, như CI): app 1,41 MB, còn trống 25 % phân vùng OTA 1,9 MB; RAM tĩnh 145 KB.
- Test trên máy tính đều đạt.
- **Chưa chạy trên bo thật**: driver theo datasheet ENS160 / AHT21 và được thử với cảm biến giả lập; cách cập nhật cluster theo mã nguồn esp-matter 1.6.1 (Temperature Measurement, Relative Humidity Measurement và Air Quality là cluster "code-driven" của Matter SDK, ghi bằng API của cluster; TVOC và CO2 nằm trong bộ lưu thuộc tính của esp-matter, ghi bằng `esp_matter::attribute::update()`).

## Cấu trúc thư mục

```
matter-ens160-aht21/
├── CMakeLists.txt             # phiên bản firmware = số commit, chỉ build cho esp32c3
├── partitions.csv             # 2 phân vùng app 1,9 MB cho OTA, NVS, fctry
├── sdkconfig.defaults         # BLE/Wi-Fi tiết kiệm RAM, PID 0x8002, chỉ build các cluster cần dùng
├── main/
│   ├── app_main.cpp           # Matter node, ghép nối, nút BOOT, LED
│   ├── sensor_endpoints.cpp   # endpoint 1-3 và cập nhật cluster
│   ├── sensors.cpp            # task đọc AHT21 + ENS160, bù trừ, mất/cắm lại cảm biến
│   ├── device_info.cpp        # Manufacturer / Model / Serial, tên thiết bị
│   ├── board.h                # kiểm tra chân lúc build
│   ├── chip_project_config.h  # tên quảng bá trước khi ghép nối
│   ├── Kconfig.projbuild
│   └── drivers/
│       ├── aht21.cpp          # AHT21 (AHT20, AHT25)
│       ├── ens160.cpp         # ENS160
│       ├── sensor_math.h      # CRC, giải mã, quy đổi (dùng chung với test)
│       ├── status_led.cpp     # LED trạng thái
│       └── user_button.cpp    # nút: bấm đúp, giữ lâu
└── test/host/                 # test trên máy tính (run.sh)
```
