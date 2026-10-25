# Bản hai chip, phía ESP32-H2: node Matter over Thread

Node Matter (5 endpoint Occupancy Sensor, Presence 1–5) của bản hai chip; trạng thái hiện diện đến từ ESP32-C3 qua UART.
Nối dây, hoạt động, build, nạp và đọc log: xem [README bản hai chip](../../ESP32-C3/ble-presence-scanner/README.md).

* Thư mục mỏng: mã nguồn và bảng phân vùng của `ESP32-C6/matter-ble-presence`, vai trò `PRESENCE_ROLE_MATTER_UART`.
* Thread MTD, Minimal End Device (cắm điện), BLE chỉ dùng lúc ghép rồi giải phóng (`USE_BLE_ONLY_FOR_COMMISSIONING=y`).
* UART1: TX GPIO24, RX GPIO23 (chân TX/RX in trên board), 115200 baud; log và console Matter qua USB-Serial-JTAG.
* VID/PID `0xFFF1`/`0x8004`, DAC thử nghiệm (không dùng cho sản phẩm).
* Không nhận được gì từ C3 trong 120 s → mọi cảm biến về "không hiện diện". Sau khi C3 khởi động lại, slot chưa biết
  (C3 chưa nghe thấy beacon, chưa quét đủ thời gian vắng mặt) giữ nguyên trạng thái.
* Controller cuối cùng xóa thiết bị → H2 tự khởi động lại sau 2 s để ghép lại được bằng BLE (BLE đã được giải phóng sau
  lần ghép trước). Không xóa gì.
* Giao thức UART phiên bản 2: nạp C3 và H2 cùng đợt.
