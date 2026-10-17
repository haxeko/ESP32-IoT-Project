# Prompt viết lại firmware ESP32-C3 từ đầu (Matter over Wi-Fi + HLK-LD2410C)

File này tổng hợp **mọi yêu cầu của bản firmware ESP32-C3 hiện có trong repo**, để dùng làm prompt viết lại một bản firmware sạch. Bản được tổng hợp là commit `4711de6`, firmware **1.0.132**.

**Nguồn đã đọc:**
- toàn bộ mã trong `ESP32-H2/matter-presence-ld2410-learning/` mà bản C3 dùng (`main/`, `presence.cmake`, `partitions.csv`, `sdkconfig.defaults`, `sdkconfig.defaults.esp32c3`, các biến thể `unique_setup`, `factory`);
- `ESP32-C3/matter-wifi-presence/` (CMakeLists, sdkconfig.defaults, README);
- README bản H2, file `sdkconfig` sinh ra khi build C3, workflow `.github/workflows/matter-presence-c3-wifi.yml`;
- lịch sử commit, các yêu cầu bạn đã gửi trong phiên làm việc, và log C3 bạn đã gửi.

**Cách dùng:**
1. Xem bảng **Quyết định** ở đầu phần PROMPT. Bảng này điền sẵn phương án đề xuất; sửa lại nếu bạn muốn khác.
2. Mở một phiên mới trên repo này và gửi: *"Đọc `ESP32-C3/matter-wifi-presence/REBUILD_PROMPT.md` và làm theo phần PROMPT"*. Cách khác là dán nguyên phần từ dòng `# PROMPT` trở xuống.

**Ký hiệu mức kiểm chứng của bản 1.0.132:**
- **[bo]**: đã thấy chạy trên bo thật, qua log bạn gửi.
- **[build]**: mới kiểm chứng bằng build hoặc test trên máy tính.
- **[chưa]**: chưa kiểm chứng.

---

# PROMPT

## 0. Quyết định (đã chốt cho lần viết lại này)

| # | Vấn đề | Quyết định | Phương án khác (nếu đổi thì sửa dòng này) |
|---|---|---|---|
| 1 | Thư mục | Project **mới, tự chứa** `ESP32-C3/matter-presence-ld2410/`. Không include gì từ thư mục ESP32-H2. Giữ nguyên `ESP32-C3/matter-wifi-presence/` làm bản tham chiếu cho tới khi bản mới chạy ổn trên bo. | Thay thế tại chỗ `ESP32-C3/matter-wifi-presence/` bằng project tự chứa |
| 2 | Tương thích với bo đang chạy 1.0.132 | **Giữ.** Cập nhật OTA từ 1.0.132 lên bản mới phải giữ ghép nối và các entity Home Assistant (mục 3). | Không cần: được đổi endpoint, phải ghép nối lại |
| 3 | Học phòng trống | **Như 1.0.132**: radar tự đo bằng lệnh 0x000B nếu radar hỗ trợ, nếu không thì ESP32 tự học với mức 95 % (`LD2410_LEARN_RADAR_AUTO=y`). | Mặc định chỉ học trên ESP32 với mức 95 % (`=n`) |
| 4 | LED | **Chỉ LED GPIO một màu** của SuperMini. Bỏ WS2812, bỏ dependency `espressif/led_strip`, bỏ mục chọn loại LED và độ sáng. | Giữ WS2812 cho board ESP32-C3 khác |
| 5 | Mã ghép nối | Mặc định dùng **mã test của SDK**. Giữ biến thể `unique_setup` làm tuỳ chọn (mỗi bo một mã; chưa thử trên C3). **Bỏ** biến thể `factory`: PAI test hiện có chỉ cấp DAC cho PID 0x8000. | Bỏ cả `unique_setup` |
| 6 | Log chẩn đoán `id_diag` | **Giữ đủ** như 1.0.132 (phần Wi-Fi). | Rút gọn |
| 7 | Console Matter (CHIP shell) | **Tắt**, có thể bật lại trong menuconfig. | Bật |

## 1. Vai trò và mục tiêu

Bạn là kỹ sư firmware nhúng cao cấp, thành thạo:
- ESP-IDF, esp-matter, FreeRTOS;
- Matter over Wi-Fi, BLE (NimBLE);
- radar HLK-LD2410.

**Nhiệm vụ:** viết lại từ đầu firmware cảm biến hiện diện Matter cho **ESP32-C3 SuperMini + HLK-LD2410C**, chạy **Matter over Wi-Fi**.
- Giữ đúng chức năng và hành vi của bản 1.0.132, mô tả đầy đủ trong tài liệu này.
- Mã gọn, chỉ cho ESP32-C3.
- Không còn nhánh ESP32-H2/Thread, không còn chức năng thừa (mục 21).

**Bản tham chiếu** (chỉ đọc, không sửa):
- `ESP32-H2/matter-presence-ld2410-learning/`: mã dùng chung cho H2 và C3;
- `ESP32-C3/matter-wifi-presence/`: project C3 mỏng và README của C3.

**Khi có khác biệt:**
- tài liệu này khác mã cũ: làm theo tài liệu;
- tài liệu không nói tới: làm theo hành vi của mã cũ;
- vẫn còn mơ hồ: hỏi trước khi làm.

**Mã cũ:** được chép lại những đoạn đã kiểm chứng (driver, thuật toán học) nếu giữ đúng hành vi. Phải bỏ mọi nhánh và cấu hình không dùng trên C3.

## 2. Cách làm việc (bắt buộc)

1. **Đọc trước khi viết.** Đọc mã tham chiếu và tài liệu này trước khi viết code. Không kết luận về file chưa đọc. Đối chiếu mọi API với đúng phiên bản SDK trong Docker image; không dùng API không tồn tại.
2. **Trình bày kế hoạch trước:** cấu trúc thư mục, các module, những gì bỏ so với bản cũ và lý do, rủi ro tương thích, các câu hỏi còn mơ hồ.
   - Nếu bảng Quyết định đã chốt và không còn điểm mơ hồ về chân hoặc giao thức: triển khai luôn.
   - Nếu còn: dừng lại và hỏi.
3. **Build thật** cho esp32c3 trong Docker (mục 5). Đạt 0 lỗi, 0 cảnh báo trong mã của project. Không tuyên bố build thành công khi chưa build.
4. **Test trên máy tính** (host test, mục 20) cho phần logic, và commit test vào repo.
5. **Không có phần cứng** thì phải nói rõ phần nào mới xác nhận bằng build/test, phần nào cần thử trên bo thật.
6. **Git:** commit từng bước rõ ràng. Chỉ tạo PR hoặc merge khi tôi yêu cầu.
7. **Thay đổi có rủi ro** (tương thích OTA, bộ nhớ, ghép nối): giải thích ảnh hưởng và cách quay lại trước khi làm.

## 3. Phạm vi và tương thích ngược

**Được sửa:**
- thư mục project mới;
- một workflow CI mới.

**Không được sửa hoặc xoá:**
- `ESP32-H2/**`: bản H2 giữ nguyên hành vi. Kiểm tra bằng `git diff --stat origin/main -- ESP32-H2/`, kết quả phải rỗng;
- `ESP32-C3/matter-wifi-presence/**`;
- các dự án khác, các workflow hiện có.

**Tương thích với bo đang chạy 1.0.132** (quyết định 2):
- cùng VID `0xFFF1`, PID **`0x8001`**. Không bao giờ dùng `0x8000`, đó là PID của bản H2;
- cùng bảng phân vùng (mục 18). OTA không đổi được bảng phân vùng;
- cùng số endpoint, device type, cluster, tên (Description) và mã mode của các Mode Select (mục 6);
- cùng khoá lưu trữ:
  - key `app/hold-init` trong Matter KVS;
  - NVS namespace `ld2410`, key `bt_on` (blob 1 byte);
  - phân vùng `fctry`, namespace `setup-code` (biến thể unique_setup).
- phiên bản là số commit của repo nên luôn lớn hơn 132; file `.ota` mới cài được lên bo cũ.

**Không** nâng ESP-IDF, esp-matter hay dependency.

## 4. Phần cứng (đã được người dùng xác nhận)

| Chức năng | Chân ESP32-C3 SuperMini | Ghi chú |
|---|---|---|
| LD2410C TX | **GPIO3** (UART1 RX) | |
| LD2410C RX | **GPIO4** (UART1 TX) | GPIO4 là chân JTAG MTMS, dùng tự do vì JTAG đi qua USB |
| LD2410C VCC | **5V** | lấy từ nguồn USB; logic UART 3,3 V nối thẳng |
| LD2410C GND | GND | |
| LED xanh trên board | **GPIO8** | sáng khi mức **thấp**; chân strapping (xem dưới) |
| Nút BOOT | **GPIO9** | tích cực mức thấp, bật pull-up trong; chân strapping |
| USB-C | GPIO18 (D−), GPIO19 (D+) | USB-Serial-JTAG: log và nạp firmware. Board không có chip USB-UART (`/dev/ttyACM0`) |

**Bo mạch:**
- ESP32-C3 SuperMini, flash 4 MB, CPU 160 MHz.
- **Chân cấm** (build phải báo lỗi bằng `static_assert`):
  - GPIO11–GPIO17: flash, GPIO11 là VDD_SPI;
  - GPIO18, GPIO19: USB;
  - RX phải khác TX; nút và LED không được trùng radar hay trùng nhau.
- Kconfig cho chọn GPIO0–21; chân nút có thêm −1 = không có nút.
- **Chân strapping GPIO2, GPIO8, GPIO9:**
  - không nối thêm gì vào GPIO8, GPIO9;
  - để vào chế độ nạp, GPIO8 phải ở mức cao (LED kéo lên 3V3 nên giữ được mức cao) và GPIO9 ở mức thấp.
- **UART0** (GPIO20 RX, GPIO21 TX) không dùng cho log, nhưng ROM vẫn in log khởi động ra GPIO21.

**Radar:**
- HLK-LD2410C, UART1, 256000 baud 8N1, không flow control.
- Radar của người dùng: firmware `V2.44.25070917`, có lệnh 0x000B, có Bluetooth **[bo]**.
- Vẫn phải chạy với LD2410, LD2410B và firmware cũ không có các lệnh 0x000B, 0x00AB, 0x00A5 (mục 8).

**Nguồn:** radar và Wi-Fi cùng lấy điện từ cổng USB. Khi ESP32 khởi động lại bằng phần mềm (OTA, lỗi, factory reset), radar không mất điện và vẫn chạy tiếp (mục 10).

## 5. Toolchain và build

**Phiên bản (không đổi):**
- **ESP-IDF v6.0.2**, **esp-matter 1.6.1** (connectedhomeip `3d1c9e3c`).
- Docker image `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`; trong môi trường cloud dùng `mirror.gcr.io/espressif/esp-matter:release-v1.6.1_idf_v6.0.2`.

**Lệnh build:** image tự `cd` vào thư mục esp-matter khi chạy, nên lệnh phải tự `cd` vào project:

```bash
docker run --rm -v $PWD:/project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c 'cd /project/ESP32-C3/matter-presence-ld2410 && git config --global --add safe.directory "*" \
           && idf.py set-target esp32c3 && idf.py build'
```

**Project:**
- Chỉ build cho `esp32c3`; CMake báo lỗi rõ ràng nếu chọn target khác.
- Tên project giữ là `matter_presence_c3_wifi`, nên ra `build/matter_presence_c3_wifi.bin` và `build/matter_presence_c3_wifi-ota.bin`.

**CMake:**
- Bắt buộc có biến môi trường `ESP_MATTER_PATH`.
- `EXTRA_COMPONENT_DIRS`:
  - `${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip/config/esp32/components`
  - `${ESP_MATTER_PATH}/components`
- Cờ biên dịch giống bản cũ:
  - C++: `-std=gnu++17 -Os -DCHIP_HAVE_CONFIG_H -Wno-overloaded-virtual`
  - C: `-Os`
  - chung: `-Wno-format-nonliteral -Wno-format-security`. Lý do: trên RISC-V, `project_include.cmake` của esp-matter đặt `-Wno-format` nhưng không xoá các cờ phụ thuộc vào nó.
- Component `main`: C++17, `-DCHIP_HAVE_CONFIG_H`.

**Phiên bản firmware:**
- `PROJECT_VER = "1.0.<git rev-list --count HEAD>"`, hiển thị là SoftwareVersionString.
- `PROJECT_VER_NUMBER` = số commit, là SoftwareVersion dùng cho OTA.
- CMake chạy lại sau mỗi commit nhờ `CMAKE_CONFIGURE_DEPENDS` trên `<git-dir>/logs/HEAD`.
- Không phải git checkout: dùng số 0 và in cảnh báo.

**Ghi chú:**
- `CONFIG_CHIP_PROJECT_CONFIG="main/chip_project_config.h"` (mục 7).
- File `sdkconfig` cũ giữ mặc định cũ. Sau khi đổi `sdkconfig.defaults`, chạy `idf.py set-target esp32c3` để tạo lại.

## 6. Mô hình dữ liệu Matter (giữ nguyên để OTA không mất ghép nối)

| Endpoint | Device type | Cluster của ứng dụng |
|---|---|---|
| 0 | Root Node | Basic Information (thêm ProductLabel, SerialNumber), OTA Requestor, các cluster mặc định của esp-matter cho Wi-Fi |
| 1 | **Occupancy Sensor 0x0107** | Occupancy Sensing (feature **Radar**, HoldTime, HoldTimeLimits), Identify, Mode Select **"Learn empty room"** |
| 2 | Mode Select 0x0027 | Mode Select **"Range"** |
| 3 | Mode Select 0x0027 | Mode Select **"Radar resolution"** |
| 4 | Mode Select 0x0027 | Mode Select **"Radar Bluetooth"**: tạo cuối cùng; tạo lỗi thì các endpoint khác vẫn chạy |

Log sau khi tạo: `Endpoints: Learn empty room 1, Range 2, Radar resolution 3, Radar Bluetooth 4`. Dòng này đã thấy trong log bản H2; bản C3 dùng cùng mã.

`CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=5`, kiểm tra bằng `static_assert(... >= 5)`.

**Các Mode Select**

Description là tên entity trong Home Assistant.

- **Learn empty room** (endpoint 1):
  - mode 0 `Off`, 1 `Learning`, 2 `Factory thresholds`;
  - lúc khởi động luôn đặt CurrentMode về Off (`attribute::set_val`, chấp nhận `ESP_ERR_NOT_FINISHED`). Lý do: CurrentMode được lưu lại, nên lần học bị mất điện giữa chừng sẽ hiện lại `Learning`.
- **Range** (endpoint 2):
  - mode = số gate 2–8;
  - nhãn = gate × độ dài gate: ở 0,75 m là `1.5 m`, `2.25 m`, `3 m`, `3.75 m`, `4.5 m`, `5.25 m`, `6 m`; ở 0,2 m là `0.4 m` … `1.6 m`;
  - nếu radar giữ gate xa nhất khác nhau cho mục tiêu chuyển động và đứng yên (đặt bằng app HLKRadarTool): thêm mode **100** với nhãn kiểu `Moving 4.5 m, still 3 m`;
  - trước khi đọc được radar: mode 8.
- **Radar resolution** (endpoint 3):
  - mode 0 `0.75 m`, 1 `0.2 m`;
  - chỉ liệt kê `0.2 m` khi radar trả lời lệnh đọc độ phân giải;
  - mặc định 0.
- **Radar Bluetooth** (endpoint 4):
  - mode 0 `Off`, 1 `On`;
  - radar không trả lời lệnh hỏi MAC: chỉ có một mode 100 `Not available`;
  - mặc định 0.
- **Định dạng khoảng cách:**
  - `"%u m"` khi tròn mét;
  - `"%u.%u m"` khi chẵn chục cm;
  - còn lại `"%u.%02u m"`.

**Hiển thị:**
- Home Assistant xếp entity theo chữ cái: **Hold time, Learn empty room, Radar Bluetooth, Radar resolution, Range**. Người dùng yêu cầu đúng thứ tự này, nên không đổi tên.
- Apple Home không hiện Mode Select. Đây là theo tài liệu, chưa thử trên thiết bị Apple **[chưa]**.

**Xử lý Mode Select:**
- Một delegate `ModeSelect::SupportedModesManager` dùng chung cho mọi Mode Select, vì server Mode Select của SDK chỉ giữ một bản toàn cục.
- **ChangeToMode:** callback `PRE_UPDATE` trên task Matter chỉ ghi lại yêu cầu và đánh thức worker radar; không gửi lệnh radar, không chờ.
  - Range, Radar resolution, Radar Bluetooth khi radar chưa đọc được thông số: trả lỗi `ESP_ERR_INVALID_STATE` để HA giữ giá trị cũ.
  - Learn empty room khi task radar không chạy: trả lỗi `ESP_ERR_INVALID_STATE` (trừ Off).
  - Mode 100: chấp nhận và không làm gì.
  - Mode ngoài danh sách: `ESP_ERR_INVALID_ARG`.
- **Báo trạng thái:** worker báo CurrentMode theo giá trị radar **thực sự** giữ, đọc lại sau khi ghi (`attribute::report`). Danh sách mode đổi thì gọi `ReportAttributeChanged` cho SupportedModes.

**Occupancy Sensing:**
- Dùng cluster code-driven của esp-matter 1.6.1: `data_model_provider/clusters/occupancy_sensing/integration.h`, `OccupancySensingCluster`.
- Cluster tự áp dụng HoldTime (có người thì báo ngay, không có người thì báo sau HoldTime) và tự lưu giá trị.
- Khi `kServerReady`: `SetHoldTimeLimits({1, 300, CONFIG_PRESENCE_HOLD_TIME_SEC})`.
- Giá trị mặc định trong Kconfig (30 s) chỉ áp dụng **một lần** (đánh dấu bằng key `app/hold-init`); sau đó giữ giá trị controller ghi.
- Log `Hold time 30 s (limits 1-300 s)`.

**Khác:**
- Identify: callback START/STOP bật/tắt nháy LED identify.
- OTA Requestor bật.
- **Cluster:** sau khi build, `sdkconfig` chỉ được bật đúng 21 cluster sau:
  - ACCESS_CONTROL, ADMINISTRATOR_COMMISSIONING, BASIC_INFORMATION, BINDING, COLOR_CONTROL, DESCRIPTOR, GENERAL_COMMISSIONING;
  - GENERAL_DIAGNOSTICS, GROUPS, GROUP_KEY_MANAGEMENT, IDENTIFY, LEVEL_CONTROL, MODE_SELECT, NETWORK_COMMISSIONING;
  - OCCUPANCY_SENSING, ON_OFF, OPERATIONAL_CREDENTIALS, OTA_SOFTWARE_UPDATE_PROVIDER, OTA_SOFTWARE_UPDATE_REQUESTOR, SCENES, WIFI_NETWORK_DIAGNOSTICS.
  - Tắt mọi `CONFIG_SUPPORT_*_CLUSTER` khác. Danh sách tắt nằm trong `sdkconfig.defaults` và `sdkconfig.defaults.esp32c3` của thư mục ESP32-H2.

## 7. Danh tính, tên thiết bị, ghép nối

**Basic Information** (provider riêng, `CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER=y`, kế thừa `GenericDeviceInstanceInfoProvider`):

| Thuộc tính | Giá trị |
|---|---|
| VendorName | `Haxeko` |
| ProductName | `Presence LD2410` |
| ProductLabel | = ProductName (HA dùng làm Model) |
| HardwareVersionString | `ESP32C3-LD2410` |
| SerialNumber | MAC gốc trong eFuse (`esp_efuse_mac_get_default`, 6 byte), 12 ký tự hex in hoa. Cũng là MAC Wi-Fi mà router hiển thị |

- Thêm hai thuộc tính ProductLabel và SerialNumber vào cluster.
- Các giá trị khác lấy mặc định: VID/PID từ Kconfig, UniqueID do SDK sinh.
- Giới hạn độ dài, kiểm tra bằng `static_assert`: Vendor, Product, Display name ≤ 32 ký tự; prefix ≤ 11; Hardware ≤ 64.

**Tên thiết bị** = `Presence-` + 4 ký tự cuối của serial, ví dụ `Presence-B2C4`:
- **NodeLabel:**
  - ghi lúc khởi động, **chỉ khi chưa có nhãn nào được lưu** (bo mới, sau factory reset); nhãn đặt từ app thì giữ nguyên;
  - ghi giống một lệnh ghi của controller qua data model provider (`WriteAttribute`). Lưu ý: `create_node_label()` của esp-matter là managed-internally nên không có tác dụng;
  - HA đặt tên thiết bị theo NodeLabel.
- **Tên GAP BLE** khi chờ ghép nối:
  - đặt bằng `ConnectivityMgr().SetBLEDeviceName`, rồi khởi động lại quảng bá nếu đang quảng bá;
  - gói quảng bá Matter không chứa tên này.
- **Prefix rỗng:** NodeLabel = `Presence Sensor`, tên BLE = `CONFIG_BLE_DEVICE_NAME_PREFIX` (`Presence-`) + discriminator.

**Trước khi ghép nối:**
- Tên DNS-SD là `Presence Sensor`, giống nhau ở mọi bo. Khai báo trong `chip_project_config.h`:
  - `CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONABLE_DEVICE_NAME 1`
  - `CHIP_DEVICE_CONFIG_DEVICE_NAME CONFIG_DEVICE_DISPLAY_NAME`
- Device type trong DNS-SD: `CONFIG_ENABLE_COMMISSIONABLE_DEVICE_TYPE=y`, `CONFIG_DEVICE_TYPE=263`.

**Mã ghép nối mặc định** (mã test của SDK; SDK có DAC/PAI/CD test cho `0xFFF1/0x8001`):
- passcode `20202021`, discriminator `3840`;
- mã nhập tay `3497-011-2332`;
- QR `MT:-24J042C00KA0648G00`. QR chứa PID nên khác bản H2.

**In mã ghép nối** (`CONFIG_PRINT_ONBOARDING_CODES=y`; công cụ phát triển, sản phẩm thật thì tắt vì QR chứa passcode):
- **Khi nào in:** lúc `kServerReady` nếu chưa ghép nối, và khi bấm đúp mở cửa sổ ghép nối. Không in khi cửa sổ do controller mở (Multi-Admin).
- **Nội dung:**
  - dòng `Setup code of this board (BLE)`, hoặc `(Wi-Fi / DNS-SD)` sau khi BLE đã tắt; thêm `, SDK TEST CODE shared by every board` khi dùng mã test;
  - QR, link ảnh QR, mã nhập tay.

**Cửa sổ ghép nối:**
- **Khi khởi động** mà chưa ghép nối: SDK tự mở trong `CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS=900` (15 phút). BLE quảng bá nhanh trong 30 s rồi chuyển sang chậm (500 ms).
- **Bấm đúp:** `OpenBasicCommissioningWindow(900 s)`.
  - Trước khi tắt BLE: quảng bá cả BLE và DNS-SD (`kAllSupported`).
  - Sau khi tắt BLE: chỉ DNS-SD (`kDnssdOnly`).
  - Cửa sổ đang mở: log `Commissioning window already open`.
- **Xoá fabric cuối cùng:** mở lại cửa sổ 300 s, giữ thông tin Wi-Fi để ghép lại qua mạng.

**BLE** chỉ dùng khi ghép nối (`CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=y`). Sau đó SDK giải phóng BLE: sự kiện `kBLEDeinitialized`, log `BLE deinitialized and memory reclaimed`.

**Biến thể `unique_setup`** (tuỳ chọn, quyết định 5):
- Mỗi bo tự sinh passcode, discriminator, salt và verifier SPAKE2+ ở lần dùng đầu, lưu vào phân vùng `fctry`, namespace `setup-code`.
- **Khi sinh mã:**
  - nguồn ngẫu nhiên: DRBG của CHIP băm cùng MAC eFuse;
  - passcode hợp lệ theo `IsValidSetupPIN`;
  - không bao giờ sinh discriminator 0xF00 (mã test);
  - PBKDF dùng số vòng tối thiểu.
- Việc sinh mã chạy trên task Matter (stack 8 KB) và phải chờ xong trước khi task `id_diag` chạy.
- Mã giữ qua reboot, OTA, factory reset và nạp file merged; chỉ `erase-flash` mới làm bo sinh mã mới.

**Thử bằng chip-tool:**

```bash
chip-tool pairing ble-wifi 0x7283 <SSID> <mật khẩu> 20202021 3840
```

## 8. Driver HLK-LD2410 (giao thức UART)

**Khung báo cáo:**

```
F4 F3 F2 F1 | LEN (2 byte LE) | TYPE AA STATE MOV_CM(2) MOV_E STILL_CM(2) STILL_E DET_CM(2) [engineering] 55 00 | F8 F7 F6 F5
```

- TYPE 0x02 là báo cáo cơ bản, 0x01 là engineering.
- **Bỏ khung** khi gặp một trong các trường hợp:
  - payload ngắn hơn 13 byte, hoặc LEN > 64;
  - TYPE khác 01/02, hoặc byte thứ 2 khác `AA`;
  - hai byte cuối khác `55 00`, hoặc footer sai.
- **Dữ liệu engineering** (sau 11 byte cơ bản):
  - N = gate chuyển động cao nhất, M = gate đứng yên cao nhất;
  - tiếp theo là năng lượng chuyển động của gate 0..N, năng lượng đứng yên của gate 0..M, rồi các byte thêm;
  - chỉ dùng khi N+1 ≤ 9, M+1 ≤ 9 và vừa với độ dài khung.
- **STATE:**
  - 0 không có ai, 1 chuyển động, 2 đứng yên, 3 cả hai;
  - khi radar tự đo nhiễu (lệnh 0x000B): 4 đang đo (kể cả 10 s radar tự chờ), 5 đo xong và đã lưu ngưỡng, 6 thất bại;
  - **STATE 4–6 không được tính là có người.** Chỉ đọc bit 0/1 khi STATE ≤ 3.

**Khung ACK:**

```
FD FC FB FA | LEN | CMD|0x0100 (2) STATUS (2, 0 = OK) [giá trị] | 04 03 02 01
```

- Lệnh gửi đi cùng khuôn này, nhưng không có STATUS.
- **Parser:** một byte làm hỏng header có thể là byte đầu của header kế tiếp, phải dò lại từ byte đó.

**Các lệnh**

Lệnh 0x0060 và 0x0064 dùng giá trị kiểu "word 2 byte + giá trị 4 byte" (LE).

| Lệnh | Giá trị gửi | Trả lời / ghi chú |
|---|---|---|
| 0x00FF bật chế độ cấu hình | `01 00` | thử 3 lần |
| 0x00FE kết thúc cấu hình | – | thử 3 lần. Radar bị bỏ lại ở chế độ cấu hình thì **ngừng gửi báo cáo** |
| 0x0060 gate xa nhất + no-one duration | 0x0000 + gate chuyển động, 0x0001 + gate đứng yên, 0x0002 + thời gian (s) | gate 2–8; lưu trong flash radar |
| 0x0061 đọc thông số | – | `AA`, N (= 8), gate chuyển động xa nhất, gate đứng yên xa nhất, 9 ngưỡng chuyển động, 9 ngưỡng đứng yên, no-one duration (2 byte) |
| 0x0062 / 0x0063 | – | bật / tắt engineering mode; radar mới bật nguồn thì engineering tắt |
| 0x0064 ngưỡng một gate | 0x0000 + gate, 0x0001 + ngưỡng chuyển động, 0x0002 + ngưỡng đứng yên | 0–100; ngưỡng đứng yên của gate 0, 1 **không chỉnh được** (radar bỏ qua) |
| 0x00A0 phiên bản firmware | – | type (2), major (2), minor (4), in kiểu `V%x.%02x.%08x`, ví dụ `V2.44.25070917` |
| 0x00A3 khởi động lại radar | – | radar có thể khởi động lại trước khi gửi ACK: **chỉ ACK từ chối mới là lỗi**; radar trả lời lại sau khoảng 1 s |
| 0x00A4 Bluetooth | `01 00` bật / `00 00` tắt | có hiệu lực sau 0x00A3 (giao thức LD2410B V1.07) |
| 0x00A5 đọc MAC Bluetooth | `01 00` | 6 byte. `08 05 04 03 02 01`, toàn 0 hoặc toàn FF nghĩa là Bluetooth tắt (cách ESPHome xử lý) |
| 0x00AA đặt độ phân giải | `00 00` = 0,75 m, `01 00` = 0,2 m | có hiệu lực sau 0x00A3 |
| 0x00AB đọc độ phân giải | – | 2 byte. Firmware cũ không trả lời: coi là cố định 0,75 m |
| 0x000B radar tự đo nhiễu nền | thời gian đo, giây (2 byte) | giao thức LD2410C V1.09 mục 2.2.20. Radar chờ 10 s, đo, rồi tự lưu ngưỡng. Firmware cũ từ chối hoặc không trả lời |
| 0x001B trạng thái đo nhiễu | – | 0 chưa chạy, 1 đang chạy, 2 xong (mục 2.2.21) |

**Mức kiểm chứng trên radar `V2.44.25070917` của người dùng:**
- **[bo]**: 0x000B, 0x0061, 0x0064 (ghi lại ngưỡng mặc định); bật Bluetooth (0x00A4 + 0x00A3 + đọc MAC bằng 0x00A5).
- **[chưa]**: trả lời của 0x001B (log không in ra), nhận biết Bluetooth tắt qua địa chỉ giả.

**Quy tắc chung:**
- **Gửi lệnh:**
  - mỗi lệnh chờ ACK tối đa 1000 ms;
  - bỏ qua ACK trễ của lệnh trước (so CMD|0x0100);
  - mọi lệnh đi qua một mutex.
- **Nơi gửi:** chỉ gửi lệnh từ task thường; **không bao giờ** gửi từ task Matter hay callback esp_timer.
- **Gate:** 9 gate (0–8), mỗi gate 75 cm, hoặc 20 cm ở độ phân giải 0,2 m.
- **Ngưỡng mặc định nhà sản xuất** (bảng 7 của giao thức):
  - chuyển động `50,50,40,30,20,15,15,15,15`;
  - đứng yên `–,–,40,40,30,30,20,20,20`.
- **Task đọc `ld2410`:**
  - priority 5, stack 2560 byte;
  - UART RX buffer 256 byte, mỗi lần đọc 128 byte, timeout 100 ms;
  - mục tiêu mới nhất được bảo vệ bằng critical section.
- **Bản ghi khi học (capture):**
  - mỗi gate một bảng đếm theo năng lượng 0–100: `uint16_t moving[9][101]`, `still[9][101]`, cộng số khung; khoảng 3,6 KB;
  - năng lượng > 100 tính là 100; bộ đếm dừng ở 65535;
  - chỉ `malloc` trong lúc ESP32 ghi.

## 9. Logic hiện diện (giữ nguyên thuật toán)

**Task:** `presence`, priority 2 (trên task Matter = 1, dưới Wi-Fi/lwIP), stack 4096, chu kỳ 100 ms (`vTaskDelayUntil`).

**Có người:** `present = moving || still`, chỉ với STATE 0–3.
- Kconfig `PRESENCE_MAX_DISTANCE_CM` mặc định 0 = tắt.
- Khác 0: chỉ tính mục tiêu chuyển động hoặc đứng yên ở khoảng cách ≤ giá trị đó.

**Radar im lặng:** không có khung hợp lệ trong `RADAR_TIMEOUT_SEC` (mặc định 10 s; trước khung đầu tiên thì tính từ lúc khởi động):
- coi là **không có người**;
- log lỗi `No data from the LD2410 for 10 s, check the wiring (radar TX -> GPIO3, RX -> GPIO4)`;
- có dữ liệu lại thì log `Radar data received`.

**Gửi vào cluster:**
- Chỉ gửi khi **trạng thái đổi**, bằng `OccupancySensingCluster::SetOccupancy`.
- Giữ khoá Matter bằng `esp_matter::lock::ScopedChipStackLock` (chờ tới khi task Matter rảnh). **Không** post vào hàng đợi sự kiện Matter: hàng đợi đầy sẽ làm bo khởi động lại.
- Gửi không được thì thử lại ở lần poll sau.
- Chỉ gửi sau khi Matter đã start. Vì vậy task này tạo sau `esp_matter::start()`.

**Log mỗi lần đổi:** `Radar: presence (moving yes 120 cm, still no 0 cm)`, `Radar: no one (...)`, hoặc `Radar: no one (no radar data)`.

**Thời gian tới khi báo không có người** ≈ no-one duration của radar + HoldTime. Firmware không đổi no-one duration (mặc định nhà sản xuất 5 s; radar của người dùng đang là 1 min **[bo]**).

**Không thêm** state machine, bộ lọc hay "presence confidence" mới (ràng buộc ở mục 22).

## 10. Task cấu hình radar và các select

**Task `radar_cfg`:** priority 2, stack 4096. Đây là task **duy nhất** gửi lệnh cấu hình radar, mỗi lần một phiên.

**Vòng lặp của task:**
1. áp dụng các thay đổi Bluetooth, Range, Radar resolution được yêu cầu;
2. đưa Bluetooth của radar về lựa chọn đã lưu;
3. ghi lại ngưỡng mặc định nếu được yêu cầu;
4. học phòng trống nếu được yêu cầu;
5. chờ task notification. Nếu radar chưa trả lời thì thay vào đó chờ để thử lại: 15 s, 30 s, 1 min… tối đa 5 min. Mỗi lần log `Could not read the LD2410 settings (check the wire from GPIO4 to the radar RX pin), next try in 15 s`.

**Một phiên thông số:**
1. config_begin;
2. đọc độ phân giải. Lỗi thì coi là cố định 0,75 m và không cho chọn 0,2 m;
3. đọc Bluetooth. Timeout thì hỏi lại một lần; vẫn lỗi thì hiện `Not available`;
4. lưu độ dài gate và Bluetooth nếu có thay đổi;
5. nếu có thay đổi cần khởi động lại: khởi động lại radar **một lần**, chờ 1 s, config_begin, đọc lại và kiểm tra đúng giá trị mới. Có lỗi trước bước khởi động lại thì ghi lại giá trị cũ;
6. đọc thông số (0x0061);
7. nếu Range đổi: ghi gate xa nhất (chuyển động = đứng yên = gate đã chọn, giữ nguyên no-one duration), rồi đọc lại kiểm tra;
8. config_end.

**Mỗi lần khởi động** bo luôn chạy một phiên như trên. Vì vậy radar bị bỏ lại ở chế độ cấu hình (ESP32 reset giữa phiên) cũng gửi báo cáo trở lại.

**Yêu cầu radar không làm được** (đổi độ phân giải trên radar cố định 0,75 m, đổi Bluetooth trên radar không có Bluetooth):
- trả `ESP_ERR_NOT_SUPPORTED`;
- không coi radar là mất kết nối;
- HA hiện lại giá trị thật.

**Đổi thất bại:** log `Changing the radar settings failed`, hiện lại giá trị đọc được lần trước.

**Log thông số:**
- lần đầu: `LD2410 settings: ...`;
- sau mỗi lần đổi: `New LD2410 settings: no-one duration 5 s, range moving 6 m (gate 8), still 6 m (gate 8), 75 cm per gate, Bluetooth off`;
- Bluetooth bật thì thêm `on, MAC xx:xx:..`; radar có độ phân giải cố định thì thêm `(fixed)`.

**Bluetooth của radar:**
- **Mặc định tắt.** Lựa chọn lưu trong NVS (namespace `ld2410`, key `bt_on`).
- Mỗi lần khởi động, nếu radar khác lựa chọn đã lưu thì đổi theo (radar khởi động lại một lần).
- Đổi thất bại: không thử lại cho tới lần khởi động sau hoặc lần chọn kế tiếp, để radar không bị khởi động lại liên tục.
- **Không đổi mật khẩu Bluetooth** của radar. Chức năng mật khẩu đã bỏ từ 1.0.116; radar từng chạy bản 1.0.111–1.0.115 có thể vẫn giữ mật khẩu `solana`.
- Để nguyên key NVS cũ `bt_password`.

**Khi đang học:** thay đổi Range, độ phân giải hay Bluetooth được áp dụng ngay sau khi học xong.

**Đổi độ phân giải:** nhãn Range đổi theo. Ngưỡng đã học là cho độ dài gate cũ; README nhắc học lại.

## 11. Học phòng trống và Factory thresholds

**Kconfig (menu *Empty Room Learning*):**

| Tuỳ chọn | Mặc định | Khoảng |
|---|---|---|
| `LD2410_LEARN_DELAY_SEC` | 20 s | 0–120 |
| `LD2410_LEARN_DURATION_SEC` | 60 s | 10–300 |
| `LD2410_LEARN_RADAR_AUTO` | y | |
| `LD2410_LEARN_PERCENTILE` | 95 | 50–100 |
| `LD2410_LEARN_MARGIN` | 10 | 0–50 |
| `LD2410_LEARN_MAX_RAISE` | 30 | 0–100 |

**Bắt đầu học:** chọn `Learning` trong HA, hoặc bấm BOOT 3 lần. Ngay đầu tiên log `Empty room learning: leave the room, recording starts in 20 s and lasts 60 s`. Có hai cách học:

**A. Radar tự đo** (`RADAR_AUTO=y`, cách mặc định):
- **Gửi lệnh:**
  - chờ (thời gian chờ − 10 s);
  - config_begin, đọc phiên bản firmware (log `LD2410 firmware V...`), đọc ngưỡng hiện tại;
  - gửi 0x000B với thời gian đo = thời gian ghi; config_end.
- **Radar nhận lệnh:** log `The LD2410 measures the empty room itself: 10 s more to leave, then 60 s`, rồi theo dõi STATE trong các báo cáo:
  - **lỗi:** trong 15 s phải thấy STATE 4, nếu không thì thất bại;
  - **xong:** STATE 5, hoặc báo cáo trở lại bình thường và 0x001B trả 2;
  - **thất bại:** STATE 6;
  - **quá thời gian:** sau 10 + thời gian đo + 30 s, khởi động lại radar và báo thất bại.
- **Sau khi xong:**
  - đọc trạng thái (0x001B) và ngưỡng mới;
  - log `Thresholds before: ...`, `Thresholds set by the radar: ...`;
  - cảnh báo gate nào > mặc định + `MAX_RAISE`;
  - log `Learning done: thresholds set by the LD2410 background noise detection`.
  - Ngưỡng radar đặt **không bị giới hạn** bởi mức 95 %, +10 hay +30.
- **Huỷ** (chọn Off hoặc Factory thresholds): khởi động lại radar, chờ 1 s, kết quả là huỷ.
- **Radar từ chối lệnh** (nhưng trả lời các lệnh khác): log `This LD2410 firmware has no background noise detection: learning on the ESP32`, rồi chuyển sang cách B, bắt đầu ghi đúng lúc hết thời gian chờ.
- **Radar không trả lời gì:** thất bại, log nhắc kiểm tra dây GPIO4 → chân RX của radar.

**B. ESP32 tự học:**
1. config_begin, bật engineering (0x0062), config_end.
2. Ghi bảng đếm năng lượng trong thời gian ghi (log `Recording the empty room for 60 s`), rồi config_begin.
3. Kiểm tra trước khi tính:
   - đã huỷ: không ghi gì;
   - ít hơn 2 khung mỗi giây (`frames < duration × 2`): thất bại.
4. Tính cho mỗi gate:
   - **mức nhiễu** = năng lượng ở hạng `ceil(frames × P / 100)` khi các khung của gate xếp theo năng lượng, tức năng lượng mà P % số khung không vượt quá. Với P = 100 thì là giá trị cao nhất;
   - **ngưỡng mới** = clamp(nhiễu + MARGIN, mặc định nhà sản xuất, min(mặc định + MAX_RAISE, 100));
   - ngưỡng đứng yên của gate 0, 1 giữ nguyên.
5. Ghi cả 9 gate (0x0064), đọc lại (0x0061) và so sánh. Không khớp thì thất bại; khi đó radar có thể giữ lẫn ngưỡng cũ và mới, log phải nói rõ.
6. Tắt engineering, config_end.
7. Log:
   - `600 reports. Noise = energy of 95 % of them, threshold = noise + 10, from the factory default to factory + 30`;
   - bảng từng gate, dạng `Gate 3:  11 ( 14) ->  30 ( 30) |  75 ( 78) ->  70 ( 40)`: nhiễu (cao nhất) -> ngưỡng (cũ);
   - cảnh báo gate bị giới hạn hoặc lên tới 100, kèm khoảng cách của gate;
   - `Learning done: new thresholds saved in the LD2410 (600 reports)`.

**Factory thresholds** (chọn trong HA, hoặc bấm 4 lần):
- Huỷ việc học đang chạy nếu có (lựa chọn cuối cùng thắng).
- Ghi ngưỡng mặc định nhà sản xuất (ngưỡng đứng yên gate 0, 1 giữ nguyên), rồi đọc lại.
- Radar đã giữ ngưỡng mặc định: không ghi gì nhưng vẫn tính là thành công.
- Log `Restoring the LD2410 factory thresholds`, `Thresholds until now: ...`, `Factory thresholds: ...`.
- Select hiện `Factory thresholds` trong lúc ghi, xong trở về `Off`.
- Range, độ phân giải, Bluetooth, no-one duration giữ nguyên; radar không khởi động lại.

**Lựa chọn cuối cùng thắng:**
- Learning rồi chọn Off: huỷ, không ghi gì, LED trở lại bình thường ngay.
- Factory thresholds khi đang học: huỷ học, rồi ghi ngưỡng mặc định.
- Learning (hoặc Off) khi Factory thresholds chưa bắt đầu: bỏ Factory thresholds.
- Bấm 3 lần khi đang học: bỏ qua (log `Empty room learning already requested or running`).
- Task radar không chạy: bấm nút thì LED đỏ; chọn từ HA thì bị từ chối (mục 6).

**Kết quả hiện trên LED:**
- học xong (lưu ngưỡng): xanh lá;
- thất bại: đỏ;
- huỷ: trở lại bình thường ngay;
- ghi ngưỡng mặc định xong: nháy tím;
- ghi ngưỡng mặc định thất bại: đỏ.

Trên LED một màu xem mục 13.

**Không bao giờ** tự ghi ngưỡng vào flash radar khi người dùng không yêu cầu.

**Mức kiểm chứng:**
- **[bo]**: radar tự đo và Factory thresholds từ HA trên radar V2.44.25070917.
- **[build]**: cách ESP32 tự học; bấm 4 lần.

## 12. Nút BOOT (GPIO9)

**Đọc nút:**
- poll bằng esp_timer mỗi 20 ms;
- chống dội 2 mẫu (40 ms);
- một lần bấm (click) dài ≤ 500 ms;
- các lần bấm cách nhau ≤ 400 ms; nhả quá 400 ms là hết chuỗi.

| Thao tác | Hành động |
|---|---|
| 2 lần | mở cửa sổ ghép nối 15 phút (mục 7) |
| 3 lần | học phòng trống |
| 4 lần | Factory thresholds |
| 5 lần trở lên | bỏ qua (log) |
| giữ 5 s | factory reset. Kích hoạt ngay khi đang giữ; giữ được 1 s thì log nhắc |

- Một lần giữ quá 500 ms huỷ các lần bấm trước nó.
- **Callback chạy trong task esp_timer**, nên không được chặn:
  - ghép nối: chuyển sang task Matter bằng `PlatformMgr().ScheduleWork`;
  - học, ngưỡng mặc định: gửi notification cho task `radar_cfg`.
- **Factory reset:**
  - `esp_matter::factory_reset()` xoá thông tin Wi-Fi và ghép nối Matter, rồi khởi động lại;
  - Matter chưa start thì `nvs_flash_erase()` rồi `esp_restart()`;
  - log `Factory reset: erasing the Wi-Fi network and Matter pairing`.

## 13. LED trạng thái (GPIO8, một màu, sáng khi mức thấp)

**Cơ chế:**
- Timer esp_timer 100 ms.
- Mọi "màu" đều là **sáng**.
- Khởi tạo: đặt mức "tắt" trước khi bật output, để LED không loé lên lúc khởi động.

**Thứ tự ưu tiên:** Identify > học / ngưỡng mặc định > chế độ.

| Trạng thái | Mẫu nháy | Bản H2 (để đối chiếu) |
|---|---|---|
| Identify | nháy 5 Hz (100 ms sáng, 100 ms tắt) | trắng |
| Chờ rời phòng | sáng liên tục suốt thời gian chờ | cyan |
| Đang ghi | nháy 0,5 Hz, bắt đầu bằng tắt (1 s tắt, 1 s sáng) | cyan |
| Học xong / thất bại | sáng 10 s | xanh lá / đỏ |
| Đã ghi ngưỡng mặc định | nháy 2,5 Hz trong 10 s, bắt đầu bằng sáng (0,2 s sáng, 0,2 s tắt) | tím |
| Cửa sổ ghép nối mở | nháy 0,5 Hz (1 s sáng, 1 s tắt) | xanh dương |
| Bình thường | tắt | |

**Quy tắc:**
- Đặt lại cùng một trạng thái kết quả (ví dụ thất bại lần hai) thì tính lại từ đầu 10 s.
- Sau mỗi sự kiện Matter: cửa sổ ghép nối mở thì nháy, nếu không thì tắt. Đã ghép nối xong thì LED tắt.
- Trạng thái radar (có người, mất tín hiệu) không hiện trên LED.

## 14. Wi-Fi và BLE

**Wi-Fi station:**
- Chỉ station, tắt SoftAP.
- Thông tin Wi-Fi đến từ lúc ghép nối. Kết nối và kết nối lại do Matter SDK (ConnectivityManager) lo; firmware không tự gọi `esp_wifi_connect`.

**Tắt modem sleep sau khi BLE tắt** (`CONFIG_PRESENCE_WIFI_STAY_AWAKE=y`, mặc định):
- **Cách làm:**
  - khi `kBLEDeinitialized`, và ở mỗi `WIFI_EVENT_STA_CONNECTED` sau đó (driver Wi-Fi có thể chưa chạy lúc BLE tắt ngay khi khởi động), gọi `esp_wifi_set_ps(WIFI_PS_NONE)`;
  - log `BLE released: Wi-Fi power save off`;
  - đang là `WIFI_PS_NONE` rồi thì bỏ qua.
- **Khi BLE còn chạy** để ghép nối: giữ mặc định của ESP-IDF (`WIFI_PS_MIN_MODEM`, coexistence).
- **Lý do:**
  - log 1.0.128 của người dùng có `Msg Retransmission` **[bo]**;
  - giải thích (ở modem sleep, router giữ gói cho bo tới beacon DTIM kế tiếp) là phân tích, chưa đo;
  - hiệu quả của việc tắt modem sleep **[chưa]** được kiểm chứng.

**Không giới hạn công suất phát Wi-Fi:**
- Bản 1.0.125–1.0.127 đã thử 8,5 dBm, không giúp được.
- Nguyên nhân lỗi kết nối là router TP-Link Deco X10 bật Beamforming: người dùng báo tắt Beamforming thì ghép nối được.

**Bảo mật Wi-Fi:**
- Bỏ Wi-Fi Enterprise và OWE: Matter SDK chỉ đặt SSID và mật khẩu (WPA/WPA2/WPA3 Personal).
- Giữ WPA3 SAE.

**Tiết kiệm RAM:**
- Mã Wi-Fi nằm trong flash thay vì IRAM (`ESP_WIFI_IRAM_OPT=n`, `ESP_WIFI_RX_IRAM_OPT=n`). Trên C3, IRAM và DRAM dùng chung SRAM, nên làm vậy được thêm heap.
- NimBLE thu gọn (mục 15): chỉ vai trò peripheral, 1 kết nối, không bảo mật BLE (Matter tự bảo mật bằng PASE), không có tính năng BLE 5, bộ đệm nhỏ.

**Log mỗi lần vào hoặc rời Wi-Fi** (sự kiện `kESPSystemEvent`):
- vào được: `EV wifi joined bssid=... ch=... security=WPA2`;
- thất bại hoặc rời mạng: `EV wifi failed or left: reason N (<giải thích>), bssid=... rssi=-55 dBm`.
- Bảng giải thích reason lấy theo `identity_diag.cpp`, ví dụ:
  - 2: router không trả lời bước xác thực;
  - 4: router không trả lời bước kết nối, hoặc ngắt bo vì không hoạt động;
  - 15: hết thời gian trao khoá, thường do sai mật khẩu;
  - 36: chính bo dừng lần thử.

## 15. `sdkconfig.defaults` (bản C3, tự chứa)

Cộng thêm danh sách tắt cluster ở mục 6.

```
CONFIG_IDF_TARGET="esp32c3"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0xC000

# BLE (NimBLE), chỉ để ghép nối
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT=n
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_MAX_CCCDS=2
CONFIG_BT_NIMBLE_SECURITY_ENABLE=n
CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n
CONFIG_BT_NIMBLE_WHITELIST_SIZE=1
CONFIG_BT_NIMBLE_GATT_MAX_PROCS=1
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=10
CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE=100
CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT=4
CONFIG_BT_NIMBLE_MSYS_2_BLOCK_SIZE=320
CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT=5
CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT=5
CONFIG_BT_NIMBLE_TRANSPORT_EVT_DISCARD_COUNT=3

# Wi-Fi station, Matter over Wi-Fi
CONFIG_ENABLE_WIFI_STATION=y
CONFIG_OPENTHREAD_ENABLED=n
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n
CONFIG_ESP_WIFI_IRAM_OPT=n
CONFIG_ESP_WIFI_RX_IRAM_OPT=n
CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT=n
CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA=n
CONFIG_LWIP_IPV6_AUTOCONFIG=y
CONFIG_LWIP_IPV6_NUM_ADDRESSES=6
CONFIG_LWIP_HOOK_IP6_ROUTE_DEFAULT=y
CONFIG_LWIP_HOOK_ND6_GET_GW_DEFAULT=y

# Log qua USB-Serial-JTAG, không console Matter
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
CONFIG_ENABLE_CHIP_SHELL=n

# Matter
CONFIG_DEVICE_PRODUCT_ID=0x8001
CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=5
CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER=y
CONFIG_BLE_DEVICE_NAME_PREFIX="Presence-"
CONFIG_CHIP_PROJECT_CONFIG="main/chip_project_config.h"
CONFIG_ENABLE_COMMISSIONABLE_DEVICE_TYPE=y
CONFIG_DEVICE_TYPE=263
CONFIG_ENABLE_OTA_REQUESTOR=y
CONFIG_CHIP_OTA_IMAGE_BUILD=y
CONFIG_EVENT_LOGGING_CRIT_BUFFER_SIZE=1024
CONFIG_EVENT_LOGGING_DEBUG_BUFFER_SIZE=256

# Chân của SuperMini (Kconfig của project)
CONFIG_LD2410_UART_RX_PIN=3
CONFIG_LD2410_UART_TX_PIN=4
CONFIG_STATUS_LED_GPIO=8
CONFIG_STATUS_LED_ACTIVE_HIGH=n
```

**Kiểm tra trong `sdkconfig` sinh ra**, các giá trị sau phải đúng như bản 1.0.132:
- Wi-Fi bật, OpenThread tắt;
- `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=y`, `CONFIG_USE_MINIMAL_MDNS=y`;
- `CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS=900`, `CONFIG_DEVICE_VENDOR_ID=0xFFF1`;
- `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE=y`, `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`;
- `CONFIG_CHIP_TASK_STACK_SIZE=8192`, `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160`;
- brownout detector bật;
- đúng 21 cluster (mục 6).

**Lưu ý:**
- Không có `CONFIG_BT_NIMBLE_MAX_BONDS` khi tắt bảo mật BLE.
- Đặt `ON_OFF`/`LEVEL`/`COLOR`=n cũng vô ích: mã codegen của SDK vẫn build các cluster đó.

## 16. Task, RAM, flash

| Task | Priority | Stack (byte) | Stack còn trống, đo trên bo 1.0.128–1.0.131 |
|---|---|---|---|
| CHIP (Matter, của SDK) | 1 | 8192 | 2320 |
| wifi (ESP-IDF) | – | – | 4652 |
| esp_timer (timer LED 100 ms, nút 20 ms, log `mem`) | – | 3584 | 3120 |
| `presence` | 2 | 4096 | 3052 |
| `radar_cfg` | 2 | 4096 | 2852 |
| `ld2410` | 5 | 2560 | 1856 |
| `id_diag` | 1 | 4096 | 2804 |

Không giảm stack khi chưa đo lại trên bo.

**Mốc của bản 1.0.132** (đo bằng `idf.py size` **[build]**):
- firmware 1 478 KB trên phân vùng app 1 920 KB (trống 442 KB, 23 %);
- RAM tĩnh 147,0 KB trên 313,8 KB DRAM (46,9 %);
- còn khoảng 166,7 KB cho heap.

**Heap đo trên bo** (log của người dùng **[bo]**; của 1.0.132 thì **[chưa]** đo):

| Bản | Lúc khởi động | Lúc ghép nối | Thấp nhất |
|---|---|---|---|
| 1.0.122 | 41,0 KB | 30,7 KB | 25,9 KB |
| 1.0.128 | | | **15,7 KB** trong 10 phút đầu |
| 1.0.131 | | 49,0 KB | 40,0 KB |

**Yêu cầu:**
- `idf.py size` của bản mới không được tệ hơn 1.0.132 (flash và RAM tĩnh); nếu tệ hơn thì giải thích.
- Heap thật lúc ghép nối phải thử trên bo bằng log `mem`.

**Log bộ nhớ (`mem`):**
- **Khi nào:** lúc khởi động, rồi mỗi `MEM_MONITOR_PERIOD_SEC` (mặc định 600 s, 0 = chỉ khi khởi động).
- **Nội dung:**
  - `Heap: <còn> bytes free, lowest <thấp nhất> since boot, largest block <khối lớn nhất>`;
  - stack còn trống của các task CHIP, wifi, esp_timer, presence, ld2410, radar_cfg, id_diag (thêm console nếu bật shell).
- **Cảnh báo:**
  - heap thấp nhất xuống dưới 16 KB: cảnh báo **một lần cho mỗi mức thấp mới**, kèm khoảng thời gian xảy ra: `Heap low: lowest N bytes free since boot, reached between A s and B s; now ... free, largest block ...`;
  - stack còn dưới 512 byte: cảnh báo.

## 17. Log và chẩn đoán

**Log khởi động:**

```
I app_main: Haxeko Presence LD2410, firmware 1.0.<số commit>
I app_main: Target esp32c3, board ESP32-C3 SuperMini, Matter over Wi-Fi
I app_main: Pins: LD2410 RX GPIO3 (radar TX), TX GPIO4 (radar RX), button GPIO9, LED GPIO8 (single colour, on when low)
I ld2410: LD2410 on UART1, RX=GPIO3 TX=GPIO4
I device_info: Serial number: <MAC>, device name: Presence-XXXX
```

**Log sự kiện Matter:** `Commissioning complete`, `Commissioning failed, fail safe timer expired`, `Commissioning window opened` / `closed`, `Fabric removed successfully`, `Wi-Fi connectivity changed`, `Interface IP Address changed`.

**Khối `id_diag`:**
- **Khi nào in:**
  - lúc khởi động;
  - mỗi `IDENTITY_DIAG_PERIOD_SEC` (mặc định 300 s, 0 = tắt);
  - khi có sự kiện: Wi-Fi kết nối/mất kết nối, đổi địa chỉ IP, ghép nối xong, fabric thêm/xoá/đổi, server sẵn sàng. Các sự kiện gần nhau gộp lại, cách nhau ít nhất 5 s.
- **Các dòng:**
  - `hw`: `efuse_mac`, `wifi_mac`, `reset=` (power-on, BROWNOUT, PANIC, WATCHDOG...), `up`, `heap`, `min_heap`;
  - `dev`: `vid`, `pid`, `serial`, `unique_id`, `discriminator`, `test_setup_code`;
  - `mtr`: commissioned, fabrics, window_open, failsafe, phiên CASE/PASE, subscriptions, reads; đếm từ lúc khởi động;
  - `fab`: mỗi fabric một dòng: index, vendor, node_id, fabric_id, compressed_fabric_id, label;
  - `wifi`: connected, SSID, BSSID, kênh, RSSI, `power_save` (min_modem / none);
  - `ip`: IPv4, số địa chỉ IPv6 theo loại. Đã kết nối mà chưa có link-local thì cảnh báo;
  - `nvs`: số mục theo namespace, không bao giờ in giá trị.
- **Log subscription** (`IDENTITY_DIAG_SUBSCRIPTION_LOG=y`): `SUB request`, `SUB established`, `SUB terminated` kèm fabric, peer và chu kỳ báo cáo. Callback ReadHandler chỉ quan sát và chuyển tiếp cho callback đăng ký trước nó.
- **Khoá:**
  - lấy dữ liệu CHIP bằng `TryLockChipStack` chờ tối đa 2 s; task Matter bị kẹt thì bỏ lượt và in cảnh báo;
  - dữ liệu Wi-Fi đọc không cần khoá.
- Task `id_diag`: priority 1, stack 4096.

**Không bao giờ in vào log:** passcode, khoá riêng DAC hay khoá vận hành, IPK, mật khẩu Wi-Fi. Discriminator là công khai, in được.

## 18. Kconfig, phân vùng, OTA, nạp firmware

**Tên tuỳ chọn Kconfig** giữ như bản cũ, để các hướng dẫn trong README vẫn đúng:

| Menu | Tuỳ chọn | Mặc định | Khoảng |
|---|---|---|---|
| Device Information | `DEVICE_VENDOR_NAME` | `Haxeko` | ≤ 32 ký tự |
| | `DEVICE_PRODUCT_NAME` | `Presence LD2410` | ≤ 32 |
| | `DEVICE_NAME_PREFIX` | `Presence-` | ≤ 11 |
| | `DEVICE_DISPLAY_NAME` | `Presence Sensor` | ≤ 32 |
| | `DEVICE_HARDWARE_VERSION_STRING` | `ESP32C3-LD2410` | ≤ 64 |
| Presence Sensor Configuration | `LD2410_UART_RX_PIN` / `LD2410_UART_TX_PIN` | 3 / 4 | 0–21 |
| | `PRESENCE_MAX_DISTANCE_CM` | 0 (tắt) | 0–800 |
| | `PRESENCE_HOLD_TIME_SEC` | 30 | 1–300 |
| | `RADAR_TIMEOUT_SEC` | 10 | 1–300 |
| | `STATUS_LED_GPIO` | 8 | 0–21 |
| | `STATUS_LED_ACTIVE_HIGH` | n | |
| | `RESET_BUTTON_GPIO` | 9 | −1–21 |
| | `PRESENCE_WIFI_STAY_AWAKE` | y | |
| Empty Room Learning | (mục 11) | | |
| Identity Diagnostics and Setup Code | `IDENTITY_DIAG_PERIOD_SEC` | 300 | 0–86400 |
| | `IDENTITY_DIAG_SUBSCRIPTION_LOG` | y | |
| | `PRINT_ONBOARDING_CODES` | y | |
| | `UNIQUE_SETUP_CODE`, `UNIQUE_SETUP_CODE_PARTITION` | y khi build unique_setup, `fctry` | |
| Memory Monitor | `MEM_MONITOR_PERIOD_SEC` | 600 | 0–86400 |

**`partitions.csv`** giữ nguyên mọi mục (tên, kiểu, offset, kích thước, cờ) để OTA được lên bo cũ:

```
# Name,   Type, SubType, Offset,  Size, Flags
# Note: Firmware partition offset needs to be 64K aligned, initial 36K (9 sectors) are reserved for bootloader and partition table
esp_secure_cert,  0x3F, ,0xd000,    0x2000, encrypted
nvs,      data, nvs,     0x10000,   0xC000,
nvs_keys, data, nvs_keys,,          0x1000, encrypted
otadata,  data, ota,     ,          0x2000
phy_init, data, phy,     ,          0x1000,
ota_0,    app,  ota_0,   0x20000,   0x1E0000,
ota_1,    app,  ota_1,   0x200000,  0x1E0000,
fctry,    data, nvs,     0x3E0000,  0x6000
```

**OTA:**
- `CONFIG_CHIP_OTA_IMAGE_BUILD=y` tạo `build/matter_presence_c3_wifi-ota.bin` (header Matter OTA: VID/PID, phiên bản), đổi đuôi thành `.ota`.
- Cài qua Home Assistant: Matter Server → Upload Firmware.
- Phiên bản trong file phải lớn hơn bản đang chạy.

**Nạp firmware:**
- Lệnh thường: `idf.py -p /dev/ttyACM0 erase-flash flash monitor`.
- File merged: `esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 matter_presence_c3_wifi_merged.bin`. File này ghi đè NVS (0x10000), nên **xoá ghép nối**.
- Không vào được chế độ nạp: giữ BOOT, cắm USB (hoặc bấm RESET), thả BOOT.

## 19. CI (GitHub Actions)

Workflow mới, đặt tên theo thư mục project; chỉ chạy khi thư mục đó hoặc chính file workflow thay đổi.
- **Môi trường:** container `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`; `actions/checkout@v4` với `fetch-depth: 0` (phiên bản lấy từ số commit).
- **Các bước:**
  1. `git config --global --add safe.directory '*'`
  2. `pushd $IDF_PATH && . ./export.sh && popd`
  3. `pushd $ESP_MATTER_PATH && . ./export.sh && popd`
  4. `idf.py set-target esp32c3 && idf.py build`
  5. `idf.py merge-bin -o matter_presence_c3_wifi_merged.bin`
  6. chép `build/matter_presence_c3_wifi-ota.bin` thành `build/matter_presence_c3_wifi.ota`
- **Artifact:** app `.bin`, `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin`, `flash_args`, file merged, file `.ota`.

## 20. Kiểm thử

**Host test** (g++ trên máy tính):
- Đặt trong `test/host/` của project, có một script chạy tất cả, và commit vào repo.
- Mock ESP-IDF/FreeRTOS vừa đủ dùng; dùng một bộ giả lập radar trả lời lệnh.
- Bộ test cũ (10 bộ) chỉ nằm ở thư mục tạm của phiên làm việc trước, không có trong repo.

| Phần | Cần kiểm tra |
|---|---|
| Parser | khung hợp lệ, khung lỗi (thiếu byte, sai đuôi, LEN > 64), dò lại header, dữ liệu engineering, STATE 4–6 không phải có người |
| Lệnh | ACK đúng, ACK trễ của lệnh trước, từ chối, timeout, thử lại config_begin/end 3 lần, 0x00A3 không có ACK |
| Phiên thông số | đổi độ phân giải hoặc Bluetooth kèm khởi động lại; ghi lại giá trị cũ khi lỗi; radar cố định 0,75 m; radar không có Bluetooth; Range ghi rồi đọc lại; mode 100 |
| Học trên ESP32 | phân vị (gồm 100 %), giới hạn dưới/trên, gate 0–1, huỷ, quá ít khung, đọc lại không khớp |
| Radar tự đo | STATE 4 → 5; STATE 6; không chạy trong 15 s; quá thời gian; huỷ giữa chừng (khởi động lại radar, chờ 1 s); từ chối → chuyển sang học trên ESP32 |
| Điều phối | lựa chọn cuối cùng thắng (Learning/Off/Factory theo mọi thứ tự, kể cả lúc radar đang tự đo); bấm 3 lần khi đang học; task radar không chạy |
| Nút | 2, 3, 4, 5+ lần bấm; giữ 5 s; chống dội; lần giữ dài huỷ chuỗi bấm |
| LED | mọi mẫu nháy và thứ tự ưu tiên; đặt lại kết quả tính lại 10 s; LED tích cực mức thấp |
| Danh tính | serial từ MAC 6 byte, tên `Presence-` + 4 ký tự cuối, prefix rỗng |

**Kiểm tra bằng build:**
- 0 cảnh báo;
- các giá trị `sdkconfig` ở mục 15;
- kích thước so với 1.0.132;
- `git diff` không đụng `ESP32-H2/`.

**Cần thử trên bo thật** (ghi lại kết quả vào README, mục "Mức độ kiểm tra"):
1. Log khởi động đúng (mục 17); radar được đọc (`LD2410 settings: ...`).
2. Ghép nối qua BLE + Wi-Fi với Apple Home và Home Assistant; thêm controller thứ hai bằng bấm đúp (chỉ DNS-SD).
3. `BLE deinitialized ...`, `BLE released: Wi-Fi power save off`, `id_diag` báo `power_save=none`; ít `Msg Retransmission` hơn bản 1.0.131.
4. Hiện diện:
   - bước vào thì báo có người ngay;
   - ra khỏi phòng thì báo không có người sau no-one duration + HoldTime;
   - ngồi im vẫn giữ có người;
   - đổi HoldTime từ HA có tác dụng và giữ sau khi khởi động lại.
5. Rút dây TX của radar: sau 10 s có `No data ...` và báo không có người; cắm lại thì có `Radar data received`.
6. Range, Radar resolution, Radar Bluetooth từ HA: radar khởi động lại, giá trị đọc lại đúng, giữ sau khi rút điện. Bluetooth mặc định tắt.
7. Học phòng trống (bấm 3 lần và từ HA), huỷ bằng Off, Factory thresholds (bấm 4 lần và từ HA, kể cả lúc đang học), đúng mẫu LED.
8. Factory reset (giữ 5 s), rồi ghép nối lại. Xoá thiết bị khỏi mọi app: cửa sổ ghép nối tự mở lại 5 phút.
9. **OTA từ 1.0.132 lên bản mới:** giữ ghép nối, endpoint và entity; sau đó OTA tiếp lên một bản mới hơn.
10. Log `mem` lúc ghép nối: không có `Heap low`; mục tiêu là heap thấp nhất không thấp hơn 1.0.131 (40,0 KB).
11. Chạy 24 h: không khởi động lại bất thường (không `reset=BROWNOUT`, `PANIC`, `WATCHDOG`), subscription ổn định (không có `SUB terminated` lặp lại).

## 21. Bỏ đi so với bản cũ (chức năng thừa trên C3)

- **Mọi thứ của ESP32-H2/Thread:**
  - cấu hình OpenThread, `thread_diag`, phần OpenThread của `id_diag`, OT CLI;
  - `board_esp32h2.h`, cơ chế chọn board theo target;
  - `presence.cmake` với logic dùng chung thư mục giữa hai project.
- **LED:** WS2812 và `espressif/led_strip` (quyết định 4).
- **Biến thể build:** `sdkconfig.defaults.factory` (quyết định 5).
- **Mã chết:**
  - `identity_diag_request_dump()`: khai báo và định nghĩa nhưng không nơi nào gọi;
  - trường `duration_s` của yêu cầu thông số radar: luôn là "giữ nguyên" từ khi bỏ entity Duration. Bản mới không có đường ghi no-one duration; lệnh 0x0060 luôn ghi lại đúng giá trị radar đang giữ.
- **Đã bỏ từ trước, không đưa lại:**
  - entity Duration;
  - đặt mật khẩu Bluetooth của radar;
  - giới hạn công suất phát Wi-Fi (`PRESENCE_WIFI_MAX_TX_POWER`);
  - log `tx_power` trong `id_diag`.
- **Giữ, nhưng chỉ build khi bật:** phần đăng ký lệnh console Matter (`#if CONFIG_ENABLE_CHIP_SHELL`).

## 22. Ràng buộc của người dùng (nguyên văn, từ các yêu cầu trước)

**Phần cứng và chân:**
- "Không được tự đoán sơ đồ chân. Nếu repository không có pinout hoặc cấu hình board, hãy hỏi tôi cung cấp sơ đồ hoặc danh sách GPIO trước khi thay đổi phần cứng."
- "Không tự ý thay đổi GPIO, pin mapping, giao thức, cấu trúc dữ liệu hoặc hành vi thiết bị."
- "Thêm log ngắn gọn để xác nhận target, cấu hình board và GPIO đang sử dụng khi khởi động."

**Phạm vi:**
- "Không làm thay đổi hành vi của phiên bản ESP32-H2 hiện tại."
- "Không sửa hoặc xóa các thay đổi không liên quan đang có trong repository."

**Thuật toán và radar:**
- "Giữ nguyên thuật toán phát hiện hiện diện, lọc nhiễu, ngưỡng, timeout và trạng thái cảm biến, trừ khi có lý do kỹ thuật rõ ràng."
- "Không được khẳng định LD2410 hỗ trợ chức năng học phòng trống nếu không tìm thấy bằng chứng kỹ thuật."
- "Không tự động ghi ngưỡng mới liên tục vào flash của radar."
- "Không bịa đặt chức năng phần cứng."
- "Không nhầm dữ liệu UART đã xử lý với tín hiệu radar thô."
- "Ưu tiên nhận diện chính xác người ngồi im, nhưng không chấp nhận giữ hiện diện giả vô thời hạn."
- "Không đưa ra phần trăm cải thiện độ chính xác khi chưa có dữ liệu thử nghiệm."

**Matter, tương thích, bảo mật:**
- "Không tự ý thay đổi Matter Vendor ID, Product ID, Device Type, Endpoint ID, Cluster ID hoặc định dạng dữ liệu lưu trữ."
- "Không làm mất khả năng tương thích với thiết bị đã được commissioning."
- "Giữ nguyên khả năng OTA."
- "Không tự ý nâng cấp ESP-IDF, ESP-Matter hoặc dependency."
- "Không thay đổi các khóa, chứng chỉ và thông tin bảo mật đang hoạt động."
- "Không in DAC private key, operational private key, setup PIN hoặc các bí mật khác vào log production."

**Cách làm và báo cáo:**
- "Không sử dụng API không tồn tại."
- "Trước thay đổi có rủi ro, giải thích ảnh hưởng và phương án rollback."
- "Không tuyên bố build thành công khi chưa thực sự build."
- "Nếu không có phần cứng để chạy thử, hãy nói rõ phần nào mới chỉ được xác nhận bằng build và phần nào cần thử trên thiết bị thật."
- "Phân biệt rõ kết luận đã kiểm chứng và giả thuyết nghiên cứu."

**Lưu ý về "viết lại":** yêu cầu cũ "Không viết lại toàn bộ dự án nếu có thể cải tiến an toàn từng phần" được thay bằng yêu cầu viết lại lần này. Các ràng buộc còn lại vẫn giữ.

**Các lựa chọn người dùng đã chốt:**
- **Phần cứng:** ESP32-C3 SuperMini; LD2410C ở GPIO3/GPIO4; LED trên board (GPIO8).
- **Mạng, SDK, PID:** Matter over Wi-Fi; giữ ESP-IDF v6.0.2 + esp-matter 1.6.1; PID riêng 0x8001.
- **Danh tính:**
  - Manufacturer `Haxeko`, Model `Presence LD2410`;
  - serial là địa chỉ MAC;
  - tên `Presence-` + 4 ký tự cuối của serial.
- **Entity và mặc định:**
  - entity xếp Hold time, Learn empty room, Radar Bluetooth, Radar resolution, Range;
  - Radar Bluetooth mặc định tắt; độ phân giải mặc định 0,75 m; Range mặc định 6 m.
- **Nút và LED:**
  - sau khi ghép nối LED tắt; bấm đúp mở chế độ ghép nối; giữ 5 s để reset; reset xong LED nháy chờ ghép nối;
  - bấm 3 lần học phòng trống: cyan, rồi xanh lá khi xong, đỏ khi thất bại;
  - bấm 4 lần ghi ngưỡng mặc định, LED nháy tím.
- **Học phòng trống:** dùng mức 95 % trên ESP32 và lệnh 0x000B của radar.

## 23. Bàn giao

1. **Kế hoạch trước khi code** (mục 2).
2. **Project mới:**
   - mã nguồn, `sdkconfig.defaults`, `partitions.csv`, workflow CI, `test/host/`;
   - README tiếng Việt cùng các mục với README C3 hiện tại: khác biệt, phần cứng, LED, yêu cầu mạng, ghép nối, build, OTA, cấu hình, Wi-Fi và BLE, RAM và flash, log, xử lý sự cố, mức độ kiểm tra, cấu trúc thư mục.
3. **Kết quả build:**
   - lệnh đã chạy và số cảnh báo;
   - `idf.py size` so với 1.0.132;
   - các giá trị `sdkconfig` đã kiểm tra.
4. **Kết quả host test.**
5. **Danh sách khác biệt so với bản cũ:** bỏ gì, vì sao; tương thích OTA và ghép nối.
6. **Bảng mức kiểm chứng:** "đã kiểm chứng bằng build/test" và "cần thử trên bo thật".
7. **Rủi ro** và các điểm cần người dùng xác nhận.
