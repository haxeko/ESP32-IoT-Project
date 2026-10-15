# Rà soát định danh và độ ổn định khi chạy nhiều ESP32-H2 Matter-over-Thread

Phạm vi: 4 dự án ESP32-H2 Matter over Thread trong repo này.

| Mã | Thư mục | Thiết bị Matter | Flash | OTA | Shell / OT CLI |
|---|---|---|---|---|---|
| P1 | `ESP32-H2/matter-presence-ld2410-learning` | Occupancy Sensor 0x0107 + 3 Mode Select (HA) | 4 MB | có | có |
| P2 | `ESP32-H2/matter-thread/matter-presence-occupancy` | Occupancy Sensor 0x0107 | 4 MB | có | có |
| P3 | `ESP32-H2/matter-thread/matter-relay-switch` | On/Off Plug-in Unit 0x010A | 4 MB | có (rollback) | chỉ bản debug |
| P4 | `ESP32-H2/matter-thread-plug-wroom-2m` | On/Off Plug-in Unit 0x010A | 2 MB | không | chỉ bản debug |

Phiên bản đối chiếu (đúng image Docker dùng để build): ESP-IDF **v6.0.2**, esp-matter **1.6.1** (commit `cd6f2a33`), connectedhomeip **`3d1c9e3c`**, OpenThread đi kèm ESP-IDF, esptool 5.4.0, esp-matter-mfg-tool 1.0.26. Các dự án ESP32-C3 dùng Wi-Fi, nằm ngoài phạm vi.

Cách làm: 7 nhóm rà soát song song (Thread, định danh Matter, lưu trữ/flash, runtime/multi-fabric, chứng thực, QR, log chẩn đoán). Mỗi nhóm có một bên kiểm chứng độc lập, đối chiếu từng dòng code với mã nguồn SDK đúng phiên bản và chạy thử các lệnh trong Docker. Cuối cùng một bên rà soát độ đầy đủ đi tìm chỗ còn thiếu.

Ký hiệu dùng trong tài liệu:

- **[Đã kiểm chứng]**: đã đọc trong code/SDK hoặc đã chạy lệnh thật.
- **[Giả thuyết]**: hợp lý nhưng chỉ xác nhận được trên phần cứng và mạng thật.
- Số dòng `file:dòng` là của commit trước khi sửa (`9a4badf`), trừ khi ghi khác.

---

## A. Tổng quan kiến trúc

### Luồng khởi động (chung cho 4 dự án)

1. `nvs_flash_init()` → tạo node Matter và các endpoint.
2. Gắn các provider: device info **tùy biến** (`main/device_info.cpp`: tên hãng, model, chuỗi phần cứng, serial lấy từ MAC eFuse); DAC **ví dụ của SDK** (`CONFIG_EXAMPLE_DAC_PROVIDER`); dữ liệu commissioning **ví dụ của SDK** (`CONFIG_EXAMPLE_COMMISSIONABLE_DATA_PROVIDER` → passcode 20202021, discriminator 3840).
3. OpenThread chạy trên radio 802.15.4 của chính ESP32-H2 (FTD, có thể làm router); trạng thái OpenThread lưu trong phân vùng `nvs`, namespace `openthread`.
4. `esp_matter::start()`. Nếu chưa ghép nối, mở cửa sổ commissioning 15 phút qua BLE và DNS-SD.
5. Controller (Apple Home, Home Assistant, chip-tool…) ghép qua BLE (PASE), gửi **dataset Thread**, cấp **NOC / Node ID / ACL**. Thiết bị vào mạng Thread và đăng ký dịch vụ `_matter._tcp` lên **SRP server** của Border Router.
6. Sau khi ghép, BLE bị tắt hẳn (`CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING=y`). Controller tìm thiết bị qua DNS-SD (advertising proxy của Border Router) rồi mở phiên CASE.

### Bản đồ flash (P1/P2/P3; P4 tương tự nhưng 2 MB, không có OTA)

| Phân vùng | Offset | Nội dung thực tế | Có thuộc về từng bo? |
|---|---|---|---|
| bootloader, partition table | 0x0, 0xC000 | mã khởi động | không |
| `esp_secure_cert` | 0xD000 | **chưa dùng** | – |
| `nvs` | 0x10000 (48 KB) | `chip-config` (UniqueID…), `chip-counters`, `CHIP_KVS` (fabric, NOC, khóa vận hành, ACL), `openthread` (dataset, ext address, ML-EID…), `psa_its` (network key, khóa SRP), `phy`, dữ liệu ứng dụng | **có**: toàn bộ danh tính lúc chạy |
| `nvs_keys` | 0x1C000 | **chưa dùng** (NVS không mã hóa) | – |
| `otadata`, `phy_init` | 0x1D000, 0x1F000 | trạng thái OTA | không |
| `ota_0` / `ota_1` (P4: `factory`) | 0x20000 / 0x200000 | firmware | không |
| `fctry` | 0x3E0000 (P4: 0x1F0000) | **chưa dùng**; nay dùng cho mã ghép nối riêng (phương án A) hoặc factory data (phương án B) | có |

---

## B. Lỗi thực tế đã tìm thấy

### B1. Mọi bo dùng chung mã ghép nối test của SDK, và vài đường code mở cửa sổ ghép bằng mã này
**Mức: trung bình (bảo mật, ghép nhầm thiết bị). [Đã kiểm chứng]**

- **Bằng chứng:**
  - `P1 sdkconfig:4538` `CONFIG_ENABLE_TEST_SETUP_PARAMS=y`; `:4692` `CONFIG_EXAMPLE_COMMISSIONABLE_DATA_PROVIDER=y`.
  - Trong SDK: `GenericConfigurationManagerImpl.ipp:102-107, 128-133` rơi về `CHIP_DEVICE_CONFIG_USE_TEST_SETUP_PIN_CODE` = 20202021 (`CHIPDeviceConfig.h:1328`) và discriminator 0xF00 (`:1338`).
  - Salt và verifier test có trong file `.bin` của P1/P2 (dò byte).
  - Mã QR trong README: `MT:Y.K9042C00KA0648G00`; `chip-tool payload parse-setup-payload` cho ra VID 65521, PID 32768, BLE, 3840, 20202021.
- **Các đường mở cửa sổ BASIC bằng mã này:**
  1. Lúc khởi động khi chưa ghép (15 phút).
  2. P1/P2 bấm đúp, kể cả khi đã ghép (`open_commissioning_window`, `app_main.cpp:231-252`).
  3. P1/P2 tự mở lại 300 s sau khi xóa fabric cuối (`app_main.cpp:256-261`).
  4. P3/P4 bấm đúp (`app_driver.cpp:123-147`).
- **Hậu quả:**
  - Hai bo chưa ghép cùng quảng bá thì không phân biệt được: tên BLE, VID/PID và discriminator giống nhau, PASE thành công với bất kỳ bo nào trả lời trước. Đây là ghép nhầm bo.
  - Trong lúc cửa sổ mở, ai trong tầm BLE hoặc trên mạng LAN/Thread cũng ghép được bằng mã công khai.
  - **Không** ảnh hưởng thiết bị đã ghép: passcode và discriminator chỉ dùng khi commissioning.
- **Sửa:**
  - Phương án A (đã thêm, bật bằng `sdkconfig.defaults.unique_setup`): mỗi bo tự sinh mã riêng và lưu vào `fctry`.
  - Phương án B: factory data theo từng bo.
  - Bản mặc định giữ nguyên mã test để không làm hỏng QR đã in.
- **Kiểm tra sau sửa:** hai bo in ra QR khác nhau (`setup_code: QR code: MT:…`). `chip-tool payload parse-setup-payload` cho ra passcode và discriminator khác nhau. Ghép bằng QR của bo A thì chỉ bo A hoàn tất.

### B2. Mọi bo dùng chung DAC và khóa riêng DAC ví dụ của SDK (kèm PAI và CD ví dụ)
**Mức: thấp khi phát triển, cao nếu đem ra sản phẩm. [Đã kiểm chứng]**

- **Bằng chứng:**
  - `CONFIG_EXAMPLE_DAC_PROVIDER=y` (`P1 sdkconfig:4688`).
  - `esp_matter_providers.cpp:113-114` chọn `kDevelopmentDAC_*_FFF1_8000` (`credentials/examples/ExampleDACs.cpp:31, 64`).
  - Chuỗi "Matter Dev DAC 0xFFF1/0x8000" nằm trong file `.bin`.
  - Đã chạy `chip-cert validate-att-cert` thành công với PAI `Matter-Development-PAI-FFF1-noPID` và PAA `Chip-Test-PAA-FFF1`.
- **Hậu quả:** khóa riêng DAC công khai trong SDK, ai cũng giả được "thiết bị" này. DAC chỉ dùng khi commissioning (chứng thực thiết bị), **không** liên quan Thread hay vận hành, nên **không** gây chập chờn.
- **Sửa:** phương án B: DAC riêng từng bo, ký bởi PAI test FFF1/8000 của SDK (công cụ ở `ESP32-H2/tools/matter-mfg`).

### B3. P1/P2: bấm đúp chờ khóa Matter vô hạn trong task `esp_timer`
**Mức: thấp (lỗi). [Đã kiểm chứng] Đã sửa.**

- **Bằng chứng:**
  - `P1 app_main.cpp:395-399` (P2 `:375`): `pairing_button_cb()` chạy trong callback `esp_timer` của nút bấm (`drivers/user_button.cpp:120-126`) và gọi `ScopedChipStackLock lock(portMAX_DELAY)`.
  - Chính file đó ghi chú "không được chặn task esp_timer" (`app_main.cpp:129-131`).
  - P3/P4 làm đúng bằng `ScheduleWork`.
- **Hậu quả:** khi task Matter bận, mọi callback `esp_timer` (LED, nút, mem monitor) bị treo theo.
- **Sửa:** chuyển việc sang task Matter bằng `PlatformMgr().ScheduleWork(...)`.
- **Kiểm tra:** bấm đúp, log có `Commissioning window open for 900 s`, LED vẫn nháy đều.

### B4. P1/P2: race giữa `thread_diag_start()` và `thread_diag_stop()`
**Mức: thấp. [Đã kiểm chứng] Đã sửa.**

- **Bằng chứng:** `thread_diag.cpp:117, 129-146` dùng hai cờ `s_running` và `s_done`. Nếu `stop()` (sự kiện `kServerReady`, task Matter) chen giữa lúc `start()` kiểm tra `s_done` và lúc đặt `s_running = true`, task log chạy mãi.
- **Sửa:** dùng một cờ một chiều `s_done` như P3/P4.
- **Kiểm tra:** khởi động bo đã ghép, log có `Commissioned, Thread diagnostics stopped`.

### B5. P3/P4 bỏ qua kết quả `nvs_flash_init()`
**Mức: thấp (độ bền). [Đã kiểm chứng] Đã sửa.**

- **Bằng chứng:** `P3/P4 app_main.cpp:272` gọi `nvs_flash_init();` mà không kiểm tra. `esp_matter::start()` gọi lại `nvs_flash_init_partition` (`ConfigurationManagerImpl.cpp:147-148`), lỗi đó làm `ABORT_APP_ON_FAILURE` dừng chương trình ở mỗi lần khởi động.
- **Sửa:** phục hồi chuẩn của ESP-IDF như P1/P2: khi `ESP_ERR_NVS_NO_FREE_PAGES` hoặc `ESP_ERR_NVS_NEW_VERSION_FOUND` thì xóa NVS kèm log lỗi rõ ràng.
- **Đổi hành vi:** chỉ trong nhánh lỗi. Trước đây thiết bị khởi động lại liên tục; nay mất ghép nối nhưng hoạt động lại được.

### B6. Không có log chẩn đoán sau khi đã ghép
**Mức: thấp (khó chẩn đoán). [Đã kiểm chứng] Đã thêm.**

- `thread_diag` dừng ngay khi đã ghép (`app_main.cpp:269-271, 283-285`).
- P3/P4 bản release không có OT CLI.
- Không log lý do reset: core dump tắt, và `BootReason` của GeneralDiagnostics xếp PANIC/TASK_WDT vào "Unspecified" (`DiagnosticDataProviderImpl.cpp:207-226`).
- Không in QR: esp-matter 1.6.1 không gọi `PrintOnboardingCodes`.
- **Sửa:** module `identity_diag` và `setup_code_print()` (mục E).

### B7. Phân vùng bảo mật có nhưng không dùng; NVS và flash không mã hóa
**Mức: thấp. [Đã kiểm chứng]**

- `esp_secure_cert`, `nvs_keys`, `fctry` không được đọc.
- `CONFIG_SECURE_FLASH_ENC_ENABLED` và `CONFIG_NVS_ENCRYPTION` tắt (`P1 sdkconfig:843, 3842`).
- Cờ `encrypted` trong `partitions.csv` không có tác dụng.
- Nếu dùng phương án B, khóa riêng DAC nằm dạng rõ trong `fctry`. Chấp nhận được cho DIY với chứng chỉ test; sản phẩm thật cần flash encryption cộng `esp-matter-mfg-tool -e`, hoặc `esp_secure_cert` với DS peripheral.

### B8. Tài liệu gây hiểu nhầm
**Mức: thấp. [Đã kiểm chứng]**

- README P1/P2 thiếu cảnh báo "ghép từng bo một" mà P3/P4 có. Đã thêm.
- README P3/P4 ví dụ `SRP host: 744DBDA1B2C3…` (giống serial). Thực tế tên host SRP là **16 ký tự hex của Thread Extended Address ngẫu nhiên** (`GenericConfigurationManagerImpl.ipp:396-401`).
- Cả 4 README dùng Node ID cố định `0x7283` trong ví dụ chip-tool. Ghép hai bo cùng Node ID trong cùng một fabric chip-tool làm trùng tên dịch vụ `<CompressedFabricId>-<NodeId>`, và SRP server của OpenThread từ chối cả gói cập nhật (`srp_server.cpp:517-522, 1045`).
- `erase-flash` trong README xóa luôn `fctry`, cần lưu ý khi dùng phương án A/B.

### B9. P1 và P2 là hai sản phẩm khác nhau nhưng cùng VID/PID và cùng cách đánh số phiên bản
**Mức: thông tin. [Đã kiểm chứng]**

- P2 bật OTA requestor. Nó sẽ nhận file `.ota` của P1 (cùng 0xFFF1/0x8000, số phiên bản lớn hơn), vì `OTAImageProcessorImpl.cpp:612-623` không kiểm tra VID/PID. Khi đó bộ endpoint đổi trên một thiết bị đã ghép.
- Quy tắc: không nạp `.ota` của P1 cho bo P2.
- P3 và P4 trùng tên project CMake `matter_relay_h2`.

### Những điểm đã kiểm tra và **không** có lỗi

Tất cả đều **[Đã kiểm chứng]**:

- Không có code nào đặt cứng Extended Address, MAC, base MAC, dataset, `srand` hay seed. Đã grep `otLinkSetExtendedAddress`, `esp_base_mac_addr_set`, `esp_iface_mac_addr_set`, `otDatasetSetActive`, `srand`… trong cả 4 dự án.
- Các giá trị mặc định `CONFIG_OPENTHREAD_NETWORK_*` (key `00112233…`, PAN 0x1234) là cấu hình chết: chỉ `esp_openthread_auto_start()` dùng, và không có nơi nào gọi hàm này. Dataset chỉ đến từ controller.
- Extended Address và ML-EID được sinh ngẫu nhiên bằng RNG phần cứng ở lần đầu (`mac.cpp:111,119`, `mle.cpp:125`) rồi lưu trong `nvs/openthread`.
- UniqueID: chuỗi ngẫu nhiên 64 bit sinh ở lần khởi động đầu, lưu ở `nvs/chip-config/unique-id`.
- Serial lấy từ MAC eFuse: duy nhất theo chip, không đổi qua OTA và erase-flash.
- File merged nạp ở 0x0 ghi `0xFF` lên `esp_secure_cert`, `nvs`, `nvs_keys`, `otadata`, `phy_init` (đã kiểm từng byte), nên **không** thể chép danh tính sang bo khác. CI chỉ phát hành ảnh firmware (app, bootloader, partition table, `ota_data_initial`, merged, `.ota`), không có ảnh `nvs`, `fctry` hay `esp_secure_cert`. Repo không có ảnh NVS nào, không có hướng dẫn `read-flash`.
- File `.ota` chỉ gồm ứng dụng cộng 83 byte header.
- Factory reset (giữ nút 5 s, xóa fabric cuối trên P3/P4, lệnh shell) xóa dữ liệu Matter, Thread và ứng dụng trong `nvs`; không đụng `fctry` hay `esp_secure_cert`.
- BLE tắt sau khi ghép: không còn chia sóng BLE/802.15.4 lúc vận hành.
- Không có "report storm": thuộc tính chỉ đổi khi có thay đổi thật.
- Thứ tự khóa đúng (CHIP → OpenThread, task OpenThread không lấy khóa CHIP).
- Tài nguyên Matter trên mỗi thiết bị: 15 subscription, 17 phiên bảo mật, 8 exchange (MAX_FABRICS = 5). Các giới hạn này **không** phụ thuộc có bao nhiêu thiết bị khác trong mạng.

---

## C. Bảng nguy cơ trùng định danh

Nguồn cấp:

- **HW**: eFuse, cố định theo chip.
- **RND**: firmware sinh ngẫu nhiên lúc chạy, lưu NVS.
- **FW**: hằng số biên dịch vào firmware.
- **FAC**: factory data.
- **CTL**: controller cấp khi commissioning.
- **NET**: mạng Thread hoặc Border Router cấp.

### Thread

| Định danh | Vai trò | Phải duy nhất? | Nguồn | Tạo / lưu ở đâu | Dùng chung được? | Hiện trạng | Kiểm tra | Sửa nếu trùng |
|---|---|---|---|---|---|---|---|---|
| MAC eFuse (6 byte) | gốc phần cứng, serial | theo chip | HW | eFuse | không | duy nhất | `esptool read-mac`; log `id_diag: hw efuse_mac=` | không sửa được (lỗi phần cứng) |
| IEEE EUI-64 802.15.4 | định danh radio | theo chip | HW | eFuse (`mac_addr.c:414-420`) | không | duy nhất | `id_diag: ot … eui64=` | như trên |
| Extended Address | địa chỉ MAC trên mạng Thread, nguồn của tên host SRP | trong mạng Thread | RND | `mac.cpp:111,119`; lưu `nvs/openthread` (NetworkInfo) | **không** | ngẫu nhiên từng bo | `id_diag: ot ext_addr=`; `chip-tool threadnetworkdiagnostics read ext-address`; `ot-ctl neighbor table` trên OTBR | factory reset hoặc erase-flash bo bị trùng rồi ghép lại |
| RLOC16 / Router ID | định tuyến | trong partition | NET | Leader/parent cấp; lưu NetworkInfo | không | do mạng | `id_diag: rloc16=`; `ot-ctl router table` | tự sửa khi attach lại |
| Mesh-Local EID | địa chỉ IPv6 ổn định trong mesh | trong mạng | RND | `mle.cpp:125`; lưu `nvs/openthread` | **không** | ngẫu nhiên | `id_diag: ml_eid=` | như Extended Address |
| Địa chỉ OMR (SLAAC) | địa chỉ để LAN/điện thoại tới được | toàn cục | NET + RND (secret) | prefix từ BR, IID = SHA256(secret…); secret ở `nvs/openthread` | **không** | duy nhất | `id_diag: br … OMR address=` | như trên |
| Dataset Thread (network key, PAN, Ext PAN, kênh, mesh-local prefix, PSKc) | thông tin mạng | **chung** trong một mạng | CTL/NET | controller gửi qua Network Commissioning; lưu `nvs/openthread` + `psa_its` | **phải giống nhau** | đúng | `id_diag: ot net= ext_panid= ch=`; so với BR | đưa mọi BR về cùng một dataset |
| Partition ID / Leader | phân vùng mesh | chung trong mạng | NET | bầu Leader | phải giống nhau | – | `id_diag: partition= leader_rid=` | kiểm tra vị trí BR |
| Tên host SRP | DNS-SD trên Thread | trong SRP server | RND (= hex của Extended Address) | `GenericConfigurationManagerImpl.ipp:396-401` | **không** | duy nhất nếu Extended Address duy nhất | `id_diag: srp host=`; `ot-ctl srp server host` | như Extended Address |
| Khóa ECDSA của SRP client | xác thực bản ghi SRP | theo thiết bị | RND | `srp_client.cpp:1163-1181`; `psa_its` | **không** | duy nhất | gián tiếp: log BR "Name conflict" | factory reset |
| SRP server đang chọn | BR giữ bản ghi | – | NET | Network Data; lưu `nvs/openthread` | – | tự chọn | `id_diag: srp server=` | xem mục D |

### Matter

| Định danh | Vai trò | Phải duy nhất? | Nguồn | Tạo / lưu ở đâu | Dùng chung được? | Hiện trạng | Kiểm tra | Sửa nếu trùng |
|---|---|---|---|---|---|---|---|---|
| Vendor ID / Product ID | loại sản phẩm | chung cho một sản phẩm | FW | Kconfig 0xFFF1/0x8000 | **có** | dùng chung (đúng) | `id_diag: dev vid= pid=` | không đổi (giữ tương thích) |
| Serial Number | nhãn Basic Information | nên duy nhất | HW | P1/P2: 6 byte của EUI-64; P3/P4: MAC eFuse (`device_info.cpp`) | không nên | duy nhất | `id_diag: dev serial=`; trang thiết bị trong HA | – |
| UniqueID | định danh ổn định cho controller | **phải** duy nhất | RND | `GenericConfigurationManagerImpl.ipp:269-272`; `nvs/chip-config/unique-id` | **không** | duy nhất | `id_diag: dev unique_id=`; `chip-tool basicinformation read unique-id` | factory reset bo bị trùng |
| Rotating Device ID | quảng bá ẩn danh | nếu bật | FAC | – | – | **tắt** (nếu bật với provider tùy biến thì là hằng `00112233…`, `CHIPDeviceConfig.h:370-373`) | `sdkconfig` | để tắt, hoặc override `GetRotatingDeviceIdUniqueId` |
| Discriminator | tìm thiết bị khi ghép | nên khác nhau | FW (test 3840) / A: RND / B: FAC | `ipp:128-133` | **nên khác** | **trùng 3840** | `id_diag: dev discriminator=`; tên BLE `<prefix>3840` (P1 `Presence-`, P2 `Occupancy-`, P3/P4 `MATTER-`) | phương án A/B |
| Setup passcode + SPAKE2+ verifier/salt | khóa PASE | **phải** khác và bí mật | FW (test) / A / B | `ipp:102-107` | **không** | **trùng 20202021, công khai** | không log; xem QR in ra (dev) | phương án A/B |
| DAC + khóa riêng DAC | chứng thực thiết bị | **phải** riêng | FW (ví dụ SDK) / B: FAC | `ExampleDACs.cpp`; B: `fctry/chip-factory` | **không** | **trùng, khóa công khai** | log commissioning; `strings` file `.bin` | phương án B |
| PAI / PAA | chuỗi chứng thực | chung cho dòng sản phẩm | FW / FAC | như trên | có | PAI dev FFF1 | `chip-cert validate-att-cert` | – |
| Certification Declaration | khai báo chứng nhận | chung cho sản phẩm | FW / FAC | `DeviceAttestationCredsExample.cpp` | có | CD ví dụ FFF1 | `chip-cert print-cd` | – |
| Fabric ID | định danh fabric | duy nhất theo controller | CTL | trong NOC; `CHIP_KVS f/<idx>/n` | chung trong một fabric | đúng | `id_diag: fab fabric_id=` | – |
| Compressed Fabric ID | tiền tố tên dịch vụ | **chung** cho mọi thiết bị trong một fabric | CTL (tính ra) | RAM | **có (theo thiết kế)** | đúng | `id_diag: fab compressed_fabric_id=` | không phải lỗi |
| Node ID | địa chỉ vận hành | duy nhất **trong một fabric** | CTL | NOC; `CHIP_KVS` | không (trong cùng fabric) | controller lo | `id_diag: fab node_id=` | gỡ rồi ghép lại với Node ID mới (chip-tool: chọn ID khác) |
| Fabric index | chỉ số nội bộ | trên mỗi thiết bị | thiết bị | `CHIP_KVS g/fidx` | – | – | `id_diag: fab index=` | – |
| NOC/ICAC/RCAC, IPK, ACL | chứng chỉ vận hành, quyền | theo fabric và thiết bị | CTL | `CHIP_KVS f/<idx>/…` | **không** | đúng | `chip-tool operationalcredentials read nocs` | không bao giờ chép |
| Khóa vận hành (operational key) | CASE | theo thiết bị và fabric | RND | `PersistentStorageOperationalKeystore.cpp:218`; `CHIP_KVS f/<idx>/o` | **không** | đúng | gián tiếp | không bao giờ chép |
| Tên BLE / tên dịch vụ commissionable | tìm khi ghép | nên khác | FW + discriminator / RND | `BLEManagerImpl.cpp:1075`; instance name ngẫu nhiên mỗi lần mở cửa sổ | – | **tên BLE trùng** | máy quét BLE | phương án A/B (tên BLE gồm discriminator) |

### Lưu trữ

| Vùng | Chứa | Được chép giữa các bo? | Hiện trạng | Kiểm tra |
|---|---|---|---|---|
| `nvs` (0x10000) | toàn bộ danh tính lúc chạy (bảng trên) | **tuyệt đối không** | quy trình trong repo không bao giờ chép | `id_diag: nvs entries per namespace` |
| `fctry` | A: `setup-code`; B: `chip-factory` | **không**: mỗi bo một file | trước đây trống | `esptool read-flash 0x3E0000 0x6000` + `nvs_tool.py` |
| `esp_secure_cert` | (chưa dùng) | không | trống | – |
| Ứng dụng (`ota_0`/`factory`) | firmware | **có**: một file cho mọi bo | đúng | – |

---

## D. Vì sao thêm bo thứ hai thì chập chờn

### Những gì loại trừ được từ code [Đã kiểm chứng]

- **Không phải do flash cùng một firmware.** File ứng dụng không chứa danh tính nào thay đổi theo bo. File merged xóa trắng `nvs`. Mọi định danh Thread/Matter lúc chạy đều sinh ngẫu nhiên trên từng chip.
- **Không phải giới hạn subscription hay phiên.** Bể tài nguyên tính riêng trên mỗi thiết bị (15 subscription, mỗi fabric được bảo đảm 3; 17 phiên bảo mật) và không đổi khi số thiết bị trong mạng tăng.
- **Không phải BLE chia sóng.** BLE đã tắt sau khi ghép.
- **Không phải report storm.**
- **Compressed Fabric ID giống nhau giữa hai bo trong cùng một nhà là bình thường.**

### Các nguyên nhân còn lại, xếp theo mức phù hợp với hiện tượng

**1. Border Router, SRP và định tuyến giữa các hệ sinh thái** [Giả thuyết, khớp nhất]
- **Cơ chế đã đọc trong SDK:**
  - CHIP bật SRP auto-start (`GenericThreadStackManagerImpl_OpenThread.hpp:737`).
  - Mỗi thiết bị chọn **một** SRP server từ Network Data theo thứ tự (`srp_client.cpp:2291-2328`): server unicast đã lưu, rồi anycast (đi theo chi phí đường mesh ngắn nhất), rồi unicast.
  - Bản ghi có lease 2 giờ.
- **Liên hệ với hiện tượng:**
  - Mỗi BR chỉ công bố ra LAN những bản ghi nó đang giữ.
  - Khi có HomePod/Apple TV, OTBR của Home Assistant, có khi cả Aqara M3, hai bo giống hệt nhau có thể đăng ký ở hai BR khác nhau.
  - Thêm một FTD làm router làm đổi chi phí đường đi, nên đổi luôn server anycast.
  - Bản ghi cũ có thể còn trên BR cũ tới 2 giờ.
- **Vì sao HA vẫn chạy:** Matter Server của HA chạy cạnh OTBR của nó, đường đi trực tiếp. Apple phải phân giải tên qua mDNS từ advertising proxy và định tuyến qua BR của Apple.
- **Phân biệt:** xem bài test hướng truyền ở mục K (test 9/10).

**2. Các BR không cùng một mạng Thread** [Giả thuyết]
- Thiết bị chỉ vào được mạng của controller đã ghép nó.
- Nếu OTBR của HA và mạng của Apple có Ext PAN ID khác nhau, Apple chỉ tới được thiết bị qua định tuyến IPv6 vòng qua BR kia, rất mong manh.
- Kiểm tra: dòng `id_diag: ot … ext_panid= partition=` trên từng bo phải trùng với dataset của mọi BR.
- Sửa: đồng bộ một dataset cho tất cả BR (HA: "Sync Thread credentials" với Apple), rồi ghép lại các bo nằm sai mạng.

**3. Topology Thread thay đổi khi thêm một FTD** [Giả thuyết, mất kết nối tạm thời]
- Cả 4 dự án là FTD, được phép làm router (`esp_matter_core.cpp:302-303`).
- Bo mới vào làm child, sau đó lên router với độ trễ ngẫu nhiên tới 120 s (`mle.hpp:1344-1346`). RLOC16 đổi, các exchange đang dở có thể hỏng vài giây tới vài phút.
- Sau khi mất điện, bo nào khởi động trước BR có thể tự lập partition riêng cho tới khi gộp lại.
- Theo dõi: dòng `id_diag: mle … partition_changes= parent_changes=` và `partition=` phải ổn định.

**4. Bo bị chép nguyên NVS từ bo khác** [Giả thuyết, khả năng thấp]
- Nếu ai đó `read-flash` một bo đã ghép rồi `write-flash` cả ảnh sang bo khác, hai bo có cùng Extended Address, ML-EID, địa chỉ OMR, tên host SRP, khóa SRP, frame counter, UniqueID, NOC và Node ID. Chúng sẽ "giành" nhau đúng như hiện tượng (`mac.cpp:1589-1591` loại frame trùng counter; `address_resolver.cpp:765-776`).
- Repo không làm điều này, nhưng không loại trừ được nếu đã từng nạp tay.
- Kiểm tra: so `id_diag: ot ext_addr=` và `dev unique_id=` của hai bo. Trùng là kết luận được.

**5. Ghép nhầm bo do mã ghép chung** [Đã kiểm chứng cơ chế]
- Không gây chập chờn, nhưng gây "bo A bấm thì bo B phản hồi".
- Kiểm tra: so Serial trong Apple Home và HA với dòng `dev serial=` của từng bo; dùng Identify.

**6. Thiết bị khởi động lại hoặc sụt áp** [Giả thuyết]
- Ví dụ hai bo radar chung một nguồn USB yếu.
- Nay log có `hw … reset=` (BROWNOUT, PANIC, TASK WATCHDOG…), cộng `chip-tool generaldiagnostics read reboot-count`.

### Multi-fabric: Apple Home + Home Assistant (+ Aqara) [Đã kiểm chứng phía thiết bị]

- **Bảng fabric:** tối đa 5 fabric (`CONFIG_MAX_FABRICS=5`). Mỗi controller là một fabric riêng, có Fabric index, Node ID, NOC, IPK và ACL riêng (`CHIP_KVS f/<idx>/…`). Node ID chỉ cần duy nhất trong fabric của nó.
- **CASE:** phiên riêng theo từng fabric, bể chung 17 phiên. Có bộ nhớ đệm resumption 15 mục.
- **Subscription:** tối đa 15, mỗi fabric được bảo đảm 3. Được lưu lại và khôi phục sau reboot (`CONFIG_ENABLE_PERSIST_SUBSCRIPTIONS=y`). Không bật ICD. Firmware không can thiệp chu kỳ báo cáo: max interval do từng controller chọn (`ReadHandler.cpp:762-763`).
- **Cửa sổ commissioning:**
  - "Bật chế độ ghép nối / Chia sẻ" từ app (Apple, HA) mở cửa sổ **Enhanced** với mã do controller sinh: an toàn, nên dùng cho multi-admin.
  - Bấm đúp nút mở cửa sổ **Basic** bằng mã của chính bo. Với mã test chung, chỉ nên dùng khi bo đó là bo duy nhất đang mở cửa sổ.
- **Aqara M3:**
  - Nếu chỉ làm Border Router: nó là thêm một SRP server hoặc advertising proxy, rơi vào nguyên nhân 1 và 2.
  - Nếu làm controller: thêm một fabric.
- **Vì sao HA chạy mà Apple chập chờn:** dữ liệu trong fabric độc lập, nên nếu fabric hay ACL hỏng thì cả lệnh lẫn báo cáo của Apple đều hỏng. Nếu bấm nút vật lý mà Apple Home vẫn cập nhật trạng thái nhưng lệnh từ Apple không tới, thì đường thiết bị→controller (subscription, CASE) còn tốt, còn đường controller→thiết bị (phân giải DNS-SD, định tuyến vào mesh qua BR) có vấn đề. Đó là nguyên nhân 1 và 2, không phải lỗi định danh.

---

## E. Phương án sửa source code

Tất cả đều **thêm vào**; bản build mặc định giữ nguyên hành vi ghép nối. Không đổi GPIO, endpoint, cluster, VID/PID hay định dạng dữ liệu đã lưu. OTA giữ nguyên.

| # | Thay đổi | Dự án | Tạm thời / production | Ảnh hưởng thiết bị đã ghép |
|---|---|---|---|---|
| E1 | `main/identity_diag.{h,cpp}`: log danh tính, Thread, SRP, fabric, phiên, subscription, NVS, lý do reset; Kconfig `IDENTITY_DIAG_PERIOD_SEC` (mặc định 300 s, 0 = tắt), `IDENTITY_DIAG_SUBSCRIPTION_LOG` | P1–P4 | công cụ chẩn đoán, giữ được trong production | chỉ thêm log, không đổi dữ liệu |
| E2 | `main/setup_code.{h,cpp}` `setup_code_print()`: in QR, link ảnh QR và mã thủ công khi bo có thể được ghép (lúc khởi động chưa ghép, hoặc khi nút mở cửa sổ); Kconfig `PRINT_ONBOARDING_CODES` (mặc định bật) | P1–P4 | dev: **tắt trong production** vì QR chứa passcode | không |
| E3 | Phương án A: `UniqueSetupCodeProvider` sinh passcode, discriminator và salt ngẫu nhiên, tính verifier SPAKE2+ ngay trên chip, lưu `fctry/setup-code`; bật bằng `sdkconfig.defaults.unique_setup` | P1–P4 | dev và lô nhỏ | fabric cũ giữ nguyên; QR in sẵn (test) hết hiệu lực cho lần ghép **mới** |
| E4 | Phương án B: `sdkconfig.defaults.factory` cộng `ESP32-H2/tools/matter-mfg` (DAC riêng, verifier, discriminator trong `fctry`) | P1–P4 | production (vẫn với VID/PID test cho DIY) | fabric cũ giữ nguyên; phải nạp `fctry` **trước** firmware |
| E5 | Sửa B3: nút bấm đúp dùng `ScheduleWork` | P1, P2 | sửa lỗi | không |
| E6 | Sửa B4: cờ một chiều trong `thread_diag` | P1, P2 | sửa lỗi | không |
| E7 | Sửa B5: phục hồi NVS chuẩn ESP-IDF | P3, P4 | sửa lỗi (nhánh lỗi) | chỉ khi NVS đã hỏng |
| E8 | README: ghép từng bo, Node ID chip-tool khác nhau, mục chẩn đoán và mã ghép | P1–P4 | tài liệu | – |

Lưu ý thiết kế E3:

- **Ngẫu nhiên:** dùng DRBG của CHIP, băm SHA-256 cùng MAC eFuse và một bộ đếm. MAC bảo đảm đầu vào khác nhau trên mọi chip; DRBG bảo đảm không đoán được.
- **Passcode:** lấy mẫu loại bỏ (rejection sampling) trên 27 bit, loại các giá trị cấm theo `PayloadContents::IsValidSetupPIN`.
- **Discriminator:** 12 bit, tránh 3840.
- **Salt:** 32 byte. **Số vòng PBKDF2:** 1000, bằng mức của mã test.
- **Lúc sinh:** ở lần khởi động đầu, `setup_code_preload()` (gọi ngay sau `esp_matter::start()`) cho task Matter (stack 8 KB) sinh mã rồi mới chạy tiếp. Cách này tránh để việc tính PBKDF2 và SPAKE2+ rơi vào task nhỏ (stack 4 KB của `id_diag`) trên bo đã ghép, nơi lúc khởi động không có cửa sổ ghép nào đọc mã. Sau đó mã được lưu và dùng lại.
- **Vì sao lưu ở `fctry`:** giữ qua reboot, OTA, factory reset của Matter (chỉ xóa `nvs`) và nạp file merged (chỉ ghi tới 0x1D47D0). Chỉ `erase-flash` mới làm mã đổi; khi đó bo in mã mới ra log.
- **Tên BLE:** tự thành `<prefix><discriminator>` (P1 `Presence-`, P2 `Occupancy-`, P3/P4 `MATTER-` mặc định của SDK), mỗi bo một tên.

## F. Code đầy đủ

Nội dung đầy đủ của mọi file nằm trong commit trên nhánh `claude/bold-dijkstra-mwkn3q`.

- **File mới (giống hệt nhau ở 4 dự án):**
  - `main/identity_diag.h`, `main/identity_diag.cpp`
  - `main/setup_code.h`, `main/setup_code.cpp`
  - `sdkconfig.defaults.unique_setup`, `sdkconfig.defaults.factory`
- **File sửa:**
  - `main/app_main.cpp` (4 dự án)
  - `main/app_driver.cpp` (P3, P4)
  - `main/thread_diag.cpp` (P1, P2)
  - `main/mem_monitor.cpp` (P1)
  - `main/Kconfig.projbuild`, `.gitignore` (4 dự án)
- **Công cụ:** `ESP32-H2/tools/matter-mfg/` gồm `in_docker.sh`, `gen_pki.sh`, `gen_factory_data.sh`, `flash_factory.sh`, `check_qr.sh`, `README.md`.

API dùng trong code đều đã tra trong header của đúng phiên bản SDK. Cái nào không có thì không dùng, ví dụ: không có bộ đếm phiên public nên phải duyệt bằng `ForEachSession`; getter fabric/peer của `ReadHandler` là private nên phải ghi nhớ lúc `OnSubscriptionRequested`; sự kiện fabric của esp-matter không kèm fabric index.

Lệnh build, nạp, xem log (ví dụ P1; P3/P4 có thêm bản debug):

```bash
cd ESP32-H2/matter-presence-ld2410-learning
idf.py set-target esp32h2 && idf.py build                    # bản mặc định (mã test chung)
idf.py -p /dev/ttyUSB0 flash monitor                         # giữ ghép nối nếu chưa từng OTA
# Mã ghép riêng từng bo (phương án A):
idf.py -B build-unique -D IDF_TARGET=esp32h2 -D SDKCONFIG=sdkconfig.unique \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.unique_setup" build
idf.py -B build-unique -p /dev/ttyUSB0 flash monitor
# Factory data (phương án B): xem mục H
```

---

## G. Quy trình chứng chỉ Matter test

### Vai trò và chuỗi xác thực

- **PAA** (Product Attestation Authority): CA gốc. Controller tin PAA qua DCL (production) hoặc một kho tin cậy cục bộ.
- **PAI** (Product Attestation Intermediate): CA trung gian của hãng, gắn với VID (có thể kèm PID). PAA ký PAI.
- **DAC** (Device Attestation Certificate): chứng chỉ **riêng từng thiết bị**, subject có VID/PID, **khóa riêng nằm trên thiết bị**. PAI ký DAC.
- **CD** (Certification Declaration): CSA ký, khẳng định VID/PID đã qua chứng nhận. Thiết bị gửi kèm khi được chứng thực.

Khi commissioning, controller gửi nonce. Thiết bị trả DAC, PAI, CD và chữ ký bằng khóa riêng DAC. Controller kiểm tra chuỗi DAC→PAI→PAA, kiểm tra CD, rồi mới cấp **NOC**.

**DAC ≠ NOC:**

- DAC do nhà sản xuất cấp, cố định suốt đời thiết bị, chứng minh "đây là sản phẩm thật".
- NOC do controller (fabric) cấp lúc ghép, mỗi fabric một NOC, chứng minh "thiết bị thuộc nhà tôi" và dùng cho CASE.

### Ba loại chứng chỉ

| Loại | Ví dụ | Dùng cho | Controller thương mại tin? |
|---|---|---|---|
| Chứng chỉ test của SDK | DAC ví dụ đang biên dịch sẵn; `Chip-Test-PAI-FFF1-8000` (khóa có trong SDK); PAA `Chip-Test-PAA-FFF1` | phát triển, DIY | Có cảnh báo "Uncertified accessory" và cho thêm (Apple, theo trải nghiệm ghi trong README). HA, Aqara: [Giả thuyết], chưa kiểm trên thiết bị thật |
| Chuỗi riêng tự tạo | `gen_pki.sh --private` → PAA/PAI riêng | thử nghiệm nội bộ với chip-tool (`--paa-trust-store-path`) | **Không**: PAA lạ không có trong DCL nên bị từ chối |
| Production | PAA trong DCL, PAI của hãng, CD do CSA ký sau khi chứng nhận | sản phẩm bán ra | Có, sau khi chứng nhận |

Đã kiểm chứng trong SDK: chip-tool và controller Python tin `credentials/development/paa-root-certs` (có `Chip-Test-PAA-FFF1`). Với Apple, HA và Aqara, chỉ dựa trên ghi chú README và trải nghiệm, chưa đo trên thiết bị thật.

### Lệnh (đã chạy thử thành công trong Docker esp-matter 1.6.1)

Cách dễ nhất là dùng script: `cd <thư mục làm việc>`, rồi `…/tools/matter-mfg/in_docker.sh gen_pki.sh [--private]`.

Lệnh gốc tương ứng:

```bash
CRED=$ESP_MATTER_PATH/connectedhomeip/connectedhomeip/credentials
T=$CRED/test/attestation; C=$CRED/test/certification-declaration
# DAC riêng cho từng bo, ký bởi PAI test FFF1/8000 của SDK (chuỗi kết thúc ở PAA test mà controller dev tin):
chip-cert gen-att-cert -t d -c "H2 DAC dev1" -V FFF1 -P 8000 \
  -C $T/Chip-Test-PAI-FFF1-8000-Cert.pem -K $T/Chip-Test-PAI-FFF1-8000-Key.pem \
  -o DAC_dev1-Cert.pem -O DAC_dev1-Key.pem -f "2026-01-01 00:00:00" -l 4294967295
chip-cert validate-att-cert -d DAC_dev1-Cert.pem -i $T/Chip-Test-PAI-FFF1-8000-Cert.pem -a $T/Chip-Test-PAA-FFF1-Cert.pem
# Chuỗi riêng (chỉ để thử với chip-tool):
chip-cert gen-att-cert -t a -c "Dev PAA" -V FFF1 -o PAA-Cert.pem -O PAA-Key.pem -f "2026-01-01 00:00:00" -l 4294967295
chip-cert gen-att-cert -t i -c "Dev PAI" -V FFF1 -P 8000 -C PAA-Cert.pem -K PAA-Key.pem -o PAI-Cert.pem -O PAI-Key.pem \
  -f "2026-01-01 00:00:00" -l 4294967295
# CD test (ký bằng khóa CD test của SDK), device type 0x0107 hoặc 0x010A:
chip-cert gen-cd -K $C/Chip-Test-CD-Signing-Key.pem -C $C/Chip-Test-CD-Signing-Cert.pem -O CD_occupancy.der \
  -f 1 -V FFF1 -p 8000 -d 0107 -c CSA00000SWC00000-01 -l 0 -i 0 -n 1 -t 0
```

Ghi chú (đã kiểm chứng):

- Kiểm tra DAC của chuỗi riêng với PAA test của SDK thì thất bại (mã 301), đúng như mong đợi.
- CD mà firmware đang gửi do khóa CSA "001" ký (VID FFF1, PID 0x8000–0x8063). CD tự ký bằng khóa test chỉ được chấp nhận ở bên kiểm tra cho phép khóa test (`DefaultDeviceAttestationVerifier.cpp:633-656`). Vì vậy `gen_factory_data.sh` **mặc định dùng lại CD ví dụ** để controller thấy đúng CD đã chấp nhận từ trước.
- VID/PID giữ 0xFFF1/0x8000 vì firmware báo VID/PID từ Kconfig (`GenericDeviceInstanceInfoProvider.ipp:25-35`). DAC phải cùng VID/PID.

Firmware lấy chứng thực từ `fctry` khi build với `sdkconfig.defaults.factory`:

- `CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER`
- `CONFIG_FACTORY_PARTITION_DAC_PROVIDER`
- `CONFIG_FACTORY_COMMISSIONABLE_DATA_PROVIDER`
- `CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL="fctry"`
- `CONFIG_ENABLE_TEST_SETUP_PARAMS=n`
- giữ `CONFIG_CUSTOM_DEVICE_INSTANCE_INFO_PROVIDER` để serial từ eFuse và tên hiển thị không đổi trên thiết bị đã ghép.

---

## H. Factory data riêng cho mỗi ESP32-H2

### So sánh hai phương án

| | A: Development (`sdkconfig.defaults.unique_setup`) | B: Manufacturing (`sdkconfig.defaults.factory`) |
|---|---|---|
| Passcode / discriminator | bo tự sinh lần đầu, lưu `fctry/setup-code` | sinh trên máy tính (`esp-matter-mfg-tool`), nạp vào `fctry` |
| Verifier SPAKE2+ | tính trên chip (1000 vòng) | tính trên máy (mặc định 10000 vòng) |
| Passcode lưu trên thiết bị | có (để tự in QR) | **không** (chỉ verifier); QR in sẵn lên nhãn |
| DAC | vẫn là DAC ví dụ dùng chung | **riêng từng bo** |
| QR | đọc từ log serial | file PNG cộng CSV, dán nhãn |
| Sau erase-flash | sinh mã mới (nhãn cũ hết hạn) | phải nạp lại đúng file `fctry` của bo đó |
| Độ an toàn | trung bình: mã riêng, nhưng DAC chung và passcode nằm rõ trong flash | cao hơn: DAC riêng; nếu bật flash encryption thì khóa được bảo vệ |
| Mở rộng | không cần công cụ, hợp vài chục bo | cần quản lý file theo serial, hợp sản xuất |
| Độ phức tạp | thấp | trung bình |
| Tương thích Matter | đúng spec cho passcode (ngẫu nhiên, không suy ra từ dữ liệu công khai) | đúng quy trình sản xuất của Espressif/CSA (DAC riêng là bắt buộc với sản phẩm chứng nhận) |

### Quy trình B (đã chạy thử tạo dữ liệu; chưa nạp lên bo thật)

```bash
# Thư mục làm việc NGOÀI repo git: kết quả chứa khóa riêng DAC và passcode
mkdir -p ~/matter-mfg && cd ~/matter-mfg
T=<repo>/ESP32-H2/tools/matter-mfg
$T/in_docker.sh gen_pki.sh                                         # CD
$T/in_docker.sh gen_factory_data.sh -t occupancy -n 10             # -t plug cho P3/P4
# với mỗi bo: nạp fctry TRƯỚC, rồi firmware build với sdkconfig.defaults.factory
$T/flash_factory.sh /dev/ttyUSB0 factory/occupancy-*/fff1_8000/<uuid>/<uuid>-partition.bin             # 4 MB: 0x3E0000
$T/flash_factory.sh /dev/ttyUSB0 factory/plug-*/fff1_8000/<uuid>/<uuid>-partition.bin 0x1F0000         # P4 (2 MB)
cd <repo>/ESP32-H2/<thư mục dự án>
idf.py -B build-factory -D IDF_TARGET=esp32h2 -D SDKCONFIG=sdkconfig.factory \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.factory" build
idf.py -B build-factory -p /dev/ttyUSB0 flash monitor
```

Kết quả chạy thử:

- Mỗi bo ra một file `<uuid>-partition.bin` 24576 byte (vừa phân vùng 0x6000), `onb_codes.csv` và `qrcode.png`.
- Mọi DAC đều qua `chip-cert validate-att-cert`. Discriminator và QR khác nhau giữa các bo.
- Namespace `chip-factory` gồm `dac-cert`, `dac-key`, `dac-pub-key`, `pai-cert`, `cert-dclrn`, `discriminator`, `iteration-count`, `salt`, `verifier`, `serial-num`, `vendor-id`, `product-id`…, **không có passcode**.
- `serial-num` trong file là chuỗi ngẫu nhiên mà firmware **không dùng**: bo vẫn báo serial từ MAC eFuse (`device_info.cpp`). Vì vậy script in danh sách theo `uuid`, không theo serial.

Lưu ý [đã kiểm chứng trong SDK]:

- **Bo đã ghép:** đổi sang firmware factory vẫn giữ các fabric hiện có, vì DAC chỉ kiểm khi commissioning.
- **Quên nạp `fctry`:** bo không ghép mới được nữa. Phải nạp `fctry` trước.
- **Xóa thư mục `internal/`:** chứa khóa riêng DAC, cần xóa sau khi nạp.
- **mfg-tool 1.0.26:** sinh passcode bằng `random` của Python (không phải nguồn ngẫu nhiên mật mã), và mỗi bo bốc discriminator độc lập trong 4096 giá trị, nên có thể trùng ngay trong một lô (khoảng 1 % với 10 bo, 26 % với 50 bo). `gen_factory_data.sh` báo lỗi khi có discriminator trùng trong lô; giữa các lô thì phải tự đối chiếu file kiểm kê.

### Quy trình A

1. Build với `sdkconfig.defaults.unique_setup`, nạp merged hoặc app.
2. Log khởi động in `setup_code: New per-board setup code generated and stored in fctry/setup-code, discriminator N`, rồi `QR code: MT:…`, `QR image: https://project-chip.github.io/…` và `Manual pairing code: …`.
3. Chép mã sang nhãn dán trên bo.
4. Các lần khởi động sau in `Per-board setup code loaded …`, mã giữ nguyên.

Factory reset phải xóa và giữ đúng thứ:

| Thao tác | Xóa | Giữ |
|---|---|---|
| Giữ nút 5 s / shell `matter esp factoryreset` | `nvs`: fabric, NOC, ACL, khóa vận hành, dataset Thread, Extended Address, ML-EID, UniqueID, dữ liệu ứng dụng | `fctry` (mã A hoặc dữ liệu B), eFuse (MAC, serial), firmware |
| Nạp file merged ở 0x0 | `nvs`, `nvs_keys`, `esp_secure_cert`, `otadata` | `fctry` |
| `erase-flash` | tất cả, kể cả `fctry` | eFuse |

---

## I. Mã QR và mã ghép thủ công

### Thành phần

- **Version:** 0.
- **VID/PID:** 0xFFF1/0x8000.
- **Commissioning flow:** 0 (standard).
- **Discovery capabilities:** bitmask, 2 = BLE cho Thread. Khi BLE đã tắt thì là 4 = on-network.
- **Discriminator:** 12 bit trong QR. Mã thủ công chỉ mang **4 bit cao**, nên 16 discriminator khác nhau vẫn có thể ra cùng mã thủ công; chỉ passcode mới phân biệt được.
- **Passcode:** 27 bit, từ 1 tới 99999998. Cấm 00000000, 11111111…99999999, 12345678, 87654321.

QR **không phải** DAC, không phải chứng chỉ, không thay quá trình chứng thực. Nó chỉ chứa thông tin để tìm thiết bị và khóa PASE. Ai có QR thì ghép được khi cửa sổ đang mở.

### Tạo và kiểm tra (đã chạy thử)

```bash
chip-tool payload generate-qrcode --vendor-id 0xFFF1 --product-id 0x8000 --discriminator 3840 \
  --setup-pin-code 20202021 --version 0 --commissioning-mode 0 --rendezvous 2      # → MT:Y.K9042C00KA0648G00
chip-tool payload generate-manualcode --vendor-id 0xFFF1 --product-id 0x8000 --discriminator 3840 \
  --setup-pin-code 20202021 --version 0 --commissioning-mode 0                      # → 34970112332
chip-tool payload parse-setup-payload MT:Y.K9042C00KA0648G00                        # hoặc tools/matter-mfg/check_qr.sh
```

- passcode `12345678` bị từ chối (lỗi 0x2F), đúng như mong đợi.
- **In lên serial (dev):** firmware mới in khi bo có thể được ghép (`CONFIG_PRINT_ONBOARDING_CODES`). P1/P2 còn có lệnh shell `matter onboardingcodes ble`.
- **Ảnh QR:**
  - phương án B: file `<uuid>-qrcode.png`;
  - phương án A hoặc mã test: mở link `QR image:` in trong log, hoặc tự tạo bằng bất kỳ công cụ QR nào với chuỗi `MT:…`.
- **So nhãn với dữ liệu thật:** `check_qr.sh <QR trên nhãn>` phải cho ra cùng discriminator với dòng `id_diag: dev discriminator=` của bo đó. Với phương án B, so thêm với `<uuid>-onb_codes.csv`. Dùng mã QR để so: mã nhập tay chỉ mang passcode và 4 bit cao của discriminator (`Short discriminator` = discriminator >> 8), VID/PID hiện là 0.

### Rủi ro khi nhiều bo dùng chung mã

Xem B1:

- ghép nhầm bo;
- mã công khai cho phép người khác ghép khi cửa sổ đang mở;
- không phân biệt được bo qua BLE hay DNS-SD.

Không ảnh hưởng thiết bị đã ghép. Cơ chế tạo mã riêng: phương án A hoặc B (mục H).

---

## J. Quy trình flash hàng loạt an toàn

1. **Chỉ chép firmware, không bao giờ chép dữ liệu.** Dùng file merged (0x0) hoặc app, `idf.py flash` hoặc `.ota`. **Không** `read-flash` một bo đã ghép để nạp sang bo khác.
2. **Bo mới hoặc bo chuyển nhà:** `erase-flash` hoặc nạp merged, rồi ghép lại.
3. **Phương án B:** chạy script trong thư mục **ngoài repo**. Mỗi bo một file `fctry`. Lúc nạp, ghi cặp `uuid ↔ QR` vào bảng kiểm kê và dán nhãn ngay; nạp `fctry` **trước** firmware, đọc lại để so (`flash_factory.sh` tự làm). Sau lần khởi động đầu, ghi thêm serial thật của bo (dòng `id_diag: dev serial=`). Xóa thư mục `internal/` chứa khóa DAC.
4. **Phương án A:** nạp firmware, đọc QR từ log, dán nhãn ngay.
5. **Ghép từng bo một.** Với mã test chung, các bo khác phải tắt nguồn hoặc đã ghép xong. Sau khi ghép, kiểm tra bằng Identify và so Serial trong app với dòng `dev serial=` của bo.
6. **Multi-admin:** dùng "Bật chế độ ghép nối" (Enhanced) trong Apple Home hoặc HA thay vì bấm đúp.
7. **chip-tool:** mỗi bo một Node ID (0x7283, 0x7284…).
8. **Sau khi nạp xong cả lô:** chạy test 2–4 ở mục K. Không có hai bo nào trùng `ext_addr`, `unique_id`, `serial` hay `discriminator` (A/B).

---

## K. Bộ test xác nhận

Chỉ số đo được ngay bằng log `id_diag` (mỗi 300 s và theo sự kiện) cộng chip-tool hoặc HA:

| Chỉ số | Nguồn |
|---|---|
| Tỷ lệ lệnh thành công | đếm lệnh gửi từ Apple, HA hoặc chip-tool so với trạng thái thực (đèn, relay, log `attribute`) |
| Độ trễ điều khiển | thời gian từ lúc bấm trong app tới log `attribute update` trên bo; chip-tool in thời gian |
| Số lần mất kết nối | `EV Thread connectivity LOST`, `mle … detached=` |
| Số lần reattach Thread | `mle attach_attempts= parent_changes= partition_changes=` |
| Lỗi CASE | `EV CASE session established` (đếm) cùng log CHIP `CASE` / `Sigma`; số phiên `mtr sessions: case=` |
| Lỗi subscription | `SUB request` không có `SUB established` đi kèm; `SUB terminated`; `mtr since boot: subscribe …` |
| RAM | `hw heap= min_heap=` (P1 có thêm `mem: Stack unused`) |
| Thời gian phục hồi sau reboot | từ `reset=` tới `EV Thread connectivity established` và `SUB established` đầu tiên |

| Test | Cách làm | Đạt khi |
|---|---|---|
| 1. Một thiết bị | ghép một bo vào HA (hoặc Apple), 30 phút, 50 lệnh | 100 % lệnh; không `LOST`; `srp state=Registered`; `OMR address=yes` |
| 2. Hai thiết bị | thêm bo 2 (ghép từng bo) | hai bo khác `ext_addr`, `unique_id`, `serial`, `ml_eid`, `srp host`; **cùng** `ext_panid`, `partition`; ≥ 99 % lệnh trên cả hai |
| 3. Năm thiết bị | như trên với 5 bo | như trên; `routers in partition` hợp lý; không bo nào `partition_changes` tăng liên tục |
| 4. Mười thiết bị | như trên với 10 bo | như trên; `nvs entries` còn dư; `min_heap` > 16 KB |
| 5. Reboot cùng lúc | rút và cắm nguồn chung | mọi bo `reset=power-on`, trở lại `Registered` và `SUB established` trong < 2 phút; sau 5 phút không còn `LOST` |
| 6. Mất điện cả nhà (cả BR) | tắt nguồn BR và bo, bật bo trước rồi BR | sau khi BR lên, `partition` của mọi bo = của BR (gộp partition), lệnh lại thành công |
| 7. Factory reset một bo | giữ nút 5 s | bo đó có `ext_addr` và `unique_id` **mới**; các bo khác không đổi; ghép lại được |
| 8. Thêm bo mới vào mạng đang chạy | ghép bo thứ N+1 | các bo cũ không có `LOST` hay `SUB terminated` kéo dài > 2 phút (thời gian router lên cấp ≤ 120 s) |
| 9. Apple Home điều khiển đồng thời | bật/tắt liên tục nhiều bo từ iPhone; **bấm nút vật lý và xem Apple Home có cập nhật không** | nút → Apple cập nhật **và** lệnh Apple → bo đều chạy. Nếu chỉ chiều nút → Apple chạy: lỗi BR/SRP/định tuyến (mục D1/D2) |
| 10. HA + Apple cùng điều khiển | xen kẽ lệnh từ hai app trên cùng bo | cả hai đúng; `fab` có 2 dòng, `subscriptions` mỗi fabric ≥ 1 |
| 11. Khởi động lại Border Router | reboot HomePod/Apple TV, rồi OTBR | trong ≤ 5 phút `srp state=Registered` trở lại; `srp server=` có thể đổi; lệnh từ cả hai app trở lại |
| 12. Sau 24 giờ | để chạy | `min_heap` ổn định; `sub_terminated` không tăng đều; `attach_attempts` không tăng; không có `reset=` khác `power-on` |

Thứ tự thử multi-fabric:

1. Từng fabric riêng: chỉ Apple, rồi gỡ và chỉ HA.
2. Sau đó multi-admin: ghép Apple, rồi chia sẻ sang HA bằng mã do Apple tạo.
3. Mỗi bước ghi log `id_diag` của mọi bo.

---

## L. Checklist nghiệm thu

- [ ] Mọi bo build từ cùng một commit; firmware chỉ nạp file merged, app hoặc OTA; không có ảnh `nvs` nào được chép.
- [ ] Log `id_diag` của từng bo: `efuse_mac`, `eui64`, `serial`, `unique_id`, `ext_addr`, `ml_eid`, `srp host` **khác nhau** giữa các bo.
- [ ] Mọi bo và mọi BR: **cùng** `ext_panid`, `net`, `ch`, `partition`.
- [ ] `srp state=Registered`, `services=n/n registered`, `OMR address=yes` trên mọi bo.
- [ ] Mỗi controller xuất hiện đúng một dòng `fab` trên mỗi bo; Node ID khác nhau giữa các bo trong cùng fabric.
- [ ] Discriminator và passcode khác nhau giữa các bo (phương án A/B); QR dán trên bo khớp `check_qr.sh` với log hoặc CSV.
- [ ] Sản phẩm thật (B): DAC riêng, qua `validate-att-cert`; đã xóa `internal/`; flash encryption bật nếu bán ra; `CONFIG_PRINT_ONBOARDING_CODES=n`.
- [ ] Đạt test 1–12 ở mục K, đặc biệt test 9 theo cả hai chiều.
- [ ] Không có `reset=` bất thường (BROWNOUT, PANIC, WATCHDOG) trong 24 giờ.
- [ ] README của từng dự án ghi rõ: ghép từng bo, Node ID chip-tool khác nhau, mã ghép nằm ở đâu.

### Đã kiểm chứng và chưa kiểm chứng

- **Đã kiểm chứng:** mọi bằng chứng trong code/SDK nêu ở trên; build cả 4 dự án và 2 biến thể A/B (kết quả ghi trong commit); script tạo PKI, CD và factory data, kiểm chuỗi DAC, tạo và giải mã QR trong Docker.
- **Chưa kiểm chứng (cần phần cứng):** log `id_diag` thực tế; phương án A và B trên bo thật (thời gian tính verifier, ghép bằng mã mới); hành vi của Apple Home, HA và Aqara với DAC riêng; các nguyên nhân D1–D4 trong mạng của bạn.
