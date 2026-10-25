# Bản hai chip: ESP32-C3 quét BLE + ESP32-H2 Matter over Thread

Cùng thiết bị như [`ESP32-C6/matter-ble-presence`](../../ESP32-C6/matter-ble-presence) (5 cảm biến hiện diện từ 5
iBeacon, cấu hình bằng trang web), nhưng chia việc cho **hai chip, mỗi chip một radio**:

| Chip | Thư mục | Việc |
|---|---|---|
| **ESP32-C3** (scanner) | `ESP32-C3/ble-presence-scanner` (thư mục này) | Quét BLE thụ động, tính hiện diện 5 slot, trang web cấu hình (bấm BOOT của C3 3 lần), gửi trạng thái sang H2 qua UART. **Không chạy Matter**, Wi-Fi tắt ngoài chế độ cấu hình |
| **ESP32-H2** (Matter node) | [`ESP32-H2/matter-ble-presence-h2`](../../ESP32-H2/matter-ble-presence-h2) | Node Matter over Thread với 5 endpoint Occupancy Sensor (Presence 1–5), nhận trạng thái từ C3. BLE chỉ dùng lúc ghép rồi giải phóng |

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
* ROM của H2 cũng in log khởi động ra GPIO24 → C3 nhận "rác". Giao thức có byte đồng bộ, độ dài và CRC-32 nên rác bị
  bỏ qua (chỉ tăng bộ đếm `CRC errors`).
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
* **H2:** áp trạng thái nhận được vào 5 endpoint (chỉ khi đổi; Matter chưa sẵn sàng thì thử lại mỗi 100 ms), gửi khung
  **STATUS** (Matter đã chạy, đã ghép, Thread lên, số fabric) về C3 mỗi giây. **Không nhận được khung nào từ C3 trong
  120 s** (`PRESENCE_LINK_TIMEOUT_SEC`) → mọi cảm biến về "không hiện diện" (không để kẹt ON), C3 nối lại thì áp trạng
  thái mới.
* **C3 khởi động lại** (thoát chế độ cấu hình, reset): mỗi slot là **"chưa biết"** cho tới khi C3 nghe thấy beacon của
  slot đó hoặc đã quét đủ thời gian vắng mặt của slot (ví dụ 45 s) mà không nghe thấy. Trong lúc đó **H2 giữ nguyên
  trạng thái** cảm biến: không còn cảnh cảm biến tắt rồi bật lại mỗi lần C3 khởi động lại. (Trước bản này, khung đầu
  tiên sau khi C3 khởi động báo "không ai" và H2 tắt ngay mọi cảm biến.)
* **Hai chip phải cùng phiên bản giao thức UART** (bản này: 2, thêm trường "đã biết"): khi cập nhật, **nạp lại cả C3 và
  H2**. Lệch phiên bản thì khung của chip kia bị loại (log `bad frames` tăng), và sau 120 s H2 tắt mọi cảm biến.
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
* **Gửi log USB của C3** lúc bị chập chờn. Mỗi phút có `wifi_config: SoftAP channel 1, max 8.50 dBm, 1 station(s)` và
  `station xx:xx:..., RSSI -45 dBm`; mỗi lần điện thoại rớt có `Station ... left (reason N, ...)`. Nếu SSID biến mất
  hẳn vài giây rồi hiện lại, xem log có khởi động lại không (dòng `ESP-ROM` / `rst:`).

## Build và nạp

```bash
# trong container espressif/esp-matter:release-v1.6.1_idf_v6.0.2 (README bản C6)
cd ESP32-C3/ble-presence-scanner && idf.py set-target esp32c3 && idf.py build
cd ../../ESP32-H2/matter-ble-presence-h2 && idf.py set-target esp32h2 && idf.py build
```

Nạp từng chip qua cổng USB-C của nó (trên Mac: `ls /dev/cu.usbmodem*`; cắm từng board để biết cổng nào là chip nào).
Lần đầu xóa sạch rồi nạp file gộp; cập nhật về sau nạp 3 file rời tại `0x0`, `0xc000`, `0x20000` (giữ ghép Matter và
cấu hình beacon):

```bash
~/esptool-venv/bin/python -m esptool --chip esp32c3 -p /dev/cu.usbmodemC3 erase-flash
~/esptool-venv/bin/python -m esptool --chip esp32c3 -p /dev/cu.usbmodemC3 -b 460800 write-flash 0x0 ble_presence_scanner_c3_merged.bin
~/esptool-venv/bin/python -m esptool --chip esp32h2 -p /dev/cu.usbmodemH2 erase-flash
~/esptool-venv/bin/python -m esptool --chip esp32h2 -p /dev/cu.usbmodemH2 -b 460800 write-flash 0x0 matter_ble_presence_h2_merged.bin
```

**Ghép Matter:** mã QR và mã ghép thủ công in trên **log của H2** (`SetupQRCode: [MT:...]`, `Manual pairing code`).
Cần Thread Border Router (Apple TV/HomePod mini, Google Nest Hub, Home Assistant + OpenThread Border Router), như bản C6.
Gỡ ghép: lệnh `matter esp factoryreset` trên console USB của H2, hoặc xóa thiết bị khỏi controller.

## Đọc log

* C3, mỗi 60 s: `diag: Scanner running, ... adv/s`, từng `Presence N: ON/OFF, RSSI, matched ...`, và
  `link: Matter node 0 s ago: commissioned 1, Thread up 1, ...`. Dòng `Matter node never heard ... (check the wiring)`
  nghĩa là C3 chưa nhận được gì từ H2: kiểm tra dây, chân H2, GND chung.
* H2, mỗi 60 s: `link: Scanner 0 s ago: NORMAL, scanning 1, assigned 0x03, present 0x01, known 0x03, ...` (bit 0 =
  Presence 1; slot chưa có trong `known` thì H2 đang giữ trạng thái cũ), các dòng `link: Presence N: ON/OFF` khi trạng
  thái đổi, và dòng Thread của Matter.

## Kiểm thử

* Host test (10 bộ, ASan/UBSan), gồm bộ `presence_link`: khung đúng, **mọi bit lật bị loại**, mọi kiểu cắt cụt, bắt
  lại nhịp sau rác giống log ROM, 200 000 byte nhiễu ngẫu nhiên không tạo ra khung giả.
* Test trang web (Playwright): 43 kiểm tra, gồm trang của bản hai chip (dòng tình trạng H2, không cảnh báo quét > 50%).
* Build (`espressif/esp-matter:release-v1.6.1_idf_v6.0.2`, `set-target` rồi `build`), 0 cảnh báo:

| Firmware | App (phân vùng 3,6 MB) | RAM tĩnh → heap lúc khởi động |
|---|---|---|
| `ble_presence_scanner_c3.bin` (C3) | 1 087 040 byte, trống 71% | 107 275 / 321 296 byte → còn 214 021 byte |
| `matter_ble_presence_h2.bin` (H2) | 1 558 608 byte, trống 59% | 162 832 / 258 000 byte → còn 95 168 byte (BLE được giải phóng sau khi ghép) |

  Bản C6 và bản C3 Wi-Fi build lại sau thay đổi chung: thành công, `sdkconfig` của C6 sinh mới giống bản đã kiểm chứng
  (chỉ thêm `CONFIG_PRESENCE_ROLE_SINGLE=y`).

**Cần thử trên phần cứng:** đường UART thật (dây, chân H2, xung đột lúc reset), hiện diện ON/OFF qua H2, ghép Matter
trên H2, ảnh hưởng của hai anten sát nhau (so `matched` mỗi phút khi H2 tắt và khi H2 chạy).
