# Matter PM2.5 + CO2 Air Quality Sensor (ESP32-C3, Wi-Fi), bản PCB không radar

Cảm biến bụi mịn PM2.5 và CO2 chuẩn **Matter over Wi-Fi** chạy trên ESP32-C3, thêm trực tiếp vào **Apple Home (HomeKit)**, Google Home, Home Assistant, SmartThings...

Bản này dành cho **PCB không có radar LD2410**: PMS5003 ở GPIO6/7 (giắc J2B), OLED và cảm biến CO2 **SCD40/SCD41** dùng chung bus I2C ở GPIO4/5, LED WS2812 ở GPIO3, **nút nhấn ở GPIO10** (giữ 5 giây: factory reset; bấm đúp: chờ ghép nối; bấm 4 lần: đổi kiểu màn hình). Màn hình luôn sáng; có thể lắp thêm nút bật/tắt màn hình rồi chọn chân trong `menuconfig`. Phần đo PM2.5, mức chất lượng không khí và LED giống [bản có radar](../matter-wifi/matter-pm25-sensor/).

Thiết bị dùng device type **Air Quality Sensor (0x002C)** và gửi qua Matter ba giá trị:

| Cluster | Nội dung |
|---|---|
| Air Quality (0x005B) | Mức chất lượng không khí, **chỉ tính từ PM2.5** (trạng thái chính của cảm biến) |
| PM2.5 Concentration Measurement (0x042A) | PM2.5, µg/m³ |
| Carbon Dioxide Concentration Measurement (0x040D) | CO2, ppm (khi build có cảm biến CO2, mặc định có) |

Màn hình OLED, đèn LED WS2812 và các nút nhấn (nếu lắp) chạy **tại chỗ trên thiết bị**, không gửi qua Matter:

- **LED WS2812** đổi màu theo mức chất lượng không khí, theo bảng màu của Apple Home: xanh lá, xanh vàng, vàng, đỏ, tím. Khi ứng dụng gửi lệnh **Identify** (ví dụ nút *Identify* của thiết bị trong Home Assistant), LED nháy trắng để chỉ ra thiết bị, xong thì trở lại màu chất lượng không khí.
- **Màn hình OLED** có hai kiểu, **bấm nút 4 lần** để đổi qua lại. Lựa chọn được **lưu lại**, mất điện cắm lại vẫn giữ:
  - **PM2.5 + CO2** (mặc định khi có cảm biến CO2): chữ *PM2.5* bên trái, *CO2* bên phải, số đo nằm ngay dưới chữ, không ghi đơn vị (PM2.5 tính bằng µg/m³, CO2 bằng ppm). Hai số **luôn cùng cỡ chữ**: chữ lớn, trừ khi cả hai cùng quá dài không vừa một hàng (ví dụ PM2.5 từ 1000 và CO2 từ 10000 cùng lúc), khi đó cả hai cùng nhỏ lại. Dòng dưới cùng là mức chất lượng không khí theo PM2.5. Chưa có số đo thì hiện `--`; cảm biến CO2 ngừng trả lời thì CO2 cũng hiện `--`.
  - **Chỉ PM2.5**: như bản trước, số PM2.5 lớn ở giữa, đơn vị ug/m3, mức chất lượng không khí ở dưới.

  Giữa dòng tiêu đề có biểu tượng ngôi nhà khi thiết bị có Wi-Fi **và** có ít nhất một ứng dụng (Apple Home, Home Assistant...) đang kết nối. Khi không còn ứng dụng nào kết nối, biểu tượng tự mất, chậm nhất sau khoảng thời gian báo cáo tối đa mà ứng dụng chọn (thường tới 10 phút).
  Màn hình sáng/tắt như sau:
  - PCB không có nút màn hình nên **màn hình luôn sáng**.
  - Nếu lắp thêm nút màn hình (`menuconfig` → *Display on/off button GPIO*): màn hình sáng khi khởi động cho tới khi có kết quả đo đầu tiên, thêm 5 giây (tối đa 120 giây nếu cảm biến PM lỗi). Sau đó mỗi lần nhấn nút, màn hình đang sáng thì tắt, đang tắt thì sáng, đổi ngay khi nhấn xuống. Lựa chọn được **lưu lại**, mất điện cắm lại vẫn giữ. Khi factory reset và khi chờ ghép nối, màn hình luôn sáng.
  - Màn hình OLED để sáng liên tục nhiều tháng có thể bị **lưu ảnh** (các chữ cố định như "PM2.5" mờ dần in lên màn hình).

## Phần cứng

- ESP32-C3 (ESP32-C3 SuperMini, ESP32-C3-DevKitM-1, ...), flash 4 MB
- Một cảm biến bụi UART:
  - **Plantower PMS5003 / PMS7003 / PMSA003** (mặc định)
  - **Nova Fitness SDS011 / SDS021**
- Màn hình OLED 128x64 I2C (địa chỉ 0x3C): **SSD1306 0.96"** trên PCB (mặc định), hoặc SH1106 1.3" (chọn trong `menuconfig` → *OLED controller*)
- Cảm biến CO2 **Sensirion SCD40 / SCD41** (I2C, địa chỉ 0x62), chung hai dây SDA/SCL với OLED
- Dải LED **WS2812**, 6 bóng
- Nút nhấn chính ở **GPIO10** (nút nối xuống GND)
- Nút bật/tắt màn hình: PCB không có, có thể lắp thêm (không bắt buộc)

Các phần OLED, cảm biến CO2, LED và các nút đều không bắt buộc. Thiếu phần nào thì firmware ghi lỗi vào log rồi chạy tiếp, cảm biến PM2.5 và Matter vẫn hoạt động. Màn hình không trả lời (chưa cắm, lỏng dây, mất nguồn giữa chừng) thì firmware thử khởi tạo lại mỗi 5 giây và tự sáng lại khi màn hình trả lời. Cảm biến CO2 không trả lời thì firmware thử lại mỗi 10 giây; trong lúc chờ nó chỉ dò địa chỉ 0x62 nên log không bị lỗi I2C làm đầy.

Thiết bị **chạy độc lập với Matter**: cảm biến, màn hình, LED và các nút khởi động trước Matter. Chúng vẫn hoạt động khi thiết bị chưa được ghép nối, khi mất Wi-Fi, hoặc khi Matter không khởi động được. Mỗi 30 giây, log in một dòng `local_ui: status: ...` cho biết trạng thái của màn hình (`setting=on/off` là lựa chọn của nút màn hình, `show_co2=1` là kiểu PM2.5 + CO2, `co2=-1` là chưa có số CO2) và LED, và một dòng `local_ui: heap: free=... min=... largest=...` cho biết RAM trống hiện tại, mức thấp nhất từ khi khởi động và khối liền lớn nhất.

Nếu luồng Matter hoặc một task của thiết bị (cảm biến PM, cảm biến CO2, màn hình) bị treo quá 30 giây, watchdog khởi động lại thiết bị thay vì để nó đứng im.

### Sơ đồ chân trên PCB

| Linh kiện | Chân | Giắc trên PCB | ESP32-C3 |
|---|---|---|---|
| PMS5003 | TX (chân 5) | J2B chân 3 | **GPIO6** (RX của ESP32) |
| PMS5003 | RX (chân 4) | J2B chân 4 | **GPIO7** (TX của ESP32) |
| PMS5003 | VCC 5V (chân 1) | J2B chân 2 | 5V |
| PMS5003 | GND (chân 2) | J2B chân 1 | GND |
| OLED SSD1306 | SDA / SCL | | **GPIO4** / **GPIO5** |
| OLED SSD1306 | VCC / GND | | 3.3V / GND |
| SCD40 / SCD41 | SDA / SCL | | **GPIO4** / **GPIO5** (chung với OLED) |
| SCD40 / SCD41 | VCC / GND | | 3.3V / GND |
| WS2812 | DIN | | **GPIO3** |
| WS2812 | VCC / GND | | 5V / GND |
| Nút nhấn | hai chân | | **GPIO10** và GND |

PMS5003 nối bắt chéo: TX của cảm biến vào chân nhận (RX) của ESP32, RX của cảm biến vào chân gửi (TX). Quạt PMS5003 cần nguồn **5V**, nhưng mức logic UART là 3.3V nên nối thẳng vào ESP32-C3 được. Chân SET/RESET của PMS (nếu có) để hở hoặc nối 3.3V. PMS5003 dùng UART1, log chạy qua USB.

Cảm biến CO2 nối song song với OLED trên cùng hai dây SDA và SCL: bus I2C cho nhiều thiết bị khác địa chỉ dùng chung (OLED 0x3C, SCD4x 0x62). Cấp **3.3V** cho module SCD4x: nếu cấp 5V, điện trở kéo lên trên module có thể kéo SDA/SCL lên 5V, quá mức chịu của ESP32-C3. Mỗi lần đo, SCD4x lấy dòng đỉnh khá lớn (cỡ vài trăm mA trong thời gian rất ngắn), nên dùng module có sẵn tụ lọc nguồn. Đặt cảm biến xa ESP32 và LED để nhiệt của chúng không ảnh hưởng.

SCD4x tự hiệu chỉnh (ASC, bật sẵn từ nhà sản xuất): nó coi mức CO2 thấp nhất trong khoảng một tuần là 400 ppm, tức không khí ngoài trời. Phòng cần được thông gió (mở cửa) thỉnh thoảng trong tuần, nếu không số đo có thể lệch dần; vài ngày đầu sau khi lắp, số đo có thể chưa chính xác. Để tham khảo: ngoài trời khoảng 420 ppm, phòng thông thoáng thường dưới 1000 ppm, trên 1500 ppm nên mở cửa thông gió.

Nút nhấn nối giữa **GPIO10** và GND (firmware tự bật điện trở kéo lên; nút 4 chân thì dùng 2 chân chéo nhau). Muốn dùng chân khác, hoặc lắp thêm nút bật/tắt màn hình: chọn chân trong `menuconfig`. Chân còn trống trên PCB: **GPIO0, 1, 20**. Firmware báo lỗi khi build nếu chọn trùng chân đang dùng, chân strapping (GPIO2/8/9), chân flash (GPIO11–17) hay chân USB (GPIO18/19). Không nên dùng GPIO21: ROM in log khởi động ra chân này.

Mạch nối dây tay theo chân cũ của bản này (PMS5003 GPIO20/21, OLED GPIO8/9, LED GPIO10, nút màn hình GPIO3, nút reset GPIO7): đặt lại các chân đó trong `menuconfig` trước khi build.

### Nút nhấn

| Thao tác | Chức năng |
|---|---|
| Giữ 5 giây rồi buông | **Factory reset** (xem bên dưới) |
| Bấm đúp (2 lần liền) | **Chờ ghép nối**: mở chế độ ghép nối Matter trong 5 phút, màn hình hiện **CHỜ GHÉP NỐI** và mã ghép nối |
| Bấm 4 lần liền | **Đổi kiểu màn hình**: PM2.5 + CO2 ↔ chỉ PM2.5 (lưu lại) |

Mỗi lần bấm trong một chuỗi phải ngắn (dưới 0,5 giây) và cách lần trước không quá 0,5 giây. Firmware chỉ đếm số lần bấm khi chuỗi kết thúc (0,5 giây sau lần buông cuối), nên bấm 4 lần không kích hoạt bấm đúp. Bấm 1 hay 3 lần không làm gì.

### Chờ ghép nối (bấm đúp)

- Thiết bị mở chế độ ghép nối Matter trong **5 phút**. Màn hình sáng và hiện **CHỜ GHÉP NỐI** cùng mã ghép nối thủ công (ví dụ `3497-011-2332`), cho tới khi ghép xong hoặc hết 5 phút thì trở lại màn hình đo.
- Thiết bị đã có Wi-Fi: ứng dụng tìm thấy thiết bị qua mạng Wi-Fi. Chưa có Wi-Fi: thiết bị bật cả Bluetooth để ứng dụng gửi thông tin Wi-Fi.
- Dùng khi thiết bị chưa ghép mà đã hết thời gian chờ ghép nối lúc khởi động, hoặc để thêm thiết bị vào ứng dụng thứ hai (ví dụ đã có trong Apple Home, thêm vào Home Assistant) mà không phải factory reset.
- Nếu một ứng dụng đã mở chế độ ghép nối với mã riêng của nó (*Bật chế độ ghép nối* trong ứng dụng), bấm đúp chỉ hiện CHỜ GHÉP NỐI, không hiện mã: dùng mã mà ứng dụng đưa ra.
- Khi Matter không chạy, bấm đúp không có tác dụng (log ghi lại).

### Factory reset

Factory reset xóa Wi-Fi và thông tin ghép nối Matter, rồi thiết bị khởi động lại và chờ ghép nối lại. Có hai cách:

- **Cắm rồi rút nguồn 5 lần liên tiếp**, mỗi lần giữ điện dưới 10 giây. Ở lần cắm thứ 5, thiết bị factory reset.
- **Nút nhấn** (GPIO10, `menuconfig` → *Button*):
  1. Giữ nút. Sau 1 giây màn hình sáng, hiện **ĐẶT LẠI THIẾT BỊ** và đếm ngược 4, 3, 2, 1.
  2. Khi màn hình hiện số **0**, buông nút. Màn hình hiện *Đang đặt lại...* rồi thiết bị khởi động lại.
  3. Buông nút trước khi về 0 là hủy. Nút bị kẹt (giữ mãi không buông) cũng không reset.
- Nếu bạn xóa thiết bị khỏi tất cả ứng dụng (Apple Home, Home Assistant...), thiết bị tự mở lại chế độ ghép nối, không cần reset.
- Cả hai cách vẫn chạy khi Matter không khởi động được: khi đó firmware xóa thẳng phân vùng NVS rồi khởi động lại.
- Nếu phân vùng NVS không dùng được (hết trang trống, hoặc do bản ESP-IDF mới hơn ghi), firmware tự xóa nó khi khởi động. Kết quả giống factory reset: phải ghép nối lại.

> **Log và nạp firmware:** log chạy qua **USB-Serial-JTAG** có sẵn trên chip (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`). Nạp firmware và xem log qua **cổng USB native** của ESP32-C3 (SuperMini chỉ có cổng này).

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

cd ESP32-C3/matter-pm25-sensor
idf.py set-target esp32c3
idf.py menuconfig          # (tùy chọn) PM2.5 / CO2 Sensor Configuration, Button
idf.py -p /dev/ttyACM0 flash monitor
```

`sdkconfig.defaults` chỉ áp dụng khi tạo file `sdkconfig` mới. Sau khi kéo code mới về, nếu thư mục đã có `sdkconfig` từ lần build trước thì xóa nó đi để các thay đổi trong `sdkconfig.defaults` và `Kconfig.projbuild` có hiệu lực. **Bắt buộc khi chuyển sang chân của PCB**: `sdkconfig` cũ vẫn giữ chân cũ (GPIO20/21, 8/9, 10...).

Mỗi lần build xong, thư mục `build/firmware/` chỉ chứa file của đúng phiên bản vừa build (xem [Phiên bản firmware](#phiên-bản-firmware)):

| File | Dùng khi | Nạp ở địa chỉ | Wi-Fi và ghép nối Matter |
|---|---|---|---|
| `haxeko_pm5003_pcb_v1.0.49_esp32c3_merged.bin` | Mạch mới, hoặc muốn xóa sạch | `0x0` | **Bị xóa**: file phủ cả vùng NVS |
| `haxeko_pm5003_pcb_v1.0.49_esp32c3_update.bin` | Cập nhật thiết bị đang dùng | `0x1D000` | Giữ nguyên |

Chữ `pcb` trong tên file phân biệt với file của bản radar (`haxeko_pm5003_v...`), vì hai bản có cùng tên kiểu máy. Đổi hoặc bỏ chữ này ở `FIRMWARE_FILE_TAG` trong `CMakeLists.txt`.

```bash
# Mạch mới (xóa ghép nối)
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 build/firmware/haxeko_pm5003_pcb_v1.0.49_esp32c3_merged.bin
# Cập nhật, giữ ghép nối
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x1d000 build/firmware/haxeko_pm5003_pcb_v1.0.49_esp32c3_update.bin
```

Có thể nạp file update của bản này lên thiết bị đang chạy bản radar (và ngược lại) mà vẫn giữ ghép nối: hai bản dùng cùng bảng phân vùng và cùng thông tin Matter.

File update chỉ ghi vùng chọn phân vùng OTA và firmware, nên vẫn chạy đúng kể cả sau một lần cập nhật OTA. Khi bootloader hoặc bảng phân vùng thay đổi (sửa `partitions.csv` hay cấu hình bootloader), phải nạp file merged. `idf.py flash` cũng giữ nguyên ghép nối.

### Cấu hình (`idf.py menuconfig`)

*PM2.5 Sensor Configuration*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| PM sensor model | PMS5003/7003/A003 | Chọn loại cảm biến |
| UART RX / TX pin | 6 / 7 | Chân nối với TX / RX của cảm biến (giắc J2B) |
| PMS measurement period | 300 s | PMS ngủ giữa các lần đo (tắt quạt và laser). Mỗi chu kỳ: đánh thức, chờ 30 giây, đo 5 giây rồi ngủ lại. Giá trị đầu tiên có sau khoảng 35 giây. Đặt `0` để đo liên tục |
| Report interval | 10 s | Chỉ dùng khi đo liên tục: lấy trung bình các mẫu trong khoảng này |
| SDS011 working period | 0 (liên tục) | Chế độ đo chu kỳ của SDS011 (1–30 phút) |

*CO2 Sensor Configuration*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| CO2 sensor | SCD40 / SCD41 | `None`: bỏ cảm biến CO2, không có CO2 trên màn hình và trong Matter (như bản trước) |
| CO2 report interval | 30 s | Cảm biến đo mỗi 5 giây; firmware lấy trung bình trong khoảng này rồi hiện lên màn hình và gửi qua Matter. Số CO2 đầu tiên có sau khoảng 30 giây |

*Local Display and Indicators*:

| Tùy chọn | Mặc định |
|---|---|
| I2C SDA / SCL pin (OLED và cảm biến CO2) | 4 / 5 |
| OLED I2C address | 0x3C |
| OLED controller | SSD1306 (0.96"). Chọn sai loại thì hình lệch 2 điểm ảnh và một mép hiện cột rác |
| Rotate OLED 180 degrees | tắt: hình đứng thẳng khi hàng chân của module ở trên, như trên PCB |
| WS2812 data pin / số LED / độ sáng | 3 / 6 / 50% |
| Display on/off button GPIO | `-1` (không có nút, màn hình luôn sáng) |

*Button*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| Button GPIO | `10` | Chân nối nút chính (nút nối xuống GND): giữ 5 giây factory reset, bấm đúp chờ ghép nối, bấm 4 lần đổi kiểu màn hình. `-1` = không có nút |

### Thông tin thiết bị và phiên bản firmware

Ứng dụng nhà thông minh hiển thị các thông tin sau:

| Mục | Giá trị | Sửa ở đâu |
|---|---|---|
| Nhà sản xuất (Manufacturer) | `Haxeko` | `menuconfig` → *Device Information* |
| Kiểu máy (Model) | `PM5003` | `menuconfig` → *Device Information* |
| Phần cứng (Hardware) | `ESP32C3-PMS5003` (`ESP32C3-SDS011` khi chọn SDS011) | `menuconfig` → *Device Information* |
| Tên thiết bị khi ghép nối | `Air Quality Sensor` | `menuconfig` → *Device Information* |
| Số serial | `<MAC Wi-Fi>`, ví dụ `8ED0B2A8594B` | Tự động, riêng cho từng chip |
| Tên Bluetooth khi chờ ghép nối | `AirQuality-3840` | `CONFIG_BLE_DEVICE_NAME_PREFIX` trong `sdkconfig.defaults` |
| Tên và loại thiết bị khi dò tìm trong mạng (DNS-SD `DN`, `DT`) | `Air Quality Sensor`, loại 0x002C | `main/chip_project_config.h`, `CONFIG_DEVICE_TYPE` |
| Firmware | `1.0.<số commit>`, ví dụ `1.0.49` | Tự động, xem bên dưới |

### Phiên bản firmware

Phiên bản có dạng **`<major>.<minor>.<build>`**, ví dụ `1.0.49`, và chỉ được tính ở **một chỗ** (`tools/firmware_version.cmake`). Cùng một số đó xuất hiện ở:

| Nơi | Ví dụ |
|---|---|
| Apple Home / Home Assistant, mục Firmware (`SoftwareVersionString`) | `1.0.49` |
| Tên file nạp trong `build/firmware/` | `haxeko_pm5003_pcb_v1.0.49_esp32c3_merged.bin` |
| Log khi khởi động | `Haxeko PM5003, firmware 1.0.49` |
| Số phiên bản Matter dùng cho OTA (`SoftwareVersion`) | `49` |

- `<build>` là số commit của repo (`git rev-list --count HEAD`): cùng một commit luôn ra cùng phiên bản, commit mới hơn luôn ra số lớn hơn, nên OTA không bao giờ coi bản mới là bản cũ. Số này đếm commit của cả repo, nên commit ở dự án khác trong repo cũng làm nó tăng.
- `<major>.<minor>`: sửa `FIRMWARE_VERSION_BASE` trong `CMakeLists.txt` khi ra bản lớn, ví dụ `1.0` thành `1.1`.
- Build khi còn sửa đổi chưa commit trong `main/`, `CMakeLists.txt`, `sdkconfig.defaults` hay `partitions.csv` thì phiên bản có đuôi **`-dev`**, ví dụ `1.0.49-dev`, ở cả ứng dụng lẫn tên file. Bản phát hành là bản build từ commit đã đẩy lên git, không có `-dev`.
- Phiên bản được tính khi CMake cấu hình dự án (sau mỗi commit hoặc checkout). Nếu sửa code sau đó mà chưa commit, bước build in cảnh báo; chạy `idf.py reconfigure` để bản build mang đuôi `-dev`.
- Clone repo đầy đủ, **không dùng `--depth`**. Với clone nông, số commit sẽ sai (bước build cũng cảnh báo).
- Nếu build từ thư mục không có git (ví dụ tải file zip), phiên bản sẽ là `1.0.0`.

Sau khi nạp bản mới qua USB, ứng dụng có thể vẫn hiện số phiên bản đã đọc trước đó một thời gian. Log khởi động luôn in số của bản đang chạy.

## Thêm vào Apple Home

1. Mở log (`idf.py monitor`); khi thiết bị chạy lần đầu sẽ in ra link QR code, ví dụ:
   `https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00`
2. Mở link đó để hiện QR code, hoặc dùng mã ghép nối thủ công **`3497-011-2332`** (passcode mặc định `20202021`, discriminator `3840`). **Bấm đúp nút** thì màn hình cũng hiện mã này.
3. iPhone: **Nhà → + → Thêm phụ kiện → quét QR** (hoặc *Tùy chọn khác* để nhập mã). Khi gặp cảnh báo phụ kiện chưa được chứng nhận (*Uncertified Accessory*), chọn tiếp tục thêm (*Add Anyway*).
4. iPhone gửi thông tin Wi-Fi cho ESP32-C3 qua BLE. Sau khi thêm xong, phụ kiện hiện là **Cảm biến chất lượng không khí**, gồm mức chất lượng không khí và mật độ PM2.5.

Apple Home có hiện thêm CO2 hay không tùy phiên bản iOS (chưa kiểm chứng trên thiết bị thật). Home Assistant đọc cluster CO2 của Matter thành một cảm biến CO2 riêng.

**Cập nhật từ bản chưa có CO2** (file update, giữ ghép nối): endpoint có thêm cluster CO2. Ứng dụng đã ghép từ trước có thể chưa thấy CO2 cho tới khi đọc lại cấu trúc thiết bị: trong Home Assistant chọn *Re-interview* thiết bị Matter (hoặc xóa rồi thêm lại); trong Apple Home có thể phải xóa rồi thêm lại phụ kiện. Màn hình chuyển sang kiểu PM2.5 + CO2; bấm nút 4 lần để về kiểu cũ. Thiết bị không lắp SCD4x thì build với *CO2 sensor* = `None` để giữ đúng như bản trước.

Nên dùng iOS bản mới và có Home hub (HomePod mini/HomePod/Apple TV 4K) để xem từ xa và chạy tự động hóa. Phiên bản iOS cũ có thể không hiển thị đầy đủ cảm biến chất lượng không khí Matter.

> Firmware dùng chứng chỉ test (VID `0xFFF1`, PID `0x8000`) nên chỉ hợp với mục đích DIY. Để làm sản phẩm thật, hãy tạo DAC/passcode riêng bằng `esp-matter-mfg-tool`.

## Cách tính mức chất lượng không khí

Thiết bị chia chất lượng không khí thành 5 bậc theo PM2.5. Mỗi bậc gửi một mức Matter tương ứng, để Apple Home hiển thị cùng đánh giá với màn hình:

| PM2.5 (µg/m³) | Màn hình | LED | Matter gửi đi | HomeKit |
|---|---|---|---|---|
| ≤ 12 | TỐT | Xanh lá | 1 Good | Good |
| 12 – 35,4 | KHÁ | Xanh vàng | 2 Fair | Fair |
| 35,4 – 55,4 | KHÔNG TỐT | Vàng | 3 Moderate | Inferior |
| 55,4 – 125,4 | KÉM | Đỏ | 4 Poor | Poor |
| > 125,4 | CỰC KỲ KÉM | Tím | 6 ExtremelyPoor | Poor |

HomeKit có 5 mức (Excellent, Good, Fair, Inferior, Poor). Hai bậc cuối cùng hiện "Poor" trên Apple Home, còn Home Assistant vẫn phân biệt được Poor và Extremely poor.

Muốn đổi ngưỡng thì sửa `local_ui_pm25_level()` trong `main/local_ui.cpp`. Muốn đổi mức gửi qua Matter thì sửa bảng `k_level_to_matter` trong `main/app_main.cpp`.

Nếu cảm biến không gửi dữ liệu (lỏng dây, mất nguồn), Matter báo PM2.5 là `null` và Air Quality là `Unknown`. Màn hình và LED giữ giá trị cuối cùng, giống bản ESPHome. Khi có lại dữ liệu, thiết bị tự cập nhật bình thường.

CO2 không ảnh hưởng tới mức chất lượng không khí (màn hình, LED, Matter Air Quality); nó chỉ được hiện và gửi riêng. Cảm biến CO2 không có số đo trong 30 giây thì Matter báo CO2 là `null` và màn hình hiện `--` (khác PM2.5: CO2 không giữ giá trị cũ). Cảm biến bị reset do chập chờn nguồn (vẫn trả lời nhưng ngừng đo) thì firmware khởi động lại phép đo sau 15 giây không có số mới.

## Kiểm thử trên máy tính

`test/host/` chạy code của driver và màn hình trên máy tính (Linux/macOS, chỉ cần `gcc`/`g++`), với UART, I2C, GPIO và đồng hồ giả lập. Không cần ESP32 hay ESP-IDF:

```bash
test/host/run.sh
```

| Test | Kiểm tra |
|---|---|
| `test_pm_sensor.cpp` (đo theo chu kỳ và đo liên tục) | Ghép khung PMS5003 bị chia nhỏ, nhiễu, khung trả lời lệnh; bỏ 30 giây khởi động; báo mất dữ liệu; gửi lại lệnh cấu hình khi cảm biến im lặng |
| `test_button.cpp` | Hai nút chạy độc lập, chống dội nút, thời gian giữ, nút bị kẹt, chân trùng; đếm số lần bấm (1, 2, 4), bấm cách quá xa, bấm giữ lâu không tính là bấm |
| `test_scd4x.cpp` | SCD4x giả lập theo datasheet: thứ tự lệnh khi khởi động, CRC, chờ đủ thời gian lệnh, trung bình theo khoảng báo cáo, bỏ mẫu 0 ppm và mẫu sai CRC; thiếu cảm biến lúc khởi động, rút ra cắm lại, reset do nguồn, cảm biến không đo |
| `test_local_ui.cpp` (có nút, không có nút, không có CO2) | OLED lúc khởi động, kiểu PM2.5 + CO2 (hai số luôn cùng cỡ chữ) và chỉ PM2.5, đổi kiểu và lưu lựa chọn, `--` khi thiếu số, màn hình chờ ghép nối (bật màn hình khi đang tắt, màn reset được ưu tiên), nút màn hình bật/tắt và lưu lựa chọn, nhấn trong lúc khởi động, nhấn hai lần liền, màn reset khi màn hình đang tắt, rút/cắm OLED, OLED mất nguồn âm thầm, không tạo được bus I2C, LED theo mức không khí, Identify |

Phần Matter, Wi-Fi và BLE vẫn phải thử trên thiết bị thật.

## Đổi font màn hình

Font chữ trên OLED là bitmap sinh sẵn từ Roboto (Regular 12 px, Bold 16 px, Bold 24 px chỉ có chữ số, Bold 28 px), nằm trong `main/ui/fonts.c`. Muốn đổi cỡ chữ hoặc thêm ký tự, sửa `tools/gen_fonts.py` rồi chạy:

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
│   ├── chip_project_config.h # Tên/loại thiết bị khi dò tìm ghép nối
│   ├── app_main.cpp          # Matter node, endpoint Air Quality Sensor (PM2.5, CO2), nút nhấn, chờ ghép nối, factory reset
│   ├── device_info.cpp       # Manufacturer / Model / Hardware (Basic Information)
│   ├── local_ui.cpp          # Logic màn hình / LED / nút màn hình chạy tại chỗ
│   ├── Kconfig.projbuild     # Cấu hình chân và tham số
│   ├── idf_component.yml
│   ├── drivers/
│   │   ├── pm_sensor.cpp     # PMS5003/7003/A003 và SDS011 (UART1)
│   │   ├── scd4x.cpp         # CO2 SCD40 / SCD41 (I2C)
│   │   ├── i2c_bus.cpp       # Bus I2C dùng chung cho OLED và cảm biến CO2
│   │   ├── sh1106.cpp        # OLED SH1106 / SSD1306 I2C + vẽ chữ
│   │   ├── status_led.cpp    # WS2812
│   │   └── button.cpp        # Nút nhấn: giữ, bấm nhiều lần (nút chính), bật/tắt màn hình
│   └── ui/
│       └── fonts.c           # Font bitmap Roboto (sinh bởi tools/gen_fonts.py)
├── test/host/                 # Kiểm thử trên máy tính (run.sh)
└── tools/
    ├── firmware_version.cmake # Tính phiên bản firmware
    ├── firmware_files.cmake   # Tạo file nạp mang số phiên bản sau mỗi lần build
    └── gen_fonts.py
```
