# ESP32-H2 Mini: cảm biến cửa Matter over Thread, nguồn USB

Firmware ESP-IDF + ESP-Matter chính thức cho board **ESP32-H2 Mini** (ESP32-H2, flash 4 MB): một **Contact Sensor** chuẩn Matter (device type `0x0015`) chạy **Matter over Thread**, đọc một công tắc từ (reed switch) nối giữa một GPIO và GND. Thiết bị được cấp nguồn liên tục qua cổng USB nên là **Thread Full Thread Device luôn thức** (receiver luôn bật, có thể làm router), không có chế độ ngủ, không có phần pin.

- Endpoint 0: Root Node. Endpoint 1: Contact Sensor (cluster Identify + Boolean State, event StateChange).
- Commission qua **BLE** (điện thoại/controller gửi Thread Operational Dataset), sau đó chỉ chạy trên **Thread** qua một Thread Border Router.
- Đổi trạng thái cửa → báo cáo ngay cho controller đã subscribe (ngay khi min interval của subscription cho phép), không cần polling.
- Mất điện / rút USB / mất Border Router: tự vào lại mạng Thread với fabric và Thread credentials lưu trong flash, đọc lại trạng thái cửa thật lúc khởi động.

Mục lục: [1. Phiên bản SDK](#1-phiên-bản-sdk-và-nguồn-tương-thích) · [2. Phần cứng, đấu dây](#2-phần-cứng-nguồn-usb-và-đấu-dây) · [3. Cấu trúc dự án](#3-cấu-trúc-dự-án) · [4. Cài đặt, build, nạp](#4-cài-đặt-môi-trường-build-và-nạp) · [5. Commission](#5-commission) · [6. Kế hoạch kiểm thử](#6-kế-hoạch-kiểm-thử) · [7. Luồng cập nhật và phục hồi](#7-luồng-cập-nhật-trạng-thái-và-luồng-phục-hồi-kết-nối) · [8. Kconfig Thread/BLE](#8-giải-thích-kconfig-thread-ble-và-nguồn-usb) · [9. Thread không attach](#9-chẩn-đoán-khi-thread-không-attach) · [10. Sản xuất hàng loạt](#10-trước-khi-sản-xuất-hàng-loạt) · [11. Đã/chưa xác minh](#11-những-gì-đã-và-chưa-được-xác-minh)

---

## 1. Phiên bản SDK và nguồn tương thích

| Thành phần | Phiên bản dùng | Nguồn quyết định |
|---|---|---|
| ESP-Matter | nhánh `release/v1.6.1`, commit **`cb3f045958ca6b68f6a440eb513d955531ca1b90`** (27/10/2026) | README của ESP-Matter (bảng *Supported Matter specification versions*: v1.6.1 → `release/v1.6.1`). ESP-Matter không phát hành tag, nên dự án pin theo commit |
| ESP-IDF | tag **`v6.0.2`** (commit `7101770dc6db`) | README `release/v1.6.1`: *"it is recommended to utilize ESP-IDF v6.0.2"*; `docs/en/developing.rst` cùng nhánh |
| connectedhomeip | commit **`b1902b39ac6734913ce5f4d2abf153693a50264b`** (submodule của esp-matter) | README `release/v1.6.1`: *"works with commit b1902b39ac of connectedhomeip"* |
| Toolchain | `riscv32-esp-elf-gcc` 15.2.0 (esp-15.2.0_20251204), C++ `-std=gnu++17` | do ESP-IDF v6.0.2 cài; chuẩn C++ đặt như example của esp-matter |
| Docker image (tuỳ chọn) | `espressif/esp-matter:release-v1.6.1_idf_v6.0.2` | image chính thức; đúng 3 commit trên (đã kiểm tra trong image) |

Không dùng `main` (Matter 1.7, đang phát triển). Không trộn API của nhánh khác: mọi hàm, kiểu, Kconfig trong dự án đều đã đối chiếu với source của 3 commit trên.

**Example gốc.** ESP-Matter `release/v1.6.1` **không có example `contact_sensor`**. Example chính thức gần nhất là `examples/sensors` (cảm biến GPIO/I2C, có `sdkconfig.defaults.esp32h2`); dự án lấy từ đó cấu trúc thư mục, `CMakeLists.txt`, `partitions.csv` (giống hệt), cách tạo node/endpoint, cấu hình OpenThread và `esp_matter::start()`. API `endpoint::contact_sensor::create()` có sẵn trong data model của SDK (`components/esp_matter/data_model/legacy/esp_matter_endpoint.cpp`). Những chỗ đã sửa so với example:

| Example `sensors` | Dự án này | Lý do |
|---|---|---|
| ISR của PIR gọi thẳng callback → `ScheduleLambda` | ISR chỉ `vTaskNotifyGiveFromISR`; task debounce; `PlatformMgr().ScheduleWork()` | không gọi Matter API, không xử lý trong ISR; có debounce |
| `attribute::update()` cho giá trị cảm biến | `BooleanState::FindClusterOnEndpoint(ep)->SetStateValue()` | trong 1.6.1 `StateValue` là thuộc tính `ATTRIBUTE_FLAG_MANAGED_INTERNALLY` do `BooleanStateCluster` giữ; `attribute::update()` → `set_val()` trả `ESP_ERR_NOT_SUPPORTED` cho thuộc tính không ghi được này (`esp_matter_data_model.cpp`, *"Internally managed attributes are set using cluster specific APIs"*). Cách dùng cluster này giống example `contact-sensor-app` của connectedhomeip |
| Nút reset qua `esp_bsp_generic` + `app_reset` (`examples/common`) | `reset_button.cpp` riêng | chân và thời gian giữ cấu hình bằng menuconfig; chống kẹt nút lúc khởi động |
| `sdkconfig.defaults` + `sdkconfig.defaults.esp32h2` | một `sdkconfig.defaults` | yêu cầu chỉ một file, cho chạy liên tục qua USB |
| Thread device type theo mặc định | `CONFIG_OPENTHREAD_FTD=y` ghi rõ, `CONFIG_PM_ENABLE=n`, `CONFIG_ENABLE_ICD_SERVER=n` | luôn thức, không SED (mục 8) |
| PID `0x8000` | PID `0x8015` | tách khỏi các firmware khác trong repo; vẫn nằm trong dải test DAC/CD của SDK |

---

## 2. Phần cứng, nguồn USB và đấu dây

### Board

"ESP32-H2 Mini" ở đây là board nhỏ dùng chip ESP32-H2 có **flash 4 MB trong package** (ví dụ ESP32-H2 SuperMini, board dùng module ESP32-H2-MINI-1 bản 4 MB). Các board khác nhau về chân ra, nút và LED, nên **mọi chân đều chọn trong menuconfig**; giá trị mặc định chỉ là đề xuất.

- Kiểm tra flash trước: `esptool --chip esp32h2 -p <PORT> flash-id` (esptool v4: `flash_id`), dòng `Detected flash size: 4MB`. Board 2 MB không chứa được bảng phân vùng này.
- Không dùng được: GPIO6–7, GPIO15–21 (flash trong package, không ra chân), GPIO26–27 (USB Serial/JTAG: nạp và log). Build dừng với lỗi nếu chọn các chân này hoặc chọn trùng chân (`static_assert` trong `main/app_main.cpp`).
- Chân strapping: GPIO2, GPIO3, GPIO8, GPIO9, GPIO25 (tài liệu GPIO của ESP-IDF cho ESP32-H2). Tránh dùng cho reed switch: tiếp điểm đóng lúc cấp điện có thể đổi chế độ boot.

### Nguồn USB (thiết kế duy nhất của firmware)

```
  Bộ sạc / cổng USB 5 V
        │  cáp USB
        ▼
  ┌───────────── ESP32-H2 Mini ─────────────┐
  │ cổng USB ── VBUS 5 V ──► LDO trên board ─┼──► 3V3 ──► ESP32-H2
  │                │                         │
  │           chân 5V/VBUS                chân 3V3
  └─────────────────────────────────────────┘
```

- **Cấp nguồn qua cổng USB** (USB-C/micro-USB của board): 5 V từ VBUS qua mạch ổn áp (LDO) trên board thành 3,3 V cho ESP32-H2. Đây là cách cấp nguồn duy nhất mà firmware và tài liệu này giả định. Một bộ sạc USB 5 V thông thường là đủ; dùng cáp tốt, ngắn.
- **Chân 5V/VBUS**: nối trực tiếp với VBUS của cổng USB. Khi đã cắm USB, chân này *có* 5 V (đầu ra). Không cấp thêm nguồn 5 V vào chân này khi đang cắm USB, trừ khi sơ đồ board có diode chống dòng ngược. Reed switch không cần nguồn, nên không cần dùng chân 5V.
- **Chân 3V3**: đầu ra của LDO. **Không bao giờ đưa 5 V vào chân 3V3** (ESP32-H2 chỉ chịu tối đa khoảng 3,6 V: chip sẽ hỏng). Chỉ cấp 3,3 V ổn áp vào chân này nếu không cắm USB và biết rõ sơ đồ board.
- Mọi GPIO là mức 3,3 V, không chịu 5 V.
- Firmware không đo pin, không có cluster Power Source/Battery, không có chế độ tiết kiệm pin.

### Sơ đồ đấu dây

```
                     ESP32-H2 Mini
                  ┌─────────────────┐
     USB 5 V ═════╡ USB             │
                  │                 │
                  │           GPIO4 ├────────┐
                  │                 │        ╪  reed switch thường hở (NO)
                  │             GND ├────────┘  nam châm gần = tiếp điểm đóng
                  │                 │
                  │    GPIO9 (BOOT) ├───[ nút ]─── GND   (nút BOOT có sẵn trên board)
                  │                 │
   (tuỳ chọn)     │    GPIO<LED>    ├───[ 330 Ω ]───►|─── GND   LED, STATUS_LED_ACTIVE_HIGH=y
                  └─────────────────┘
```

| Chức năng | Mặc định | Nối | Kconfig |
|---|---|---|---|
| Reed switch | GPIO4 | GPIO ↔ reed ↔ GND; pull-up nội bật sẵn | `CONTACT_SENSOR_GPIO`, `CONTACT_ACTIVE_LOW`, `CONTACT_DEBOUNCE_MS` |
| Nút factory reset | GPIO9 (BOOT trên nhiều board) | GPIO ↔ nút ↔ GND; pull-up nội | `FACTORY_RESET_GPIO`, `FACTORY_RESET_HOLD_MS` |
| LED trạng thái | tắt | GPIO → 330 Ω → LED → GND (sáng mức cao), hoặc 3V3 → LED → 330 Ω → GPIO (sáng mức thấp) | `STATUS_LED_ENABLE`, `STATUS_LED_GPIO`, `STATUS_LED_ACTIVE_HIGH`, `STATUS_LED_SHOW_CONTACT` |

- **Reed switch**: gắn reed switch lên khung cửa, nam châm lên cánh cửa, khoảng cách khi đóng theo datasheet reed (thường dưới 10–15 mm). Mặc định (`CONTACT_ACTIVE_LOW=y`, reed thường hở): **GPIO mức thấp = cửa đóng, mức cao = cửa mở**. Reed thường đóng (NC) hoặc module có ngõ ra đảo: tắt `CONTACT_ACTIVE_LOW`.
- **Dây dài** (trên khoảng 1–2 m, hoặc gần dây điện lưới): thêm điện trở kéo lên ngoài 4,7–10 kΩ lên 3V3 và tụ 100 nF từ GPIO xuống GND; cân nhắc điện trở 1 kΩ nối tiếp và diode TVS chống ESD. Pull-up nội của ESP32-H2 khá yếu (vài chục kΩ).
- **GPIO9 = BOOT**: giữ nút trong lúc board khởi động/reset sẽ vào chế độ nạp của ROM, không phải factory reset. Chỉ bấm giữ khi firmware đã chạy.
- **LED**: không phải board nào cũng có LED đơn sắc ở cùng một chân; LED RGB địa chỉ (loại WS2812, thường ở GPIO8) không điều khiển được như LED GPIO. Khi đó gắn LED ngoài vào một chân trống. Mẫu nháy: sáng liên tục = đang factory reset; nháy nhanh (100 ms) = đang giữ nút reset; nháy vừa (200 ms) = Identify; nháy chậm (500 ms) = cửa sổ commissioning đang mở; còn lại tắt (hoặc sáng khi cửa mở nếu bật `STATUS_LED_SHOW_CONTACT`).

---

## 3. Cấu trúc dự án

```
ESP32-H2/matter-thread/esp32h2_matter_contact_sensor/
├── CMakeLists.txt              # boilerplate của esp-matter examples/sensors, chỉ build cho esp32h2
├── README.md
├── partitions.csv              # giống hệt examples/sensors (cần: app Matter > 1 MB của bảng mặc định)
├── sdkconfig.defaults          # một file duy nhất: ESP32-H2, Thread FTD luôn thức, BLE commissioning
├── .gitignore
└── main/
    ├── CMakeLists.txt
    ├── Kconfig.projbuild       # chân, mức active, debounce, nút reset, LED, in mã ghép nối
    ├── app_main.cpp            # node Matter, endpoint Contact Sensor, event, đồng bộ StateValue, factory reset
    ├── contact_sensor_driver.h # driver reed switch: GPIO → ISR → debounce → trạng thái ổn định (không biết Matter)
    ├── contact_sensor_driver.cpp
    ├── reset_button.h          # nút factory reset: ISR → task đo thời gian giữ
    ├── reset_button.cpp
    ├── status_led.h            # LED trạng thái tuỳ chọn (thêm so với cây yêu cầu)
    └── status_led.cpp
```

Điều chỉnh so với cây thư mục yêu cầu: thêm `status_led.h/.cpp` để LED (tuỳ chọn) tách khỏi logic Matter và hai driver; thêm `.gitignore`. Không có `main/idf_component.yml`: example gốc chỉ cần file này cho `esp_bsp_generic` (nút), dự án này dùng driver nút riêng. Các component phụ thuộc của esp-matter (`cjson`, `esp_secure_cert_mgr`, `mdns`...) do Component Manager tự tải lúc build đầu tiên (cần Internet).

---

## 4. Cài đặt môi trường, build và nạp

### Cách A: cài từ máy sạch (Ubuntu 22.04/24.04)

```bash
# Gói hệ thống cho ESP-IDF và Matter (Prerequisites của ESP-IDF và connectedhomeip)
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv python3-dev cmake ninja-build \
    ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 unzip pkg-config libglib2.0-dev libdbus-1-dev \
    libavahi-client-dev libgirepository1.0-dev libcairo2-dev libreadline-dev

mkdir -p ~/esp && cd ~/esp

# 1) ESP-IDF v6.0.2 kèm submodule
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v6.0.2
git submodule update --init --recursive
./install.sh esp32h2
source ./export.sh
cd ..

# 2) ESP-Matter release/v1.6.1, pin đúng commit đã kiểm thử, kèm submodule connectedhomeip
git clone --depth 1 -b release/v1.6.1 https://github.com/espressif/esp-matter.git
cd esp-matter
git fetch --depth 1 origin cb3f045958ca6b68f6a440eb513d955531ca1b90
git checkout cb3f045958ca6b68f6a440eb513d955531ca1b90
git submodule update --init --depth 1
cd connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 linux --shallow
git log -1 --format=%H       # phải là b1902b39ac6734913ce5f4d2abf153693a50264b
cd ../..
./install.sh                 # cài Python deps, build chip-tool/chip-cert (host tools); --no-host-tool để bỏ qua
source ./export.sh
cd ..
```

Mỗi terminal mới:

```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
```

### Cách B: Docker (đúng môi trường đã build thử, giống CI của repo)

```bash
cd ESP32-H2/matter-thread/esp32h2_matter_contact_sensor
docker run --rm -v "$PWD":/project -w /project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 bash -c \
  'git config --global --add safe.directory "*"; . $IDF_PATH/export.sh; . $ESP_MATTER_PATH/export.sh;
   idf.py set-target esp32h2 && idf.py build'
```

Nạp từ máy host (không cần ESP-IDF, chỉ cần `pip install esptool`): `cd build && python -m esptool --chip esp32h2 -p <PORT> -b 460800 write-flash @flash_args` (esptool v4: `write_flash`).

### Cấu hình, build, nạp

```bash
cd ESP32-H2/matter-thread/esp32h2_matter_contact_sensor
idf.py set-target esp32h2
idf.py menuconfig        # "Contact sensor (reed switch)", "Factory reset button", "Status LED"
idf.py build
idf.py -p <PORT> erase-flash      # chỉ lần đầu, hoặc khi board từng chạy firmware khác (xoá NVS cũ)
idf.py -p <PORT> flash monitor    # thoát monitor: Ctrl+]
```

- `<PORT>`: board chỉ có USB native (USB Serial/JTAG) → Linux `/dev/ttyACM0`, macOS `/dev/cu.usbmodem*`, Windows `COMx`. Board có chip USB-UART → `/dev/ttyUSB0`.
- Log ra cả UART0 và USB Serial/JTAG (mặc định của ESP-IDF: UART0 chính + USB Serial/JTAG phụ), nên board nào cũng xem được log.
- Không nạp được (USB không nhận khi firmware lỗi): giữ BOOT, bấm RESET, nhả BOOT → chế độ nạp, rồi `flash` lại.
- Sau khi sửa `sdkconfig.defaults`: xoá `sdkconfig` (hoặc `idf.py set-target esp32h2`), vì `sdkconfig` cũ giữ giá trị cũ.
- Nạp lại firmware (`idf.py flash`) **không** xoá NVS: ghép nối và mạng Thread được giữ. `erase-flash` xoá hết.

Log khởi động mong đợi (chưa commission, cửa đóng):

```
I (...) reset_button: Factory reset button on GPIO9, hold 6000 ms
I (...) contact: Reed switch on GPIO4 (closed = LOW, debounce 50 ms): closed
I (...) app_main: Door closed at start-up (GPIO4 level 0 -> StateValue true)
I (...) app_main: Contact sensor on endpoint 1
...
I (...) app_main: Not commissioned: waiting for a controller (BLE)
I (...) chip[SVR]: SetupQRCode: [MT:...]
I (...) chip[SVR]: Copy/paste the below URL in a browser to see the QR Code:
I (...) chip[SVR]: https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT%3A...
I (...) chip[SVR]: Manual pairing code: [34970112332]
I (...) app_main: Matter: door closed, StateValue = true
```

---

## 5. Commission

**Điều kiện:** một **Thread Border Router** cùng mạng LAN với controller và điện thoại (HomePod mini / HomePod 2 / Apple TV 4K có Thread; Google Nest Hub 2 / Nest Hub Max / Nest Wifi Pro; Home Assistant với OpenThread Border Router; hoặc OTBR trên Raspberry Pi). Điện thoại cần Bluetooth. Thiết bị phải ở gần (BLE) trong lúc ghép.

Mã ghép nối development (test, **chỉ để phát triển**): passcode `20202021`, discriminator `3840`, manual code `34970112332`, QR trong log (`SetupQRCode: [MT:...]`, mở URL in kèm để hiện mã QR). Cửa sổ commissioning BLE mở 15 phút sau khi cấp điện (`CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS=900`); hết giờ thì rút/cắm lại USB.

- **Apple Home**: Home → + → Thêm phụ kiện → quét QR hoặc *Thêm tùy chọn* → nhập mã 11 số. Thiết bị test hiện cảnh báo *Phụ kiện chưa được chứng nhận* → *Vẫn thêm*. Hiện là cảm biến tiếp xúc (Mở/Đóng).
- **Google Home**: thiết bị test (VID `0xFFF1`) cần tạo integration Matter cho VID `0xFFF1` / PID `0x8015` trong Google Home Developer Console trước; sau đó Google Home → Thêm → Thiết bị Matter.
- **Home Assistant**: cần integration Matter (Matter Server) và integration Thread có Border Router; ứng dụng Home Assistant Companion trên điện thoại phải có Thread credentials của mạng (trên iOS/Android đồng bộ từ trang integration Thread). Settings → Devices & services → Add device → Add Matter device → quét QR. Entity `binary_sensor` loại *door*: *Open*/*Closed* (Home Assistant tự đảo `StateValue`).
- **chip-tool** (Linux có Bluetooth, cùng LAN với OTBR):

```bash
# Dataset của mạng Thread, lấy trên OTBR
sudo ot-ctl dataset active -x
# (OTBR trong Docker: sudo docker exec -it otbr sh -c "sudo ot-ctl dataset active -x")

# Commission qua BLE, đưa dataset Thread vào thiết bị: node id 1
chip-tool pairing ble-thread 1 hex:<DATASET_HEX> 20202021 3840

# Đọc trạng thái: true = đóng (có tiếp xúc), false = mở
chip-tool booleanstate read state-value 1 1
# Event StateChange đã ghi
chip-tool booleanstate read-event state-change 1 1
# Device type của endpoint 1: 21 (0x0015 Contact Sensor)
chip-tool descriptor read device-type-list 1 1
# StateValue chỉ đọc: lệnh ghi phải bị từ chối (UNSUPPORTED_WRITE)
chip-tool booleanstate write-by-id 0 true 1 1

# Báo cáo tức thì khi đóng/mở cửa
chip-tool interactive start
>>> booleanstate subscribe state-value 0 300 1 1
>>> booleanstate subscribe-event state-change 0 300 1 1
```

Máy chạy chip-tool phải có route IPv6 tới mạng Thread (OTBR quảng bá qua Router Advertisement; trên Linux có thể cần `sudo sysctl -w net.ipv6.conf.<iface>.accept_ra_rt_info_max_plen=64`).

**Factory reset rồi commission lại:** giữ nút (GPIO9) 6 giây khi firmware đang chạy → LED (nếu có) nháy nhanh rồi sáng liên tục → log `Factory reset: removing all Matter fabrics and the Thread network, then restarting` → khởi động lại, mở lại cửa sổ BLE và in mã ghép nối. Xoá thiết bị cũ khỏi controller (nó không còn trả lời), rồi thêm lại như trên. Giữ dưới 6 giây rồi nhả: không có gì xảy ra (`Button released after ... no factory reset`).

**Thêm controller thứ hai (multi-admin):** mở chế độ ghép từ controller thứ nhất (Apple Home: *Bật chế độ ghép nối*; Home Assistant: *Share device*; chip-tool: `chip-tool pairing open-commissioning-window 1 1 300 1000 3840`), controller thứ hai ghép qua mạng bằng mã mới. Thiết bị không tự mở cửa sổ ghép khi đã có fabric.

---

## 6. Kế hoạch kiểm thử

Chuẩn bị: board đã nạp, monitor log đang mở, một controller (chip-tool đang subscribe `state-value` và `state-change`, hoặc Home Assistant/Apple Home).

| # | Kịch bản | Thao tác | Kết quả mong đợi |
|---|---|---|---|
| 1 | Boot khi cửa đóng | Nam châm sát reed, cấp điện | `contact: ... : closed`; `Door closed at start-up (GPIO4 level 0 -> StateValue true)`; `Matter: door closed, StateValue = true`. Controller: *Đóng* (`state-value` = true) |
| 2 | Boot khi cửa mở | Nam châm xa, cấp điện | `... : open`; `Door open at start-up (GPIO4 level 1 -> StateValue false)`. Không có dòng `Matter: ...` vì cluster khởi tạo là false, đúng giá trị: không ghi lại. Controller: *Mở* (false) |
| 3 | Rung tiếp điểm, đóng/mở nhanh | Gõ nhẹ cửa; lướt nam châm qua reed; đóng/mở liên tục | Xung ngắn hơn 50 ms: không log, không báo cáo. Mỗi thay đổi ổn định: đúng **một** dòng `contact: Contact ...` và **một** `Matter: door ...`; controller nhận đúng một report/event mỗi lần. Đổi nhanh hơn debounce: gộp lại, trạng thái cuối luôn đúng với GPIO. Rung liên tục > 2 s: `still bouncing after 2000 ms, reading it anyway` |
| 4 | Đổi trạng thái khi chưa commission | Chưa ghép, đóng/mở cửa | Log `Contact ...` và `Matter: door ...` vẫn chạy (cluster có sẵn trước khi có fabric). Sau khi commission, lần đọc đầu tiên trả đúng trạng thái hiện tại |
| 5 | Commission qua Border Router | Mục 5 | `Commissioning session started` → `Commissioning complete (fabric index 1)` → `Thread attached` → `Thread role: child` (có thể sau đó `router`) → `BLE deinitialized and memory reclaimed`. Controller hiện Contact Sensor đúng trạng thái |
| 6 | Mất Border Router rồi có lại | Tắt nguồn BR 5–10 phút, đổi trạng thái cửa trong lúc đó, bật BR lại | Khi mất: `Thread detached: re-attaching with the stored network` hoặc `Thread role: detached`; một FTD có thể tự thành `leader` của partition riêng (bình thường). **Không** có `Commissioning window opened`, không xoá gì. Khi BR có lại: vào lại mạng (`Thread role: child`/`router`), controller subscribe lại (vài giây tới vài phút tuỳ controller) và hiện trạng thái **hiện tại** |
| 7 | Reboot, mất điện | Bấm RESET, hoặc rút USB 10 s | `Commissioned (1 fabric(s)), Thread credentials restored from flash`; không bật BLE, không in mã ghép; `Thread attached` sau vài giây; controller hiện lại thiết bị không cần ghép lại |
| 8 | Rút USB, đổi cửa, cắm lại | Rút USB, mở cửa (trước đó đóng), cắm lại | Như #7, cộng `Door open at start-up ...`; controller hiện *Mở* sau khi kết nối lại (giá trị đọc từ GPIO, không lấy giá trị cũ) |
| 9 | Giữ nút factory reset | Giữ GPIO9 3 s rồi nhả; sau đó giữ 6 s | 3 s: `Button released after ~3000 ms: no factory reset`. 6 s: `Button held for 6000 ms` → `Factory reset ...` → khởi động lại → `Not commissioned` + mã ghép. Controller: thiết bị không phản hồi; xoá rồi ghép lại. Board khởi động trong lúc nút đang bị giữ (ví dụ nút kẹt trên chân không phải strapping): `pressed at start-up: release it to arm`, không reset |
| 10 | Xoá thiết bị từ controller rồi ghép lại | Apple Home: *Xoá phụ kiện* (HA: *Remove device*; chip-tool: `pairing unpair 1`) | `Fabric removed (0 left)` → `Commissioning window open for 300 s` + mã ghép (on-network, BLE đã tắt). Ghép lại trong 5 phút qua mạng Thread; sau 5 phút: rút/cắm USB → cửa sổ BLE 15 phút (không còn fabric nên BLE được bật lại). Còn fabric khác (multi-admin): chỉ log, không mở cửa sổ |
| 11 | Hai thiết bị, cùng firmware, factory data riêng | Mục 10: mỗi bo một file `fctry` riêng, build bản factory, ghép cả hai vào cùng controller | Hai mã QR/discriminator khác nhau; hai DAC khác nhau (CN = uuid); cả hai ghép được vào cùng fabric, node id khác nhau; mở cửa ở bo A chỉ đổi trạng thái của A. `chip-tool basicinformation read serial-number <node> 0` khác nhau |

Thêm: `chip-tool generaldiagnostics read reboot-count 1 0` tăng sau mỗi lần mất điện; `boot-reason` phân biệt mất điện, reset nút, watchdog.

---

## 7. Luồng cập nhật trạng thái và luồng phục hồi kết nối

### Đường đi dữ liệu

```
reed switch ──► GPIO4 (pull-up, ngắt cả hai cạnh)
            ──► contact_gpio_isr(): chỉ vTaskNotifyGiveFromISR()
            ──► task "contact" (prio 4): chờ đường dây im lặng CONTACT_DEBOUNCE_MS sau cạnh cuối
                (mỗi cạnh mới bắt đầu lại; tối đa 2 s), đọc lại chân
            ──► reed_level_to_contact_state(): mức GPIO → CLOSED/OPEN (áp CONTACT_ACTIVE_LOW)
            ──► khác trạng thái trước? không → bỏ qua (glitch)
            ──► on_contact_change() → schedule_contact_sync():
                PlatformMgr().ScheduleWork(sync_contact_state) — không chặn; hàng đợi đầy → thử lại sau 200 ms
            ──► task Matter (đang giữ stack lock): sync_contact_state()
                contact_state_to_matter_state_value(): CLOSED → true, OPEN → false
                BooleanState::FindClusterOnEndpoint(1); chưa có → thử lại sau 200 ms
                GetStateValue() == giá trị mới? → không ghi (không lặp report/event)
                SetStateValue(): đánh dấu StateValue dirty + ghi event StateChange
            ──► Interaction Model gửi report cho mọi subscription qua Thread ──► controller
```

- **Mapping**: Device Library spec, Contact Sensor `0x0015`: `StateValue` **TRUE = đóng/có tiếp xúc, FALSE = mở/không tiếp xúc**. SDK chỉ khai báo `StateValue` là `bool` chỉ đọc (`data_model/1.5.1/clusters/BooleanState.xml`), nên ý nghĩa ghi ngay trong `contact_state_to_matter_state_value()` và `reed_level_to_matter_state()` (`main/app_main.cpp`). Home Assistant đảo giá trị này cho entity cửa (`device_to_ha=lambda x: not x`), khớp với cùng quy ước.
- **Không ghi trùng**: driver chỉ gọi callback khi trạng thái ổn định đổi; tầng Matter so với `GetStateValue()` trước khi `SetStateValue()`. Nhiều thay đổi dồn lại khi task Matter bận chỉ tạo một lần đồng bộ (cờ `s_sync_scheduled`), luôn với trạng thái mới nhất.
- **Trạng thái ban đầu**: `contact_sensor_init()` đọc chân thật (hai lần đọc giống nhau cách nhau một khoảng debounce) trước khi Matter chạy; ngay sau `esp_matter::start()` firmware lên lịch đồng bộ. Giá trị `boolean_state.state_value` trong config endpoint không được dùng vì thuộc tính do cluster quản lý (`BooleanStateCluster` khởi tạo `false`).
- **Lưới an toàn**: task contact so chân với trạng thái mỗi 30 s, phòng khi một ngắt bị mất (ESD); thay đổi bình thường luôn đi từ ngắt.
- **Ghi từ controller**: `StateValue` chỉ đọc, Interaction Model từ chối trước khi tới callback; `app_attribute_update_cb()` không điều khiển gì. Không có logic "ghi giả" trạng thái vật lý.
- **Ngữ cảnh**: ISR không gọi Matter API, không log. Task contact/nút không giữ stack lock; mọi truy cập cluster chạy trên task Matter. Đường xử lý sự kiện không cấp phát động (`ScheduleWork` mang `intptr_t`, timer tạo sẵn lúc khởi động).

### Phục hồi kết nối

| Tình huống | Ai xử lý | Hành vi |
|---|---|---|
| Thread mất parent/BR | OpenThread | Thử attach lại liên tục với dataset đã lưu; FTD có thể tạm làm leader partition riêng rồi tự gộp khi mạng có lại. Firmware chỉ log, không xoá cấu hình, không mở commissioning |
| Controller mất subscription | Controller + Matter SDK | Controller subscribe lại khi thiết bị trả lời; thiết bị đăng ký lại DNS-SD qua SRP khi có địa chỉ mới; report đầu tiên mang trạng thái hiện tại |
| Rút USB / mất điện / reboot | ESP-Matter | Fabric, khoá, ACL trong NVS (`nvs`), dataset Thread trong NVS của OpenThread; `CHIP_DEVICE_CONFIG_ENABLE_THREAD_AUTOSTART` bật lại Thread khi có dataset. BLE không bật khi đã có fabric (`USE_BLE_ONLY_FOR_COMMISSIONING`). GPIO được đọc lại |
| Treo task | Task watchdog | `CONFIG_ESP_TASK_WDT_PANIC=y`: idle bị chặn 5 s → khởi động lại, rồi như dòng trên |
| Xoá fabric cuối | Firmware (như example) | Mở cửa sổ ghép 300 s qua mạng; không xoá Thread credentials |
| Factory reset | Nút giữ 6 s | `esp_matter::factory_reset()`: xoá fabric, dataset Thread, KVS Matter, khởi động lại |

---

## 8. Giải thích Kconfig Thread, BLE và nguồn USB

Đường dẫn trong `idf.py menuconfig` (đã đối chiếu với các file Kconfig của ESP-IDF v6.0.2 / esp-matter 1.6.1 / connectedhomeip `b1902b39ac`):

| Kconfig | Giá trị | menuconfig | Ý nghĩa |
|---|---|---|---|
| `CONFIG_OPENTHREAD_ENABLED` | y | Component config → OpenThread → OpenThread | bật OpenThread trên radio 802.15.4 native |
| `CONFIG_OPENTHREAD_FTD` | y | Component config → OpenThread → Thread Core Features → Thread device type → Full Thread Device | FTD. `esp_matter_core.cpp` (`init_thread_stack_and_start_thread_task`) gọi `SetThreadDeviceType(kThreadDeviceType_Router)` → link mode `rx-on-when-idle` + full device, `otThreadSetRouterEligible(true)`: vào mạng làm child (REED), OpenThread nâng lên router khi mesh cần. Mặc định của ESP-IDF cũng là FTD |
| `CONFIG_OPENTHREAD_MTD` | n | như trên → Minimal Thread Device | chọn nếu muốn **chỉ end device** (MED): esp-matter đặt `kThreadDeviceType_MinimalEndDevice`, vẫn rx-on-when-idle, không bao giờ làm router, code nhỏ hơn |
| `CONFIG_ENABLE_ICD_SERVER` | n | Component config → CHIP Device Layer → General Options → Enable ICD server | chỉ chọn được với MTD; bật sẽ thành Sleepy End Device. Ghi `n` để vẫn tắt nếu đổi sang MTD |
| `CONFIG_OPENTHREAD_RX_ON_WHEN_IDLE` | n (mặc định) | Component config → OpenThread → Thread Core Features → Enable OpenThread radio capability rx on when idle | **khả năng của driver radio**, không phải link mode Thread; mặc định n khi bật coexistence BLE/802.15.4 (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`, Component config → Wireless Coexistence). Receiver vẫn luôn bật nhờ link mode ở trên. Giữ mặc định |
| `CONFIG_OPENTHREAD_SRP_CLIENT` | y | Component config → OpenThread → Thread Core Features | đăng ký dịch vụ Matter (DNS-SD) với SRP server của Border Router |
| `CONFIG_PM_ENABLE` | n | Component config → Power Management | không DFS, không light sleep tự động, không tickless idle. Firmware không gọi deep/light sleep |
| `CONFIG_BT_ENABLED`, `CONFIG_BT_NIMBLE_ENABLED` | y | Component config → Bluetooth | NimBLE cho CHIPoBLE (commissioning) |
| `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING` | y | Component config → CHIP Device Layer → BLE Options | tắt BLE sau khi commission xong; khi khởi động mà đã có fabric thì không bật BLE |
| `CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS` | 900 (mặc định) | Component config → CHIP Device Layer → Commissioning Window Options | thời gian cửa sổ BLE lúc khởi động khi chưa có fabric |
| `CONFIG_ENABLE_WIFI_STATION` | n | Component config → CHIP Device Layer → WiFi Station Options | không có Wi-Fi |
| `CONFIG_USE_MINIMAL_MDNS` | n | Component config → CHIP Core → General Options | DNS-SD qua SRP (Thread) |
| `CONFIG_ENABLE_OTA_REQUESTOR` | y | Component config → CHIP Core → System Options | như example: nhận firmware qua Matter OTA |
| `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT` | 2 | Component config → ESP Matter | endpoint 0 và 1 |
| `CONFIG_DEVICE_VENDOR_ID` / `CONFIG_DEVICE_PRODUCT_ID` | `0xFFF1` / `0x8015` | Component config → CHIP Device Layer → Device Identification Options | VID/PID test |

**Tự attach lại**: không cần Kconfig riêng. OpenThread lưu dataset trong NVS (`storage_partition_name = "nvs"` trong `app_main.cpp`), `CHIP_DEVICE_CONFIG_ENABLE_THREAD_AUTOSTART` (mặc định 1) bật Thread khi đã có dataset, và MLE tự attach lại khi mất parent. Với MTD còn có `CONFIG_OPENTHREAD_PARENT_SEARCH_MTD` (tìm parent tốt hơn định kỳ); FTD luôn có tính năng này.

**Vì sao FTD (router-eligible)**: thiết bị cắm điện liên tục là loại Thread khuyến nghị làm router: nó mở rộng mesh cho thiết bị khác, không phụ thuộc vào một parent duy nhất khi đã là router, và receiver luôn bật nên báo cáo không chờ chu kỳ poll như SED. Đây là cấu hình mặc định của ESP-IDF và là cách esp-matter xử lý `CHIP_DEVICE_CONFIG_THREAD_FTD`. Nếu mạng đã có đủ router hoặc muốn tiết kiệm flash/RAM: đổi sang MTD (MED), vẫn luôn thức.

---

## 9. Chẩn đoán khi Thread không attach

1. Log sau commission phải có `Thread attached` và `Thread role: child|router|leader`. Nếu chỉ thấy `Thread role: detached` lặp lại: thiết bị có dataset nhưng không nghe thấy mạng.
2. Bật log chi tiết của OpenThread: menuconfig → Component config → OpenThread → Thread Log → chọn mức *INFO* hoặc *DEBG* (thay `CONFIG_OPENTHREAD_LOG_LEVEL_NOTE`), build lại. Tìm `Mle-----------: Attach attempt`, `Role detached -> child`, `Send Parent Request`.
3. Kiểm tra Border Router: `sudo ot-ctl state` (phải `leader`/`router`), `sudo ot-ctl channel`, `sudo ot-ctl dataset active -x` (dataset dùng khi ghép phải khớp), `sudo ot-ctl child table` / `router table` (thiết bị có trong bảng?).
4. SRP: `sudo ot-ctl srp server host` phải có tên host của thiết bị; nếu không, controller không tìm thấy thiết bị dù Thread đã attach.
5. Khoảng cách/nhiễu: đưa thiết bị gần BR; Wi-Fi 2,4 GHz kênh chồng với kênh Thread có thể gây mất gói.
6. Commission lỗi ở bước mạng (`Commissioning failed, fail-safe timer expired`): thường do dataset sai, BR không thông LAN, hoặc điện thoại không có Thread credentials của mạng đó.
7. Muốn xem trực tiếp trạng thái OpenThread: bật `CONFIG_ENABLE_CHIP_SHELL=y`, `CONFIG_OPENTHREAD_CLI=y` và đặt console chính là USB Serial/JTAG (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, vì console phụ chỉ xuất ra); sau đó gõ `matter esp ot_cli state` trong monitor. Chỉ cho bản debug.

---

## 10. Trước khi sản xuất hàng loạt

**Định danh và chứng thực (bắt buộc):**

- VID riêng của công ty (CSA cấp) và PID riêng; PAI/DAC từ PKI được CSA công nhận (PAA của bạn, hoặc dịch vụ pre-provisioning của Espressif); Certification Declaration thật sau khi chứng nhận Matter. Không dùng VID `0xFFF1`, test DAC, test CD trong sản phẩm.
- Mỗi thiết bị một factory partition riêng do `esp-matter-mfg-tool` tạo: **discriminator, passcode, SPAKE2+ salt/verifier, DAC + khoá riêng, PAI, CD, serial number, ngày sản xuất** riêng. **Không sao chép NVS hay `fctry` giữa các thiết bị**, không nhân bản ảnh flash của một bo đã chạy (fabric, khoá node, dataset Thread nằm trong `nvs`).
- Ví dụ (development, đã chạy thử trong Docker image ở mục 4: tạo 2 bo, DAC kiểm chuỗi tới *Matter Test PAA* bằng `chip-cert validate-att-cert`):

```bash
CHIP=$ESP_MATTER_PATH/connectedhomeip/connectedhomeip
# CD test cho 0xFFF1/0x8015, device type 0x0015 (sản phẩm thật: CD do CSA cấp)
chip-cert gen-cd -f 1 -V 0xFFF1 -p 0x8015 -d 0x0015 -c "CSA00000SWC00000-01" -l 0 -i 0 -n 1 -t 0 \
  -K $CHIP/credentials/test/certification-declaration/Chip-Test-CD-Signing-Key.pem \
  -C $CHIP/credentials/test/certification-declaration/Chip-Test-CD-Signing-Cert.pem -O TEST_CD_FFF1_8015.der
# N bo: mỗi bo DAC (ký bằng PAI dev "no PID" của SDK), passcode, discriminator, verifier riêng
esp-matter-mfg-tool -n 2 --target esp32h2 -cn "Contact Sensor PAI" --pai \
  -c $CHIP/credentials/development/attestation/Matter-Development-PAI-FFF1-noPID-Cert.pem \
  -k $CHIP/credentials/development/attestation/Matter-Development-PAI-FFF1-noPID-Key.pem \
  -cd TEST_CD_FFF1_8015.der -v 0xFFF1 --vendor-name "DIY" -p 0x8015 --product-name "Contact Sensor" \
  --hw-ver 1 --hw-ver-str v1.0 --outdir out
# Kết quả: out/fff1_8015/<uuid>/<uuid>-partition.bin (0x6000 byte), <uuid>-qrcode.png, <uuid>-onb_codes.csv,
#          summary-*.csv (discriminator, passcode, QR, mã nhập tay của từng bo). <uuid>/internal chứa khoá DAC.
# Nạp vào phân vùng fctry (0x3E0000), mỗi bo đúng file của nó:
python -m esptool --chip esp32h2 -p <PORT> write-flash 0x3E0000 out/fff1_8015/<uuid>/<uuid>-partition.bin
```

  Chạy ở thư mục **ngoài repo git**: thư mục kết quả chứa khoá riêng DAC và passcode. Kiểm tra không trùng discriminator/passcode giữa các lô (`summary-*.csv`). Repo này có sẵn script tương tự cho các firmware H2 khác: `ESP32-H2/tools/matter-mfg`.

- Firmware đọc factory data: thêm vào cấu hình (menuconfig, hoặc một file defaults riêng cho bản sản xuất truyền bằng `-D SDKCONFIG_DEFAULTS="sdkconfig.defaults;<file>"`):

```
CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER=y
CONFIG_ENABLE_ESP32_DEVICE_INSTANCE_INFO_PROVIDER=y
CONFIG_FACTORY_PARTITION_DAC_PROVIDER=y
CONFIG_FACTORY_COMMISSIONABLE_DATA_PROVIDER=y
CONFIG_FACTORY_DEVICE_INSTANCE_INFO_PROVIDER=y
CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL="fctry"
CONFIG_DEVICE_VENDOR_ID=<VID của bạn>
CONFIG_DEVICE_PRODUCT_ID=<PID của bạn>
CONFIG_CONTACT_PRINT_ONBOARDING_CODES=n
```

  Khi đó passcode không nằm trên thiết bị (chỉ có verifier), nên firmware không in được mã ghép: in QR/mã 11 số từ `onb_codes.csv` lên nhãn sản phẩm. Thay cho DAC trong `fctry` có thể dùng `esp_secure_cert` (`--dac-in-secure-cert`, `CONFIG_SEC_CERT_DAC_PROVIDER=y`, ESP32-H2 hỗ trợ ECDSA peripheral: `CONFIG_ESP_SECURE_CERT_DS_PERIPHERAL=y`, xem `docs/en/developing.rst` của esp-matter).

**Bảo mật:** bật Secure Boot v2 và Flash Encryption (release mode), NVS encryption (phân vùng `nvs_keys` đã có trong bảng), mã hoá factory partition (`esp-matter-mfg-tool -e`); tắt CHIP shell/OpenThread CLI (đã tắt); không để khoá DAC/PAI trong repo.

**Firmware:** tăng `PROJECT_VER`/`PROJECT_VER_NUMBER` mỗi bản; bật `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` và xác nhận ảnh mới sau khi đã vào lại mạng nếu dùng OTA; chạy test chứng nhận (TC-BOOL-2.x, TC-DESC, TC-SC...); cân nhắc tắt các cluster không dùng (`CONFIG_SUPPORT_*_CLUSTER=n`) để giảm flash.

**Phần cứng:** đường dây reed dài cần pull-up ngoài, lọc RC, TVS (mục 2); vỏ không che anten; kiểm tra nguồn USB khi cắm/rút nóng; ghi nhãn QR + mã 11 số riêng cho từng thiết bị.

---

## 11. Những gì đã và chưa được xác minh

Đã làm trong Docker image `espressif/esp-matter:release-v1.6.1_idf_v6.0.2` (esp-matter `cb3f045958ca`, ESP-IDF v6.0.2, connectedhomeip `b1902b39ac67`):

- Đối chiếu mọi API dùng trong mã với header/source của đúng các commit đó: `contact_sensor::create`, `BooleanState::FindClusterOnEndpoint`, `BooleanStateCluster::SetStateValue/GetStateValue`, `PlatformMgr().ScheduleWork`, `CommissioningWindowManager::OpenBasicCommissioningWindow(Seconds32, ...)`, `PrintOnboardingCodes`, `esp_matter::factory_reset`, các event `kCommissioningSessionStarted`, `kFabricRemoved`, `kThreadConnectivityChange`... và mọi tên Kconfig ở mục 8.
- `idf.py set-target esp32h2` và `idf.py build` với `sdkconfig.defaults` này: build thành công, không có warning trong `main/`. Ảnh app `esp32h2_matter_contact_sensor.bin` 0x19DE40 byte (1 695 296), phân vùng app 0x1E0000: còn trống 0x421C0 (14 %). `idf.py size`: flash code 1 610 148 byte, DIRAM tĩnh 170 200 / 258 000 byte (66 %). Bootloader 0x5570 byte. Flash 4 MB, DIO, 48 MHz.
- Một build biến thể (không phát hành) để biên dịch các nhánh tuỳ chọn: LED bật (`STATUS_LED_GPIO=10`, sáng mức thấp, `STATUS_LED_SHOW_CONTACT=y`), `CONTACT_ACTIVE_LOW=n`, `CONFIG_ENABLE_CHIP_SHELL=y` + `CONFIG_OPENTHREAD_CLI=y` + console USB Serial/JTAG, và đúng các tuỳ chọn factory data ở mục 10 (`fctry`): build thành công, ảnh 0x1AFEB0 byte (còn 10 %), không có warning trong `main/`.
- Trong `sdkconfig` sinh ra: `CONFIG_OPENTHREAD_FTD=y`, `CONFIG_PM_ENABLE` tắt, không có `CONFIG_ESP_WIFI_ENABLED`, `CONFIG_ENABLE_ICD_SERVER` không chọn được (chỉ có với MTD), `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=y`, console UART0 + USB Serial/JTAG.
- Lệnh `chip-cert gen-cd` và `esp-matter-mfg-tool` ở mục 10 (2 bo, DAC khác nhau, kiểm chuỗi DAC → PAI → PAA thành công).

**Chưa xác minh** (không có board và Border Router trong môi trường build): chạy trên ESP32-H2 thật, commission với Apple Home / Google Home / Home Assistant / chip-tool, các kịch bản ở mục 6, hành vi phục hồi Thread. Log mẫu ở mục 4 được dựng từ chuỗi log trong mã, không chép từ board.
