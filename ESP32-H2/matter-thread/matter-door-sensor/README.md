# Cảm biến cửa Matter over Thread (ESP32-H2, pin CR2032 / CR2450)

Firmware **cảm biến cửa** chạy trên chính bo của nút bấm [`matter-button-cr2032`](../matter-button-cr2032), tách ra từ firmware nút bản **1.0.2611060757**. Phần cứng chỉ thêm một **công tắc lưỡi gà (reed switch)** hàn song song với nút SW1 và một viên nam châm gắn ở cánh cửa. Thiết bị hiện trong Apple Home và Home Assistant là **cảm biến tiếp xúc "Mở / Đóng"**.

- **Không dùng nút bấm nữa.** Ghép nối, ghép thêm hệ và factory reset đều làm được không cần nút (mục 3).
- Pin, ngủ nhẹ, Thread, OTA, đo pin, LED: giữ nguyên như bản nút. Thời lượng pin gần như không đổi: CR2032 ~4 tháng, CR2450 ~11 tháng ở chế độ SIT.
- **Không nạp firmware nút lên bo đã gắn công tắc lưỡi gà**: cửa đóng lúc lắp pin sẽ bị hiểu là "giữ nút lúc khởi động" và factory reset sau 5 giây, cửa đóng quá 20 giây cũng factory reset.

## Mục lục

1. [Phần cứng](#1-phần-cứng)
2. [Mô hình Matter](#2-mô-hình-matter)
3. [Ghép nối không cần nút](#3-ghép-nối-không-cần-nút)
4. [Pin](#4-pin)
5. [Build và nạp](#5-build-và-nạp)
6. [Kiểm thử](#6-kiểm-thử)
7. [Khác biệt so với firmware nút](#7-khác-biệt-so-với-firmware-nút)

## 1. Phần cứng

Bo, sơ đồ, BOM, file sản xuất: [`../matter-button-cr2032/hardware`](../matter-button-cr2032/hardware) (bo module 25 × 27 mm) hoặc [`../matter-button-cr2032/hardware-chip`](../matter-button-cr2032/hardware-chip) (bo chip, LED RGB). Không cần sửa PCB.

### 1.1 Mạch vào có sẵn trên bo

```
3V3 ── R6 1 MΩ ──┬── BTN_SW ── R7 1 kΩ ── BTN (GPIO10) ── C8 1 nF ── GND
                 │
                SW1 (nút)            ← công tắc lưỡi gà hàn song song ở đây
                 │
                GND
```

- **Hàn hai dây của công tắc lưỡi gà vào hai chân SW1** (một chân ra BTN_SW, một chân ra GND), song song với nút. Hoặc tháo SW1 rồi hàn vào chỗ đó.
- R6 1 MΩ kéo lên, R7 1 kΩ + C8 1 nF lọc nhiễu và chống tĩnh điện. Firmware chỉ cần thêm lọc dội 50 ms.
- Dây tới công tắc nên ngắn, dưới ~20 cm. Dây dài làm anten bắt nhiễu, nhất là khi chạy dọc khung cửa kim loại.
- **Không dùng pad BOOT (GPIO9)**: công tắc đóng lúc lắp pin sẽ đưa chip vào chế độ nạp firmware.

### 1.2 Chọn công tắc

| Loại | Hoạt động | Kconfig | Dòng thêm |
|---|---|---|---|
| **Thường mở (NO, Form A)**, loại phổ biến, có trong module "MC-38" | Nam châm lại gần thì đóng: cửa đóng thì chân vào xuống mức thấp | `CONTACT_CLOSED_WHEN_LOW=y` (mặc định) | 3,3 µA qua R6 **suốt lúc cửa đóng** |
| **Thường đóng (NC, Form B)** hoặc tiếp điểm NC của loại Form C | Nam châm lại gần thì mở | `CONTACT_CLOSED_WHEN_LOW=n` | 3,3 µA **chỉ lúc cửa mở** |

Ảnh hưởng của dòng qua R6 tới tuổi pin, tính ở 60 µA trung bình:

| Pin | Không có dòng qua R6 | Loại NO, cửa đóng cả ngày (~+4,3 µA tại pin) |
|---|---|---|
| CR2032 | ~4,0 tháng | ~3,8 tháng |
| CR2450 | ~11,2 tháng | ~10,5 tháng |

Tụ bulk giữ nguyên 2 × 220 µF gốm của bo nút; so sánh với 1 tụ 470 µF và khi nào nên hàn thêm: [README bản nút, mục 2.8](../matter-button-cr2032/README.md#28-tụ-bulk-2--220-µf-gốm-hay-1--470-µf).

**Không bật kéo lên bên trong** (`CONTACT_INTERNAL_PULLUP`): điện trở trong chip chỉ ~45 kΩ, tốn 73 µA khi công tắc đóng. Với loại NO, cửa đóng gần như cả ngày, tuổi pin còn khoảng 40 %.

### 1.3 Lắp đặt

- Gắn cảm biến lên khung cửa, nam châm lên cánh cửa, hoặc ngược lại. Khi đóng, nam châm phải nằm trong khoảng tác động của công tắc, thường 10–20 mm với module MC-38: xem datasheet của loại bạn mua.
- Cạnh cực dương của pin CR2450 dễ chạm hàng pad nạp ở mặt dưới: dán băng keo Kapton lên hàng pad.
- Tránh dán sát kim loại: khung nhôm hoặc thép làm lệch anten PCB và giảm tầm sóng Thread.
- Nếu hàn song song, nút SW1 vẫn còn trên bo. Nhấn nó tương đương "cửa đóng" trong lúc nhấn; firmware không dùng nó cho việc gì khác.

## 2. Mô hình Matter

| Endpoint | Device type | Cluster |
|---|---|---|
| 0 | Root Node | như bản nút: Basic Information, ICD Management, OTA Requestor, **Power Source** (`EndpointList` = {0, 1}) … |
| 1 | **Contact Sensor (0x0015)** | Identify, **Boolean State**: `StateValue` = `true` khi đóng (nam châm ở công tắc), `false` khi mở; sự kiện `StateChange` mỗi lần đổi |

- Apple Home hiện "Mở / Đóng" và dùng được trong automation. Home Assistant tạo `binary_sensor` loại `door` (HA tự đảo: `on` = mở).
- **Độ trễ**: ngắt GPIO đánh thức chip ngay cả khi đang ngủ nhẹ, lọc dội 50 ms, rồi gửi report: tổng khoảng 0,1–0,3 s.
- Mỗi lần đổi trạng thái: `StateValue` thay đổi, sự kiện `StateChange` được gửi ngay tới mọi subscriber (`CONTACT_FORCE_EVENT_DELIVERY`).
- **ICD**: SIT mặc định (Apple Home + Home Assistant). Bản LIT (`sdkconfig.defaults.lit`, chỉ Home Assistant có Battery Saving) dùng gợi ý UAT `ActuateSensor`: mở/đóng cửa để đánh thức thiết bị.
- VID/PID test `0xFFF1`/`0x8000` và mã ghép test như bản nút (mục 3.3).

## 3. Ghép nối không cần nút

### 3.1 Các thiết bị không có nút làm thế nào

- **Chuẩn Matter**: thiết bị **chưa có fabric** tự mở cửa sổ ghép nối khi được cấp nguồn (Basic Commissioning). Thêm hệ thứ hai không cần chạm vào thiết bị: hệ đang quản lý mở một cửa sổ ghép có mã mới (Enhanced Commissioning). Trên Apple Home là *Bật chế độ ghép nối*, trên Home Assistant là *Share device*.
- **Factory reset bằng bật/tắt nguồn nhiều lần**: cách quen thuộc của thiết bị không có nút như bóng đèn thông minh, ví dụ IKEA là 6 lần.
- **Dùng chính cảm biến làm thao tác**: một chuỗi kích hoạt mà môi trường bình thường không tạo ra được, ở đây là đưa nam châm ra vào nhanh nhiều lần.
- Phần lớn cảm biến cửa thương mại vẫn có một nút nhỏ giấu dưới nắp để reset. Bản này không cần nút đó.

### 3.2 Các thao tác của bản này

| Việc cần làm | Thao tác | LED (đỏ trên bo module) |
|---|---|---|
| **Ghép lần đầu** | Lắp pin: cửa sổ ghép BLE mở **15 phút** ngay. Trên Apple Home: *Thêm phụ kiện*, quét QR hoặc nhập mã (mục 3.3) | nháy 40 ms mỗi 3 giây |
| **Cửa sổ đã đóng mà chưa ghép** | Thiết bị đã ngủ sâu (~7 µA). **Mở hoặc đóng cửa 1 lần** (đưa nam châm lại gần hoặc ra xa): chip khởi động lại, mở cửa sổ 15 phút mới. Lắp lại pin cũng được | |
| **Thêm hệ thứ hai** | Từ hệ đang quản lý: Apple Home → phụ kiện → *Bật chế độ ghép nối*; Home Assistant → *Share device*. Hoặc dùng cử chỉ nam châm bên dưới | |
| **Cử chỉ nam châm**: mở cửa sổ ghép bằng mã gốc | Đưa nam châm ra xa rồi lại gần **5 lần trong 10 giây** (10 lần đổi trạng thái). Đã ghép: cửa sổ 15 phút qua Thread; chưa ghép: qua BLE | nháy như cửa sổ ghép |
| **Factory reset, cách 1** | Xoá thiết bị khỏi hệ cuối cùng (Apple Home: *Xoá phụ kiện*): thiết bị tự reset sau 1 giây và mở cửa sổ ghép mới | |
| **Factory reset, cách 2** | Lắp pin, **trong vòng 10 giây** tháo ra, đợi ~2 giây, lắp lại. Làm **5 lần liên tiếp**: lần lắp thứ 5 thì reset | từ lần lắp thứ 2: 1 nháy ngắn; lần thứ 5: 3 nháy rồi khởi động lại |

Factory reset xoá dữ liệu ghép nối và mạng Thread, rồi mở cửa sổ ghép 15 phút. Mã ghép nối gốc và số đếm pin được giữ.

**Vì sao không kích hoạt nhầm:**
- Cửa mở/đóng bằng tay cách nhau ít nhất 1–2 giây, không tạo được 10 lần đổi trong 10 giây. Gió làm cửa rung thì hiếm khi đủ, và hậu quả chỉ là một cửa sổ ghép 15 phút với mã gốc, không mất dữ liệu.
- Chỉ **reset do cấp nguồn** (lắp pin) mới được đếm. Reset do brownout của pin yếu, đánh thức từ ngủ sâu hay reset phần mềm đều đưa bộ đếm về 0. Một lần lắp pin chạy quá 10 giây cũng đưa bộ đếm về 0.
- Cửa đóng lúc lắp pin không còn ý nghĩa gì đặc biệt (khác bản nút).

**Lưu ý:** mỗi lần đổi trạng thái trong cử chỉ nam châm đều được báo lên hệ thống, nên automation gắn với cửa sẽ chạy vài lần.

### 3.3 Mã ghép nối

Bản mặc định dùng mã test của SDK, giống bản nút:

- Mã nhập tay: **34970112332**
- QR: `MT:Y.K9042C00KA0648G00` (discriminator 3840, passcode 20202021)

Nếu cùng lúc có nút bấm và cảm biến cửa đang chờ ghép, hai thiết bị quảng bá cùng một mã: ghép từng cái một, hoặc build bản mã riêng từng bo `sdkconfig.defaults.unique_setup` (mã in trong log UART lúc khởi động). Bản sản xuất có factory data: `ESP32-H2/tools/matter-mfg/gen_factory_data.sh -t contact`, rồi build với `sdkconfig.defaults.factory`.

## 4. Pin

Giống firmware nút bản 1.0.2611060757, xem [`../matter-button-cr2032/README.md`](../matter-button-cr2032/README.md) mục 2.5:

- `BatPercentRemaining` giảm đều theo thời gian chạy × dòng trung bình mô hình 60 µA, từ 100 % xuống 20 % rồi đứng ở 20 %; dưới 20 % chỉ theo áp nghỉ đo được (vùng "khuỷu" ~2,85 V).
- Đo áp nghỉ 12 giờ một lần, chỉ khi radio đã yên 10 phút: **mỗi lần mở/đóng cửa** được tính là hoạt động như một lần nhấn nút.
- Sau khi nạp file đầy đủ ở `0x0` (xoá NVS), % để trống cho tới lần đo nghỉ đầu tiên. Nạp bằng `@flash_args` để giữ ghép nối và số liệu pin.
- Pin CR2450: build `sdkconfig.defaults.cr2450`.

## 5. Build và nạp

Cài đặt như [README bản nút, mục 8.1](../matter-button-cr2032/README.md#81-cài-đặt-một-lần). Build trong Docker (PowerShell):

```powershell
$repo = git rev-parse --show-toplevel
$img  = "espressif/esp-matter:release-v1.6.1_idf_v6.0.2"
$proj = "/repo/ESP32-H2/matter-thread/matter-door-sensor"
$pre  = "git config --global --add safe.directory /repo && cd $proj"

# Bản mặc định: CR2032, ICD SIT (Apple Home + Home Assistant)
docker run --rm -v "${repo}:/repo" $img bash -c "$pre && { [ -f sdkconfig ] || idf.py set-target esp32h2; } && idf.py build"

# Pin CR2450
docker run --rm -v "${repo}:/repo" $img bash -c "$pre && idf.py -B build-cr2450 -D IDF_TARGET=esp32h2 -D SDKCONFIG=sdkconfig.cr2450 -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.cr2450' build"

# LIT (chỉ Home Assistant có Battery Saving); bo chip (hardware-chip): sdkconfig.defaults.chip; ghép nhiều file:
#   SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.cr2450;sdkconfig.defaults.lit'
docker run --rm -v "${repo}:/repo" $img bash -c "$pre && idf.py -B build-lit -D IDF_TARGET=esp32h2 -D SDKCONFIG=sdkconfig.lit -D SDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.defaults.lit' build"
```

Các bản `debug`, `pmdiag`, `unique_setup`, `factory`, `chip` dùng giống hệt bản nút, chỉ đổi thư mục.

| File | Dùng để |
|---|---|
| `build*/matter_door_h2-<phiên bản>.bin` | nạp một lần ở `0x0`; **xoá ghép nối** (phải ghép lại) |
| `build*/matter_door_h2-ota.bin` | cập nhật qua Matter OTA, không nạp bằng dây |
| `build*/matter_door_h2.bin` + `flash_args` | nạp bằng `write-flash "@flash_args"`, **giữ ghép nối** |

Đấu dây nạp như [bản nút, mục 8.3](../matter-button-cr2032/README.md#83-nạp-firmware):

```powershell
py -m esptool --chip esp32h2 -p COM5 -b 460800 --before no-reset --after no-reset write-flash 0x0 matter_door_h2-<phiên bản>.bin
py -m serial.tools.miniterm COM5 115200      # log; Ctrl+] để thoát
```

**Từ bo đang chạy firmware nút sang cảm biến cửa:** nạp file đầy đủ ở `0x0`, xoá thiết bị "nút" cũ khỏi Apple Home, rồi ghép lại như một phụ kiện mới. Loại thiết bị đổi từ Generic Switch sang Contact Sensor nên controller phải nhận lại từ đầu.

### Log chính

| Dòng log | Ý nghĩa |
|---|---|
| `contact: Door contact on GPIO10 (external pull-up, closed = low level), debounce 50 ms, now closed` | khởi động |
| `door: Door open` / `door: Door closed` | mỗi lần đổi trạng thái |
| `door: Magnet gesture: commissioning window` rồi `door: Commissioning window open for 900 s (...)` | cử chỉ nam châm |
| `app_main: Battery inserted 3 times in a row: 5 for a factory reset (take it out within 10 s)` | đếm lắp pin |
| `power: Not commissioned, commissioning window closed: Deep-sleep, opening or closing the door restarts ...` | ngủ sâu khi chưa ghép |

## 6. Kiểm thử

Host test (không cần ESP-IDF): `test/host/run.sh`, chạy máy trạng thái tiếp điểm, bộ đếm lắp pin và phần pin. Trên bo thật:

| # | Bước | Kết quả mong đợi |
|---|---|---|
| D1 | Lắp pin khi cửa **đóng**, rồi khi cửa **mở** | Không factory reset; log `now closed` / `now open` đúng; Apple Home hiện đúng trạng thái sau khi ghép |
| D2 | Ghép với Apple Home bằng mã ở mục 3.3 | Hiện "Cảm biến tiếp xúc", trạng thái Mở/Đóng đúng |
| D3 | Mở/đóng cửa 20 lần, mỗi lần cách ≥ 2 s | 20 thay đổi trên Apple Home, trễ < 0,5 s; không mở cửa sổ ghép |
| D4 | Cửa để mở 1 giờ, rồi đóng 1 giờ | Không đổi trạng thái giả; chip ngủ (đo ở JP1: ~60 µA trung bình, +3,3 µA lúc đóng với công tắc NO) |
| D5 | Cử chỉ nam châm: ra xa/lại gần 5 lần trong < 10 s | Log `Magnet gesture`, LED nháy cửa sổ ghép; ghép thêm được Home Assistant bằng mã gốc |
| D6 | Ra xa/lại gần 5 lần trong 15 s | Không có cử chỉ |
| D7 | Lắp/tháo pin 5 lần, mỗi lần tháo trong 10 s, đợi 2 s | Lần 2–4: 1 nháy ngắn; lần 5: factory reset, cửa sổ ghép mở |
| D8 | Lắp/tháo 3 lần, rồi để chạy > 10 s, rồi lắp/tháo 2 lần | Không reset (bộ đếm về 0 sau 10 s) |
| D9 | Chưa ghép, đợi cửa sổ 15 phút đóng + 60 s | Log `Deep-sleep`; dòng ở JP1 ~7 µA (+3,3 µA nếu công tắc NO đang đóng); mở hoặc đóng cửa → khởi động lại, cửa sổ mới |
| D10 | Xoá phụ kiện khỏi Apple Home | Tự factory reset sau 1 s, cửa sổ ghép mới |
| D11 | Tắt Border Router 1 giờ, mở/đóng cửa vài lần, bật lại | Sau khi nối lại, Apple Home hiện trạng thái cuối đúng |

Các mục pin, OTA, Thread của checklist bản nút ([`../matter-button-cr2032/docs/test-checklist.md`](../matter-button-cr2032/docs/test-checklist.md)) áp dụng như cũ.

## 7. Khác biệt so với firmware nút

| Phần | Bản nút | Bản cảm biến cửa |
|---|---|---|
| Đầu vào GPIO10 | `button.cpp` + `button_fsm.cpp`: nhấn 1 lần, nhấn đúp, giữ | `contact.cpp` + `contact_fsm.cpp`: lọc dội 50 ms, báo đổi trạng thái, cử chỉ nam châm |
| Endpoint 1 | Generic Switch (sự kiện nhấn) | Contact Sensor, Boolean State (`door_events.cpp`) |
| Mở cửa sổ ghép | nhấn ngắn khi chưa ghép; giữ 10 s | mở/đóng cửa khi chưa ghép; cử chỉ nam châm |
| Factory reset | giữ 20 s; giữ nút lúc lắp pin 5 s; xoá khỏi hệ cuối | lắp pin 5 lần liên tiếp (`power_cycle.cpp`); xoá khỏi hệ cuối |
| Đánh thức từ ngủ sâu | nhấn nút (mức thấp) | đổi trạng thái tiếp điểm, theo cả hai chiều |
| Kconfig | menu Button, Matter Switch | menu Door contact (`CONTACT_*`, `POWER_CYCLE_*`) |
| Pin, nguồn, Thread, OTA, LED, chẩn đoán | | giữ nguyên |
