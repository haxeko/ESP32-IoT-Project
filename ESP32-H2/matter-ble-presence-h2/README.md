# Bản hai chip, phía ESP32-H2: iBeacon Bridge (Matter bridge over Thread)

Matter bridge **"iBeacon Bridge"** của bản hai chip: mỗi Presence đã bật và có MAC trên trang cấu hình của ESP32-C3 (tối
đa **20**) là **một thiết bị con** (Bridged Node + Occupancy Sensor + Power Source). Trạng thái hiện diện, danh sách
thiết bị (MAC, model, pin) và lệnh của trang cấu hình đến từ ESP32-C3 qua UART. Nối dây, hoạt động, build, nạp, ghép
và đọc log: xem [README bản hai chip](../../ESP32-C3/ble-presence-scanner/README.md).

* Thư mục mỏng: mã nguồn và bảng phân vùng của `ESP32-C6/matter-ble-presence`, vai trò `PRESENCE_ROLE_MATTER_UART`
  (`matter_bridge.cpp`, `matter_device.cpp`, `presence_link.cpp`, `status_ui.cpp`, `app_main_h2.cpp`).
* Thiết bị con trên ứng dụng Nhà: **tên và số sê-ri = địa chỉ MAC** của beacon, **nhà sản xuất `Haxeko`**, **model = tên
  thiết bị trong gói tin của beacon** (gõ hoặc tự điền trên trang của C3; để trống thì dùng tên beacon quảng bá),
  **pin** từ gói Eddystone-TLM của beacon (BatPercentRemaining, BatChargeLevel, BatReplacementNeeded).
* Endpoint 0 Root Node, endpoint 1 Aggregator, từ endpoint 2 trở đi là thiết bị con
  (`ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=22`: root + aggregator + 20; `ESP_MATTER_MAX_DEVICE_TYPE_COUNT=4`). esp-matter
  không dùng lại số endpoint; bảng slot → (endpoint, MAC, model, pin) lưu trong NVS `pbridge` (bảng của bản 5 slot được
  chuyển tự động), nên thiết bị con trở lại đúng endpoint cũ sau khi khởi động lại.
* **Bộ nhớ:** mỗi thiết bị con tốn vài KB heap (log `bridge: Presence N: device ... added on endpoint E (B bytes of heap,
  F left)`). H2 chỉ tạo thiết bị khi còn trống ít nhất **44 KB** lúc chưa ghép (BLE đang chạy, việc ghép cần bộ nhớ) hoặc
  **20 KB** khi đã ghép (BLE đã được giải phóng); thiết bị chưa tạo được thì **chờ** (trang của C3 ghi rõ lý do) và được
  thêm ngay khi đủ, thường là ngay sau khi ghép xong. Console Matter **tắt** (`ENABLE_CHIP_SHELL=n`, thêm ~10 KB heap theo
  tài liệu esp-matter cho ESP32-H2).
* **Nút BOOT (GPIO9) và đèn RGB WS2812 (GPIO8) của ESP32-H2 SuperMini:**
  * **bấm 2 lần** → chế độ ghép (mở cửa sổ ghép 10 phút): **đèn nháy xanh dương** chừng nào cửa sổ còn mở. Chưa ghép thì
    qua BLE như lúc mới nạp; đã ghép thì qua mạng Thread (thêm bridge vào một ứng dụng nữa);
  * **giữ 5 giây** → đèn **đỏ**, **thả nút** → xóa ghép Matter, mạng Thread và bảng thiết bị con, khởi động lại; khởi
    động xong kiểm tra không còn fabric nào thì **đèn nháy đỏ 3 lần** (reset thành công), rồi nháy xanh dương (chờ ghép).
    Reset chạy lúc thả vì GPIO9 là chân strapping: chip không được khởi động lại khi nút còn giữ;
  * nút "Mở chế độ ghép" và "Xóa ghép Matter của ESP32-H2" trên trang của C3 làm đúng hai việc này.
* Thread MTD, Minimal End Device (cắm điện), BLE chỉ dùng lúc ghép rồi giải phóng (`USE_BLE_ONLY_FOR_COMMISSIONING=y`).
  Controller cuối cùng xóa thiết bị → H2 tự khởi động lại sau 2 s để ghép lại được bằng BLE. Không xóa gì.
* UART1: TX GPIO24, RX GPIO23 (chân TX/RX in trên board, RX có kéo lên nội), 115200 baud, giao thức **phiên bản 4**: nạp
  C3 và H2 cùng đợt. Log qua USB-Serial-JTAG.
* Không nhận được gì từ C3 trong 120 s → mọi cảm biến về "không hiện diện" và thiết bị con báo không truy cập được.
  Sau khi C3 khởi động lại, slot chưa biết (C3 chưa nghe thấy beacon, chưa quét đủ thời gian vắng mặt) giữ nguyên trạng
  thái; model và pin đã biết được giữ khi C3 chưa có thông tin (chế độ cấu hình, vừa khởi động).
* VID/PID `0xFFF1`/`0x8004`, DAC thử nghiệm (không dùng cho sản phẩm). Loại thiết bị khi quảng bá: Aggregator (14).
