# Bản hai chip, phía ESP32-H2: Matter bridge over Thread

Matter bridge của bản hai chip: mỗi Presence đã bật và có MAC trên trang cấu hình của ESP32-C3 là **một thiết bị con**
(Bridged Node + Occupancy Sensor) **tên là địa chỉ MAC của beacon**. Trạng thái hiện diện, danh sách thiết bị và lệnh
xóa ghép đến từ ESP32-C3 qua UART. Nối dây, hoạt động, build, nạp, ghép và đọc log: xem
[README bản hai chip](../../ESP32-C3/ble-presence-scanner/README.md).

* Thư mục mỏng: mã nguồn và bảng phân vùng của `ESP32-C6/matter-ble-presence`, vai trò `PRESENCE_ROLE_MATTER_UART`
  (`matter_bridge.cpp`, `matter_device.cpp`, `presence_link.cpp`, `app_main_h2.cpp`).
* Endpoint 0 Root Node, endpoint 1 Aggregator, từ endpoint 2 trở đi là thiết bị con (`ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=7`:
  root + aggregator + 5). esp-matter không dùng lại số endpoint; bảng slot → (endpoint, MAC) lưu trong NVS `pbridge`, nên
  thiết bị con trở lại đúng endpoint cũ sau khi khởi động lại.
* Thread MTD, Minimal End Device (cắm điện), BLE chỉ dùng lúc ghép rồi giải phóng (`USE_BLE_ONLY_FOR_COMMISSIONING=y`).
  Controller cuối cùng xóa thiết bị → H2 tự khởi động lại sau 2 s để ghép lại được bằng BLE. Không xóa gì.
* UART1: TX GPIO24, RX GPIO23 (chân TX/RX in trên board, RX có kéo lên nội), 115200 baud, giao thức phiên bản 3: nạp C3
  và H2 cùng đợt. Log qua USB-Serial-JTAG.
* **Không cần console:** mã ghép hiện trên trang của C3 (H2 gửi qua UART), xóa ghép bằng nút trên trang. Console Matter
  vẫn bật để chẩn đoán (`matter esp ot_cli state`...).
* Không nhận được gì từ C3 trong 120 s → mọi cảm biến về "không hiện diện" và thiết bị con báo không truy cập được.
  Sau khi C3 khởi động lại, slot chưa biết (C3 chưa nghe thấy beacon, chưa quét đủ thời gian vắng mặt) giữ nguyên trạng
  thái.
* VID/PID `0xFFF1`/`0x8004`, DAC thử nghiệm (không dùng cho sản phẩm). Loại thiết bị khi quảng bá: Aggregator (14).
