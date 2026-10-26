# Bản hai chip: ESP32-C3 quét BLE + ESP32-H2 "iBeacon Bridge" (Matter bridge over Thread)

Cùng kiểu thiết bị như [`ESP32-C6/matter-ble-presence`](../../ESP32-C6/matter-ble-presence) (cảm biến hiện diện từ
iBeacon, cấu hình bằng trang web), nhưng chia việc cho **hai chip, mỗi chip một radio**, và ESP32-H2 là một **Matter
bridge tên "iBeacon Bridge"**: mỗi beacon thêm trên trang của C3 (tối đa **20**) hiện thành **một cảm biến hiện diện
riêng trong ứng dụng Nhà**, kèm thông tin phụ kiện và **dung lượng pin** của beacon:

| Trong ứng dụng Nhà | Lấy từ |
|---|---|
| Tên phụ kiện, **Số sê-ri** | địa chỉ MAC của beacon (`C0:11:22:33:44:01`) |
| **Nhà sản xuất** | `Haxeko` |
| **Kiểu máy (Model)** | tên thiết bị trong gói tin của beacon (ví dụ `CP101`): tự điền khi gán từ danh sách quét, sửa được; để trống thì H2 dùng tên beacon quảng bá khi C3 nghe thấy |
| **Pin** | điện áp trong gói **Eddystone-TLM** của beacon → % (cảnh báo pin yếu ≤ 20 %, cần thay ≤ 10 %), xem [Pin của beacon](#pin-của-beacon-eddystone-tlm) |

| Chip | Thư mục | Việc |
|---|---|---|
| **ESP32-C3** (scanner) | `ESP32-C3/ble-presence-scanner` (thư mục này) | Quét BLE thụ động, tính hiện diện 20 slot, đọc pin từ gói TLM, trang web cấu hình (bấm BOOT của C3 3 lần): thêm/gỡ thiết bị con, model, mã ghép Matter, mở chế độ ghép / xóa ghép H2. Gửi trạng thái và thiết bị của từng slot sang H2 qua UART. **Không chạy Matter**, Wi-Fi tắt ngoài chế độ cấu hình |
| **ESP32-H2** (iBeacon Bridge) | [`ESP32-H2/matter-ble-presence-h2`](../../ESP32-H2/matter-ble-presence-h2) | Matter over Thread: endpoint 1 là Aggregator, mỗi Presence đã bật và có MAC là một thiết bị con (Bridged Node + Occupancy Sensor + Power Source). Nút BOOT: **bấm 2 lần** = chế độ ghép (đèn nháy xanh dương), **giữ 5 s** = xóa ghép (đèn nháy đỏ 3 lần khi xong). BLE chỉ dùng lúc ghép rồi giải phóng |

**Không cần gõ lệnh console ở chip nào:** mã ghép Matter hiện trên trang của C3, thêm/gỡ thiết bị con bằng nút Lưu,
chế độ ghép và xóa ghép H2 bằng nút BOOT của H2 hoặc nút trên trang (hoặc xóa thiết bị trong ứng dụng Nhà).

Hai thư mục chỉ có `CMakeLists.txt`, `sdkconfig.defaults` và README; mã nguồn, trang web, bảng phân vùng dùng chung
với bản C6 (chọn vai trò bằng Kconfig `PRESENCE_ROLE`). Lý do chọn: BLE scan và Thread không còn tranh nhau một radio
(trên ESP32-C6 cặp này chỉ đạt mức "C1" trong bảng coexistence của Espressif), và C3 được dùng radio cho BLE gần như
toàn thời gian (mặc định quét 120 ms mỗi 160 ms = 75%).

> **Trạng thái:** build thành công cả hai firmware, host test và test trang web đạt. **Chưa chạy trên phần cứng, chưa
> ghép Matter/Thread thật:** môi trường làm việc này không có board ESP32-H2, Thread Border Router hay điện thoại, nên
> không thể chạy thử ghép nối. Cách thử trên phần cứng và những log cần gửi lại: [Thử ghép trên H2](#thử-ghép-matterthread-trên-h2).

---

## Nối dây

Theo cách bạn đã nối:

| ESP32-H2 | | ESP32-C3 | Firmware C3 dùng chân này là |
|---|---|---|---|
| **TX** (GPIO24) | → | **GPIO21** | RX (UART1) |
| **RX** (GPIO23) | ← | **GPIO20** | TX (UART1) |
| GND | — | GND | |

* Mức logic 3,3 V cả hai bên, nối thẳng. 115200 baud, 8N1 (`PRESENCE_LINK_BAUD`).
* **GPIO20/21 là chân UART0 mặc định của ESP32-C3** (20 = RX, 21 = TX). Firmware C3 đổi vai bằng ma trận GPIO (UART1:
  RX = 21, TX = 20) và chuyển log sang **USB-Serial-JTAG** (cổng USB-C), nên khi chạy hai chân này chỉ phục vụ đường
  nối. Riêng **lúc C3 vừa reset, ROM của C3 vẫn in vài dòng khởi động ra GPIO21** (khoảng chục mili giây) trong khi
  TX của H2 cũng đang giữ đường đó: hai ngõ ra cùng lái một dây trong chốc lát. Thường không gây hỏng, nhưng nếu được
  nên **chèn một điện trở 470 Ω–1 kΩ** nối tiếp trên dây H2 TX → C3 GPIO21 (hoặc nối chéo lại: H2 TX → C3 GPIO20,
  H2 RX → C3 GPIO21, rồi đổi `PRESENCE_LINK_TX_GPIO=21`, `PRESENCE_LINK_RX_GPIO=20` trong
  `sdkconfig.defaults` của C3).
  Cách chỉ bằng firmware là bật `CONFIG_BOOT_ROM_LOG_ALWAYS_OFF` cho C3 (ROM không in nữa), nhưng nó **ghi eFuse một lần,
  không đảo ngược được**, nên không bật sẵn.
* ROM của H2 cũng in log khởi động ra GPIO24 → C3 nhận "rác". Giao thức có byte đồng bộ, độ dài và CRC-32 nên rác bị
  bỏ qua (chỉ tăng bộ đếm `CRC errors`). Chân RX của cả hai chip bật điện trở kéo lên nội, nên khi chip kia đang
  reset hay dây lỏng, chân RX không bị thả nổi.
* **Chỉ nối TX, RX, GND (và nguồn).** Nếu hai board hàn úp bằng cả hàng chân, kiểm tra không có chân nào khác của C3
  dính vào chân của H2, nhất là **GPIO9 (nút BOOT)**, GPIO8, GPIO2 (chân strapping): một chân H2 kéo GPIO9 sẽ làm nút
  BOOT bấm giả hoặc không ăn.
* **Chân H2:** tôi giả định chân in chữ **TX/RX** trên ESP32-H2 SuperMini là **GPIO24/GPIO23** (UART0 của H2). Nếu
  board của bạn khác, sửa `CONFIG_PRESENCE_LINK_TX_GPIO` / `CONFIG_PRESENCE_LINK_RX_GPIO` trong
  `ESP32-H2/matter-ble-presence-h2/sdkconfig.defaults`. Log H2 cũng chuyển sang USB-Serial-JTAG.
* **Hai anten sát nhau** khi hàn úp: xoay để anten C3 và H2 ở hai đầu, không chồng lên nhau và không nằm trên mặt đồng
  của board kia; nên dùng kênh Thread 18–24 (xa kênh quảng bá BLE). C3 quét thụ động nên ở chế độ thường **không phát**,
  không làm phiền Thread; H2 phát thì C3 có thể lỡ vài gói beacon.

## Hoạt động

* **C3, chế độ thường:** quét thụ động (Wi-Fi tắt), tính hiện diện như bản một chip (thời gian vắng mặt trên "đồng hồ
  quét", không ghi gì xuống flash), gửi khung **STATE** mỗi giây và ngay khi có thay đổi (≤ 100 ms).
* **C3, chế độ cấu hình** (bấm BOOT **của C3** 3 lần, xem [Nút BOOT của C3](#nút-boot-của-c3)): SoftAP **`VN-Presence-XXXX`**,
  **mật khẩu cố định `69696969`**, trang `http://192.168.4.1` như bản một chip (có thêm dòng tình trạng của ESP32-H2: đã
  ghép chưa, Thread lên chưa). Quét BLE chỉ chạy khi có điện thoại nối SoftAP, bắt đầu 5 s sau khi điện thoại vào (xem
  [Wi-Fi cấu hình chập chờn](#wi-fi-cấu-hình-chập-chờn)). C3 vẫn gửi khung STATE với cờ CONFIG: **H2 giữ nguyên trạng
  thái** các cảm biến trong lúc cấu hình. "Khởi động lại về chế độ Matter" đưa C3 về chế độ thường (H2 không khởi động
  lại).
* **Thiết bị con của bridge:** C3 gửi thiết bị của từng slot (khung **SLOT**: đã bật và có MAC chưa, MAC, model, pin),
  mỗi slot 5 s một lần (một khung mỗi 250 ms), và cả 20 slot trong khoảng 2 s ngay khi bấm **Lưu** trên trang (cả lúc
  đang ở chế độ cấu hình). H2 so với thiết bị đang có, mỗi lượt 100 ms một slot:
  * slot mới bật/có MAC → **thêm** thiết bị con (endpoint mới, tên = MAC, ví dụ `C0:11:22:33:44:01`); ứng dụng Nhà hiện
    thêm một cảm biến hiện diện;
  * slot tắt hoặc xóa MAC → **gỡ** thiết bị con khỏi Nhà;
  * gán beacon khác vào slot → gỡ thiết bị cũ, thêm thiết bị mới (tên mới);
  * đổi model, pin thay đổi → cập nhật ngay trên thiết bị đang có (không tạo lại). Đổi nhãn, thời gian vắng mặt, lọc
    RSSI không đổi gì trên Nhà.
  C3 chỉ gửi khi cấu hình **đọc được từ flash** (hoặc vừa lưu): C3 mới nạp chưa có cấu hình, hay phân vùng cấu hình lỗi,
  không bao giờ làm bridge mất thiết bị. H2 lưu bảng slot → (endpoint, MAC, model, pin) trong NVS (`pbridge`), nên sau
  khi khởi động lại các thiết bị con trở lại đúng endpoint cũ (với model và pin đã biết) trước cả khi C3 gửi gì. Trang
  của C3 hiện từng slot "Trên Nhà: thiết bị con … (endpoint N)", hoặc vì sao thiết bị còn chờ (xem
  [20 thiết bị và bộ nhớ của H2](#20-thiết-bị-và-bộ-nhớ-của-h2)).
* **H2:** áp trạng thái nhận được vào thiết bị con của từng slot (chỉ khi đổi; Matter chưa sẵn sàng thì thử lại mỗi
  100 ms), gửi khung **STATUS** (Matter đã chạy, đã ghép, Thread lên, đang ở chế độ ghép, số fabric, endpoint của từng
  thiết bị con, thiết bị đang chờ và lý do, heap trống) về C3 mỗi giây và **mã ghép** mỗi 5 s. **Không nhận được khung nào từ C3 trong 120 s** (`PRESENCE_LINK_TIMEOUT_SEC`) → mọi
  cảm biến về "không hiện diện" và thiết bị con chuyển **không truy cập được** (Reachable = false, Nhà báo "Không phản
  hồi"); C3 nối lại thì trở lại bình thường.
* **C3 khởi động lại** (thoát chế độ cấu hình, reset): mỗi slot là **"chưa biết"** cho tới khi C3 nghe thấy beacon của
  slot đó hoặc đã quét đủ thời gian vắng mặt của slot (ví dụ 45 s) mà không nghe thấy. Trong lúc đó **H2 giữ nguyên
  trạng thái** cảm biến: không còn cảnh cảm biến tắt rồi bật lại mỗi lần C3 khởi động lại. (Trước bản này, khung đầu
  tiên sau khi C3 khởi động báo "không ai" và H2 tắt ngay mọi cảm biến.)
* **Hai chip phải cùng phiên bản giao thức UART** (bản này: **4**, 20 slot, model, pin): khi cập nhật, **nạp lại cả C3
  và H2**. Lệch phiên bản thì khung của chip kia bị loại (log `bad frames` tăng), và sau 120 s H2 tắt mọi cảm biến.
* Mật khẩu cố định giống nhau trên mọi thiết bị: ai biết mật khẩu và ở gần đều vào được SoftAP **trong lúc thiết bị ở
  chế độ cấu hình** (tối đa 10 phút không thao tác). Trang vẫn chỉ nhận request đúng địa chỉ 192.168.4.1 và POST JSON.

## Pin của beacon (Eddystone-TLM)

Beacon như CP101 phát xen giữa các gói iBeacon một gói **Eddystone-TLM** (telemetry) có **điện áp pin** (mV). C3 đọc gói
này **khi nó đến từ đúng địa chỉ MAC của beacon** (gói TLM không chứa mã nhận diện nào khác) và gửi % pin sang H2:

* % pin tuyến tính từ **2200 mV = 0 %** đến **3000 mV = 100 %** (`PRESENCE_BATTERY_EMPTY_MV`, `PRESENCE_BATTERY_FULL_MV`;
  pin cúc áo CR2032/CR2477 và 2 pin AA/AAA mới đều khoảng 3 V và giữ gần 3 V gần hết đời rồi tụt nhanh, nên % đọc cao
  trong nhiều tháng rồi giảm). **≤ 20 %**: BatChargeLevel = Warning (ứng dụng báo pin yếu); **≤ 10 %**: Critical và
  BatReplacementNeeded (cần thay pin) (`PRESENCE_BATTERY_WARNING_PCT`, `PRESENCE_BATTERY_CRITICAL_PCT`).
* Điện áp trong gói dao động vài chục mV (lúc phát, nhiệt độ): C3 lấy **trung vị 5 lần đọc gần nhất**, % chỉ **giảm** khi
  giảm từ 2 % trở lên và chỉ **tăng** khi tăng từ 15 % trở lên (thay pin mới), nên không nhảy qua lại. Đọc tối đa một gói
  TLM mỗi giây mỗi beacon. Gói TLM **không** làm slot hiện diện: chỉ gói iBeacon mới tính.
* Trên ứng dụng Nhà pin là thuộc tính BatPercentRemaining của cụm Power Source trên chính thiết bị con, **trống (null)
  cho tới gói TLM đầu tiên**. C3 ở chế độ cấu hình hay vừa khởi động chưa có số đo: H2 **giữ % đã biết** (lưu cả trong
  NVS), không về "chưa biết".
* **Kiểm tra trên trang của C3:** danh sách beacon xung quanh có dòng **"Pin: 94% · 2950 mV"** cho beacon gửi TLM cùng
  MAC (số đo một gói, chưa lọc). Dòng "chưa thấy gói Eddystone-TLM" nghĩa là beacon không bật khung TLM, hoặc gửi TLM
  bằng **địa chỉ khác** với iBeacon: bật khung TLM trong ứng dụng cấu hình của beacon và cho mọi khung dùng chung một MAC.
* Log C3 khi % đổi: `presence: Presence N "...": battery 94% (2950 mV)`; mỗi phút `diag: Presence N: ..., battery 94%
  (2950 mV, 3 s ago, 120 TLM)`. Log H2: `bridge: Presence N: battery 94%`.

## Nút BOOT và đèn của ESP32-H2

ESP32-H2 SuperMini: nút **BOOT = GPIO9**, đèn **RGB WS2812 = GPIO8** (Kconfig "Matter node button and LED"; đèn một màu
trên chân khác cũng chọn được).

| Thao tác trên BOOT **của H2** | Kết quả | Đèn |
|---|---|---|
| **Bấm 2 lần** (lần sau trong vòng 2 s, mỗi lần giữ < 1,5 s) | **chế độ ghép**: mở cửa sổ ghép **10 phút** (`PRESENCE_PAIRING_WINDOW_SEC`). Chưa ghép: qua BLE, như lúc mới nạp. Đã ghép: qua mạng Thread, để thêm bridge vào một ứng dụng nữa (Home Assistant, Google…). Mã ghép hiện lại trên trang của C3 | **nháy xanh dương** chừng nào cửa sổ còn mở, tắt khi ghép xong hoặc hết giờ |
| **Giữ 5 giây** (`PRESENCE_FACTORY_RESET_HOLD_SEC`) | tới 5 s đèn **đỏ**: **thả nút** là xóa ghép Matter, mạng Thread và bảng thiết bị con rồi khởi động lại. Cấu hình beacon nằm trên C3 nên giữ nguyên; thiết bị con được tạo lại theo cấu hình đó | đỏ lúc giữ quá 5 s; khởi động xong và kiểm tra **không còn fabric nào** thì **nháy đỏ 3 lần** (reset thành công), rồi nháy xanh dương (chờ ghép mới) |

* Reset chạy **lúc thả nút**, không chạy ngay khi đủ 5 s: GPIO9 là chân strapping, chip khởi động lại trong lúc nút còn
  giữ sẽ vào chế độ nạp firmware thay vì chạy.
* Giữ lâu hơn 1,5 s mà chưa tới 5 s: không làm gì. Lần bấm trước khi giữ không cộng với lần giữ.
* Trang của C3 có hai nút làm đúng hai việc này: **"Mở chế độ ghép trên ESP32-H2"** và **"Xóa ghép Matter của
  ESP32-H2"** (cũng nháy đỏ 3 lần sau khi H2 khởi động lại).
* Log H2: `ui: BOOT double click: pairing mode`, `matter: Pairing mode: commissioning window open for 600 s over ...`,
  `ui: BOOT held 5 s: release it ...`, sau khởi động lại `ui: Factory reset done: the Matter pairing is removed`.

## 20 thiết bị và bộ nhớ của H2

**C3:** 20 slot không đáng kể: cấu hình 20 slot là 1,9 KB trong flash (định dạng 2, cấu hình 5 slot cũ được đọc và
chuyển tự động), trạng thái mỗi slot vài chục byte, mỗi slot một so sánh MAC cho mỗi gói quảng bá. Khung UART: STATE
27 byte/giây, STATUS 68 byte/giây, SLOT ≤ 54 byte mỗi 250 ms: khoảng 0,4 KB/s trên 11,5 KB/s của 115200 baud.

**H2:** giới hạn thật là **heap**. ESP32-H2 có 320 KB SRAM; firmware dùng tĩnh khoảng 170 KB (`idf.py size`, DIRAM), còn lại là heap, BLE
chiếm thêm khoảng 34 KB cho tới khi ghép xong (tài liệu esp-matter, ví dụ light trên ESP32-H2: 44 KB trống lúc khởi động,
78 KB sau khi ghép). Mỗi thiết bị con (endpoint, Descriptor, Bridged Device Basic Information với 4 chuỗi, Identify,
Occupancy Sensing, Power Source) tốn ước tính **2,5–4 KB heap**: 20 thiết bị ≈ 50–80 KB. Vì vậy:

* **Console Matter tắt** trên H2 (thêm ~10 KB heap theo tài liệu esp-matter cho ESP32-H2), số endpoint động = 22 (root +
  Aggregator + 20), số loại thiết bị mỗi endpoint = 4: không cấp RAM tĩnh thừa.
* H2 **chỉ tạo một thiết bị con khi heap còn đủ**: trên **44 KB** + 4 KB lúc **chưa ghép** (BLE đang chạy, việc ghép cần bộ
  nhớ cho chứng chỉ và phiên bảo mật; 44 KB là heap lúc khởi động của ví dụ esp-matter trên ESP32-H2, ví dụ đó ghép được),
  trên **20 KB** + 4 KB khi **đã ghép** (`PRESENCE_BRIDGE_*_RESERVE_KB`). Thiết bị chưa tạo được thì **chờ** và được tạo
  ngay khi đủ: trước khi ghép chỉ một phần (ước tính 6–8) thiết bị có mặt, **phần còn lại được thêm ngay sau khi ghép xong**
  (ứng dụng Nhà hiện thêm phụ kiện mới ngay sau đó). Ước tính sau khi ghép: 20 thiết bị vẫn còn trống 20–40 KB.
* Trang của C3 hiện **"Thiết bị con trên bridge: 12/20 · 8 đang chờ · bộ nhớ H2 trống 46 KB (thấp nhất 41 KB)"** và ở
  từng slot đang chờ lý do: chờ ghép xong, H2 không đủ bộ nhớ, hoặc lỗi tạo thiết bị (thử lại sau 30 s).
* Log H2 ghi heap thật mỗi thiết bị tốn: `bridge: Presence N: device ... added on endpoint E (B bytes of heap, F left)`
  và mỗi phút `bridge: Bridge: 20 device(s), 0 waiting ...; heap of the last device B bytes, largest M`. **Gửi lại các dòng
  này sau khi thử:** ước tính trên chưa đo được trên board thật.
* Thread và Matter: 20 cảm biến đổi trạng thái thưa (vài lần mỗi giờ), pin đổi vài tuần một lần: lưu lượng báo cáo
  nhỏ. Lúc ghép, controller đọc toàn bộ cấu trúc (khoảng 20 × 50 thuộc tính), chia nhiều gói trên Thread: lâu hơn bridge
  5 thiết bị vài giây.

## Thử ghép Matter/Thread trên H2

Phiên làm việc này **không chạy được ghép thật** (không có board, Border Router, điện thoại). Trình tự thử và những gì
cần nhìn:

1. Nạp cả hai chip (lệnh ở [Build và nạp](#build-và-nạp)). H2 mới: `erase-flash` + file gộp, hoặc giữ BOOT 5 s sau khi
   nạp 3 file. Mở log H2 (`screen /dev/cu.usbmodem2101 115200` khi chỉ cắm H2):
   `app_main: Haxeko iBeacon Bridge (two-chip version, Matter node), firmware 1.0.N`,
   `bridge: Matter bridge "iBeacon Bridge": Aggregator on endpoint 1, up to 20 bridged devices`,
   `matter: Not commissioned: commissioning window open (BLE)`, đèn H2 **nháy xanh dương**.
2. C3 ở chế độ thường đã có cấu hình: log H2 có `link: Scanner seen (mode NORMAL, ...)` và các dòng
   `bridge: Presence N: device C0:...:01 "CP101" added on endpoint 2 (... bytes of heap, ... left)`; thiết bị vượt mức
   bộ nhớ ghi `device waits (memory kept for commissioning, added once commissioned ...)`.
3. Trang của C3 (bấm BOOT của C3 3 lần): dòng "Mã ghép Matter" (ví dụ `3497-011-2332`), "đang ở chế độ ghép".
4. Ứng dụng Nhà → Thêm phụ kiện → Thêm mã thủ công (hoặc quét QR từ log). Cần Thread Border Router (HomePod mini/Apple
   TV 4K). Trong lúc ghép, log H2: `Commissioning session started`, `Commissioning complete`, `BLE released after
   commissioning`, `Thread up (child)`; đèn tắt khi ghép xong.
5. Sau khi ghép: các thiết bị đang chờ được thêm trong vài giây (log `added on endpoint`), ứng dụng Nhà hiện "iBeacon
   Bridge" và các cảm biến hiện diện. Mở chi tiết một cảm biến: **Nhà sản xuất Haxeko, Kiểu máy = tên beacon, Số sê-ri =
   MAC, Pin %** (pin hiện sau gói TLM đầu tiên: C3 ở chế độ thường).
6. Thử: beacon ra xa / tắt beacon → sau thời gian vắng mặt cảm biến về "không phát hiện"; bấm BOOT H2 2 lần → đèn nháy
   xanh, thêm bridge vào ứng dụng thứ hai bằng mã ghép trên trang C3; giữ BOOT 5 s → đỏ, thả → khởi động lại, nháy đỏ 3
   lần, nháy xanh; thêm/gỡ beacon trên trang C3 (Lưu) → phụ kiện xuất hiện/biến mất trên Nhà.
7. Gửi lại log H2 (từ lúc khởi động tới sau khi ghép, và vài dòng `diag:` / `bridge:` mỗi phút), log C3 lúc ở chế độ
   thường, và ảnh chụp chi tiết một phụ kiện trên Nhà.

## Nút BOOT của C3

Bấm-nhả BOOT **của C3** 3 lần khi firmware đang chạy, **thong thả cũng được**: lần bấm sau chỉ cần bắt đầu trong vòng
**2 s** kể từ lần nhả trước (`PRESENCE_BOOT_CLICK_GAP_MS`, tối đa 5 s), mỗi lần bấm giữ tối đa **1,5 s**
(`PRESENCE_BOOT_CLICK_MAX_PRESS_MS`). Nghỉ lâu hơn thì đếm lại từ đầu; giữ lâu hơn không tính và hủy các lần đã đếm.
Log USB của C3 in từng lần được đếm:

```
I (...) button: BOOT click 1 of 3 (next press within 2000 ms)
I (...) button: BOOT click 2 of 3 (next press within 2000 ms)
I (...) button: Triple click on BOOT
```

Không thấy dòng `BOOT click` khi bấm → lần bấm không tới firmware (nút, hoặc đang giữ BOOT lúc reset: ROM vào chế độ nạp).

## Wi-Fi cấu hình chập chờn

SoftAP và quét BLE dùng chung một radio trên C3. Bảng coexistence của ESP-IDF xếp **SoftAP có điện thoại đang
kết nối/đã kết nối + quét BLE** ở mức **C1** (*hỗ trợ nhưng hiệu năng không ổn định*). Firmware hiện:

* **chỉ quét BLE khi có điện thoại nối SoftAP**, bắt đầu **5 s** sau khi điện thoại vào
  (`PRESENCE_CONFIG_SCAN_HOLD_OFF_SEC`), và chỉ **40 ms mỗi 160 ms** (`PRESENCE_CONFIG_SCAN_*`, trước là 50 ms mỗi
  100 ms, quét cả lúc không có ai). Lúc điện thoại dò mạng, bắt tay WPA2, xin địa chỉ và tải trang đầu, radio chỉ phục
  vụ Wi-Fi. Danh sách beacon trống vài giây đầu (trạng thái "Sắp quét");
* phát SoftAP ở **8,5 dBm** (`PRESENCE_AP_LIMIT_TX_POWER`, `PRESENCE_AP_MAX_TX_POWER=34`) thay vì 20 dBm: rất nhiều
  người dùng board ESP32-C3 SuperMini báo Wi-Fi rớt hoặc không nối được ở công suất tối đa (thiết kế anten/nguồn của
  board) và 8,5 dBm là cách chữa thường dùng; anten C3 đặt sát board H2 còn bị lệch cộng hưởng. Công suất thấp cũng giảm
  dòng đỉnh khi phát. Điện thoại ở gần lúc cấu hình nên công suất thấp không thiệt;
* dùng kênh **20 MHz** (mặc định của driver là 40 MHz, Espressif khuyên dùng 20 MHz khi xung quanh nhiều Wi-Fi);
* dùng bộ đệm Wi-Fi mặc định của driver (10/32/32) và đặt mã Wi-Fi trong IRAM (trước đây cắt còn 4/8/8 như các bản có
  Matter; C3 scanner không chạy Matter nên thừa RAM).

Nếu vẫn chập chờn:

* **Điện thoại:** SoftAP không có Internet. Khi điện thoại báo "Không có Internet", chọn **giữ kết nối**; trên Android tắt
  "Tự động chuyển sang dữ liệu di động"/"Wi-Fi thông minh" (hoặc tắt dữ liệu di động trong lúc cấu hình), nếu không máy
  có thể tự rời SoftAP.
* **Anten:** xoay để anten (đầu có anten gốm/anten mạch) của C3 thò ra ngoài, không nằm đè lên board H2 hay vật kim
  loại; để điện thoại cách thiết bị 1–3 m.
* **Kênh:** router nhà cũng ở kênh 1 thì đổi `PRESENCE_AP_CHANNEL` sang 6 hoặc 11.
* **Thử nhanh phần cứng:** giữ H2 ở trạng thái reset (nối chân EN/RST của H2 xuống GND) rồi so độ ổn định của SoftAP.
  Tốt hẳn lên thì nguyên nhân là H2 ở sát (anten, nguồn chung).
* Trang web giờ chỉ gửi một request mỗi loại một lúc và bỏ request sau 10 s, nên Wi-Fi chậm không còn làm request
  chồng lên nhau (trước đây trang hỏi đều 2–3 s/lần dù câu trả lời trước chưa về).
* **Gửi log USB của C3** lúc bị chập chờn. Mỗi phút có `wifi_config: SoftAP channel 1, max 8.50 dBm, 1 station(s)` và
  `station xx:xx:..., RSSI -45 dBm`; mỗi lần điện thoại rớt có `Station ... left (reason N, ...)`. Nếu SSID biến mất
  hẳn vài giây rồi hiện lại, xem log có khởi động lại không (dòng `ESP-ROM` / `rst:`).

## Build và nạp

```bash
# trong container espressif/esp-matter:release-v1.6.1_idf_v6.0.2 (README bản C6)
cd ESP32-C3/ble-presence-scanner && idf.py set-target esp32c3 && idf.py build
cd ../../ESP32-H2/matter-ble-presence-h2 && idf.py set-target esp32h2 && idf.py build
```

Nạp từng chip qua cổng USB-C của nó, cắm lần lượt từng board vào Mac (cổng `/dev/cu.usbmodem2101`; máy đổi số khác thì
xem bằng `ls /dev/cu.usbmodem*`). File zip giải nén ra thư mục `presence_bridge`:

```bash
# C3: cập nhật, giữ cấu hình beacon
cd ~/Downloads/presence_bridge/ble_presence_scanner_c3
~/esptool-venv/bin/python -m esptool --chip esp32c3 -p /dev/cu.usbmodem2101 -b 460800 write-flash 0x0 bootloader.bin 0xc000 partition-table.bin 0x20000 ble_presence_scanner_c3.bin

# H2: cập nhật, giữ ghép Matter và các thiết bị con
cd ~/Downloads/presence_bridge/matter_ble_presence_h2
~/esptool-venv/bin/python -m esptool --chip esp32h2 -p /dev/cu.usbmodem2101 -b 460800 write-flash 0x0 bootloader.bin 0xc000 partition-table.bin 0x20000 matter_ble_presence_h2.bin
```

Nạp mới hoàn toàn (xóa sạch, dùng file gộp; **xóa cả ghép Matter** nếu là H2):

```bash
~/esptool-venv/bin/python -m esptool --chip esp32h2 -p /dev/cu.usbmodem2101 erase-flash
~/esptool-venv/bin/python -m esptool --chip esp32h2 -p /dev/cu.usbmodem2101 -b 460800 write-flash 0x0 ~/Downloads/presence_bridge/matter_ble_presence_h2_merged.bin
```

Không nạp file gộp lên chip đang dùng mà không xóa: file gộp ghi đè cả vùng `nvs` (0x10000–0x1FFFF), mất ghép Matter.

**Lên bản này (20 slot, pin, model, "iBeacon Bridge") từ bản bridge trước (1.0.19x):**
1. **Nạp cả hai chip** (giao thức UART đổi sang phiên bản 4). C3: nạp 3 file, **cấu hình beacon được giữ** (5 slot cũ
   thành Presence 1–5, Presence 6–20 trống, model trống). H2: nạp 3 file cũng chạy được (bảng thiết bị con được chuyển,
   giữ endpoint cũ), **nhưng nên ghép lại từ đầu** để ứng dụng Nhà đọc tên bridge mới, cụm pin và thông tin phụ kiện
   (Apple Home thường không đọc lại cấu trúc của phụ kiện đã thêm):
2. xóa "BLE Presence x5"/bridge cũ khỏi ứng dụng Nhà;
3. H2: **giữ BOOT 5 giây rồi thả** (đèn đỏ, sau đó nháy đỏ 3 lần), hoặc `erase-flash` + file gộp (lệnh ở trên);
4. vào trang của C3, gán lại beacon từ danh sách (model tự điền tên beacon) hoặc gõ model, bấm **Lưu**;
5. ghép lại H2 bằng mã ghép trên trang của C3 (xem [Thử ghép trên H2](#thử-ghép-matterthread-trên-h2)).

**Lên bản bridge từ bản cũ hơn (5 cảm biến cố định, chưa là bridge):** xóa thiết bị cũ khỏi ứng dụng Nhà; H2:
`erase-flash` rồi nạp `matter_ble_presence_h2_merged.bin`; C3: nạp 3 file (giữ cấu hình beacon); ghép lại H2.

**Ghép Matter:** mã ghép thủ công hiện ngay trên trang cấu hình của C3 (dòng "Mã ghép Matter", ví dụ `3497-011-2332`,
không đổi) khi H2 chưa ghép hoặc đang ở chế độ ghép, không cần mở console hay log của H2. Trong ứng dụng Nhà: thêm phụ
kiện, chọn nhập mã thủ công. Cần Thread Border Router (Apple TV/HomePod mini, Google Nest Hub, Home Assistant +
OpenThread Border Router), như bản C6. Sau khi ghép, Nhà hiện "iBeacon Bridge" và mỗi thiết bị con là một cảm biến hiện
diện tên là MAC (nhà sản xuất Haxeko, kiểu máy = tên beacon, số sê-ri = MAC, pin); có thể đổi tên trong Nhà. Thêm bridge
vào ứng dụng thứ hai: bấm BOOT của H2 2 lần (hoặc nút trên trang của C3) rồi dùng cùng mã ghép.

**Gỡ ghép:** xóa thiết bị trong ứng dụng Nhà. H2 đã giải phóng BLE sau lần ghép trước (tiết kiệm RAM) nên khi
controller cuối cùng xóa thiết bị, **H2 tự khởi động lại sau 2 s** (log `Last fabric removed: restarting...`) rồi phát
BLE để ghép lại bằng mã cũ; không xóa gì. Nếu không ghép lại được (ví dụ đã xóa trong Nhà lúc H2 đang tắt): **giữ BOOT
của H2 5 giây rồi thả**, hoặc nút **"Xóa ghép Matter của ESP32-H2"** trên trang của C3: H2 xóa ghép Matter, mạng Thread
và bảng thiết bị con, khởi động lại (đèn nháy đỏ 3 lần), chờ ghép mới; thiết bị con được tạo lại theo cấu hình đã lưu
trên C3.

## Đọc log

* C3, mỗi 60 s: `diag: Scanner running, ... adv/s, ... iBeacon, ... TLM`, từng `Presence N: ON/OFF, RSSI, matched ...,
  battery 94% (2950 mV, 3 s ago, 120 TLM)`, và `link: Matter node 0 s ago: commissioned 1, Thread up 1, pairing window 0,
  bridged devices 0x0000f, waiting 0x00000 (reason 0), heap 46 KB (min 41 KB), ...`. Dòng `Matter node never heard ...
  (check the wiring)` nghĩa là C3 chưa nhận được gì từ H2: kiểm tra dây, chân H2, GND chung.
* H2, mỗi 60 s: `link: Scanner 0 s ago: NORMAL, scanning 1, assigned 0x00003, present 0x00001, known 0x00003, ...` (bit
  0 = Presence 1; slot chưa có trong `known` thì H2 đang giữ trạng thái cũ), `bridge: Bridge: N device(s), M waiting ...;
  heap of the last device ... bytes`, `diag: ... heap free ...`, các dòng `link: Presence N: ON/OFF` khi trạng thái đổi,
  `bridge: Presence N: battery 94%` khi pin đổi, và dòng Thread của Matter.

## Rà soát độc lập lần 2 (bản hai chip)

Toàn bộ mã hai chip được rà soát lại độc lập, đối chiếu mã nguồn ESP-IDF v6.0.2 / esp-matter v1.6.1 / CHIP:

| Lỗi | Hậu quả | Sửa |
|---|---|---|
| C3 khởi động lại thì khung đầu tiên (≈1 s) báo "không ai", H2 áp ngay | mỗi lần thoát chế độ cấu hình, mở serial monitor (reset C3), C3 reset: cảm biến đang ON tắt rồi bật lại, tự động hóa "khi vắng người" chạy nhầm | trường `known_mask`: slot chưa biết thì H2 giữ trạng thái; C3 không quét được (BLE lỗi) thì slot vẫn được coi là đã biết (vắng) sau thời gian vắng mặt + 5 phút, không kẹt ON mãi (host test) |
| H2 (`USE_BLE_ONLY_FOR_COMMISSIONING=y`) giải phóng BLE sau khi ghép; khi controller cuối cùng xóa thiết bị, cửa sổ ghép mở lại chỉ qua DNS-SD | xóa khỏi Apple/Google Home rồi thêm lại bằng mã QR: điện thoại tìm BLE không thấy, phải rút điện | H2 tự khởi động lại 2 s sau khi fabric cuối cùng bị xóa (chỉ khi BLE đã bị giải phóng) |
| `uart_read_bytes(…, 64 byte, 100 ms)` chỉ trả về khi đủ 64 byte hoặc 100 ms im lặng; chân RX không có kéo lên | nhiễu lác đác trên chân RX thả nổi giữ vòng lặp UART tới vài giây (chậm gửi trạng thái, chậm áp trạng thái, lệch timeout) | đọc phần đã có trong bộ đệm hoặc chờ 1 byte tối đa 100 ms; bật kéo lên trên chân RX cả hai chip |
| C3 scanner: 4 bộ đệm RX tĩnh < cửa sổ Block Ack 6; SoftAP 40 MHz; quét BLE 50% chủ động cả khi chưa có điện thoại | SoftAP chập chờn | xem [Wi-Fi cấu hình chập chờn](#wi-fi-cấu-hình-chập-chờn) |
| Trang web hỏi đều 2–3 s/lần, không chờ câu trả lời trước | Wi-Fi chậm: request chồng nhau, bị đóng, trang báo lỗi | một request mỗi loại một lúc, bỏ sau 10 s (test trình duyệt) |

Đã kiểm và thấy đúng: định dạng khung và bộ phân tích, đồng bộ giữa các task, thứ tự khởi động Matter, stack các task,
chân UART và console (cả hai chip log qua USB-Serial-JTAG), cấu hình theo vai trò trong Kconfig/CMake. Chưa kiểm được
ở đây: chân TX/RX in trên board ESP32-H2 SuperMini có đúng GPIO24/23 không.

## Kiểm thử

* Host test (10 bộ, ASan/UBSan), gồm bộ `presence_link`: khung đúng, **mọi bit lật bị loại**, mọi kiểu cắt cụt, bắt
  lại nhịp sau rác giống log ROM, 200 000 byte nhiễu ngẫu nhiên không tạo ra khung giả, khung của giao thức cũ bị loại,
  **slot "chưa biết" sau khi C3 khởi động lại** (đến khi nghe thấy beacon, quét đủ thời gian vắng mặt, hoặc quá 5 phút
  khi không quét được); bộ `click_detector`: bấm thong thả, nghỉ quá 2 s thì đếm lại, giữ lâu.
* Test trang web (Playwright): 46 kiểm tra, gồm trang của bản hai chip (dòng tình trạng H2, không cảnh báo quét > 50%,
  "Sắp quét" trong 5 s đầu) và Wi-Fi chậm (thiết bị trả lời sau 3 s: chỉ một request mỗi loại một lúc).
* Build (`espressif/esp-matter:release-v1.6.1_idf_v6.0.2`, `set-target` rồi `build`), 0 cảnh báo:

| Firmware | App (phân vùng 3,6 MB) | RAM tĩnh → heap lúc khởi động |
|---|---|---|
| `ble_presence_scanner_c3.bin` (C3) | 1 092 064 byte, trống 71% | 126 795 / 321 296 byte → còn 194 501 byte (mã Wi-Fi trong IRAM: thêm ~19 KB) |
| `matter_ble_presence_h2.bin` (H2) | 1 559 616 byte, trống 59% | 162 840 / 258 000 byte → còn 95 160 byte (BLE được giải phóng sau khi ghép) |

  Bản C6 và bản C3 Wi-Fi build lại sau thay đổi chung (`sdkconfig` sinh mới từ defaults): thành công, 0 cảnh báo.

**Cần thử trên phần cứng:** đường UART thật (dây, chân H2, xung đột lúc reset), hiện diện ON/OFF qua H2, ghép Matter
trên H2, ảnh hưởng của hai anten sát nhau (so `matched` mỗi phút khi H2 tắt và khi H2 chạy), độ ổn định SoftAP sau các
thay đổi trên (log RSSI điện thoại, `Station ... left`), bấm BOOT thong thả, thoát chế độ cấu hình mà cảm biến đang ON
vẫn giữ ON, xóa thiết bị khỏi controller rồi ghép lại bằng BLE.
