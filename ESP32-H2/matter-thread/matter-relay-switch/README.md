# ESP32-H2 Matter over Thread: công tắc relay GPIO4

Firmware ESP-IDF + [esp-matter](https://github.com/espressif/esp-matter) cho **ESP32-H2**. Thiết bị là một **công tắc/ổ cắm Matter** (On/Off Plug-in Unit) điều khiển relay ở **GPIO4**, kết nối mạng bằng **Thread** (802.15.4), commissioning qua **BLE**.

Cấu trúc Matter giống bản ESP32-C3 Wi-Fi trong repo này (`ESP32-C3/matter-wifi/matter-energy-bl0942`), bỏ phần đo công suất và thay Wi-Fi bằng OpenThread.

| Chức năng | Chân |
|---|---|
| Relay (active-high mặc định) | **GPIO4** |
| Nút BOOT, nhấn ngắn: bật/tắt relay | GPIO9 |
| Nút BOOT, giữ 5 s (LED chuyển đỏ) rồi thả: LED nháy đỏ 3 lần, factory reset | GPIO9 |
| LED RGB WS2812 trên board: xanh lá khi relay ON, nháy xanh dương khi Identify | GPIO8 |

- **Trạng thái relay sau mất điện:** relay quay về trạng thái cuối cùng (Matter `StartUpOnOff` = null). Home Assistant đổi được trong mục *Power-on behavior* (luôn tắt / luôn bật / đảo / trạng thái cuối).
- **Khởi động lại mềm** (OTA, factory reset, crash, watchdog): relay giữ nguyên mức, không bị tắt rồi bật lại. Nếu thiết bị khởi động lại liên tục trước khi Matter chạy xong (lỗi lặp lúc khởi động), relay về OFF để tải không bị giữ ON mà không điều khiển được. Cắm điện lần đầu hoặc sụt áp thì relay bắt đầu từ OFF rồi mới khôi phục trạng thái cuối.
- **GPIO4 lúc cấp điện:** GPIO4 là chân MTCK của JTAG. Từ lúc cấp điện tới khi firmware chạy (~0,5 s), chip tự bật điện trở kéo lên ~45 kΩ trên chân này. Với mạch kích relay active-high (NPN/MOSFET), **phải có điện trở kéo xuống ≤10 kΩ** từ IN/cực G xuống GND, nếu không relay có thể nhảy lúc cấp điện. Module relay active-low không bị ảnh hưởng.
- **Thông tin thiết bị** (Basic Information), xem trong Apple Home ở phụ kiện → cài đặt, hoặc dòng log `device_info: Manufacturer: ..., Model: ..., Hardware: ..., Serial number: ...` khi khởi động:
  - Nhà sản xuất: **Haxeko**
  - Kiểu máy: **ESP32-H2**
  - Phần cứng: **ESP32-H2-DevKitM-1 v1.0**
  - Số sê-ri: **địa chỉ MAC gốc** của chip (eFuse, giống dòng `BASE MAC` của esptool), 12 ký tự hex in hoa, ví dụ `744DBDA1B2C3`. Mỗi chip một số riêng, không đổi khi OTA hay `erase-flash`.
- **Identify:** nút *Identify* trong Home Assistant (hoặc lệnh Identify của app khác) làm LED nháy xanh dương để nhận ra thiết bị.
- **Xoá khỏi hệ cuối cùng:** khi thiết bị bị xoá khỏi hệ Matter cuối cùng (Apple Home / HA / Aqara), nó tự factory reset (relay OFF) và mở lại cửa sổ ghép nối BLE 15 phút với mã QR gốc.
- Thiết bị chạy dạng **FTD** (Full Thread Device): cấp nguồn lưới nên có thể làm Thread Router, giúp mở rộng mạng mesh.

## Yêu cầu mạng

Matter over Thread cần một **Thread Border Router** trong nhà, ví dụ:

- Apple: HomePod mini / HomePod 2 / Apple TV 4K (bản có Ethernet hoặc 2022)
- Google: Nest Hub (2nd gen) / Nest Hub Max / Nest Wifi Pro
- Amazon: Echo 4th gen
- Home Assistant: SkyConnect / ZBT-1 + OpenThread Border Router add-on
- Tự làm: ESP Thread Border Router (ESP32-S3 + ESP32-H2)

Điện thoại gửi thông tin mạng Thread cho thiết bị qua BLE trong lúc ghép nối, nên không cần nhập SSID/mật khẩu.

## Cấu hình

`idf.py menuconfig` → menu **Relay Configuration**, **Thread Diagnostics**, **Device Identity**:

| Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `RELAY_GPIO` | 4 | chân điều khiển relay (*Relay Configuration*) |
| `RELAY_ACTIVE_LEVEL` | 1 | 1 = active-high, 0 = active-low; module relay opto rẻ tiền thường là active-low (*Relay Configuration*) |
| `STATUS_LED_ENABLE` | y | LED WS2812 GPIO8; tắt nếu board tự làm không có LED này, khi đó Identify báo "không có đèn" (*Relay Configuration*) |
| `THREAD_DIAG_LOG` | y | cứ 10 s in log chẩn đoán Thread/SRP cho tới khi ghép nối xong (*Thread Diagnostics*) |
| `DEVICE_VENDOR_NAME` | Haxeko | nhà sản xuất, tối đa 32 ký tự (*Device Identity*) |
| `DEVICE_PRODUCT_NAME` | ESP32-H2 | kiểu máy, tối đa 32 ký tự (*Device Identity*) |
| `DEVICE_HARDWARE_VERSION_STRING` | ESP32-H2-DevKitM-1 v1.0 | phiên bản phần cứng (*Device Identity*) |
| `SERIAL_NUMBER_PREFIX` | (trống) | tiền tố thêm trước số sê-ri (địa chỉ MAC), tối đa 20 ký tự, ví dụ `RLY-` → `RLY-744DBDA1B2C3` (*Device Identity*) |

Hoặc sửa trực tiếp `sdkconfig.defaults` / `sdkconfig.defaults.esp32h2` rồi xoá `sdkconfig` (và `sdkconfig.debug` nếu dùng bản debug) để áp dụng lại. Độ dài các chuỗi được kiểm tra lúc build.

Khi đổi chân relay, tránh: các chân strapping (GPIO2, GPIO3, GPIO8, GPIO9, GPIO25; trên DevKitM-1 GPIO8 là LED, GPIO9 là nút BOOT), chân flash (GPIO15–GPIO21), GPIO23/GPIO24 (UART0 console; ROM và bootloader in log trên GPIO24 mỗi lần khởi động, relay sẽ nhảy loạn) và GPIO26/GPIO27 (USB D-/D+). GPIO0/1/5/10–14/22 không có điện trở kéo lên lúc reset.

## Build

Yêu cầu: ESP-IDF v6.0.2 + esp-matter release/v1.6 (đã `export.sh` cả hai). Cách nhanh nhất là dùng Docker. Mount **cả repo** (không chỉ thư mục dự án) để mỗi commit mới tự lấy giờ build mới cho số phiên bản:

```bash
# chạy từ bất kỳ thư mục nào trong repo
docker run --rm -it -v "$(git rev-parse --show-toplevel)":/repo \
  espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c "git config --global --add safe.directory /repo && cd /repo/ESP32-H2/matter-thread/matter-relay-switch && { [ -f sdkconfig ] || idf.py set-target esp32h2; } && idf.py build"
```

`set-target` chỉ chạy khi chưa có `sdkconfig`, nên các lần sau build nối tiếp và giữ thay đổi menuconfig. Đổi `sdkconfig.defaults` thì xoá `sdkconfig` (bản debug: `sdkconfig.debug`) để áp dụng.

Hoặc cài local:

```bash
. ~/esp/esp-idf/export.sh
. ~/esp/esp-matter/export.sh
idf.py set-target esp32h2
idf.py build
idf.py -p /dev/ttyUSB0 erase-flash flash monitor
```

`/dev/ttyUSB0` (Windows: `COMx`) là cổng USB-C **UART** của DevKit: nạp và xem log được; gõ lệnh trên cổng này chỉ được với [bản build debug](#bản-build-debug-chip-shell--openthread-cli) (bản phát hành không có shell). Cổng USB gốc (`/dev/ttyACM0`) cũng nạp được, nhưng monitor trên cổng đó chỉ xem log, không gõ lệnh được.

### File firmware có hậu tố phiên bản

Mỗi lần build tự tạo trong `build/` một file firmware đầy đủ mang tên phiên bản, để phân biệt file của các lần build khác nhau (ví dụ phiên bản `1.0.2610131450`):

| File | Dùng để |
|---|---|
| `matter_relay_h2-<phiên bản>.bin` | nạp một lần ở địa chỉ `0x0` (bootloader + bảng phân vùng + app). Xoá cả vùng NVS nên phải ghép nối lại |
| `matter_relay_h2.bin` | file app, dùng với `idf.py flash` / `write-flash @flash_args` |
| `matter_relay_h2-ota.bin` | cập nhật OTA qua Matter |

```bash
esptool --chip esp32h2 -p /dev/ttyUSB0 write-flash 0x0 build/matter_relay_h2-1.0.2610131450.bin
```

(cú pháp esptool v5, có sẵn trong ESP-IDF v6; esptool v4.x dùng `esptool.py ... write_flash`). Muốn giữ ghép nối thì nạp bằng `idf.py flash`, hoặc `write-flash @flash_args` trong thư mục `build/`.

GitHub Actions (`.github/workflows/matter-relay-h2-thread.yml` ở gốc repo) tự build và upload, tên artifact cũng có phiên bản:

- `matter_relay_h2-<phiên bản>`: file `matter_relay_h2-<phiên bản>.bin` (nạp ở `0x0`), `matter_relay_h2-ota.bin`, cùng `flash_args`, `bootloader/`, `partition_table/`, `ota_data_initial.bin`, `matter_relay_h2.bin` để nạp bằng `write-flash @flash_args`;
- `matter_relay_h2-<phiên bản>-debug`: file `.elf`/`.map` để giải mã địa chỉ khi thiết bị crash.

Vào tab **Actions** → lần chạy mới nhất → **Artifacts**.

> **Nâng cấp từ bản cũ:** bản này bật OTA rollback nên bootloader thay đổi. Bootloader không cập nhật được qua OTA, nên phải nạp qua USB một lần (`idf.py flash`, hoặc `write-flash @flash_args` trong thư mục artifact; hai cách này giữ nguyên ghép nối). Dùng file `matter_relay_h2-<phiên bản>.bin` (nạp ở `0x0`) thì vùng NVS bị xoá, phải ghép nối lại.

### Bản build debug (CHIP shell + OpenThread CLI)

Bản phát hành tắt CHIP shell và OpenThread CLI để tiết kiệm ~95 KB flash (bản debug ~1,52 MB, vẫn vừa phân vùng OTA); log `thread_diag` đã đủ để chẩn đoán. Khi cần gõ lệnh trên cổng UART, build bản debug ở thư mục riêng:

```bash
idf.py -B build-debug -D IDF_TARGET=esp32h2 -D SDKCONFIG=sdkconfig.debug \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.debug" build
idf.py -B build-debug -p /dev/ttyUSB0 flash monitor
```

Sau khi sửa `sdkconfig.defaults*` (kể cả `sdkconfig.defaults.debug`), xoá `sdkconfig.debug` rồi build lại; nếu không, các giá trị cũ trong `sdkconfig.debug` vẫn được giữ mà không có cảnh báo.

## OTA

- Mỗi bản build tạo thêm `build/matter_relay_h2-ota.bin` (định dạng Matter OTA) để đưa vào một OTA provider (ví dụ `chip-ota-provider-app`).
- **Rollback:** image mới khởi động ở trạng thái chờ xác nhận. Firmware chỉ xác nhận khi đã kết nối Thread liên tục 60 s, đang ở trong mạng Thread thật (làm child/router, hoặc leader có thiết bị lân cận; tự lập mạng một mình không tính) và vẫn còn các ghép nối. Nếu image mới crash hoặc khởi động lại trước khi được xác nhận, bootloader tự quay về bản cũ. Image không vào được mạng thì không bao giờ được xác nhận: rút điện cắm lại là quay về bản cũ.

## Phiên bản firmware

Mỗi lần build, firmware tự có phiên bản mới dạng `1.0.<yyMMddHHmm>` (giờ build theo UTC). Ví dụ `1.0.2610131450` là bản build lúc 14:50 UTC ngày 13/10/2026. Xem ở Apple Home (phụ kiện → cài đặt → **Firmware**) hoặc dòng `App version:` trong log khởi động. Tên file firmware đầy đủ cũng là phiên bản này: `matter_relay_h2-1.0.2610131450.bin`.

- Số phiên bản Matter dùng cho OTA (`SoftwareVersion`) là phần thời gian `yyMMddHHmm`, nên luôn tăng.
- Build local: commit mới (hoặc checkout) sẽ tự cấu hình lại và lấy phiên bản mới ở lần build sau. Chỉ sửa file thì phiên bản không đổi cho tới lần cấu hình lại (`idf.py reconfigure` hoặc `fullclean`). GitHub Actions luôn build từ đầu nên mỗi lần chạy đều có phiên bản mới. Đổi `FW_VERSION_MAJOR_MINOR` trong `CMakeLists.txt` khi muốn tăng số chính.

## Commissioning

Firmware dùng chứng chỉ test (DAC test) của esp-matter, mã ghép nối mặc định:

- Manual code: **34970112332**
- QR: `MT:Y.K9042C00KA0648G00`
- Discriminator 3840, passcode 20202021

Mở Apple Home / Google Home / SmartThings / Home Assistant → *Add Matter device* → quét QR hoặc nhập mã ở trên. Vì dùng chứng chỉ test, app sẽ cảnh báo "Uncertified accessory": chọn tiếp tục.

Mọi mạch nạp firmware này đều dùng **chung mã ghép nối** trên: khi có nhiều mạch, ghép nối **từng mạch một** (các mạch khác chưa ghép thì để tắt nguồn).

Với chip-tool (lấy dataset từ Border Router, ví dụ `ot-ctl dataset active -x`):

```bash
chip-tool pairing ble-thread 0x7283 hex:<ACTIVE_DATASET> 20202021 3840
chip-tool onoff toggle 0x7283 1
chip-tool basicinformation read serial-number 0x7283 0
```

Sau khi bật nguồn, thiết bị quảng bá BLE nhanh trong 30 s rồi chuyển sang chậm (log `bleAdv Timeout : Start slow advertisement`, chu kỳ 500 ms). Đây là bình thường: thiết bị vẫn chờ ghép nối, cứ quét QR như bình thường. Cửa sổ ghép nối mở 15 phút; hết thời gian thì rút điện cắm lại.

Thêm vào hệ thứ hai (ví dụ đã có Home Assistant, muốn thêm Apple Home): mã QR gốc không dùng lại được khi thiết bị đã ghép. Dùng chức năng chia sẻ của hệ đang quản lý (HA: *Share device*; Apple Home: *Bật chế độ ghép nối*) để lấy mã mới. Chuyển sang mạng Thread khác: giữ BOOT 5 s để factory reset trước.

> Nếu thiết bị đã từng được thêm vào Home với firmware khác, hãy xoá phụ kiện, `erase-flash`, nạp lại rồi thêm lại.

## Xử lý sự cố: điện thoại không tìm thấy thiết bị

Ghép nối Matter over Thread có 2 giai đoạn:

1. **Qua BLE:** điện thoại tìm thấy thiết bị, kiểm tra chứng chỉ, gửi thông tin mạng Thread. Log: `Role detached -> child`, `advertising srp service: ...._matter._tcp`.
2. **Qua mạng IP:** điện thoại tìm dịch vụ `_matter._tcp` của thiết bị thông qua Border Router (Wi-Fi/LAN → Thread) rồi mở phiên bảo mật CASE. Log thành công: `Commissioning complete`.

Nếu giai đoạn 1 xong mà app báo không tìm thấy thiết bị, thường là do mạng nhà, không phải do firmware:

- Điện thoại và Border Router phải cùng một mạng Wi-Fi/LAN (không dùng mạng khách, tắt VPN trên điện thoại).
- Router Wi-Fi phải bật **IPv6** và không chặn **mDNS/multicast**; tắt "AP/client isolation".
- Border Router phải là của hệ đang dùng (Apple Home → HomePod/Apple TV làm Home hub; Google Home → Nest Hub/Nest Wifi; Home Assistant → OTBR add-on). Thử khởi động lại Border Router.
- Mỗi lần thử lại: `erase-flash` hoặc factory reset (giữ nút BOOT 5 s) để xoá thông tin mạng Thread cũ.

### Log chẩn đoán tự động

Khi chưa được ghép nối, cứ 10 s firmware tự in khối `thread_diag` ra log (tắt bằng `THREAD_DIAG_LOG`, tự dừng khi ghép nối xong):

```
I thread_diag: ===== Thread diagnostics =====
I thread_diag: Role: router, RLOC16: 0x7c00
I thread_diag: Address: fd61:c2f3:2668:ad22:0:ff:fe00:7c00  [mesh-local RLOC]
I thread_diag: Address: fd12:3456:789a:1:abcd:...  [OMR (reachable from Wi-Fi/LAN)]
I thread_diag: On-mesh prefix: fd12:3456:789a:1::/64 (border router RLOC16 0x2c00, SLAAC)
I thread_diag: SRP host: 744DBDA1B2C3..., state: Registered
I thread_diag: SRP service: 2AB1...-....._matter._tcp, state: Registered
I thread_diag: Summary: OMR address YES, on-mesh prefixes 1, SRP host Registered, SRP service Registered
```

Chỉ cần xem log, không phải gõ lệnh. `OMR address NO` hoặc SRP không `Registered` nghĩa là lỗi phía Border Router / mạng LAN.

### Gõ lệnh OpenThread CLI (chỉ bản build debug)

Cần [bản build debug](#bản-build-debug-chip-shell--openthread-cli). Gõ lệnh trên serial monitor ở **115200 baud**, qua cổng USB-C **UART** của DevKit (`COMx` / `/dev/ttyUSB0`). Cổng USB gốc chỉ xuất log, **không nhận lệnh gõ**. Gõ `matter help` để thử. Gõ trong lúc điện thoại vẫn đang ghép nối:

| Lệnh | Kết quả mong đợi |
|---|---|
| `matter esp ot_cli state` | `router` hoặc `child` |
| `matter esp ot_cli ipaddr` | ngoài `fe80::...` và địa chỉ mesh-local (cùng tiền tố `fdxx:...` của dòng `detected server`), phải có thêm **một địa chỉ có tiền tố khác** (địa chỉ OMR do Border Router cấp). Không có địa chỉ này thì điện thoại không thể tới được thiết bị |
| `matter esp ot_cli netdata show` | mục `Prefixes:` có ít nhất 1 prefix (OMR), mục `Services:` có SRP server |
| `matter esp ot_cli srp client host` | `state:Registered` |
| `matter esp ot_cli srp client service` | `_matter._tcp ... state:Registered` |
| `matter esp ot_cli ping <địa chỉ>` | ping được Border Router / máy trong LAN |

- Không có địa chỉ OMR / `netdata` không có prefix: Border Router chưa định tuyến được giữa Wi-Fi và Thread (lỗi phía Border Router hoặc router Wi-Fi).
- SRP `state:Registered` và có địa chỉ OMR mà điện thoại vẫn không thấy: Border Router không quảng bá dịch vụ lên LAN, hoặc mạng Wi-Fi chặn mDNS.

## Cấu trúc

```
CMakeLists.txt             project esp-matter (esp32h2), phiên bản firmware theo giờ build, file firmware đầy đủ mang tên phiên bản
sdkconfig.defaults         cấu hình chung: Matter, BLE tối giản, OTA + rollback, tối ưu flash, cluster không dùng
sdkconfig.defaults.esp32h2 OpenThread (FTD, SRP client), chỉ IPv6, relay GPIO4, LED trạng thái
sdkconfig.defaults.debug   bật thêm CHIP shell + OpenThread CLI (bản build debug)
partitions.csv             4 MB, 2 phân vùng OTA
dependencies.lock          phiên bản cố định của các managed component
main/app_main.cpp          node + endpoint On/Off Plug-in Unit, Identify, sự kiện Matter, xác nhận OTA
main/app_driver.cpp        relay GPIO (giữ trạng thái qua reset mềm), nút BOOT, LED trạng thái/Identify/reset
main/app_priv.h            khai báo dùng chung của driver
main/device_info.cpp       nhà sản xuất, kiểu máy, phần cứng, số sê-ri MAC (custom Device Instance Info provider)
main/thread_diag.cpp       log chẩn đoán Thread/SRP khi chưa ghép nối
main/Kconfig.projbuild     menu Relay Configuration / Thread Diagnostics / Device Identity
```

## Sơ đồ đấu nối

```
ESP32-H2 GPIO4 ──► IN  (module relay 3.3V, hoặc qua transistor NPN/MOSFET + diode)
            IN ── 10k ── GND   (bắt buộc với mạch kích active-high NPN/MOSFET, xem mục GPIO4 ở trên)
ESP32-H2 3V3   ──► VCC (module relay 5V: cấp 5V, IN vẫn dùng mức 3.3V)
ESP32-H2 GND   ──► GND
```

⚠️ Phía tải dùng điện 220 V: cách ly và đi dây an toàn. Không cắm USB vào máy tính khi mạch đang nối điện lưới mà không có cách ly.
