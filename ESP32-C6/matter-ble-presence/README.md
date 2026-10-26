# ESP32-C6 · Matter over Thread · 5 cảm biến hiện diện từ iBeacon

Một thiết bị Matter (một node, ghép một lần) có **5 endpoint Occupancy Sensor cố định** (Presence 1–5). Mỗi endpoint
theo dõi một iBeacon do người dùng gán (theo địa chỉ MAC + loại địa chỉ): nghe được gói iBeacon hợp lệ → có hiện
diện ngay; hết thời gian vắng mặt mà không nghe thấy → không hiện diện. Beacon được gán qua trang web cấu hình chạy
trên Wi-Fi SoftAP của chính thiết bị (chế độ CONFIG, vào bằng cách bấm BOOT 3 lần).

Chỉ dùng ESP-IDF và esp-matter chính thức của Espressif, NimBLE của ESP-IDF, OpenThread của ESP-IDF. Không Arduino,
không ESPHome, không Matter bridge.

> **Bản ESP32-C3 (Matter over Wi-Fi):** [`ESP32-C3/matter-ble-presence`](../../ESP32-C3/matter-ble-presence) dùng chung
> mã nguồn, bảng phân vùng và phần cấu hình chung của thư mục này (`ble_presence.cmake`), chỉ khác mạng Matter (Wi-Fi
> thay Thread), product ID và các tùy chọn tiết kiệm RAM. Cấu hình riêng của bản C6 (Thread) nằm trong
> `sdkconfig.defaults.esp32c6`.
>
> **Bản hai chip (ESP32-C3 quét BLE + ESP32-H2 Matter over Thread, nối UART):**
> [`ESP32-C3/ble-presence-scanner`](../../ESP32-C3/ble-presence-scanner) và
> [`ESP32-H2/matter-ble-presence-h2`](../../ESP32-H2/matter-ble-presence-h2), cùng mã nguồn này với vai trò chọn bằng
> Kconfig `PRESENCE_ROLE`; mỗi chip một radio nên BLE scan và Thread không tranh nhau.

> **Trạng thái:** mã nguồn hoàn chỉnh, **build thành công** với ESP-IDF v6.0.2 + esp-matter v1.6.1 (kết quả ở mục
> [Kết quả build](#kết-quả-build-thực-tế)), host test đạt. **Chưa chạy trên phần cứng** trong phiên làm việc này: mọi
> hành vi RF (BLE cùng Thread), ghép nối với Apple Home / Google Home / Home Assistant, độ trễ và độ tin cậy phải đo
> theo [quy trình kiểm thử phần cứng](#quy-trình-kiểm-thử-phần-cứng). Không coi việc biên dịch được là hệ thống ổn định.

---

## Mục lục

1. [Phiên bản SDK (đã ghim)](#phiên-bản-sdk-đã-ghim)
2. [Phần cứng và giả định](#phần-cứng-và-giả-định)
3. [Kiến trúc](#kiến-trúc)
4. [BLE cùng Thread trên một chip](#ble-cùng-thread-trên-một-chip)
5. [Thuật toán hiện diện](#thuật-toán-hiện-diện)
6. [Lưu trữ và lỗi NVS](#lưu-trữ-và-lỗi-nvs)
7. [Build](#build)
8. [Nạp firmware](#nạp-firmware)
9. [Ghép Matter qua Thread](#ghép-matter-qua-thread)
10. [Cấu hình beacon (chế độ CONFIG)](#cấu-hình-beacon-chế-độ-config)
11. [Kiểm thử](#kiểm-thử)
12. [Đã xác minh / cần thử trên phần cứng](#đã-xác-minh--cần-thử-trên-phần-cứng)
13. [Giới hạn đã biết](#giới-hạn-đã-biết)
14. [Khuyến nghị: phương án hai chip](#khuyến-nghị-phương-án-hai-chip)

---

## Phiên bản SDK (đã ghim)

| Thành phần | Phiên bản | Commit |
|---|---|---|
| ESP-IDF | **v6.0.2** | `7101770dc6db2667b3c477cc31365dd1acd6db4e` |
| esp-matter | **release/v1.6.1** (Matter 1.6.1) | `4c9dd7aa80b3ddfa64c814bdfabe76c60944cc75` |
| connectedhomeip (submodule của esp-matter) | — | `b1902b39ac6734913ce5f4d2abf153693a50264b` |
| OpenThread (trong ESP-IDF v6.0.2) | — | `a98813b30ae58f9a95ece680b9cc46c3874de6ea` |
| Managed components (ESP Component Registry) | `espressif/cjson` 1.7.19~2, `espressif/mdns` 1.14.1, `espressif/esp_secure_cert_mgr` 2.9.3, `espressif/esp_encrypted_img` 2.7.0, `espressif/esp_delta_ota` 1.1.4 | ghim trong `dependencies.lock` (commit cùng mã nguồn) |
| Toolchain | riscv32-esp-elf GCC 15.2.0 (`esp-15.2.0_20251204`) | đi kèm ESP-IDF v6.0.2 |
| Docker image dùng để build | `espressif/esp-matter:release-v1.6.1_idf_v6.0.2` | amd64 `sha256:4e5ea1bfd18de6301c382e128968e22273f9ddbab89a33376e6aecba8658c4ed` |

Cặp phiên bản theo tài liệu chính thức: README của nhánh `release/v1.6.1` ghi *"This SDK currently works with
commit b1902b39ac of connectedhomeip"* và *"it is recommended to utilize ESP-IDF v6.0.2"*; trang *Developing with
the SDK* (bản latest) cũng dùng `git checkout v6.0.2`. Repo này đã dùng đúng cặp đó cho các firmware khác
(`matter-presence-sensor`, CI dùng cùng image Docker).

Lưu ý: tag Docker `release-v1.6.1_idf_v6.0.2` được build lại định kỳ (nhánh release/v1.6.1 tiếp tục nhận backport).
Commit esp-matter ở trên là commit có trong image đã dùng để build; khi cần tái lập chính xác, dùng digest amd64 ở
bảng (`docker pull espressif/esp-matter@sha256:4e5ea1bf...`) hoặc checkout đúng các commit.

Mọi API, Kconfig và hành vi được dùng trong mã đã được đối chiếu trực tiếp với mã nguồn của **đúng các commit này**
(trích dẫn đường dẫn file ở các mục bên dưới), không chép từ phiên bản khác.

---

## Phần cứng và giả định

| Hạng mục | Giả định | Căn cứ |
|---|---|---|
| Chip | ESP32-C6 (Thread 802.15.4 + BLE 5 + Wi-Fi 6, **một radio 2,4 GHz dùng chung**) | CMakeLists.txt từ chối target khác |
| Board tham chiếu | ESP32-C6-DevKitC-1 (ESP32-C6-WROOM-1, 8 MB) hoặc ESP32-C6-DevKitM-1 (ESP32-C6-MINI-1, 4 MB) | user guide chính thức của hai board |
| Flash | ≥ 4 MB. Bảng phân vùng dùng 4 MB đầu (`CONFIG_ESPTOOLPY_FLASHSIZE_4MB`). Trên DevKitC-1 (8 MB) firmware chạy bình thường, 4 MB sau không dùng | `partitions.csv` |
| Nút BOOT | **GPIO9, mức tác động thấp**, cấu hình được qua Kconfig (`PRESENCE_BOOT_GPIO`, `PRESENCE_BOOT_ACTIVE_LOW`, `PRESENCE_BOOT_PULLUP`) | sơ đồ DevKitC-1 v1.4: SW1 nối GPIO9 thẳng xuống GND, **không có điện trở kéo lên ngoài**, tụ chống dội C13 **không lắp (NC)** → firmware bật pull-up nội và chống dội 40 ms bằng phần mềm |
| LED | Không dùng (GPIO8 là LED RGB và cũng là chân strapping; không đụng tới) | |
| Nguồn | Cấp nguồn liên tục (USB/adapter), không chạy pin, không ngủ | Thread Minimal End Device, không light-sleep |
| Console | UART0 (cổng USB-UART của DevKit) và USB-Serial-JTAG (cổng USB) | mặc định ESP-IDF cho C6 |

### Chân BOOT khi reset phần cứng

GPIO9 là **chân strapping**. Nếu GPIO9 đang ở mức thấp **trong lúc chip reset hoặc cấp nguồn** (giữ BOOT rồi bấm
RESET/cắm điện), ROM vào **chế độ nạp firmware (download mode)**: firmware không chạy, không có NORMAL hay CONFIG.
Vì vậy chức năng bấm 3 lần **chỉ có tác dụng khi firmware đang chạy**; bấm BOOT trước/trong lúc reset không bao giờ
được firmware nhìn thấy. Nếu BOOT đang bị giữ khi firmware khởi động, bộ đếm bỏ qua cho tới khi nhả ra.

Trên DevKitC-1, mạch auto-download của chip USB-UART (DTR/RTS → transistor kép, bảng "Auto program" trong sơ đồ)
**cũng có thể kéo GPIO9 xuống** khi một phần mềm terminal điều khiển DTR/RTS trên cổng UART. `idf.py monitor` dùng
DTR/RTS để reset chip theo đúng trình tự nên không gây bấm giả; nếu dùng terminal khác và thấy log "Triple click"
lạ khi mở cổng, hãy tắt điều khiển DTR/RTS hoặc xem log qua cổng USB (USB-Serial-JTAG).

---

## Kiến trúc

### Hai chế độ, chọn một lần khi khởi động

```
                 cấp nguồn / reset bất kỳ
                          │
                          ▼
   ┌───────────── NORMAL (mặc định) ─────────────┐        bấm BOOT 3 lần
   │ Matter over Thread (Minimal End Device)     │ ───────────────────────────┐
   │ NimBLE quét thụ động (host của Matter)      │                            │  ghi yêu cầu CONFIG
   │ 5 endpoint Occupancy Sensor                 │                            │  vào RTC RAM, restart
   │ Wi-Fi TẮT                                    │                            ▼
   └─────────────────────────────────────────────┘          ┌──────────── CONFIG ─────────────┐
                          ▲                                 │ Matter/Thread KHÔNG khởi động   │
                          │  restart (yêu cầu RTC bị xóa)   │ (dữ liệu ghép giữ nguyên)       │
                          └──────────────────────────────── │ Wi-Fi SoftAP + web 192.168.4.1  │
     nút "Khởi động lại về chế độ Matter" · bấm BOOT 3 lần  │ NimBLE quét (host riêng)        │
     · 10 phút không tương tác · mất nguồn · crash          └─────────────────────────────────┘
```

* Yêu cầu vào CONFIG là **cờ trong RTC RAM** (`RTC_NOINIT_ATTR`, `main/app_mode_logic.cpp`) gồm magic, phần bù
  của magic, mã yêu cầu và CRC-32; chỉ được chấp nhận khi lý do reset là **reset phần mềm** (`ESP_RST_SW`) và bị xóa
  ngay trong lần khởi động đó. Mất nguồn (RTC RAM ngẫu nhiên, lý do `POWERON`), brown-out, watchdog, panic → luôn
  về NORMAL. Không ghi cờ chế độ vào flash, nên không thể kẹt vĩnh viễn trong CONFIG.
* CONFIG không gọi `esp_matter::start()`: không có Matter, không có OpenThread, radio 802.15.4 tắt, không đọc/ghi
  khóa nào của Matter. Wi-Fi giữ cấu hình trong RAM (`CONFIG_ESP_WIFI_NVS_ENABLED=n`, `WIFI_STORAGE_RAM`).
* Trong CONFIG, bấm BOOT 3 lần cũng quay về NORMAL; không có tương tác trong `PRESENCE_CONFIG_IDLE_TIMEOUT_MIN`
  (mặc định 10 phút) → tự về NORMAL. Các lần thăm dò định kỳ của trang (`?auto=1`) **không** tính là tương tác, nên
  điện thoại để quên trang mở không giữ thiết bị ở CONFIG; mọi thao tác (bấm, nhập, chạm) gia hạn.
* **Không có factory reset trên nút BOOT.** Factory reset: xóa thiết bị khỏi mọi ứng dụng Matter (khi fabric cuối bị
  xóa, cửa sổ ghép tự mở lại 5 phút) hoặc lệnh console `matter esp factoryreset`.

### Module

| File (`main/`) | Vai trò |
|---|---|
| `app_main.cpp` | Khởi tạo NVS (không xóa), nạp cấu hình, chọn chế độ, khởi động đúng tập dịch vụ |
| `app_mode.*`, `app_mode_logic.*` | Cờ RTC một lần, khởi động lại có kiểm soát, đếm thời gian rảnh ở CONFIG |
| `button_handler.*`, `click_detector.*` | Đọc GPIO9 mỗi 10 ms bằng `esp_timer` (không ISR), chống dội, nhận diện 3 lần bấm-nhả (bấm chậm được: nghỉ giữa hai lần tối đa 2 s); giữ lâu (bản hai chip, H2) |
| `ble_scanner.*` | Vòng đời quét GAP (observer) trên **host NimBLE duy nhất**, backoff, watchdog, "đồng hồ quét" |
| `ibeacon_parser.*`, `ble_addr.*`, `json_util.*` | Giải mã khung iBeacon, Eddystone-TLM (điện áp pin) và tên an toàn độ dài, MAC và thứ tự byte, UTF-8/JSON escape |
| `presence_manager.*`, `presence_logic.*`, `presence_battery.*` | Lọc MAC trong callback BLE → hàng đợi giới hạn → task hiện diện → báo Matter; pin từ gói TLM (trung vị, bước công bố) và tên quảng bá của từng slot |
| `scan_cache.*` | Danh sách iBeacon ở CONFIG (gộp trùng, giới hạn 32, loại bản ghi cũ, pin và tên của chính địa chỉ đó) |
| `presence_config.*`, `config_store.*`, `crc32.*` | Cấu hình 5 slot (bản hai chip: 20, có model): mặc định, kiểm tra, blob có version + CRC (định dạng 3 có danh tính iBeacon, đọc được định dạng 1 và 2), NVS phân vùng riêng |
| `wifi_config.*`, `web_server.*`, `http_guard.*`, `web/index.html` | SoftAP WPA2 kênh 20 MHz (giới hạn công suất phát trên ESP32-C3), HTTP server + API JSON (chỉ nhận `Host` đúng và POST JSON), trang tiếng Việt nhúng |
| `matter_device.*` | Node Matter, 5 endpoint, Thread MED (bản C6) hoặc Wi-Fi station (bản C3), cổng chặn scanner theo sự kiện Matter/mạng, cập nhật Occupancy |
| `matter_bridge.*`, `presence_link*`, `status_ui.*`, `app_main_h2.cpp` | Chỉ bản hai chip: Matter bridge "iBeacon Bridge" trên ESP32-H2 (tối đa 20 thiết bị con: tên và sê-ri là MAC, model, pin; giới hạn theo heap), giao thức UART v6 giữa C3 và H2, nút BOOT (bấm 2 lần: ghép, giữ 5 s: xóa ghép) và đèn RGB của H2 |
| `diag.*` | Log chẩn đoán định kỳ (heap, scanner, từng slot, bộ đếm Thread) |

Các module thuần logic (`click_detector`, `presence_logic`, `presence_battery`, `ibeacon_parser`, `ble_addr`,
`presence_config`, `presence_link_proto`, `app_mode_logic`, `json_util`, `http_guard`, `crc32`, `config_store` với NVS
giả lập) không phụ thuộc ESP-IDF và có host test (các bộ có số slot chạy cả với 5 và 20 slot).

### Mô hình dữ liệu Matter

| Endpoint | Device type | Cluster server | Ghi chú |
|---|---|---|---|
| 0 | Root Node | Basic Information, Access Control, General Commissioning, Network Commissioning (Thread), Operational Credentials, Group Key Management, General Diagnostics, Thread Network Diagnostics, Administrator Commissioning, Descriptor | do esp-matter tạo |
| 1–5 | **Occupancy Sensor `0x0107`** (rev. 4) | Identify, Occupancy Sensing (rev. 7), Descriptor | "Presence 1" … "Presence 5" |

* 5 endpoint được tạo **trước** `esp_matter::start()`, theo thứ tự cố định, nên luôn là ID 1–5 qua mọi lần khởi
  động, đổi MAC, đổi cấu hình (esp-matter cấp ID theo thứ tự tạo; giá trị `min_uu_ep_id` lưu trong NVS chỉ áp dụng
  cho endpoint tạo **sau** khi start — `components/esp_matter/data_model/esp_matter_data_model.cpp`,
  `esp_matter_core.cpp`). Các endpoint luôn tồn tại kể cả khi chưa gán beacon.
* **Feature của Occupancy Sensing: `RFSensing` (bit 6)**. Mô hình dữ liệu Matter 1.6.1
  (`connectedhomeip/data_model/1.6.1/clusters/OccupancySensing.xml`) định nghĩa RFS là *"Supports sensing using
  analysis of radio signals, e.g.: RSSI, CSI and/or any other metric from the signal"* — đúng bản chất phát hiện qua
  quảng bá BLE; **không khai báo PIR** vì thiết bị không có cảm biến hồng ngoại. Có thể đổi sang `Other` qua Kconfig
  `PRESENCE_OCCUPANCY_FEATURE`. SDK tự thêm bit `OccupancyEvent` (bit 9), nên FeatureMap đọc được là `0x240`.
* Các thuộc tính cũ (deprecated) do cluster của SDK tính theo bảng ánh xạ của spec: `OccupancySensorType` = 0 (PIR) và
  `OccupancySensorTypeBitmap` = 0 khi không có feature PIR/US/PHY (`src/app/clusters/occupancy-sensor-server/
  OccupancySensingCluster.cpp`). Đó là quy tắc tương thích ngược của spec, không phải khai báo phần cứng PIR.
* **Không có thuộc tính HoldTime**: thời gian vắng mặt là của firmware (từng slot, cấu hình trên web). Không có
  HoldTime thì `OccupancySensingCluster::SetOccupancy()` đặt giá trị ngay và chỉ báo khi giá trị thay đổi
  (`DoSetOccupancy()` bỏ qua giá trị trùng), kèm sự kiện `OccupancyChanged`; báo cáo đi qua cơ chế subscription chuẩn.
* Occupancy Sensing ở esp-matter v1.6.1 là cluster **code-driven**: `attribute::update()` không dùng được cho
  Occupancy (FAQ A1.19). Firmware dùng `OccupancySensing::FindClusterOnEndpoint()` + `SetOccupancy()` dưới
  `esp_matter::lock::ScopedChipStackLock`, gọi từ task hiện diện, **không bao giờ từ callback BLE** (FAQ A1.11).
* **Descriptor TagList**: mỗi endpoint có một semantic tag namespace *Common Number* (`0x07`), tag 1…5, nhãn cố định
  "Presence N" — phân biệt 5 endpoint anh em cùng loại. TagList là thuộc tính cố định nên **tên tùy chỉnh trên web
  chỉ là nhãn nội bộ** (log, trang web); không cam kết ứng dụng Matter nào hiển thị tên này.
* VID/PID **thử nghiệm** `0xFFF1`/`0x8002` với DAC/PAI/CD ví dụ của SDK (`src/credentials/examples/ExampleDACs.cpp`
  có DAC cho 0x8002; CD ví dụ phủ 0x8000–0x8063). Mã ghép thử nghiệm: passcode `20202021`, discriminator `3840`.
  Chỉ dùng để phát triển; sản phẩm thật cần DAC/PAI/CD và passcode riêng.

### Một host NimBLE dùng chung

* NORMAL: SDK Matter (`src/platform/ESP32/nimble/BLEManagerImpl.cpp`) gọi `nimble_port_init()`, sở hữu
  `ble_hs_cfg.sync_cb/reset_cb`, host task và quảng bá commissioning. Firmware **không khởi tạo host/controller thứ
  hai, không ghi đè `ble_hs_cfg`**; scanner chờ `ble_hs_synced()` rồi gọi `ble_gap_disc()` với **callback riêng của
  thủ tục quét** — NimBLE gửi sự kiện discovery cho callback đó, sự kiện quảng bá/kết nối vẫn về callback của Matter.
* `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=n` (tùy chọn có trong `config/esp32/components/chip/Kconfig` của
  connectedhomeip này, mặc định `y`): giữ host NimBLE sau khi commissioning; nếu để `y`, SDK gọi
  `BLEMgr().Shutdown()` và giải phóng bộ nhớ BLE (`esp_matter_core.cpp`, `deinit_ble_if_commissioned`) — scanner sẽ
  chết vĩnh viễn (FAQ A1.16).
* Vai trò cần cho quét thụ động: `CONFIG_BT_NIMBLE_ROLE_OBSERVER=y`; central tắt (không kết nối beacon);
  peripheral/broadcaster giữ cho commissioning. Quảng bá legacy (`CONFIG_BT_NIMBLE_EXT_ADV=n`, như ví dụ C6 Thread
  của esp-matter) → sự kiện `BLE_GAP_EVENT_DISC`; mã vẫn xử lý `BLE_GAP_EVENT_EXT_DISC` nếu bật EXT_ADV.
* CONFIG: Matter không chạy, scanner tự khởi tạo NimBLE (`nimble_port_init()` + host task riêng).

---

## BLE cùng Thread trên một chip

ESP32-C6 chỉ có **một đường RF 2,4 GHz**; BLE, 802.15.4 và Wi-Fi chia thời gian (TDM). Bảng *Supported Coexistence
Scenario for ESP32-C6* trong hướng dẫn coexistence của **ESP-IDF v6.0.2**
(`docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32c6/api-guides/coexist.html`):

| 802.15.4 \ BLE | Scan | Advertising | Connected |
|---|---|---|---|
| Scan | X | Y | Y |
| **Router** | **X** (không hỗ trợ) | Y | Y |
| **End Device** | **C1** (hỗ trợ nhưng hiệu năng không ổn định) | Y | Y |

| Wi-Fi SoftAP \ BLE | Scan |
|---|---|
| TX Beacon (chưa có điện thoại) | Y |
| Connecting / Connected (có điện thoại) | **C1** |

Vì vậy ở CONFIG firmware **chỉ quét BLE khi có điện thoại nối SoftAP**, bắt đầu 5 s sau khi điện thoại vào
(`PRESENCE_CONFIG_SCAN_HOLD_OFF_SEC`) và chỉ 40 ms mỗi 160 ms: lúc điện thoại dò mạng và kết nối (bắt tay WPA2, xin
DHCP, tải trang đầu), radio chỉ phục vụ Wi-Fi.

Tài liệu cũng nêu: *"Normal receive operations [of 802.15.4] are assigned the lowest priority, meaning Wi-Fi and BLE
will take over the RF whenever needed"*. Hệ quả và các lựa chọn của firmware:

1. **Không bao giờ là Thread Router.** Build **MTD** (`CONFIG_OPENTHREAD_MTD=y`): OpenThread MTD không có mã router/leader,
   nên không thể thành router dù mạng thiếu router. Không dùng FTD + tắt router-eligible.
2. **Minimal End Device (MED, rx-on-when-idle)** cho thiết bị cấp nguồn liên tục: không có độ trễ polling, không cần
   ICD (`CONFIG_ENABLE_ICD_SERVER` tắt). OpenThread mặc định `kModeRxOnWhenIdle` cho MTD
   (`openthread/src/core/thread/mle.cpp`), firmware vẫn gọi
   `ConnectivityMgr().SetThreadDeviceType(kThreadDeviceType_MinimalEndDevice)` khi server sẵn sàng và log kiểu thiết
   bị. Đánh đổi: trong cửa sổ quét BLE, radio không nhận 802.15.4; khung parent gửi tới MED rơi vào cửa sổ đó cần
   MAC retry, và nếu cả loạt retry (vài ms đến ~20 ms) đều trùng cửa sổ 40 ms thì cần lớp trên (MLE, Matter MRP)
   gửi lại. (SED — ngủ và poll — cũng là "End Device" C1 trong bảng, thêm độ trễ poll và cấu hình ICD, không có lợi rõ
   ràng cho thiết bị cắm điện.)
3. **Không chiếm toàn bộ radio.** Mặc định cửa sổ **40 ms / chu kỳ 160 ms = 25%** (Kconfig
   `PRESENCE_SCAN_WINDOW_MS`/`PRESENCE_SCAN_INTERVAL_MS`, đổi được trên trang web, đơn vị ms; firmware đổi sang đơn
   vị HCI 0,625 ms). Trang web cảnh báo khi > 50%.
4. **Ưu tiên Thread khi đang attach hoặc commissioning**, scanner có trạng thái tạm dừng rõ ràng (`SCAN_PAUSE_*`):
   chưa có fabric; phiên PASE; kết nối CHIPoBLE; Thread chưa attach / mất attach; thêm **10 s** sau khi attach hoặc
   commissioning xong (`PRESENCE_SCAN_RESUME_DELAY_SEC`) để SRP và subscription ổn định. Khi không slot nào có
   beacon, scanner không chạy.
5. **Không có OFF giả vì firmware ngừng quét**: timeout tính trên *đồng hồ quét* (chỉ chạy khi radio thật sự đang
   quét), xem [thuật toán](#thuật-toán-hiện-diện).
6. `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y` (bắt buộc theo hướng dẫn coexistence).

### Giới hạn hiệu năng (lý thuyết, cần đo)

Xác suất nghe được một sự kiện quảng bá ≈ cửa sổ/chu kỳ = *p* (mỗi sự kiện phát lần lượt trên kênh 37/38/39 trong
~1–2 ms; bộ quét nghe một kênh mỗi cửa sổ). Xác suất **không nghe thấy gói nào** trong cả thời gian vắng mặt
*T* với beacon phát mỗi *t* giây ≈ (1 − *p*)^(*T*/*t*):

| Beacon phát mỗi | Timeout | Số sự kiện | p = 25% (mặc định) | p = 50% |
|---|---|---|---|---|
| 100 ms | 45 s | 450 | ≈ 0 | ≈ 0 |
| 1 s | 45 s | 45 | 2,4·10⁻⁶ | ≈ 0 |
| 2 s | 45 s | 22 | 1,8·10⁻³ | 2,4·10⁻⁷ |
| 5 s | 60 s | 12 | 3,2% | 0,02% |
| 10 s | 60 s | 6 | **17,8%** | 1,6% |
| 10 s | 180 s | 18 | 0,56% | ≈ 0 |

Đây là **cận dưới lý tưởng**: thực tế *p* nhỏ hơn vì 802.15.4 TX/ACK và các thao tác ưu tiên cao cắt ngang cửa sổ BLE,
pha-đinh, cơ thể người che, và hàng đợi báo cáo của controller (`BT_CTRL_BLE_ADV_REPORT_FLOW_CTRL`) bỏ gói khi host
bận. Kết luận: **beacon nên phát ≤ 1–2 s**. Ví dụ beacon 10 s/lần với 45–60 s chỉ là điểm bắt đầu khi có thể tăng
tỉ lệ quét; ở mặc định 25% sẽ có OFF giả thường xuyên. Cách sửa đúng là tăng tần suất phát của beacon (hoặc tăng cửa
sổ quét nếu Thread chịu được), **không** che bằng timeout quá lớn.

---

## Thuật toán hiện diện

Mỗi slot: `enabled`, `label`, MAC + loại địa chỉ (`public`, `random`, hoặc `any` khi nhập tay không rõ loại),
`absence_timeout_seconds` (10–3600, mặc định **45 s**), lọc RSSI tùy chọn (mặc định tắt).

* Gói **iBeacon hợp lệ** (đúng khung Apple 0x004C/0x02/0x15, đủ 25 byte) từ **đúng MAC và loại địa chỉ** → bật
  ngay; mỗi gói sau cập nhật `last_seen`. Tên quảng bá **không** dùng để nhận diện. Không cần kết nối GATT/pair.
* Không có gói hợp lệ trong `timeout` → tắt. Slot tắt hoặc chưa có MAC → luôn "không hiện diện".
* **Đồng hồ monotonic**: `esp_timer` (từ lúc khởi động), không phụ thuộc giờ Internet. Cụ thể, timeout đếm trên
  **đồng hồ quét** = tổng thời gian radio thật sự đang quét (`ble_scanner_scan_clock_ms()`); khi scanner tạm dừng
  (commissioning, Thread attach lại, host reset, backoff) đồng hồ dừng, trạng thái giữ nguyên, nên **không có OFF
  giả do chính firmware ngừng quét**. Khi quét lại, slot chỉ tắt sau phần thời gian còn lại. Đổi lại, một slot đang
  ON giữ ON trong suốt khoảng tạm dừng.
* **Gói lặp cùng payload vẫn làm mới**: mọi lệnh quét đặt `filter_duplicates = 0`. Đây là điểm quan trọng: ví dụ
  `blecent` của ESP-IDF đặt `filter_duplicates = 1`; với controller ESP32-C6, bộ lọc trùng theo địa chỉ có chu kỳ
  làm mới mặc định `BT_LE_SCAN_DUPL_CACHE_REFRESH_PERIOD=0` = *"will not be cleared until the scanning is disabled"*
  (`components/bt/controller/esp32c6/Kconfig.in`) → một beacon phát liên tục chỉ được báo **một lần** rồi bị coi
  là vắng. Firmware còn đặt refresh period = 1 s để phòng khi ai đó bật lại bộ lọc. Observer mode bật
  (`disable_observer_mode = 0`): nhận mọi gói bất kể cờ Flags.
* Lọc RSSI (tắt mặc định, có trễ): đang vắng cần RSSI ≥ ngưỡng (mặc định −80 dBm) để bật; đang hiện diện thì gói ≥
  (ngưỡng − trễ) (mặc định 5 dB) vẫn làm mới; gói yếu hơn bị bỏ qua (slot tắt sau timeout). RSSI dao động ±10 dB theo
  người, hướng, vật cản: chỉ phân biệt thô "gần"/"xa", **không phải khoảng cách**.
* **Sau reboot mọi slot OFF** cho tới khi nhận gói mới. `last_seen`, RSSI, trạng thái ON/OFF **không bao giờ ghi
  flash**.
* Trong callback BLE (host task NimBLE): chỉ so MAC với bảng 5 slot bất biến, giải mã khung, rồi đẩy tối đa 1 bản
  ghi/slot mỗi 250 ms (`PRESENCE_REPORT_THROTTLE_MS`, giữ RSSI mạnh nhất) vào hàng đợi 32 phần tử với timeout 0;
  tràn thì đếm (`queue_drops`). Task hiện diện xử lý hết hàng đợi **trước** khi xét timeout. Gói đầu tiên sau một
  khoảng im lặng đi ngay; các gói trong 250 ms sau đó chỉ được gộp và gửi cùng gói kế tiếp, nên khi beacon im hẳn,
  những gói của 250 ms cuối không được tính: OFF có thể sớm tối đa 0,25 s (không đáng kể với timeout ≥ 10 s).

Chọn timeout: phải lớn hơn **nhiều** chu kỳ phát của beacon, xem bảng ở mục coexistence (ví dụ beacon 1 s → 45 s là
dư; beacon 10 s cần cân nhắc như đã nêu).

### Beacon dùng địa chỉ thay đổi

Firmware giữ **MAC làm tiêu chí nhận diện chính** (không âm thầm chuyển sang UUID). Trang web phân loại địa chỉ theo
2 bit cao (Core Spec Vol 6 Part B 1.3.2): public, random tĩnh (ổn định) và **random riêng tư RPA/NRPA (đổi theo thời
gian)** → cảnh báo *"nhận diện theo MAC sẽ không bền vững"*. Nếu cùng UUID/Major/Minor xuất hiện dưới hai MAC, trang
cũng cảnh báo beacon có thể đổi địa chỉ. Với beacon như vậy, hãy cấu hình beacon dùng địa chỉ cố định.

---

## Lưu trữ và lỗi NVS

| Phân vùng | Nội dung | Firmware ghi? |
|---|---|---|
| `nvs` (0x10000, 48 KB) | Fabric/khóa Matter, Thread dataset (OpenThread), hiệu chuẩn PHY | chỉ SDK Matter/OpenThread/PHY ghi; **không bao giờ xóa** |
| `app_cfg` (0x3C0000, 24 KB) | Cấu hình 5 slot (1 blob) + mật khẩu SoftAP | chỉ khi người dùng bấm Lưu (và một lần tạo mật khẩu) |
| `fctry`, `esp_secure_cert` | Dữ liệu sản xuất (dự phòng) | không |

* Cấu hình 5 slot là **một blob NVS** (magic `BPRC`, version, độ dài, CRC-32): ghi bằng **một** `nvs_set_blob()` +
  `nvs_commit()`; NVS giữ blob cũ tới khi blob mới hoàn tất → mất điện giữa chừng còn nguyên bản cũ hoặc bản mới,
  không bao giờ một nửa số slot. Sau khi ghi, firmware **đọc lại, so từng byte và giải mã lại**; chỉ khi khớp mới áp
  dụng. Có version để nâng cấp: firmware mới giữ bộ giải mã cho version cũ và chỉ ghi định dạng mới khi người dùng lưu.
* Không ghi flash trong callback BLE; không ghi `last_seen`/trạng thái.
* **Không tự gọi `nvs_flash_erase()`** (khác ví dụ ESP-IDF):
  * `nvs` lỗi (`ESP_ERR_NVS_NO_FREE_PAGES`, `ESP_ERR_NVS_NEW_VERSION_FOUND`, …): firmware **không khởi động Matter**,
    log lỗi, vẫn cho vào CONFIG (trang web hiện lỗi). Dữ liệu ghép giữ nguyên để khôi phục. Cách xử lý: (1) sao lưu
    `esptool.py --chip esp32c6 read_flash 0x10000 0xC000 nvs_backup.bin`; (2) `NEW_VERSION_FOUND` thường do nạp
    firmware build bằng ESP-IDF cũ hơn lên chip đã chạy bản mới → nạp lại firmware đúng phiên bản; (3) chỉ khi chấp
    nhận ghép lại từ đầu: `parttool.py --port PORT erase_partition --partition-name=nvs` rồi commissioning lại.
  * `app_cfg` lỗi hoặc blob hỏng (CRC, version mới hơn, giá trị sai): chạy với mặc định (mọi slot tắt), **giữ nguyên
    dữ liệu** trong flash, trang web hiện trạng thái và nút **"Định dạng phân vùng cấu hình beacon"** — chỉ xóa
    `app_cfg`, không chạm `nvs` của Matter/Thread. Lưu thất bại (hết chỗ, lỗi commit, đọc lại không khớp): báo lỗi,
    cấu hình cũ vẫn dùng, thiết bị ở lại trang cấu hình.
* Không sao chép NVS hay định danh từ thiết bị khác: mỗi thiết bị tự tạo khóa Matter khi ghép và mật khẩu SoftAP ngẫu
  nhiên riêng (RNG phần cứng, bật nguồn entropy trước khi radio chạy).

---

## Build

Cần ESP-IDF v6.0.2 + esp-matter release/v1.6.1. Cách đơn giản nhất là image Docker chính thức (đã gồm cả hai):

```bash
docker run --rm -it -v "$PWD":/project -w /project/ESP32-C6/matter-ble-presence \
    espressif/esp-matter:release-v1.6.1_idf_v6.0.2 bash
# trong container:
git config --global --add safe.directory '*'
idf.py set-target esp32c6
idf.py build
```

Cài trên máy (theo *Developing with the SDK*): clone `esp-idf` tag `v6.0.2` (`./install.sh`, `. ./export.sh`), clone
`esp-matter` nhánh `release/v1.6.1` + submodule connectedhomeip (`./install.sh`, `. ./export.sh`), rồi hai lệnh
`idf.py` như trên. Lần build đầu tải `espressif/cjson` (và các phụ thuộc của esp-matter) từ ESP Component Registry
nên cần Internet.

`sdkconfig.defaults` chứa phần dùng chung với mọi bản (BLE, SoftAP, bảng phân vùng), `sdkconfig.defaults.esp32c6`
phần riêng của bản C6 (Thread, product ID 0x8002, console Matter; ESP-IDF tự áp dụng ngay sau file chung khi target là
esp32c6), `sdkconfig.defaults.matter` phần Matter (chỉ các bản chạy Matter nạp, `ble_presence.cmake`).
Sau khi sửa các file này, chạy lại `idf.py set-target esp32c6` (hoặc xóa `sdkconfig`).
Tùy chọn của dự án: `idf.py menuconfig` → **BLE Presence x5** (nút BOOT, SoftAP, quét BLE, hiện diện, feature Matter,
chẩn đoán).

CI: `.github/workflows/matter-ble-presence-c6.yml` chạy host test, test trang web và build trong cùng image;
`matter-ble-presence-c3.yml` build bản ESP32-C3 (cả khi chỉ mã nguồn chung ở thư mục này thay đổi).

---

## Nạp firmware

```bash
idf.py -p /dev/ttyUSB0 flash monitor      # DevKitC-1: cổng "UART"; hoặc cổng USB (/dev/ttyACM0)
```

`idf.py flash` chỉ ghi bootloader, bảng phân vùng và app: **không xóa** `nvs`/`app_cfg`, nên nạp bản mới vẫn giữ
ghép Matter và cấu hình beacon. Chỉ với chip mới hoặc khi đổi bảng phân vùng từ firmware khác mới cần
`idf.py -p PORT erase-flash` (xóa luôn ghép Matter).

Khi khởi động, console in phiên bản, chế độ, lý do reset, SSID và **mật khẩu SoftAP** của thiết bị, 5 slot, và
(nếu chưa ghép) **QR payload + mã ghép thủ công**:

```
I (...) app_main: Configuration page: triple-click BOOT, join Wi-Fi "VN-Presence-3A7C" (password "k7m2..."), open http://192.168.4.1
I (...) chip[SVR]: SetupQRCode: [MT:...]
I (...) chip[SVR]: Manual pairing code: [34970112332]
```

---

## Ghép Matter qua Thread

Cần một **Thread Border Router** (Apple TV/HomePod mini, Google Nest Hub/Wifi, Home Assistant + OpenThread Border
Router, …) và ứng dụng/controller Matter hỗ trợ Thread.

1. Nạp firmware, mở console, lấy QR/mã ghép thủ công (thử nghiệm: passcode `20202021`, discriminator `3840`).
2. Lần đầu (chưa có fabric): thiết bị quảng bá BLE commissioning **15 phút** sau khi bật nguồn
   (`CONFIG_CHIP_DISCOVERY_TIMEOUT_SECS`); quá thời gian thì rút điện cắm lại. Trong lúc này **scanner beacon tạm dừng**
   để ưu tiên ghép.
3. Ghép bằng ứng dụng (quét QR) hoặc `chip-tool`:
   ```bash
   chip-tool pairing ble-thread 0x11 hex:<active-dataset-TLV-của-mạng-Thread> 20202021 3840
   chip-tool occupancysensing read occupancy 0x11 1     # endpoint 1..5
   chip-tool descriptor read tag-list 0x11 3
   ```
4. Sau `Commissioning complete` và `Thread up`, scanner tự chạy lại sau 10 s. Reboot sau đó tự dùng lại fabric và
   Thread dataset đã lưu, không cần ghép lại.

Ứng dụng có thể hiển thị thiết bị là cảm biến *Occupancy* với 5 ô. Firmware **chưa được thử** với Apple Home, Google
Home hay Home Assistant trong phiên này — xem [cần thử](#đã-xác-minh--cần-thử-trên-phần-cứng).

---

## Cấu hình beacon (chế độ CONFIG)

1. Khi firmware đang chạy, **bấm-nhả BOOT 3 lần**, thong thả cũng được: lần bấm sau chỉ cần bắt đầu trong vòng
   **2 giây** kể từ lần nhả trước (`PRESENCE_BOOT_CLICK_GAP_MS`), mỗi lần bấm ngắn hơn 1,5 s; nghỉ lâu hơn thì đếm lại từ
   đầu, giữ lâu không bao giờ được tính là nhiều lần bấm. Mỗi lần bấm được đếm, log in `BOOT click 1 of 3`,
   `BOOT click 2 of 3`; đủ 3 lần, log `Triple click on BOOT` và thiết bị khởi động lại vào CONFIG. Matter tạm thời
   offline với controller; dữ liệu ghép không bị đụng tới.
2. Điện thoại kết nối Wi-Fi **`VN-Presence-XXXX`** (4 số hex cuối của MAC SoftAP), mật khẩu in ở console khi khởi
   động (mật khẩu ngẫu nhiên riêng của thiết bị, 12 ký tự; hoặc đặt chung qua Kconfig `PRESENCE_AP_PASSWORD`). Không
   có AP mở.
3. Mở **http://192.168.4.1** (không cần Internet, không captive portal).
4. Mục *iBeacon xung quanh*: MAC, loại địa chỉ (cảnh báo địa chỉ đổi), RSSI, UUID, Major, Minor, thời điểm nhận gần
   nhất, tên quảng bá (nếu có, lấy cả từ scan response — CONFIG quét chủ động). Danh sách gộp trùng, tối đa 32 beacon,
   bỏ beacon không nghe thấy sau 120 s. Có nút dừng/tiếp tục quét và xóa danh sách. Quét bắt đầu khoảng 5 s sau khi
   điện thoại nối SoftAP (trạng thái "Sắp quét"), nên danh sách trống trong vài giây đầu.
5. Chọn **"Gán vào → Presence N"** trên một beacon, hoặc nhập MAC tay (chấp nhận `AA:BB:CC:DD:EE:FF`,
   `aa-bb-cc-dd-ee-ff`, `aabbccddeeff`, chuẩn hóa thành `AA:BB:CC:DD:EE:FF`; loại địa chỉ `Tự động` khi không rõ).
   Đặt tên, bật/tắt, thời gian vắng mặt, lọc RSSI (nâng cao). Một beacon không được gán cho 2 slot đang bật (báo lỗi
   cụ thể; Kconfig `PRESENCE_ALLOW_DUPLICATE_BEACON` để cho phép).
6. **Lưu cấu hình**. Trang kiểm tra trước, thiết bị kiểm tra lại toàn bộ; lỗi hiện ở đúng ô. Lưu thành công =
   đã ghi, đọc lại và so khớp.
7. **Khởi động lại về chế độ Matter**: thiết bị trả lời HTTP trước rồi mới khởi động lại (chờ 1 s). Không bấm thì sau 10
   phút không thao tác cũng tự về.

API JSON (giới hạn 2 KB cho cấu hình, 256 byte cho lệnh khác; vượt → 413): `GET /api/status`, `GET/POST /api/scan`,
`GET/POST /api/config`, `POST /api/restart`, `POST /api/keepalive`, `POST /api/format`. Mọi chuỗi trả về được escape
JSON (cả `<`, `>`, `&`, `'`); trang chèn dữ liệu bằng `textContent`, không `innerHTML`. JSON lồng tối đa 16 cấp
(`CONFIG_CJSON_NESTING_LIMIT`; mặc định 1000 của cJSON cho phép vài trăm dấu `[` làm tràn stack task web).

Chống trang web khác (điện thoại nối SoftAP vẫn có thể lướt Internet bằng 4G): thiết bị chỉ trả lời request có
`Host` đúng địa chỉ SoftAP (`192.168.4.1`, chống DNS rebinding; sai → 403) và mọi POST phải có
`Content-Type: application/json` (sai → 415). Trình duyệt chỉ gửi kiểu đó sang trang khác sau khi hỏi bằng `OPTIONS`
(CORS preflight), mà thiết bị không trả lời `OPTIONS`, nên trang lạ không đổi được cấu hình hay khởi động lại thiết bị.

---

## Kiểm thử

### Host test (không cần ESP-IDF)

```bash
ESP32-C6/matter-ble-presence/test/host/run.sh      # g++ ≥ 9, chạy với AddressSanitizer + UBSan
```

| Bộ test | Nội dung |
|---|---|
| `test_ibeacon` | khung hợp lệ (có/không Flags, sau tên), không phải iBeacon (hãng khác, sai type/độ dài, Eddystone), **mọi độ dài cắt cụt**, độ dài vượt cuối, cấu trúc độ dài 0, 2 cấu trúc manufacturer, tên (complete/shortened, UTF-8, ký tự điều khiển, cắt đúng ranh giới), **Eddystone-TLM** (điện áp, nhiệt độ, TLM mã hóa / UID / UUID khác bị loại, mọi độ dài cắt cụt), **fuzz 250 000 mẫu** dưới ASan |
| `test_presence_battery` | mV → % (kẹp 0–100, làm tròn, khoảng cấu hình sai không chia 0), mức Warning/Critical, trung vị 5 lần đọc, nhiễu và một gói thấp bất thường không đổi %, chỉ giảm theo bước 2 %, thay pin (tăng ≥ 15 %) |
| `test_ble_addr` | các dạng MAC hợp lệ/không hợp lệ, chuẩn hóa, **thứ tự byte NimBLE ↔ hiển thị**, loại địa chỉ (public/static/RPA/NRPA), khớp loại |
| `test_presence_logic` | ON tức thì, OFF đúng tại timeout, **1 giờ quảng bá cùng payload vẫn ON**, **tạm dừng scanner 10 phút không gây OFF**, slot độc lập, slot tắt, RSSI có trễ |
| `test_click_detector` | 3 lần bấm, 2 lần, **bấm thong thả** (nghỉ đúng 2 s, bấm 1,5 s), nghỉ quá 2 s thì đếm lại, dội phím (cả khi bấm chậm), nhiễu ngắn, **giữ lâu không thành nhiều lần bấm**, nút đang giữ lúc khởi động, 4 lần, tràn bộ đếm ms; H2: bấm 2 lần, **giữ 5 s báo một lần lúc còn giữ rồi lúc nhả**, bấm trước khi giữ không cộng, giữ 2 s không làm gì, dội phím trong lúc giữ |
| `test_app_mode` | cờ RTC chỉ hợp lệ sau reset phần mềm, một lần, bị hỏng → NORMAL, 100 000 mẫu RTC ngẫu nhiên không bao giờ vào CONFIG |
| `test_presence_config` | (5 và 20 slot) mặc định (cửa sổ < chu kỳ), mọi lỗi trường, trùng beacon (any/public/random, slot tắt), nhãn và model UTF-8, blob round-trip, **mọi bit lật bị phát hiện**, version mới hơn, magic sai, **blob định dạng 1 của bản 5 slot đọc được** (slot thêm lấy mặc định, lưu lại thành định dạng 2), blob nhiều slot hơn firmware (20 slot đọc bằng bản 5 slot): slot thừa không có beacon thì bỏ, có beacon (kể cả slot tắt) hoặc giá trị sai thì từ chối và không ghi gì |
| `test_presence_link` | giao thức UART v6: khung đúng, mặt nạ 20 bit, khung SLOT (model UTF-8, pin, slot ngoài khoảng, ký tự điều khiển, độ dài sai), STATUS (thiết bị chờ, thiết bị lỗi, heap), **mọi bit lật bị loại**, mọi kiểu cắt cụt, rác giống log ROM, khung phiên bản 1–5 bị loại, danh tính iBeacon `E2C5/5/6`, slot "chưa biết" sau khi C3 khởi động lại |
| `test_json_util` | escape JSON/HTML (`<script>`), UTF-8 hợp lệ/không hợp lệ, cắt không vỡ chuỗi |
| `test_http_guard` | chỉ nhận `Host` = địa chỉ SoftAP (có/không `:80`; từ chối tên miền khác, `192.168.4.10`, `:8080`), POST phải là `application/json` (từ chối `text/plain`, form, `application/jsonp`…) |
| `test_config_store` | NVS giả lập: lần đầu, lưu + đọc lại, **lưu lỗi (set/commit/open) giữ cấu hình cũ**, đọc lại không khớp, blob hỏng → mặc định và giữ dữ liệu, phân vùng `NO_FREE_PAGES`/`NEW_VERSION_FOUND` **không tự xóa**, định dạng theo yêu cầu, mật khẩu SoftAP; khẳng định **không bao giờ gọi `nvs_flash_erase()` và không chạm phân vùng khác** |

### Kiểm thử trang web (trình duyệt headless, API giả lập)

```bash
cd ESP32-C6/matter-ble-presence/test/web
npm install --no-save --no-package-lock playwright@1.63.0 && npx playwright install chromium
node ui_test.js                 # tùy chọn: node ui_test.js ../../main/web/index.html <thư-mục-ảnh-chụp>
```

`ui_test.js` mở đúng file `main/web/index.html` được nhúng vào firmware trong Chromium (khung 390×844, cảm ứng),
trả lời các API `/api/*` bằng dữ liệu giả và kiểm tra 89 điểm: tên beacon chứa `<img onerror=…>` chỉ hiện dạng chữ
(không chèn phần tử, không chạy script), cảnh báo RPA, nạp cấu hình đã lưu, gán beacon từ danh sách (điền MAC + loại
địa chỉ, bật slot), **từ chối gán một beacon cho hai slot** kèm tên slot đang dùng, MAC nhập tay sai/trùng bị chặn và
không gửi gì lên thiết bị, timeout ngoài khoảng, cảnh báo chu kỳ quét > 50%, nội dung lệnh lưu (MAC chuẩn hóa
`AA:BB:CC:DD:EE:FF`, số nguyên, đủ 5 slot), keepalive khi có thao tác, **mọi POST gửi `application/json`**,
**danh sách beacon không bị dựng lại khi đang mở ô "Gán vào…"** (trên điện thoại việc dựng lại sẽ đóng ô chọn), nút
khởi động lại, chữ hiển thị theo mạng của thiết bị (`"network"` trong API: "Thread" + mức coexistence C1 cho bản
C6, "Wi-Fi" + mức Y cho bản C3, dòng tình trạng ESP32-H2, mã ghép Matter, thiết bị con của bridge theo từng slot và
nút xóa ghép H2 cho bản hai chip; 20 slot, model giới hạn 32 byte UTF-8 và tự điền tên beacon, pin, lý do thiết
bị con còn chờ: chờ ghép, thiếu bộ nhớ, H2 không tạo được, riêng từng slot), ô lỗi nằm trong mục "Lọc RSSI" đang gập
được mở ra khi bấm Lưu, trạng thái quét "Sắp quét", và **Wi-Fi
chậm** (thiết bị trả lời danh sách beacon sau 3 s, lâu hơn chu kỳ hỏi 2 s: không bao giờ có hai request cùng loại
đang chờ). Kết quả trong phiên này:
`UI checks passed` (Playwright 1.63.0, Node 22). Test này không thay cho thử trên điện thoại thật nối vào SoftAP.

### Kết quả build thực tế

Chạy trong `espressif/esp-matter:release-v1.6.1_idf_v6.0.2` (các commit ở đầu tài liệu), lệnh đúng như mục
[Build](#build): `idf.py set-target esp32c6` rồi `idf.py build`.

```
matter_ble_presence.bin binary size 0x1fef70 bytes. Smallest app partition is 0x3a0000 bytes. 0x1a1090 bytes (45%) free.
Project build complete.
```

| Hạng mục | Giá trị |
|---|---|
| App `matter_ble_presence.bin` | 2 092 912 byte (0x1FEF70) trong phân vùng `factory` 3 712 KB (0x3A0000): **còn trống 45%** |
| Bootloader / bảng phân vùng | 22 192 byte tại 0x0 / 3 072 byte tại 0xC000 |
| Flash code (`idf.py size`) | 1 970 174 byte (.text 1 643 898, .rodata 325 552) |
| DIRAM tĩnh | 218 908 / 452 112 byte (48,4%): IRAM .text 104 320, .bss 96 248, .data 18 340 → 233 204 byte cho heap lúc khởi động (heap còn lại khi Matter + Thread + BLE chạy **cần đo trên phần cứng**, log `diag` in mỗi 60 s) |
| LP SRAM | `.rtc_noinit` 16 byte = bản ghi yêu cầu CONFIG (`app_mode_rtc_t`), `.rtc_reserved` 24 byte |
| Cảnh báo biên dịch từ mã dự án | 0 (cờ mặc định của ESP-IDF: `-Wall -Werror` trừ vài nhóm, `-Wextra`) |
| Kconfig | không còn ký hiệu không tồn tại trong `sdkconfig.defaults` (chỉ còn các NOTE của chính SDK) |
| Lỗi đã sửa trong quá trình build | (1) container không ra Internet → tải component registry qua proxy; (2) `CONFIG_BLE_DEVICE_NAME_PREFIX` quá dài làm `BLEManagerImpl.cpp` của SDK lỗi `-Werror=format-truncation` (tên BLE tối đa 16 ký tự) → `"Presence-"`; (3) `CONFIG_MBEDTLS_HKDF_C` không còn ở IDF 6.0 → bỏ; (4) `DeviceEventType` là namespace của CHIP → dùng alias namespace |

Nạp: `build/flash_args` (`--flash-mode dio --flash-freq 80m --flash-size 4MB`, `0x0 bootloader`, `0xc000 partition-table`,
`0x20000 app`).

Host test (`test/host/run.sh`, g++ 13.3, ASan + UBSan):

```
app_mode             11 checks, 0 failed
ble_addr             63 checks, 0 failed
click_detector       51 checks, 0 failed
config_store        108 checks, 0 failed
config_store 20     108 checks, 0 failed
http_guard           26 checks, 0 failed
ibeacon_parser     333139 checks, 0 failed
json_util            22 checks, 0 failed
presence_battery   4037 checks, 0 failed
presence_config     711 checks, 0 failed
presence_config 20 2496 checks, 0 failed
presence_link       615 checks, 0 failed
presence_logic     4248 checks, 0 failed
presence_logic 20  4278 checks, 0 failed
All host tests passed
```

(Các bộ "20" là cùng test, biên dịch với 20 slot của bản hai chip.)

### Rà soát mã độc lập

Sau khi build xong, toàn bộ mã được rà soát lại độc lập, đối chiếu với mã nguồn esp-matter/CHIP/OpenThread/NimBLE
đúng phiên bản đã ghim. Lỗi tìm thấy và cách sửa:

| Lỗi | Hậu quả | Sửa |
|---|---|---|
| cJSON cho lồng tới 1000 cấp, mỗi cấp một lần đệ quy trên stack 8 KB của task web | một POST 256 byte toàn dấu `[` làm tràn stack, thiết bị reset | `CONFIG_CJSON_NESTING_LIMIT=16` (`#error` nếu > 32); body cấu hình tối đa 2 KB (giới hạn luôn heap cJSON dùng khi phân tích) |
| Phần khởi động (in QR/mã ghép, đặt MED, mở cổng scanner) chờ `kServerReady`; trên bản chỉ có Thread, sự kiện này chờ DNS-SD, tức chờ SRP server của Border Router trả lời | lần đầu (chưa ghép) không in QR/mã ghép; đã ghép mà SRP server không trả lời thì scanner không bao giờ chạy | chạy ngay sau `esp_matter::start()` (hàm này chờ `Server::Init` xong) bằng `ScheduleWork` |
| API nhận request từ trang web khác (CSRF) và mọi `Host` (DNS rebinding) | trang lạ mở trên điện thoại đang nối SoftAP có thể ghi cấu hình, khởi động lại thiết bị | kiểm tra `Host` và `Content-Type: application/json` (`http_guard.*`, có host test) |
| Danh sách beacon được dựng lại mỗi 2 s | ô "Gán vào…" đang mở trên điện thoại bị đóng, mất lựa chọn | không dựng lại khi ô chọn đang focus, cập nhật khi rời ô (có test trình duyệt; test này hỏng với trang cũ) |
| Trang hỏi trạng thái/danh sách bằng `setInterval` 3 s/2 s, không chờ câu trả lời trước, không giới hạn thời gian (rà soát lần 2) | Wi-Fi chậm (SoftAP + quét BLE là C1): request chồng lên nhau trong khi thiết bị phục vụ từng request một, socket cũ bị đóng → trang báo lỗi, càng chậm thêm | mỗi loại request chỉ một cái đang chờ, lần sau bắt đầu sau khi lần trước xong, bỏ request sau 10 s (test trình duyệt "slow link": thiết bị trả lời chậm 3 s; test hỏng với trang cũ) |

Giữ nguyên, đã ghi ở [Thuật toán hiện diện](#thuật-toán-hiện-diện): gói của 250 ms cuối trước khi beacon im hẳn
không được tính (OFF sớm tối đa 0,25 s).

### Quy trình kiểm thử phần cứng

Chuẩn bị: board C6 cấp nguồn ổn định, Thread Border Router, controller Matter (chip-tool + Home Assistant hoặc ứng
dụng), 5 iBeacon đặt chu kỳ phát 1 s (ghi lại thông số), máy tính mở `idf.py monitor` để lưu log
(`idf.py monitor | tee log.txt`). Log chẩn đoán mỗi 60 s (`PRESENCE_DIAG_PERIOD_SEC`) gồm heap, trạng thái scanner,
số gói/s, từng slot, bộ đếm MAC/MLE của Thread.

| # | Bước | Kỳ vọng / đo |
|---|---|---|
| 1 | Ghép Matter, đọc đủ 5 endpoint (`descriptor read parts-list 0x11 0`, `occupancysensing read occupancy 0x11 1..5`, `read feature-map`) | parts-list = [1,2,3,4,5]; occupancy = 0; feature-map = 0x240 |
| 2 | Bấm BOOT 3 lần; điện thoại vào SoftAP, mở web | log `Boot in CONFIG mode (reset: software)`; trang tải được |
| 3 | Quét, gán đủ 5 beacon, lưu, khởi động lại | lưu báo thành công; log NORMAL in 5 slot đúng MAC |
| 4 | Sau reboot | Matter trở lại mạng cũ không cần ghép (`Commissioned (1 fabric(s))`, `Thread up`) |
| 5 | Bật/tắt (hoặc mang ra xa) từng beacon | đúng endpoint đổi; đo độ trễ ON (kỳ vọng ~vài giây) và OFF (≈ timeout ± 0,25 s) |
| 6 | Rút nguồn, cấp lại | về NORMAL; cấu hình còn nguyên; mọi slot OFF tới khi nhận gói mới |
| 7 | Đổi MAC một slot ở CONFIG | endpoint ID không đổi (parts-list giống bước 1) |
| 8 | Tắt/bật Border Router; ghép thêm controller (multi-admin) | log `Thread detached` → scanner `paused by network_down` → `Thread up` → quét lại sau 10 s; không OFF giả trong thời gian dừng |
| 9 | Beacon phát chậm (5 s, 10 s), che chắn, beacon địa chỉ RPA | đối chiếu bảng xác suất; trang web cảnh báo RPA; ghi số lần OFF giả |
| 10 | Chạy ≥ 24 giờ | ghi: độ trễ ON/OFF, **số lần báo vắng sai** (so với nhật ký thực tế), số lần `Thread detached`, `MAC retry`/`no-ack-expiry`, heap min, `unexpected stops`, `watchdog restarts`, `queue drops` |

Thêm: lặp bước 2 nhiều lần với cả nút trên web, bấm 3 lần ở CONFIG, và chờ 10 phút → luôn về NORMAL; tắt nguồn
trong lúc đang lưu cấu hình → khởi động lại vẫn đọc được bản cũ hoặc bản mới.

Nếu một chip C6 không đạt độ tin cậy (OFF giả hoặc Thread rớt mạng thường xuyên ở tỉ lệ quét cần thiết), ghi lại số
liệu của bước 10 và xem [khuyến nghị](#khuyến-nghị-phương-án-hai-chip); đừng tăng timeout quá mức để che.

---

## Đã xác minh / cần thử trên phần cứng

**Đã xác minh trong phiên này**

* Phiên bản SDK, commit, image Docker (digest) như bảng trên.
* API/Kconfig đối chiếu với mã nguồn đúng phiên bản: `CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING`, `BLEManagerImpl`
  (NimBLE) sở hữu `ble_hs_cfg`, `ble_gap_disc()`/`disable_observer_mode`/`filter_duplicates`, `DISC_COMPLETE` khi host
  reset, cluster Occupancy Sensing code-driven, feature RFSensing trong mô hình dữ liệu 1.6.1, cấp endpoint ID,
  TagList, `SetThreadDeviceType`, OpenThread MTD mặc định rx-on-when-idle, Kconfig bộ lọc trùng của controller C6,
  `RTC_NOINIT_ATTR` trên C6, DAC thử nghiệm cho PID 0x8002, sơ đồ nút BOOT của DevKitC-1 v1.4.
* `idf.py set-target esp32c6` + `idf.py build` thành công (số liệu ở mục kết quả build).
* Host test: 14 bộ (gồm 3 bộ biên dịch lại với 20 slot), đạt (ASan/UBSan).
* Trang web cấu hình chạy trong Chromium headless với API giả lập: 89 kiểm tra đạt (escape tên, gán/trùng beacon,
  kiểm tra dữ liệu, nội dung lệnh lưu, kiểu nội dung POST, ô chọn không bị đóng, khởi động lại, chữ theo mạng
  Thread/Wi-Fi/hai chip, 20 slot, model, pin, thiết bị con từng slot, trạng thái "Sắp quét", Wi-Fi chậm: không
  request chồng nhau).
* Rà soát mã độc lập (đối chiếu mã nguồn SDK): các lỗi tìm được đã sửa, xem [mục dưới](#rà-soát-mã-độc-lập).

**Chưa xác minh — cần thử trên phần cứng**

* Mọi hành vi RF: tỉ lệ nghe beacon thực tế ở 25%, ảnh hưởng tới Thread (rớt mạng, MRP retry), Wi-Fi SoftAP + quét BLE.
* Commissioning thật qua BLE + Thread; tương thích Apple Home, Google Home, Home Assistant (hiển thị 5 endpoint, tên).
* Độ trễ ON/OFF, OFF giả trong 24 giờ, độ ổn định heap.
* Hành vi khi NimBLE host reset (xem giới hạn bên dưới), watchdog quét, backoff.
* Nút BOOT thật (dội phím của SW1, mạch auto-download).

---

## Giới hạn đã biết

* **BLE scan + Thread End Device là "C1" trên ESP32-C6** (không ổn định theo chính Espressif). Firmware giảm thiểu
  (MTD/MED, 25%, tạm dừng khi attach/commissioning) nhưng không loại bỏ được.
* **Host NimBLE reset trong NORMAL**: callback `bleprph_on_sync()` của SDK gọi `xSemaphoreGive(semaphoreHandle)` trong
  khi handle này đã bị xóa và đặt `NULL` sau lần đồng bộ đầu (`src/platform/ESP32/nimble/BLEManagerImpl.cpp`). Nếu host
  reset rồi đồng bộ lại, FreeRTOS có thể assert và thiết bị khởi động lại (về NORMAL, tự ghép lại mạng). Firmware không
  ghi đè callback của Matter để vá; scanner tự xử lý mất đồng bộ/`DISC_COMPLETE` bằng backoff nếu SDK không assert.
* Cửa sổ ghép lần đầu 15 phút; hết thì cần cấp nguồn lại.
* Matter offline trong lúc ở CONFIG (theo thiết kế).
* Không OTA trong phiên bản này (bảng phân vùng 1 app factory 3,6 MB).
* Thông tin xác thực Matter thử nghiệm (VID 0xFFF1, DAC ví dụ, passcode 20202021): không dùng cho sản phẩm.

---

## Khuyến nghị: phương án hai chip

Nếu đo thực tế cho thấy một C6 không đủ tin cậy (C1), phương án tách radio:

* **ESP32-H2 hoặc ESP32-C6 chỉ chạy Matter over Thread** + **một ESP32-C3/S3 chỉ quét BLE** (100% thời gian radio),
  nối UART: chip BLE gửi "slot N thấy beacon, RSSI x" (hoặc trạng thái 5 slot), chip Thread giữ nguyên mô hình 5
  endpoint của firmware này. Hai radio, hai anten → quét BLE không còn lấy thời gian của Thread (cùng khuyến nghị
  dual-SoC của Espressif cho Thread + radio khác).
* Hoặc giữ một chip nhưng cho Thread làm **SED** với poll nhanh và bật ICD, chấp nhận độ trễ downlink (cần đo, vẫn C1).

Không tự đổi mục tiêu dự án: firmware này vẫn là một chip C6 theo yêu cầu.
