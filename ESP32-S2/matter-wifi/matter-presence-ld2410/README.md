# Matter Presence Sensor (ESP32-S2 + HLK-LD2410, Wi-Fi)

Cảm biến hiện diện chuẩn **Matter over Wi-Fi** chạy trên ESP32-S2 với radar 24 GHz **HLK-LD2410**. Thêm trực tiếp vào **Apple Home**, Google Home, Home Assistant, SmartThings...

Radar LD2410 phát hiện được cả người đang ngồi yên (thở, cử động nhẹ), khác với cảm biến PIR chỉ thấy chuyển động.

Thiết bị dùng device type **Occupancy Sensor (0x0107)**:

| Cluster | Nội dung |
|---|---|
| Occupancy Sensing (0x0406) | Có người / không có người. Feature **Radar** |
| Occupancy Sensing → HoldTime | Thời gian giữ trạng thái "có người" sau khi radar không còn thấy ai. Mặc định 30 s, chỉnh được 1–300 s |

## Phần cứng

- Board **ESP32-S2**, flash 4 MB: LOLIN S2 Mini, ESP32-S2-Saola-1, ESP32-S2-DevKitM-1...
- Radar hiện diện **HLK-LD2410** (LD2410, LD2410B, LD2410C)

### Sơ đồ nối dây

| LD2410 | ESP32-S2 |
|---|---|
| TX | **GPIO33** |
| RX | **GPIO18** |
| VCC | 5V |
| GND | GND |

LD2410 cần nguồn **5V**, nhưng mức logic UART là 3.3V nên nối thẳng vào ESP32-S2 được. Radar dùng UART1 ở 256000 baud. Đổi chân trong `menuconfig` → *Presence Sensor Configuration* nếu cần.

> Trên ESP32-S2-Saola-1, GPIO18 cũng nối với LED RGB có sẵn trên board. Radar vẫn chạy, nhưng LED có thể nháy theo dữ liệu UART.

## ESP32-S2 không có Bluetooth: cách ghép nối

Các thiết bị Matter Wi-Fi thường nhận thông tin Wi-Fi từ điện thoại qua Bluetooth. ESP32-S2 không có Bluetooth, nên việc ghép nối chia làm hai bước:

1. **Cài Wi-Fi qua trang web** của thiết bị (chỉ làm một lần).
2. **Thêm vào ứng dụng Matter**. Điện thoại tìm thấy thiết bị qua mạng Wi-Fi nhà (ghép nối *on-network*).

### Bước 1: cài Wi-Fi

1. Cấp nguồn cho thiết bị lần đầu. Trong **10 giây đầu**, LED nháy theo nhóm để báo lý do của lần khởi động trước (xem [Chẩn đoán](#chẩn-đoán-khi-thiết-bị-tự-khởi-động-lại)). Sau đó LED nháy nhanh và thiết bị phát Wi-Fi tên **`Presence-XXXX`** (4 số cuối của địa chỉ MAC), không có mật khẩu.
2. Dùng điện thoại kết nối vào mạng đó. Trang cài đặt thường tự mở. Nếu không, mở trình duyệt và vào **http://192.168.4.1**.
3. Nhập tên mạng Wi-Fi nhà (**2.4 GHz**) và mật khẩu, bấm **Kết nối**. Muốn chọn từ danh sách thì bấm **Tìm các mạng Wi-Fi xung quanh**. Thiết bị không tự quét lúc khởi động, vì trên board yếu, việc quét có thể làm chip khởi động lại.
4. Thiết bị thử kết nối trước. Nếu sai mật khẩu, trang báo lỗi để nhập lại. Nếu thành công, thiết bị lưu lại rồi khởi động lại, mạng `Presence-XXXX` biến mất và LED chuyển sang nháy chậm.

> **Không thấy hoặc không vào được mạng `Presence-XXXX`?** Nhiều board ESP32-S2 nhỏ (LOLIN S2 Mini...) bị lỗi Wi-Fi khi phát ở công suất mặc định ~20 dBm do anten và mạch nguồn 3.3V yếu. Firmware vì vậy giới hạn công suất phát ở **8.5 dBm** (`menuconfig` → *Wi-Fi Setup* → *Maximum Wi-Fi transmit power*). Nếu vẫn không được: đặt điện thoại gần thiết bị, cắm nguồn USB tốt, tắt "Wi-Fi riêng tư / địa chỉ MAC ngẫu nhiên" nếu điện thoại báo lỗi, và xem log (`idf.py monitor`): dòng `Phone ... joined the setup network` cho biết điện thoại đã vào được mạng cài đặt.

#### Chẩn đoán khi thiết bị tự khởi động lại

Firmware lưu lý do của 8 lần khởi động gần nhất (mất điện, **BROWNOUT** = sụt áp, **CRASH**, watchdog...) và tóm tắt lần crash cuối (core dump trong flash).

Trong 10 giây trước khi bật sóng, LED báo lý do của lần khởi động trước: luôn luôn khi chưa có Wi-Fi, và khi đã có Wi-Fi thì chỉ sau một lần sụt áp, crash hoặc watchdog (cắm điện bình thường thì chạy ngay). Mỗi chu kỳ 3 giây gồm N lần nháy:

| LED nháy | Lần khởi động trước |
|---|---|
| 1 lần | Bình thường (cắm điện, nút RST, khởi động lại có chủ đích) |
| 2 lần | **Sụt áp** (brownout): nguồn 3.3V tụt khi chip phát sóng. Thường do nguồn USB yếu, cáp dài hoặc board thiếu tụ lọc |
| 3 lần | **Crash** phần mềm |
| 4 lần | **Watchdog**: một tác vụ bị treo |

Gặp **2 lần** (sụt áp) liên tục: dùng cáp USB ngắn, cục sạc 5V tốt, hoặc gắn thêm tụ 100–470 µF giữa chân 3V3 và GND. Nếu vẫn bị, có thể tắt cơ chế reset khi sụt áp: `menuconfig` → *Component config → Power Supplier → Brownout Detector* → bỏ *Hardware brownout detect & reset* (`CONFIG_ESP_BROWNOUT_DET=n`), và hạ `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER` xuống 10. Rủi ro: chip chạy tiếp khi điện áp tụt, nên lúc đó có thể treo hoặc lỗi khi đang ghi flash. Muốn bật lại thì đặt lại `CONFIG_ESP_BROWNOUT_DET=y`.

Trong 10 giây đó, cổng USB cũng hiện ổn định để mở log. Khi mạng cài đặt đang bật, log in mỗi 10 giây một dòng `Setup network ... on, N phone(s) joined` kèm lịch sử khởi động lại. Thông tin cũng hiện ở dòng chữ xám cuối trang cài đặt. Đổi thời gian này trong `menuconfig` → *Wi-Fi Setup* → *Show the last reset reason before the radio starts*, đặt `0` để tắt.

Nếu không mở được trang lẫn log, hãy đưa board vào chế độ nạp (giữ nút 0, bấm RST) rồi đọc ra hai file sau và gửi cho người phát triển. Kèm theo file `.elf` của đúng bản firmware (artifact `-debug` trên GitHub Actions) thì có thể giải mã chính xác dòng code bị crash:

```bash
python -m esptool --chip esp32s2 -p COMx read-flash 0x10000 0xC000 nvs.bin
python -m esptool --chip esp32s2 -p COMx read-flash 0x3F0000 0x10000 coredump.bin
```

Muốn cài sẵn Wi-Fi lúc build thì đặt `CONFIG_DEFAULT_WIFI_SSID` / `CONFIG_DEFAULT_WIFI_PASSWORD` trong `menuconfig` (*Component config → CHIP Device Layer → WiFi Station Options*). Khi đó thiết bị bỏ qua bước 1.

### Bước 2: thêm vào Apple Home

1. Đưa iPhone về **cùng mạng Wi-Fi** với thiết bị.
2. Mở app **Nhà → + → Thêm phụ kiện → Thêm tùy chọn khác... → Nhập mã**. Nhập mã ghép nối **`3497-011-2332`**.
   Hoặc quét QR code: mở link [https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K90AFN00KA0648G00](https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K90AFN00KA0648G00) để hiện QR. Thiết bị cũng in link này ra log (`idf.py monitor`) khi chưa được ghép nối.
3. Khi gặp cảnh báo phụ kiện chưa được chứng nhận (*Uncertified Accessory*), chọn **Vẫn thêm** (*Add Anyway*).
4. Phụ kiện hiện là **Cảm biến chiếm chỗ** (*Occupancy Sensor*). Dùng được trong tự động hóa, ví dụ bật đèn khi có người, tắt đèn khi không còn ai.

Google Home, Home Assistant, SmartThings làm tương tự: điện thoại ở cùng mạng Wi-Fi, chọn thêm thiết bị Matter rồi nhập mã. Home Assistant hiện thêm mục **Hold time** để chỉnh thời gian giữ trạng thái.

Chế độ ghép nối mở trong **15 phút** sau khi thiết bị khởi động mà chưa thuộc ứng dụng nào (LED nháy chậm). Nếu bạn xóa thiết bị khỏi tất cả ứng dụng, chế độ ghép nối tự mở lại trong 5 phút, giữ nguyên Wi-Fi. Khi LED ngừng nháy chậm mà thiết bị chưa được thêm, nghĩa là chế độ ghép nối đã đóng: rút điện cắm lại để mở lại.

> Firmware dùng chứng chỉ test (VID `0xFFF1`, PID `0x8000`, passcode `20202021`, discriminator `3840`) nên chỉ hợp với mục đích DIY. Để làm sản phẩm thật, hãy tạo DAC/passcode riêng bằng `esp-matter-mfg-tool`.

## Hoạt động

- Có người ngay khi radar thấy mục tiêu đang di chuyển hoặc đứng yên.
- Không có người sau khi radar không thấy ai trong khoảng **HoldTime** (mặc định 30 s). Giá trị này lưu trong thiết bị và giữ nguyên sau khi mất điện.
- Nếu radar không gửi dữ liệu quá 10 giây (lỏng dây, mất nguồn), thiết bị báo **không có người** và ghi lỗi vào log. Có dữ liệu lại thì tự hoạt động bình thường.
- Độ nhạy và khoảng cách của từng vùng (gate) dùng cấu hình sẵn của LD2410. Có thể chỉnh bằng app **HLKRadarTool** qua Bluetooth của LD2410B/C. Muốn chỉ tính người trong một khoảng nhất định thì đặt *Only count targets closer than* trong `menuconfig`.

### Đèn LED (GPIO15, LED xanh của LOLIN S2 Mini)

| LED | Trạng thái |
|---|---|
| Nháy theo nhóm 1–4 lần, 10 giây đầu | Chưa có Wi-Fi: lý do của lần khởi động trước (xem *Chẩn đoán*) |
| Nháy nhanh | Chưa có Wi-Fi, đang phát mạng cài đặt `Presence-XXXX` |
| Nháy chậm (1 giây sáng, 1 giây tắt) | Đã có Wi-Fi, chế độ ghép nối đang mở, chờ thêm vào ứng dụng Matter |
| Sáng / tắt | Ngoài các trường hợp trên. Sáng khi radar thấy người (tức thì, không có HoldTime), tiện để chỉnh vị trí đặt radar |
| Nháy nhanh vài giây | Ứng dụng yêu cầu *Identify* (xác định thiết bị) |

### Factory reset

**Giữ nút BOOT (GPIO0) 5 giây.** Thiết bị xóa Wi-Fi và toàn bộ thông tin ghép nối Matter, khởi động lại rồi phát mạng cài đặt `Presence-XXXX` như lúc mới.

- Nút chỉ có tác dụng khi thiết bị đã có Wi-Fi. Lúc đang ở trang cài đặt thì chưa có gì để xóa, nên nút bị tắt.
- Chỉ tính lần nhấn bắt đầu sau khi nút đã được nhả. Nếu chân GPIO0 bị kéo xuống thấp ngay từ lúc khởi động (nút kẹt, đấu nhầm), thiết bị không tự reset liên tục.

## Phần mềm

Yêu cầu: **ESP-IDF v6.0.2** và **esp-matter `release/v1.6`** (giống CI). Cách dễ nhất là dùng Docker image của Espressif:

```bash
# chạy ở thư mục gốc của repo (cần cả lịch sử git để đánh số phiên bản)
docker run --rm -it -v $PWD:/project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
    bash -c 'git config --global --add safe.directory "*" && cd /project/ESP32-S2/matter-wifi/matter-presence-ld2410 && idf.py set-target esp32s2 && idf.py build'
```

Image tự nạp môi trường ESP-IDF và esp-matter, rồi chuyển về thư mục esp-matter, nên lệnh phải `cd` vào thư mục dự án (tùy chọn `-w` của Docker không có tác dụng).

Hoặc cài trực tiếp:

```bash
git clone -b v6.0.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s2 && cd ..

git clone -b release/v1.6 --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
./connectedhomeip/connectedhomeip/scripts/checkout_submodules.py --platform esp32 linux --shallow
./install.sh
cd ..

. ./esp-idf/export.sh
. ./esp-matter/export.sh
cd ESP32-S2/matter-wifi/matter-presence-ld2410
idf.py set-target esp32s2
idf.py menuconfig          # (tùy chọn)
idf.py build
```

Build tạo ra:

| File trong `build/` | Dùng để |
|---|---|
| `matter_presence_ld2410-<phiên bản>.bin` | Nạp một lần ở địa chỉ `0x0` (bootloader + bảng phân vùng + firmware) |
| `matter_presence_ld2410-ota.bin` | Cập nhật qua Matter OTA (phục vụ từ một OTA provider) |
| `matter_presence_ld2410.bin` | Chỉ phần firmware, nạp ở `0x20000` |

Mỗi lần push, GitHub Actions chạy host test, build firmware và đính kèm các file trên (artifact `matter_presence_ld2410-<phiên bản>`), cùng file `.elf`/`.map` (artifact `...-debug`) để giải mã địa chỉ crash.

### Host test

Phần logic không phụ thuộc phần cứng (đọc frame LD2410, quyết định có người, trả lời DNS của trang cài đặt, giải mã form và kiểm tra tên/mật khẩu Wi-Fi) là C thuần trong `main/`. Chạy test trên máy tính, chỉ cần gcc (hoặc clang qua `CC=clang`):

```bash
test/host/run.sh
```

### Nạp firmware

LOLIN S2 Mini chỉ có cổng USB native của ESP32-S2:

1. **Lần nạp đầu tiên**: giữ nút **0** (BOOT), bấm và nhả nút **RST**, rồi nhả nút 0. Board vào chế độ nạp và hiện cổng `/dev/ttyACM0` (Windows: `COMx`).
2. Nạp: `idf.py -p /dev/ttyACM0 flash`, sau đó bấm **RST** để chạy firmware.
3. Xem log: `idf.py -p /dev/ttyACM0 monitor`. Firmware xuất log qua USB CDC. Từ lần sau, `idf.py flash` tự đưa board vào chế độ nạp.

Board có chip USB-UART (CP2102/CH340, ví dụ ESP32-S2-Saola-1): log mặc định ra cổng USB native. Muốn log ra cổng USB-UART thì đổi `CONFIG_ESP_CONSOLE_USB_CDC=y` thành `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` trong `sdkconfig.defaults`, xóa file `sdkconfig` rồi build lại.

### RAM

ESP32-S2 có 320 KB SRAM, ít hơn ESP32-C3. Bù lại ESP32-S2 không có Bluetooth nên Matter không phải chạy BLE. Firmware tự dùng **PSRAM** nếu có (ESP32-S2FN4R2 trên LOLIN S2 Mini, module WROVER). Firmware vẫn khởi động trên chip không có PSRAM, nhưng **chưa được kiểm chứng** là đủ RAM cho việc ghép nối Matter, nên nên dùng chip có PSRAM.

Các cluster Matter không dùng đến đã bị tắt trong `sdkconfig.defaults` để tiết kiệm RAM và flash. Bộ nhớ của trang cài đặt Wi-Fi chỉ được cấp khi cần. Log in lượng RAM trống khi khởi động, sau khi ghép nối xong và mỗi 10 phút (`diag: Heap: internal free ... (lowest ...)`), để theo dõi trên thiết bị thật.

### Cấu hình (`idf.py menuconfig`)

*Presence Sensor Configuration*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| ESP32 UART RX pin | 33 | Nối với TX của LD2410 |
| ESP32 UART TX pin | 18 | Nối với RX của LD2410 |
| Only count targets closer than | 0 (tắt) | Bỏ qua mục tiêu xa hơn khoảng cách này (cm) |
| Report unoccupied when the radar sends nothing for | 10 s | Coi radar mất kết nối sau khoảng thời gian này |
| Status LED pin | 15 | `-1` để tắt LED |
| Status LED is on when the pin is high | bật | Tắt nếu LED sáng ở mức thấp |
| Factory reset button pin | 0 | Nút BOOT, giữ 5 giây để reset. `-1` để tắt |

Khi build, firmware kiểm tra các chân của LD2410, LED và nút: các chân phải khác nhau và dùng được trên ESP32-S2. Không được dùng GPIO19/20 (cổng USB đang dùng cho log), GPIO26–32 (flash và PSRAM), hay GPIO46 làm ngõ ra. Nếu sai, build báo lỗi rõ ràng.

*Wi-Fi Setup*:

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| Show the last reset reason before the setup page starts | 10 s | Thời gian LED báo lý do khởi động lại trước khi bật sóng. `0` để tắt |
| Maximum Wi-Fi transmit power | 34 (8.5 dBm) | Đơn vị 0,25 dBm. Board tốt có thể tăng lên 52 (13 dBm) hoặc 78 (19.5 dBm) để xa hơn |
| Setup access point name prefix | `Presence-` | Tên mạng cài đặt là `<prefix><4 số cuối MAC>`, tối đa 28 ký tự |
| Setup access point password | (trống) | Để trống: mạng mở. Hoặc 8–63 ký tự (build kiểm tra) |

### Thông tin thiết bị và phiên bản firmware

| Mục | Giá trị | Sửa ở đâu |
|---|---|---|
| Nhà sản xuất (Manufacturer) | `Haxeko` | `menuconfig` → *Device Information* |
| Kiểu máy (Model) | `ESP32S2 LD2410` | `menuconfig` → *Device Information* |
| Tên thiết bị khi ghép nối | `Presence Sensor` | `menuconfig` → *Device Information* |
| Số serial | Địa chỉ MAC Wi-Fi, ví dụ `7CDFA1E2F3A4` | Tự động, riêng cho từng chip |
| Firmware | `1.0.<số commit>`, ví dụ `1.0.40` | Tự động |

Phiên bản firmware **tự tăng sau mỗi commit**. Số cuối là số commit của repo (`git rev-list --count HEAD`), và cũng được dùng làm số phiên bản Matter (SoftwareVersion) cho OTA. Khi ra bản lớn, sửa `FIRMWARE_VERSION_BASE` trong `CMakeLists.txt`, ví dụ từ `1.0` thành `1.1`.

- Clone repo đầy đủ, **không dùng `--depth`**. Với clone nông, số commit sẽ sai.
- Nếu build từ thư mục không có git (ví dụ tải file zip), phiên bản sẽ là `1.0.0`.

## Cấu trúc thư mục

```
matter-presence-ld2410/
├── CMakeLists.txt            # Phiên bản firmware, file nạp đặt tên theo phiên bản
├── partitions.csv            # 2 phân vùng OTA, NVS, core dump
├── sdkconfig.defaults
├── main/
│   ├── app_main.cpp          # Matter node, endpoint Occupancy Sensor, radar -> occupancy, kiểm tra chân
│   ├── presence.c            # Quyết định có người / radar còn dữ liệu (C thuần, có host test)
│   ├── wifi_setup.cpp        # Trang cài Wi-Fi (access point + captive portal)
│   ├── captive_dns.c         # Trả lời DNS của trang cài đặt (C thuần, có host test)
│   ├── portal_form.c         # Giải mã form, kiểm tra tên/mật khẩu Wi-Fi (C thuần, có host test)
│   ├── diag.cpp              # Lý do khởi động lại, tóm tắt core dump, log RAM
│   ├── device_info.cpp       # Manufacturer / Model / Serial (Basic Information)
│   ├── Kconfig.projbuild     # Cấu hình chân và tham số
│   └── drivers/
│       ├── ld2410.cpp        # Radar LD2410 (UART1, 256000 baud)
│       ├── ld2410_parser.c   # Đọc frame LD2410 (C thuần, có host test)
│       ├── status_led.cpp    # LED trạng thái
│       └── reset_button.cpp  # Nút factory reset
└── test/host/                # Host test: run.sh
```
