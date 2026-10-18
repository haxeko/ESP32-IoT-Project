# KEN Dashboard cho Raspberry Pi 3

Phiên bản này dựng lại giao diện từ `ken_ui_2.yaml` của màn Waveshare ESP32-P4 7B, nay theo phong cách iOS với hiệu ứng kính (glassmorphism). Trên màn hình ngang (Pi 1024 × 600, iPad ngang, máy tính) khung vẫn giữ đúng tỷ lệ và vị trí 1024 × 600:

- Hàng trên: logo KEN (trạng thái kết nối), giờ/ngày, thời tiết, công suất, điện tháng và tiền điện.
- Phòng: Tổng quan, Phòng khách, Phòng ngủ, Bếp và Phòng giặt.
- Thanh trái: Nhà, Nhạc, An ninh, Đèn, Khí hậu, Điện và Cài đặt.
- Thẻ điều khiển máy lạnh, quạt, rèm, COB, đèn, nước nóng; thẻ trạng thái cửa, mưa, máy giặt, máy sấy, lọc nước và bếp từ.
- Trang nhạc Phicomm R1, biểu đồ điện lưu cục bộ và trang thông tin Pi.

Trên điện thoại và tablet dựng dọc, dashboard chuyển sang bố cục cuộn dọc với thanh tab ở đáy (xem mục *Giao diện kính và bố cục theo màn hình*).

Toàn bộ entity mặc định được lấy trực tiếp từ phần `substitutions` của YAML. Pi 3 chạy Nginx; Safari/Chromium kết nối trực tiếp tới Home Assistant bằng WebSocket. Không cần cài thêm gì trong Home Assistant hay HACS: dashboard là trang web riêng, không dùng card Lovelace.

## Cập nhật bản đang chạy trên Pi 3

Chép file ZIP mới vào thư mục home của Pi rồi chạy trong PuTTY:

```bash
cd ~
sudo apt install -y unzip
unzip -o KEN_Dashboard_Pi3_glass.zip
cd ken-dashboard
sudo bash install.sh
```

`install.sh` tự sao lưu bản đang chạy (giao diện, `config_server.py`, `config.json`) vào `/var/backups/ken-dashboard/<ngày-giờ>/` trước khi chép bản mới, giữ 5 bản sao lưu gần nhất, rồi khởi động lại dịch vụ cấu hình và Nginx.

Sau khi cài, mở:

```text
http://IP-CUA-PI3:8080
```

Nếu Safari còn giao diện cũ, đóng tab rồi mở lại hoặc vào Cài đặt Safari → Xóa dữ liệu trang web của địa chỉ Pi.

## Khôi phục bản cũ

```bash
sudo bash /usr/local/lib/ken-dashboard/rollback.sh
```

Lệnh trên đưa giao diện về bản sao lưu mới nhất (bản đang chạy trước lần `install.sh` gần nhất). Nếu đã chạy `install.sh` nhiều lần, xem danh sách rồi chọn đúng thư mục:

```bash
ls -lt /var/backups/ken-dashboard
sudo bash /usr/local/lib/ken-dashboard/rollback.sh /var/backups/ken-dashboard/<ngày-giờ>
```

Cấu hình dùng chung (`/var/lib/ken-dashboard/config.json`) được giữ nguyên vì định dạng không đổi; `rollback.sh` in sẵn lệnh nếu muốn lấy lại cả bản `config.json` lúc sao lưu. Bản cũ đọc được mọi dữ liệu bản mới ghi trong trình duyệt.

## Kết nối Home Assistant

1. Trong Home Assistant, mở hồ sơ người dùng → Bảo mật → tạo Long-lived access token.
2. Trên dashboard, vào **Cài đặt → Home Assistant** (hoặc chạm logo KEN để mở hộp thoại kết nối).
3. Nhập địa chỉ LAN của HA, ví dụ `http://192.168.1.10:8123`, dán token và bấm **Lưu & kết nối**.
4. Có thể sửa nhanh entity thời tiết, công suất, điện tháng và tiền điện ngay trên thẻ. Bấm **Cài đặt entity** để cấu hình toàn bộ thiết bị.

Token chỉ lưu trong trình duyệt của từng máy, không chuyển qua Pi 3 và không ghi vào thư mục Nginx. Lưu từ trang Cài đặt sẽ nhớ token trên máy đó; hộp thoại ở logo KEN cho chọn “Nhớ token” hoặc chỉ giữ trong phiên.

Token được gắn với địa chỉ HA đã nhập cùng nó. Địa chỉ HA nằm trong cấu hình dùng chung, nên nếu địa chỉ bị đổi (trên máy khác hoặc qua `/api/config`), các máy còn lại sẽ không gửi token sang địa chỉ mới mà hiện thông báo; vào Cài đặt và nhập lại token cho địa chỉ đó.

## Đồng bộ cấu hình giữa các máy

`install.sh` cài thêm dịch vụ `ken-dashboard-config.service`. Bố cục, phòng, tên/icon thiết bị, danh sách entity, địa chỉ HA và cấu hình loa được lưu chung tại:

```text
/var/lib/ken-dashboard/config.json
```

Dashboard kiểm tra phiên bản cấu hình khoảng 5 giây một lần. Thay đổi trên một máy sẽ tự xuất hiện trên các máy còn lại khi máy kia không mở biểu mẫu hoặc đang sắp xếp thiết bị. Lần đầu nâng cấp, máy đầu tiên mở Dashboard sẽ đưa cấu hình đang lưu trong trình duyệt của máy đó lên Pi làm cấu hình chung.

Token Home Assistant, độ sáng, hiệu ứng kính, thời gian làm tối, nguồn nhạc đang chọn và lịch sử biểu đồ điện vẫn lưu riêng trên từng máy. API chỉ lắng nghe tại `127.0.0.1:8091`; thiết bị trong mạng truy cập qua Nginx tại `/api/config`. API này không có mật khẩu: mọi thiết bị trong LAN đều đọc/ghi được cấu hình chung (không chứa token).

Kiểm tra dịch vụ:

```bash
sudo systemctl status ken-dashboard-config --no-pager
curl http://127.0.0.1:8080/api/config
```

## Chế độ bản mẫu

Khi chưa có token, giao diện mở bằng dữ liệu minh họa và logo hiện “Bản mẫu”. Chạm các thẻ để thử bố cục mà không điều khiển thiết bị thật. Để trở lại bản mẫu: chạm logo KEN → Chạy bản mẫu.

## Giao diện kính và bố cục theo màn hình

- Nền là ảnh gradient tĩnh theo chủ đề; thẻ, thanh bên, ô phòng và popup là lớp kính mờ có viền sáng mảnh và bóng nhẹ. Không có hiệu ứng chạy liên tục (chỉ biểu tượng máy giặt/máy sấy/bếp/lọc nước chuyển động khi thiết bị đang chạy, tắt khi hệ thống yêu cầu giảm chuyển động).
- Trạng thái thiết bị luôn có cả màu và chữ:
  - **Tắt**: thẻ kính, biểu tượng xám, chữ “Tắt”/“Đóng”/“Khô”.
  - **Bật**: thẻ sáng nổi lên, vòng biểu tượng có màu (đèn vàng, máy lạnh xanh dương, quạt xanh ngọc, rèm tím, nước nóng cam, công tắc xanh lá), chữ “Bật”, chế độ máy lạnh (Lạnh/Khô/…) hoặc % độ sáng/vị trí.
  - **Cảnh báo**: cửa mở, phát hiện nước → thẻ cam, chữ “Mở”/“Ướt”.
  - **Đang chạy**: máy giặt, máy sấy, lọc nước, bếp từ → vòng biểu tượng xanh dương; máy giặt xong → thẻ xanh lá.
  - **Không khả dụng**: thẻ mờ, viền nét đứt, biểu tượng ⊘ (entity không có hoặc báo `unavailable`/`unknown`).
- **Cài đặt → Kính mờ** bật/tắt hiệu ứng làm mờ trên từng máy. Khi tắt, khi trình duyệt không hỗ trợ `backdrop-filter`, hoặc khi hệ thống bật “Giảm độ trong suốt”, các lớp kính chuyển sang nền đặc cùng màu, bố cục và độ tương phản giữ nguyên. Mặc định tắt trên Chromium của Raspberry Pi (GPU yếu), bật trên iPad/điện thoại/máy tính.
- Màn hình ngang đủ rộng (tỷ lệ thu nhỏ ≥ 72 %) dùng khung 1024 × 600. Màn hình 800 × 480 vẫn dùng khung này ở tỷ lệ 78 %.
- Điện thoại (dọc/ngang) và tablet dựng dọc dùng bố cục cuộn: thẻ đầu trang 3 cột (6 cột trên tablet), hàng phòng dính ở đầu khi cuộn, chip cảm biến cuộn ngang, lưới thẻ 3–9 cột theo thứ tự đọc của bố cục ngang, thanh tab ở đáy có chừa vùng an toàn của iPhone. Ở bố cục này vẫn sửa tên/entity, xóa và thêm thiết bị được; kéo thả vị trí thẻ chỉ làm trên màn hình ngang để không làm lệch bố cục 1024 × 600.

## Cách điều khiển

- Hàng cảm biến (hàng 3): mặc định Nhiệt độ, Độ ẩm, PM2.5 và Không khí. Giữ một thẻ 3 giây để thêm/xóa, sửa tên/entity hoặc đổi thứ tự bằng nút lên/xuống như phần phòng. Có 6 loại: CO₂, VOC, Nhiệt độ, Độ ẩm, PM2.5, Không khí; mỗi loại tối đa một thẻ trong một phòng. Các thẻ tự chia đều chiều ngang; khi có 5–6 thẻ, tên và chỉ số được xếp thành hai dòng trong cùng chiều cao. Có thể bỏ trống entity; thẻ khi đó hiển thị “-”. Nếu xóa hết, giữ ô “+ Cảm biến” 3 giây để thêm lại. Danh sách và thứ tự được lưu riêng theo phòng, đồng bộ qua Pi; entity đã lưu vẫn được giữ khi xóa để có thể thêm lại.
- Đèn, quạt trần, nước nóng và hút mùi: chạm thẻ để bật/tắt.
- Máy lạnh: chạm hàng tên/biểu tượng để bật/tắt, −/+ để đổi nhiệt độ; chế độ đang chạy hiện dưới tên. Thẻ không có nút đổi chế độ.
- Quạt Toshiba: nguồn, cấp 1–3 (cấp đang chạy được tô màu) và đảo gió.
- Rèm: kéo thanh phần trăm hoặc chạm biểu tượng để đóng/mở.
- COB/đèn điều chỉnh: chạm biểu tượng để bật/tắt, kéo độ sáng; giữ thẻ 0,6 giây để mở bốn ô màu. Chạm màu sẽ bật đèn ở mức sáng cuối đã ghi nhận; giữ ô RGB 0,6 giây để sửa màu. Cả hai popup cập nhật thanh độ sáng từ trạng thái Home Assistant và không tự gán 100% khi HA thiếu thuộc tính `brightness` lúc đèn tắt.
- Máy giặt, máy sấy, lọc nước và bếp từ: chạm thẻ để xem công suất/điện năng chi tiết.
- An ninh → Tổng quan: cửa chính, cửa sổ, cửa ban công, cảm biến mưa/rò nước, máy giặt, máy sấy, lọc nước, bếp từ.
- Nhạc: phát/dừng, bài trước/sau, âm lượng và nguồn phát của `media_player.phicomm_r1_fd73`.
- Giữ 3 giây trên vùng trống giữa các thẻ để vào chế độ sắp xếp; giữ 1,8 giây trên tên phòng để sắp xếp phòng.

Khi HA mất kết nối, logo chuyển chấm đỏ “Mất kết nối HA”, dashboard giữ giá trị cuối nhưng khóa lệnh điều khiển và tự kết nối lại. Trạng thái từ HA được gộp và vẽ lại tối đa khoảng 8 lần mỗi giây, và không vẽ lại khi đang chạm, nên lần chạm không bị mất khi HA gửi nhiều cập nhật.

Mức sáng cuối của mỗi entity đèn được ghi nhận từ phản hồi HA và giữ trong trình duyệt qua lần tải lại trang. Nếu lần đầu mở bản mới khi đèn đã tắt và HA không còn gửi độ sáng, hãy đặt mức sáng mong muốn một lần trước khi tắt để Dashboard ghi nhớ. Khi chưa có giá trị đã ghi nhận, lệnh chọn màu để HA quyết định độ sáng, không ép lên 100%.

Trên Dashboard, trong popup chọn màu và popup sửa màu RGB, thanh độ sáng hiển thị 0% khi đèn tắt, kể cả khi HA vẫn giữ thuộc tính độ sáng cũ. Giá trị nhớ dùng để bật lại được lưu riêng; khi chọn màu, đèn bật ở mức đã nhớ và thanh kéo cập nhật theo phản hồi HA.

## Trang Điện

Biểu đồ giữ cấu trúc giống YAML. Dashboard lấy:

- Điện tháng từ `sensor.ken_energy_monthly`.
- Tiền điện từ `sensor.ken_electricity_cost`.
- Chênh lệch tổng điện từ `sensor.tasmota_energy_total_2` để tích lũy theo ngày khi dashboard đang hoạt động.

Ngày và tháng của biểu đồ tính theo giờ địa phương của máy đang mở dashboard. Dữ liệu biểu đồ được lưu trong trình duyệt của từng iPad/màn hình. Xóa dữ liệu trang web sẽ xóa lịch sử này. Giá trị tháng hiện tại vẫn được đọc lại từ Home Assistant.

## Trang Cài đặt

Trong **Giao diện**, chọn Mặc định, Mùa xuân, Mùa hạ, Mùa thu, Mùa đông hoặc Giao diện tối, rồi bấm **Lưu giao diện** để áp dụng. Lựa chọn được lưu và đồng bộ chung qua Pi; đổi lựa chọn mà chưa lưu không làm thay đổi giao diện đang dùng.

Mỗi chủ đề có nền gradient và màu nhấn riêng; bốn mùa giữ họa tiết SVG tĩnh trên nền (lá/hoa xuân, nắng/sóng hạ, lá vàng thu, bông tuyết đông) nằm dưới lớp kính. Giao diện tối dùng kính tối, thẻ đang bật vẫn sáng như iOS. Họa tiết không chặn thao tác và không chạy hiệu ứng nền liên tục trên Pi 3. Màu trạng thái thiết bị giống nhau ở mọi chủ đề.

Giữ **3 giây** trên thẻ **Thời tiết, Điện, Tháng này, Tiền điện** để đổi entity của từng thẻ. Trong Cài đặt, thẻ **Máy chủ Homeassistant** hiển thị **Nhiệt độ CPU, Dung lượng trống, RAM**; giữ 3 giây trên thẻ này để sửa cả ba entity trong một popup. Bấm **Lưu entity** để lưu và đồng bộ, hoặc **Hủy** để giữ cấu hình trước đó. Popup không tự focus ô nhập liệu. Di chuyển ngón tay hoặc thả trước 3 giây sẽ hủy thao tác giữ; bàn phím có thể dùng Enter/Space.

Chạm giữ không bôi chọn chữ hoặc mở menu chọn văn bản trên Dashboard. Khi popup mở, các bộ đếm sắp xếp còn sót được hủy; lần thả tay của thao tác giữ không bấm xuyên vào popup/nền. Focus ở khung popup, không tự mở bàn phím. Chạm mới vào ô nhập để sửa entity bình thường. Chế độ sắp xếp chỉ mở bằng một thao tác giữ mới, đủ 3 giây trên ô trống khi không có popup.

“Độ sáng” phủ một lớp tối lên trang (nhẹ hơn bộ lọc cũ và không làm mất hiệu ứng kính). “Làm tối sau” phủ màn hình đen và chạm để mở lại; trình duyệt web không thể điều khiển trực tiếp phần cứng đèn nền của mọi loại màn hình. “Kính mờ” bật/tắt hiệu ứng làm mờ trên máy đang dùng. Các cảm biến Pi lấy từ ba entity `system_monitor` trong YAML.

Ô **Địa chỉ loa Phicomm R1** nhận địa chỉ trang điều khiển của loa, mặc định `http://192.168.1.236:8081/`. Dashboard tự lấy hostname/IP từ địa chỉ này để kết nối các cổng WebSocket. Trong trang Nhạc:

- **Music Assistant** điều khiển entity media player qua Home Assistant.
- **AI BOX** kết nối trực tiếp tới loa trong LAN qua WebSocket cổng 8082; âm lượng dùng cổng 8080. Pi/màn hình và loa phải ở cùng mạng và firmware loa phải cung cấp hai dịch vụ này. Chuyển lại Music Assistant sẽ đóng kết nối tới loa.

## Mở tự động trên màn hình gắn trực tiếp vào Pi

Nếu Pi 3 có Raspberry Pi OS Desktop và Chromium, có thể mở thử chế độ kiosk:

```bash
chromium-browser --kiosk --noerrdialogs --disable-infobars http://127.0.0.1:8080
```

Tên lệnh trên Raspberry Pi OS mới có thể là `chromium`. Cấu hình tự khởi động phụ thuộc bản Raspberry Pi OS và desktop đang dùng. Nếu thao tác trên Pi còn chậm, giữ **Kính mờ** ở trạng thái tắt.

## Kiểm tra nhanh

```bash
hostname -I
sudo nginx -t
sudo systemctl status nginx --no-pager
sudo ss -ltnp | grep ':8080'
```

Nếu iPad không mở được, kiểm tra iPad và Pi ở cùng LAN, Wi-Fi khách/AP isolation đã tắt và IP Pi chưa thay đổi. Nên đặt DHCP reservation cho Pi 3 và Home Assistant.

## Các file

- `index.html`: khung giao diện.
- `style.css`: vị trí và kích thước khung 1024 × 600 theo LVGL (không đổi).
- `themes.css`: bảng màu, nền và họa tiết của sáu chủ đề.
- `glass.css`: lớp kính iOS, màu trạng thái, phương án nền đặc và bố cục điện thoại/tablet dọc.
- `app.js`: entity, điều khiển, trang và dữ liệu demo.
- `ha.js`: kết nối Home Assistant WebSocket.
- `direct-speaker.js`: kết nối trực tiếp Phicomm R1/AI BOX trong mạng LAN.
- `config_server.py`: API lưu và đồng bộ cấu hình dùng chung trên Pi.
- `install.sh`: cài/cập nhật Nginx trên Pi 3, sao lưu bản đang chạy.
- `rollback.sh`: khôi phục bản đã sao lưu.

Logo đầu trang dùng ảnh minh họa Xeko cục bộ tại `assets/xeko.png` (256 × 256), vì vậy giao diện vẫn hiển thị đầy đủ khi Pi không có Internet. Bộ icon điều hướng và thiết bị dùng SVG nội tuyến để giữ nét đồng nhất trên Chromium/iPad. `assets/seasons/*-card.svg` và `*-panel.svg` không còn được dùng trong giao diện kính nhưng vẫn được cài để quay về bản cũ không thiếu file.
