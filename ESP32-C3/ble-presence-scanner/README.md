# Bản hai chip: ESP32-C3 quét BLE + ESP32-H2 Matter bridge over Thread

Cùng thiết bị như [`ESP32-C6/matter-ble-presence`](../../ESP32-C6/matter-ble-presence) (tối đa 5 cảm biến hiện diện từ
5 iBeacon, cấu hình bằng trang web), nhưng chia việc cho **hai chip, mỗi chip một radio**, và ESP32-H2 là một
**Matter bridge**: mỗi beacon thêm trên trang của C3 hiện thành **một thiết bị riêng trong ứng dụng Nhà, tên là địa chỉ
MAC của beacon**.

| Chip | Thư mục | Việc |
|---|---|---|
| **ESP32-C3** (scanner) | `ESP32-C3/ble-presence-scanner` (thư mục này) | Quét BLE thụ động, tính hiện diện 5 slot, trang web cấu hình (bấm BOOT của C3 3 lần): thêm/gỡ thiết bị con, mã ghép Matter, xóa ghép H2. Gửi trạng thái và danh sách thiết bị sang H2 qua UART. **Không chạy Matter**, Wi-Fi tắt ngoài chế độ cấu hình |
| **ESP32-H2** (Matter bridge) | [`ESP32-H2/matter-ble-presence-h2`](../../ESP32-H2/matter-ble-presence-h2) | Matter over Thread: endpoint 1 là Aggregator, mỗi Presence đã bật và có MAC là một thiết bị con (Bridged Node + Occupancy Sensor, tên = MAC). BLE chỉ dùng lúc ghép rồi giải phóng |

**Không cần gõ lệnh console ở chip nào:** mã ghép Matter hiện trên trang của C3, thêm/gỡ thiết bị con bằng nút Lưu, xóa
ghép H2 bằng một nút trên trang (hoặc xóa thiết bị trong ứng dụng Nhà).

Hai thư mục chỉ có `CMakeLists.txt`, `sdkconfig.defaults` và README; mã nguồn, trang web, bảng phân vùng dùng chung
với bản C6 (chọn vai trò bằng Kconfig `PRESENCE_ROLE`). Lý do chọn: BLE scan và Thread không còn tranh nhau một radio
(trên ESP32-C6 cặp này chỉ đạt mức "C1" trong bảng coexistence của Espressif), và C3 được dùng radio cho BLE gần như
toàn thời gian (mặc định quét 120 ms mỗi 160 ms = 75%).

> **Trạng thái:** build thành công cả hai firmware, host test và test trang web đạt. **Chưa chạy trên phần cứng.**

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
* **C3, chế độ cấu hình** (bấm BOOT **của C3** 3 lần, xem [Nút BOOT](#nút-boot)): SoftAP **`VN-Presence-XXXX`**,
  **mật khẩu cố định `69696969`**, trang `http://192.168.4.1` như bản một chip (có thêm dòng tình trạng của ESP32-H2: đã
  ghép chưa, Thread lên chưa). Quét BLE chỉ chạy khi có điện thoại nối SoftAP, bắt đầu 5 s sau khi điện thoại vào (xem
  [Wi-Fi cấu hình chập chờn](#wi-fi-cấu-hình-chập-chờn)). C3 vẫn gửi khung STATE với cờ CONFIG: **H2 giữ nguyên trạng
  thái** các cảm biến trong lúc cấu hình. "Khởi động lại về chế độ Matter" đưa C3 về chế độ thường (H2 không khởi động
  lại).
* **Thiết bị con của bridge:** C3 gửi danh sách (khung **SLOTS**: slot nào đã bật và có MAC, MAC nào) mỗi 5 s và ngay
  khi bấm **Lưu** trên trang (cả lúc đang ở chế độ cấu hình). H2 so với danh sách đang có:
  * slot mới bật/có MAC → **thêm** thiết bị con (endpoint mới, tên = MAC, ví dụ `C0:11:22:33:44:01`); ứng dụng Nhà hiện
    thêm một cảm biến hiện diện;
  * slot tắt hoặc xóa MAC → **gỡ** thiết bị con khỏi Nhà;
  * gán beacon khác vào slot → gỡ thiết bị cũ, thêm thiết bị mới (tên mới). Đổi nhãn, thời gian vắng mặt, lọc RSSI
    không đổi gì trên Nhà.
  C3 chỉ gửi danh sách khi cấu hình **đọc được từ flash** (hoặc vừa lưu): C3 mới nạp chưa có cấu hình, hay phân vùng
  cấu hình lỗi, không bao giờ làm bridge mất thiết bị. H2 lưu bảng slot → endpoint trong NVS (`pbridge`), nên sau khi
  khởi động lại các thiết bị con trở lại đúng endpoint cũ trước cả khi C3 gửi gì. Trang của C3 hiện từng slot "Trên Nhà:
  thiết bị con … (endpoint N)".
* **H2:** áp trạng thái nhận được vào thiết bị con của từng slot (chỉ khi đổi; Matter chưa sẵn sàng thì thử lại mỗi
  100 ms), gửi khung **STATUS** (Matter đã chạy, đã ghép, Thread lên, số fabric, endpoint của từng thiết bị con) về C3
  mỗi giây và **mã ghép** mỗi 5 s. **Không nhận được khung nào từ C3 trong 120 s** (`PRESENCE_LINK_TIMEOUT_SEC`) → mọi
  cảm biến về "không hiện diện" và thiết bị con chuyển **không truy cập được** (Reachable = false, Nhà báo "Không phản
  hồi"); C3 nối lại thì trở lại bình thường.
* **C3 khởi động lại** (thoát chế độ cấu hình, reset): mỗi slot là **"chưa biết"** cho tới khi C3 nghe thấy beacon của
  slot đó hoặc đã quét đủ thời gian vắng mặt của slot (ví dụ 45 s) mà không nghe thấy. Trong lúc đó **H2 giữ nguyên
  trạng thái** cảm biến: không còn cảnh cảm biến tắt rồi bật lại mỗi lần C3 khởi động lại. (Trước bản này, khung đầu
  tiên sau khi C3 khởi động báo "không ai" và H2 tắt ngay mọi cảm biến.)
* **Hai chip phải cùng phiên bản giao thức UART** (bản này: 3, bridge): khi cập nhật, **nạp lại cả C3 và H2**. Lệch
  phiên bản thì khung của chip kia bị loại (log `bad frames` tăng), và sau 120 s H2 tắt mọi cảm biến.
* Mật khẩu cố định giống nhau trên mọi thiết bị: ai biết mật khẩu và ở gần đều vào được SoftAP **trong lúc thiết bị ở
  chế độ cấu hình** (tối đa 10 phút không thao tác). Trang vẫn chỉ nhận request đúng địa chỉ 192.168.4.1 và POST JSON.

## Nút BOOT

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

**Lên bản bridge từ bản cũ (5 cảm biến cố định):** cấu trúc thiết bị Matter đổi (endpoint 1 thành Aggregator), nên:
1. xóa thiết bị cũ khỏi ứng dụng Nhà;
2. H2: `erase-flash` rồi nạp `matter_ble_presence_h2_merged.bin` (lệnh ở trên); C3: nạp 3 file (giữ cấu hình beacon);
3. ghép lại H2 (mã ghép trên trang của C3), rồi vào trang của C3 bấm **Lưu** một lần nếu thiết bị con chưa hiện (C3
   cũng tự gửi danh sách mỗi 5 s nếu đã có cấu hình lưu).

**Ghép Matter:** mã ghép thủ công hiện ngay trên trang cấu hình của C3 (dòng "Mã ghép Matter", ví dụ `3497-011-2332`,
không đổi), không cần mở console hay log của H2. Trong ứng dụng Nhà: thêm phụ kiện, chọn nhập mã thủ công. Cần Thread
Border Router (Apple TV/HomePod mini, Google Nest Hub, Home Assistant + OpenThread Border Router), như bản C6. Sau khi
ghép, Nhà hiện bridge và mỗi thiết bị con là một cảm biến hiện diện tên là MAC; có thể đổi tên trong Nhà.

**Gỡ ghép:** xóa thiết bị trong ứng dụng Nhà. H2 đã giải phóng BLE sau lần ghép trước (tiết kiệm RAM) nên khi
controller cuối cùng xóa thiết bị, **H2 tự khởi động lại sau 2 s** (log `Last fabric removed: restarting...`) rồi phát
BLE để ghép lại bằng mã cũ; không xóa gì. Nếu không ghép lại được (ví dụ đã xóa trong Nhà lúc H2 đang tắt), dùng nút
**"Xóa ghép Matter của ESP32-H2"** trên trang của C3: H2 xóa ghép Matter, mạng Thread và bảng thiết bị con, khởi động
lại, chờ ghép mới; thiết bị con được tạo lại theo cấu hình đã lưu trên C3.

## Đọc log

* C3, mỗi 60 s: `diag: Scanner running, ... adv/s`, từng `Presence N: ON/OFF, RSSI, matched ...`, và
  `link: Matter node 0 s ago: commissioned 1, Thread up 1, ...`. Dòng `Matter node never heard ... (check the wiring)`
  nghĩa là C3 chưa nhận được gì từ H2: kiểm tra dây, chân H2, GND chung.
* H2, mỗi 60 s: `link: Scanner 0 s ago: NORMAL, scanning 1, assigned 0x03, present 0x01, known 0x03, ...` (bit 0 =
  Presence 1; slot chưa có trong `known` thì H2 đang giữ trạng thái cũ), các dòng `link: Presence N: ON/OFF` khi trạng
  thái đổi, và dòng Thread của Matter.

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
