# Haxeko Vibe Light: Matter RGB Light WS2812 (ESP32-C3, Wi-Fi)

Đèn RGB chuẩn **Matter over Wi-Fi** chạy trên ESP32-C3 (SuperMini), dùng dải **30 LED WS2812** nối vào **GPIO4**. Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa...

Thiết bị dùng device type **Extended Color Light (0x010D)** đúng chuẩn Matter: bật/tắt, độ sáng, màu (Hue/Saturation, XY) và **nhiệt độ màu trắng 2000–6500 K**. Dải WS2812 chỉ có LED R, G, B, không có LED trắng, nên các màu trắng được pha từ R, G, B.

## Cách đèn hoạt động

- **Mỗi LED đang sáng luôn sáng ở mức 30 %** (`CONFIG_LED_FIXED_BRIGHTNESS_PERCENT`). Độ sáng chỉnh từ ứng dụng **không làm LED mờ đi**.
- **Độ sáng 0–100 % quyết định số LED sáng** trên tổng 30 LED, sáng dần từ LED đầu tiên (gần chân DIN):

| Độ sáng trong app | Số LED sáng |
|---|---|
| 1 % | 1 |
| 10 % | 3 |
| 25 % | 8 |
| 50 % | 15 |
| 75 % | 23 |
| 100 % | 30 |

  Công thức: `số LED = round(CurrentLevel × 30 / 254)`, tối thiểu 1 LED khi đèn bật.
- **Tắt** trong app thì tắt hết 30 LED.
- **Màu** chọn trong app được áp cho mọi LED đang sáng, sau đó nhân với 30 %. Ví dụ đỏ = RGB(77, 0, 0), trắng = RGB(77, 77, 77).
- **Trạng thái trong app luôn khớp đèn**: mỗi khi bật/tắt, độ sáng hay màu thay đổi, đèn tăng *DataVersion* của cluster tương ứng như chuẩn Matter yêu cầu. esp-matter 1.6 bỏ sót việc này khi chính đèn đổi giá trị trong lúc chạy lệnh, khiến Apple Home giữ màu cũ trong bộ nhớ đệm: thanh kéo hiện màu cũ và màu vừa chọn không được lưu vào các ô màu gần đây.
- **Màu đọc theo cách nào cũng đúng**: Matter lưu màu theo 3 cách (Hue/Saturation, XY, nhiệt độ màu) và mỗi lệnh chỉ cập nhật một cách. Khi màu đã đứng yên 0,3 giây, đèn ghi cùng màu đó vào Hue/Saturation và XY (ví dụ chọn 2700 K thì Hue/Saturation và XY cũng là màu trắng ấm đó).
- **Màu trắng**: chọn ở tâm bánh xe màu (độ bão hòa 0, cả 3 màu R, G, B bằng nhau), hoặc chọn nhiệt độ màu từ 2000 K (trắng ấm, ngả cam) đến 6500 K (trắng lạnh) ở tab *Nhiệt độ* hay ô trắng của Apple Home. Đèn pha màu trắng đó từ R, G, B (công thức đường cong vật đen Tanner Helland), nên trắng ấm không trong như đèn có LED trắng thật.
- **Tắt rồi bật lại** (từ app hoặc nút BOOT): đèn sáng lại ngay đúng số LED và màu như trước khi tắt. Hiệu ứng "sáng dần từ mức thấp nhất" của Matter khi Bật được bỏ qua, vì trên dải LED này mức thấp nhất là 1 LED và sẽ trông như đèn nháy tắt một lần. Khi Tắt, dải LED thu ngắn rất nhanh về 1 LED rồi tắt.
- **Mất điện / rút nguồn**: có điện lại thì đèn khôi phục bật/tắt, độ sáng và màu như trước khi mất điện. Bật/tắt được lưu vào flash ngay; độ sáng và màu được lưu khi giá trị đã đứng yên **3 giây** (giống ví dụ `light` của Espressif, tránh ghi flash ở mỗi bước chuyển sáng/màu). Nếu rút điện trong vòng 3 giây sau khi vừa chỉnh độ sáng/màu thì lần chỉnh đó không được nhớ. Nếu vùng lưu trữ NVS hỏng không đọc được (đầy, hoặc do firmware khác ghi), đèn tự xóa vùng này và khởi động như đèn mới để có thể ghép lại, thay vì chạy mà không lưu được gì.
- **Nhận dạng (Identify)**: nút "Nhận dạng" trong Apple Home / Home Assistant làm cả dải LED nháy (0,5 s sáng, 0,5 s tắt) bằng màu hiện tại, kể cả khi đèn đang tắt.
- Lần đầu khởi động (chưa có gì lưu): đèn bật, 100 % (30 LED), màu trắng.

Mức 30 %, số LED, chân GPIO và chiều sáng (từ đầu hay từ cuối dải) đều đổi được trong `idf.py menuconfig` → **WS2812 RGB Light**.

## Phần cứng

- ESP32-C3 SuperMini (hoặc ESP32-C3-DevKitM-1...), flash 4 MB.
- Dải 30 LED WS2812 / WS2812B.
- Nguồn 5V cho LED. 30 LED ở 30 %, màu trắng, ăn khoảng **0,55 A**, nên dùng nguồn **5V 1A** trở lên.

### Sơ đồ nối dây

| WS2812 | Nối vào |
|---|---|
| DIN | **GPIO4** (nên nối tiếp điện trở 330 Ω) |
| 5V | 5V nguồn |
| GND | GND nguồn **và** GND của ESP32-C3 |

Nên đặt tụ 470–1000 µF giữa 5V và GND ở đầu dải LED. Mức logic 3.3V của ESP32-C3 thường đủ cho WS2812; nếu LED nhấp nháy sai màu, thêm IC chuyển mức (74AHCT125) hoặc cấp cho LED đầu tiên 4.5V qua một diode.

Có thể cấp nguồn cho cả mạch từ chân **5V** của SuperMini khi cắm USB, nhưng cổng USB máy tính thường chỉ cho 0,5 A. Nếu chỉnh mức sáng cao hơn 30 % hoặc dùng nhiều LED hơn, hãy dùng nguồn 5V riêng.

### Nút bấm

Nút **BOOT (GPIO9)**:
- **Nhấn nhanh**: bật/tắt đèn (trạng thái được báo về app).
- **Giữ 5 giây rồi nhả ra**: factory reset (xóa Wi-Fi và thông tin ghép nối Matter). Reset chạy lúc nhả nút, nên giữ quá 5 giây vẫn được.

Đổi chân nút trong menuconfig → **Button** (`-1` để tắt).

## Phần mềm

Yêu cầu: **ESP-IDF v6.0.x** và **esp-matter `release/v1.6`** (giống image Docker CI `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`).

```bash
cd ESP32-C3/matter-wifi/matter-rgb-light-ws2812
. $IDF_PATH/export.sh
. $ESP_MATTER_PATH/export.sh

idf.py set-target esp32c3
idf.py menuconfig          # (tùy chọn) số LED, mức 30 %, GPIO
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Hoặc build bằng Docker, không cần cài gì:

```bash
docker run --rm -v "$PWD":/project -w /project espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c '. $IDF_PATH/export.sh && . $ESP_MATTER_PATH/export.sh && cd /project && idf.py set-target esp32c3 && idf.py build'
```

### File firmware

Mỗi lần `idf.py build` tạo thêm 3 file trong `build/`, tên có **hậu tố là phiên bản firmware**:

| File | Dùng khi | Nạp tại | Ghép nối, Wi-Fi |
|---|---|---|---|
| `matter_rgb_light_ws2812_esp32c3_update_<phiên bản>.bin` | Cập nhật đèn đang dùng qua USB | `0x1D000` | Giữ nguyên |
| `matter_rgb_light_ws2812_esp32c3_merged_<phiên bản>.bin` | Đèn mới, hoặc muốn xóa sạch | `0x0` | Bị xóa |
| `matter_rgb_light_ws2812_esp32c3_<phiên bản>.bin` | Chỉ có app: nguồn để tạo file OTA Matter | (OTA) | Giữ nguyên |

```bash
# Cập nhật, giữ ghép nối (thay <phiên bản>)
esptool.py --chip esp32c3 -p /dev/ttyACM0 write_flash 0x1d000 build/matter_rgb_light_ws2812_esp32c3_update_<phiên bản>.bin
```

File `update` gồm vùng OTA data (để trống) và app: đèn luôn khởi động bản vừa nạp, kể cả khi trước đó một bản OTA Matter đã chuyển sang phân vùng `ota_1`. Nếu chỉ nạp app tại `0x20000` trong trường hợp đó, đèn vẫn chạy bản OTA cũ. Không nạp file nào vào `0x10000`: đó là vùng NVS (ghép nối và Wi-Fi).

Ví dụ: `matter_rgb_light_ws2812_esp32c3_merged_1.0.2610131432.bin`.

Phiên bản có dạng `1.0.<thời điểm build UTC yyMMddHHmm>` và được lấy lúc CMake cấu hình. Khi build lại trong cùng thư mục, chạy `idf.py fullclean` trước để có phiên bản mới (CI luôn build mới nên mỗi lần đều có phiên bản riêng).

Mỗi lần push, GitHub Actions chạy test và build sẵn firmware, artifact tên `matter_rgb_light_ws2812-esp32c3-<phiên bản>` chứa 3 file trên.

Log và console Matter chạy qua **cổng USB có sẵn** của ESP32-C3 (USB-Serial-JTAG), nạp firmware cũng qua cổng này. Nếu không nạp được, giữ nút BOOT, nhấn RST, rồi nhả BOOT.

## Thông tin thiết bị

Ứng dụng nhà thông minh hiển thị:

| Mục | Giá trị | Sửa ở đâu |
|---|---|---|
| Nhà sản xuất (Manufacturer) | `Haxeko` | `menuconfig` → *Device Information* |
| Kiểu máy (Model) | `Vibe Light` | `menuconfig` → *Device Information* |
| Tên thiết bị khi ghép nối | `Vibe Light` | `menuconfig` → *Device Information* |
| Số serial | Địa chỉ MAC Wi-Fi, ví dụ `8ED0B2A8594B` | Tự động, riêng cho từng chip |
| Tên Bluetooth khi chờ ghép nối | `VibeLight-3840` | `CONFIG_BLE_DEVICE_NAME_PREFIX` trong `sdkconfig.defaults` |
| Firmware | `1.0.<yyMMddHHmm>` | Tự động |

Thiết bị đã ghép nối từ trước sẽ thấy Manufacturer/Model mới sau khi nạp firmware mới. Một số app (Apple Home) lưu tạm thông tin cũ, có thể cần xóa rồi thêm lại đèn mới hiện tên mới.

## Hàng ô màu trong Apple Home

Hàng ô màu dưới thanh độ sáng là **giao diện của app Home**, Apple tự quản lý. Matter không có thuộc tính nào để đèn đặt số ô hay màu trong từng ô. Đèn chỉ quyết định nó khai báo khả năng gì: đèn này khai báo cả **màu** và **nhiệt độ màu** (bắt buộc với device type Extended Color Light), nên app Home có ô màu, ô **trắng ấm / trắng lạnh** và tab *Nhiệt độ*. Ô ngoài cùng bên phải (viền cầu vồng) mở bánh xe để chọn màu.

Sửa một ô: nhấn giữ đèn, chạm một ô màu, chọn *Sửa*, chọn màu mới rồi *Xong*; ô đó lưu màu mới.

**Đèn đã ghép với firmware chỉ RGB** (bản không có nhiệt độ màu, từ 1.0.2610140424 đến 1.0.2610141656): Apple Home có thể vẫn nhớ đèn không có nhiệt độ màu. Nếu sau khi cập nhật không thấy ô trắng và tab *Nhiệt độ*, hãy **xóa đèn khỏi app Home, giữ nút BOOT 5 giây rồi nhả để reset, rồi thêm lại**.

## Ghép nối

Thiết bị được ghép bằng **Bluetooth** như các thiết bị Matter Wi-Fi thông thường: điện thoại gửi tên/mật khẩu Wi-Fi (2.4 GHz) cho đèn qua BLE.

Thông tin ghép nối mặc định (chứng chỉ test của esp-matter):

| | |
|---|---|
| Mã QR | `MT:Y.K9042C00KA0648G00` |
| Mã thủ công | `3497-011-2332` |
| Passcode / Discriminator | `20202021` / `3840` |

Tạo ảnh QR từ chuỗi trên, ví dụ: `https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00`.

- **Apple Home**: Thêm phụ kiện → quét mã QR (hoặc nhập mã thủ công). iOS báo "phụ kiện chưa được chứng nhận" → Vẫn thêm.
- **Home Assistant**: dùng app Home Assistant trên điện thoại → Settings → Devices & services → Add Matter device.
- **Google Home / SmartThings / Alexa**: thêm thiết bị Matter, quét mã. Cần bật chế độ developer/test cho thiết bị chưa chứng nhận theo hướng dẫn của từng hãng.
- **chip-tool**:
  ```bash
  chip-tool pairing ble-wifi 1 <SSID> <mat-khau> 20202021 3840
  chip-tool onoff on 1 1
  chip-tool levelcontrol move-to-level 127 0 0 0 1 1   # 50 % = 15 LED
  chip-tool colorcontrol move-to-hue-and-saturation 0 254 0 0 0 1 1   # đỏ
  ```

Khi xóa thiết bị khỏi tất cả ứng dụng, thiết bị tự mở lại chế độ ghép nối 5 phút (qua Wi-Fi đã lưu). Muốn ghép lại từ đầu, giữ nút BOOT 5 giây rồi nhả ra để factory reset.

## Khi app báo "Không phản hồi"

"Không phản hồi" (No Response) nghĩa là app không liên lạc được với đèn qua mạng. Cách đèn thể hiện độ sáng (số LED sáng) **không liên quan**: qua Matter, đèn chỉ báo thuộc tính chuẩn `CurrentLevel` của cluster Level Control và trả lời lệnh như mọi đèn Matter khác. App không biết và cũng không kiểm tra đèn hiển thị độ sáng ra sao.

Firmware tự bảo vệ:
- **Tự khởi động lại khi treo**: luồng Matter phải chạy một việc nhỏ mỗi 5 giây. Nếu nó ngừng quá 30 giây (treo, kẹt), task watchdog khởi động lại đèn. Đèn khôi phục bật/tắt, độ sáng và màu như trước.
- **Wi-Fi không ngủ** sau khi ghép xong (menuconfig → **Wi-Fi** → *Keep the Wi-Fi radio awake*, mặc định bật). Mặc định ESP-IDF cho radio ngủ giữa các beacon: gói multicast mà app dùng để tìm đèn (mDNS, IPv6 neighbor discovery) phải chờ tới beacon DTIM và có thể bị mất với một số router. Đèn cắm điện nên giữ radio luôn thức, tốn thêm vài chục mA.
- **Log chẩn đoán** trên cổng USB: lý do khởi động lần trước, mỗi 60 giây một dòng trạng thái, lý do mỗi lần mất Wi-Fi.

### Tìm nguyên nhân

1. **Nhấn nút BOOT** khi app đang báo Không phản hồi:
   - Đèn bật/tắt theo nút: firmware vẫn chạy bình thường, lỗi nằm ở đường mạng (sóng Wi-Fi, router, hub Apple TV/HomePod).
   - Đèn không đổi: đèn mất nguồn, hoặc firmware bị treo (khi đó watchdog sẽ khởi động lại đèn trong vòng 30 giây).
2. **Ghi log**: cắm đèn vào máy tính bằng cáp USB **từ trước** và để chạy cho tới khi lỗi xảy ra (rút ra cắm lại là khởi động lại đèn, mất dấu vết). Mở log mà không reset chip:
   ```bash
   idf.py -p /dev/ttyACM0 monitor --no-reset --timestamps    # Ctrl+T rồi Ctrl+L: lưu log ra file
   # hoặc không cần ESP-IDF (pip install pyserial; Windows: COM3...)
   python -m serial.tools.miniterm --dtr 0 --rts 0 /dev/ttyACM0 115200
   ```
3. **Đọc log** (ví dụ):
   ```
   W (512) app_health: Reset reason: brownout (supply voltage dropped) (9)
   I (60512) app_health: up 60 s | heap 151208 free, 98012 min, 65536 largest | Wi-Fi -58 dBm ch 6, 3 IPv6, 0 drops | 1 fabrics, 2 subscriptions
   W (912345) app_health: Wi-Fi connection lost: reason 200, RSSI -86 dBm (1 since boot)
   ```
   | Dấu hiệu | Nguyên nhân thường gặp |
   |---|---|
   | `Reset reason: brownout` | Nguồn sụt áp khi LED và Wi-Fi cùng ăn dòng: dùng nguồn 5V khỏe hơn, dây ngắn và to hơn, thêm tụ |
   | `Reset reason: task watchdog` hoặc `crash (panic)` | Firmware treo hoặc lỗi: gửi log (đoạn trước lần khởi động lại) để sửa |
   | RSSI yếu hơn khoảng -75 dBm, `drops` tăng dần | Sóng yếu hoặc ăng-ten kém: dời đèn/router, xem mục SuperMini bên dưới |
   | `reason 200` (beacon timeout), `201` (không thấy AP), `15`/`204` (bắt tay quá hạn) | Sóng yếu, nhiễu, router khởi động lại hoặc đổi kênh |
   | `heap ... free` giảm đều theo từng giờ | Rò rỉ bộ nhớ: gửi log |
   | Wi-Fi tốt, `fabrics` ≥ 1 nhưng `0 subscriptions` | App/hub mất kết nối tới đèn: kiểm tra router (bên dưới), khởi động lại hub |

   Không có máy tính bên cạnh: số lần khởi động của đèn nằm trong cluster General Diagnostics (`RebootCount`), đọc bằng chip-tool (`chip-tool generaldiagnostics read reboot-count <node-id> 0`) hoặc trong file *Download diagnostics* của thiết bị trên Home Assistant. Số này tăng mà không ai rút điện nghĩa là đèn đã tự khởi động lại.

### Phần cứng và router

- **ESP32-C3 SuperMini**: nhiều lô có ăng-ten phối hợp kém, Wi-Fi chập chờn hoặc rớt khi phát công suất tối đa. Cách sửa phổ biến là giảm công suất phát: menuconfig → **Wi-Fi** → *Maximum Wi-Fi transmit power* → **8.5 dBm** (tầm phủ giảm, nên đặt router đủ gần). Tránh để dây nguồn, dải LED hay vật kim loại đè lên đầu ăng-ten của board.
- **Nguồn**: lúc phát Wi-Fi, ESP32-C3 có những xung dòng vài trăm mA, cộng thêm dải LED. Nguồn yếu hoặc dây dài làm sụt áp, gây brownout hoặc lỗi Wi-Fi. Dùng nguồn 5V từ 1 A trở lên, tụ 470–1000 µF ở đầu dải LED, và thêm tụ 10–100 µF sát chân 5V/GND của SuperMini.
- **Router**: phải có Wi-Fi 2.4 GHz; tắt *AP/client isolation* (cách ly thiết bị); nếu đèn hay mất kết nối, thử tắt *IGMP/MLD snooping* hoặc *multicast to unicast*; hub (Apple TV/HomePod) cùng mạng với đèn.

## Cấu trúc mã nguồn

| File | Nội dung |
|---|---|
| `main/app_main.cpp` | Khởi tạo NVS, tạo node Matter, endpoint Extended Color Light (Hue/Saturation, XY, nhiệt độ màu 2000–6500 K) |
| `main/app_health.cpp` | Watchdog cho luồng Matter, Wi-Fi không ngủ, công suất phát Wi-Fi, log lý do khởi động / trạng thái 60 giây / mất Wi-Fi |
| `main/device_info.cpp` | Manufacturer / Model / tên / số serial (Basic Information) |
| `main/app_driver.cpp` | Nhận thay đổi OnOff / LevelControl / ColorControl từ Matter, Identify, nút BOOT |
| `main/led_bar.c` | Điều khiển WS2812 qua RMT (`espressif/led_strip`): số LED sáng theo độ sáng, cường độ cố định 30 %, nháy Identify; chỉ gửi dữ liệu ra LED khi hình ảnh thay đổi |
| `main/color_convert.c` | Đổi Hue/Saturation, XY sang RGB và ngược lại |
| `main/Kconfig.projbuild` | Tùy chọn: Manufacturer/Model, GPIO, số LED, mức 30 %, chiều sáng, chân nút, Wi-Fi (không ngủ, công suất phát) |
| `test/host/` | Test chạy trên máy tính (không cần ESP32): driver dải LED và đổi màu |

## Kiểm thử

```bash
test/host/run.sh      # chỉ cần gcc; CI chạy bước này trước khi build firmware
```

Test kiểm tra: số LED theo từng mức độ sáng (bảng ở trên), cường độ cố định 30 %, chỉ refresh khi hình ảnh đổi, không nháy tắt khi Bật (hiệu ứng fade-in của Matter), nháy Identify, chiều sáng ngược, dải LED khởi tạo lỗi; đổi màu Hue/Saturation ↔ RGB ↔ XY (các màu chuẩn sRGB; sai số khứ hồi Hue/Saturation → XY → Hue/Saturation đo được 1/254).

## Giới hạn đã biết và lưu ý trước khi bán

- **Chứng chỉ test**: DAC/PAI, Vendor ID `0xFFF1`, Product ID `0x8000` và mã ghép nối `20202021` / `3840` là dữ liệu test của esp-matter, **mọi đèn giống nhau**. Hàng bán ra cần mã riêng cho từng đèn và chứng chỉ thật (công cụ `esp-matter-mfg-tool`, phân vùng `esp_secure_cert` và `fctry` đã có sẵn trong `partitions.csv`).
- **Bảo mật**: chưa bật Secure Boot, Flash Encryption, NVS Encryption; console Matter (`CONFIG_ENABLE_CHIP_SHELL`) mở trên cổng USB. Nên tắt shell và bật mã hóa ở bản phát hành.
- **OTA**: chưa bật rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Nếu một bản OTA lỗi ngay khi khởi động, đèn không tự quay về bản cũ, phải nạp lại qua USB.
