# Factory data và chứng chỉ test cho các bo ESP32-H2 Matter

Script cho **phương án B**: mỗi bo một DAC riêng (kèm khóa riêng), passcode, discriminator và SPAKE2+ verifier riêng, nằm trong phân vùng `fctry`. File cũng có một `serial-num` ngẫu nhiên mà firmware không dùng: bo vẫn báo serial từ MAC eFuse. Firmware đọc chúng khi build với `sdkconfig.defaults.factory`. Phân tích đầy đủ: [`../../docs/multi-device-audit.md`](../../docs/multi-device-audit.md), mục G, H, I, J.

Các script chạy trong đúng image Docker dùng để build firmware (ESP-IDF v6.0.2 + esp-matter 1.6.1), trong đó đã có sẵn `chip-cert`, `chip-tool` và `esp-matter-mfg-tool` 1.0.26. Riêng `flash_factory.sh` chạy thẳng trên máy cắm bo (cần `esptool` v5).

| Script | Việc |
|---|---|
| `in_docker.sh <script> [args]` | Chạy một script trong Docker; thư mục hiện tại là thư mục làm việc (`/work`). Chuyển biến `CD_FILE`, `OUT` vào container; trả quyền sở hữu file kết quả cho người gọi; thư mục chứa khóa riêng chỉ chủ sở hữu đọc được |
| `gen_pki.sh [--private]` | Tạo CD: `pki/cd/CD_example_FFF1.der` (CD firmware đang gửi, lấy từ SDK) và CD test 0x0107 / 0x010A. `--private`: thêm chuỗi PAA → PAI riêng (controller thương mại **không** tin) |
| `gen_factory_data.sh -t occupancy\|plug -n N [--private]` | Tạo N file `fctry`; DAC ký bởi PAI test FFF1/8000 của SDK (hoặc PAI riêng); kiểm chuỗi DAC bằng `chip-cert validate-att-cert`; in uuid, discriminator, QR và mã nhập tay; báo lỗi nếu discriminator trùng trong lô |
| `flash_factory.sh PORT FILE [OFFSET]` | Nạp `fctry` (0x3E0000; bo 2 MB `matter-thread-plug-wroom-2m`: 0x1F0000), đọc lại và so sánh |
| `check_qr.sh MT:...` | Giải mã QR (VID, PID, discriminator 12 bit, passcode, flow, discovery). Mã nhập tay 11 số chỉ cho passcode và 4 bit cao của discriminator, không có VID/PID |

Chạy trong một thư mục **ngoài repo git**: kết quả chứa khóa riêng DAC và passcode.

```bash
mkdir ~/matter-mfg && cd ~/matter-mfg
T=/đường/dẫn/ESP32-IoT-Project/ESP32-H2/tools/matter-mfg
$T/in_docker.sh gen_pki.sh
$T/in_docker.sh gen_factory_data.sh -t occupancy -n 5
# Mỗi bo: nạp fctry TRƯỚC, rồi firmware build với sdkconfig.defaults.factory
$T/flash_factory.sh /dev/ttyUSB0 factory/occupancy-*/fff1_8000/<uuid>/<uuid>-partition.bin
```

Đã chạy thử trong Docker:

- `gen_pki.sh`, rồi chạy lại với `--private` (file đã có được giữ nguyên)
- `gen_factory_data.sh -t occupancy -n 2`, và `-n 3` với `CD_FILE=pki/cd/CD_test_occupancy_0107.der`
- `gen_factory_data.sh -t plug -n 1 --private`
- `check_qr.sh` với QR và với mã nhập tay

Mọi DAC đều qua kiểm tra chuỗi; mỗi file `fctry` là 24576 byte (0x6000). `flash_factory.sh` chưa chạy trên bo thật.

Lưu ý:

- **VID/PID:** giữ 0xFFF1/0x8000, đúng như firmware báo. DAC phải cùng VID/PID.
- **CD:** mặc định dùng lại CD ví dụ mà firmware đang gửi. Controller đã chấp nhận CD đó; CD tự ký bằng khóa test chỉ được nơi cho phép khóa test chấp nhận. Muốn dùng CD test 0x0107/0x010A thì đặt biến môi trường `CD_FILE=pki/cd/...`.
- **Thư mục `<uuid>/internal`:** chứa khóa riêng DAC. Xóa sau khi nạp; giữ file `partition.bin` cùng nhãn QR của bo đó.
- **Sau `erase-flash`:** phải nạp lại đúng file `fctry` của bo. Firmware factory không có `fctry` thì vẫn chạy với các hệ đã ghép, nhưng không ghép mới được.
- **Bảo mật khóa DAC:** flash encryption đang tắt, nên khóa DAC nằm dạng rõ. Chấp nhận được cho DIY với chứng chỉ test. Sản phẩm thật: bật flash encryption và dùng `esp-matter-mfg-tool -e`, hoặc `esp_secure_cert` với DS peripheral; dùng PAI và CD thật sau khi chứng nhận.
- **esp-matter-mfg-tool 1.0.26:** sinh passcode và discriminator bằng `random` của Python, mỗi bo bốc độc lập nên có thể trùng (script báo lỗi khi trùng trong cùng lô). Giữ file kiểm kê (`summary-*.csv`) và kiểm tra trùng giữa các lô.
- `gen_pki.sh` chạy lại được: file đã có thì giữ nguyên, không ghi đè CD hay khóa PAA/PAI đã dùng.
