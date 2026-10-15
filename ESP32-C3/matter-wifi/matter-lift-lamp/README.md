# Haxeko Vibe Lift Lamp: đèn hình trụ nâng hạ bằng step motor (Matter, ESP32-C3)

Đèn hình trụ hai phần. Phần đế đứng yên, bên trong có một cột dựng đứng dán dải LED WS2812. Phần trên là ống che, được step motor nâng lên hạ xuống bằng trục vít. Ống che nâng tới đâu thì dải LED lộ ra tới đó, và **chỉ những LED đã lộ ra mới sáng**; phần còn bị che vẫn tắt.

Thiết bị là đèn **Matter over Wi-Fi** (device type Extended Color Light), thêm thẳng vào Apple Home, Google Home, Home Assistant...

Cảm biến khoảng cách VL53L0X/L1X để điều khiển bằng tay chưa có trong bản này.

## Cách đèn hoạt động

- **Độ sáng trong app = độ cao của đèn.** 100 % là mở hết hành trình, 50 % là mở nửa. Số LED lộ ra (và sáng) theo cùng bảng với đèn thanh LED cũ: 50 % → 15/30 LED, 10 % → 3 LED...
- **Trong lúc ống che chạy**, LED sáng dần (khi mở) hoặc tắt dần (khi đóng) theo đúng phần đang lộ. Một LED tính là lộ khi đã lộ quá nửa chiều cao của nó.
- **Tắt**: LED tắt ngay, ống che hạ xuống đóng hẳn. 10 mm cuối chạy chậm (tốc độ về gốc) và lần đóng nào cũng phải chạm công tắc hành trình, nên vị trí 0 được kiểm tra lại mỗi lần đóng. **Bật**: ống che mở lại tới độ sáng trước khi tắt. Hiệu ứng "sáng dần từ mức thấp nhất" của Matter khi Bật được bỏ qua, nên đèn không bị hạ xuống rồi mới nâng lên.
- **Màu, nhiệt độ màu trắng, cường độ cố định 30 %**: giống đèn `matter-rgb-light-ws2812`.
- **Khởi động / có điện lại**: firmware không biết ống che đang ở đâu, nên luôn **về gốc** trước. Ống che hạ xuống tới công tắc hành trình, nhích lên 3 mm, rồi chạm lại thật chậm để lấy điểm 0 chính xác. Sau đó nó chạy tới trạng thái đã lưu (bật/tắt, độ sáng, màu). Trong lúc về gốc, LED tắt.
- **Công tắc hành trình chỉ được tin ở gần đáy** (trong khoảng 3 mm quanh điểm 0). Gặp công tắc trong khoảng đó khi đóng: vị trí 0 được chỉnh lại; lệch quá 1 mm thì log báo mất bước.
- **Công tắc báo nhấn ở chỗ không thể** (cao hơn 3 mm, hoặc không nhả ra khi mở): có thể do đứt dây NC, nhiễu, hay mất nhiều bước. Motor dừng ngay tại chỗ, không tin công tắc, rồi về gốc lại. Nếu công tắc thật sự bị nhấn (mất bước), về gốc thành công và đèn chạy tiếp. Nếu công tắc kẹt "nhấn" (dây đứt), về gốc báo lỗi và motor bị tắt.
- **Đóng mà không chạm được công tắc** (sau khi đã xuống quá điểm 0 thêm 5 mm): có vật kẹt hoặc mất bước khi hạ. Ống che nhấc lên 10 mm rồi motor nhả (tắt driver). Lệnh tiếp theo trong app sẽ về gốc lại trước khi chạy. Sau 3 lần đóng hỏng như vậy, motor tắt cho tới khi khởi động lại.
- **Không có motor, hoặc về gốc thất bại** (công tắc không bị nhấn sau 125 % hành trình; vẫn bị nhấn sau khi đã nhấc lên 3 mm): motor được tắt, đèn chạy như đèn thanh LED thường (độ sáng = số LED sáng) cho tới khi khởi động lại, log báo lỗi.
- **Lần đầu khởi động** (chưa có gì lưu): đèn ở trạng thái **tắt** (đóng), để đèn mới nạp không tự chạy ngay tới hết hành trình. Bật lên thì mở 100 %.
- **Nhận dạng (Identify)**: nháy phần LED đang lộ ra. Khi đèn đóng, phần nháy (LED đầu) nằm trong ống che nên có thể không thấy.
- **Nút BOOT**: nhấn nhanh để bật/tắt; giữ 5 giây rồi nhả để factory reset.
- **Tự bảo vệ**: watchdog khởi động lại nếu luồng Matter treo, Wi-Fi không ngủ, log trạng thái mỗi 60 giây (giống đèn RGB).

## Phần cứng

| Linh kiện | Gợi ý |
|---|---|
| Vi điều khiển | ESP32-C3 SuperMini (flash 4 MB) |
| Driver motor | **TMC2209** dạng module (BIGTREETECH, FYSETC, MKS...), chạy chế độ standalone STEP/DIR (không dùng UART) |
| Motor | NEMA17 thân ngắn (20–34 mm), 1,8°/bước, dòng định mức ≤ 1,5 A |
| Truyền động | Trục vít **T8** + đai ốc, khớp nối mềm 5×8 mm, 1–2 thanh dẫn hướng (hoặc ray) để ống che không xoay |
| Công tắc hành trình | Micro switch có chân **NC**, đặt ở đáy hành trình: bị nhấn khi đèn đóng hẳn |
| LED | Dải WS2812/WS2812B, mặc định 30 LED, dán dọc cột, **chân DIN ở dưới** |
| Nguồn | Adapter **12 V 2 A** cho motor + mạch hạ áp (buck) **12 V → 5 V, 3 A** cho LED và board (loại cố định 5 V càng tốt) |
| Linh kiện nhỏ | Tụ 100 µF 25 V (VM), tụ 1000 µF 10 V (đầu dải LED), tụ 100 µF 10 V (5V board), điện trở 330 Ω, 2 × 10 kΩ, tụ 100 nF, cầu chì 2–3 A (hoặc cầu chì tự phục hồi) + diode Schottky SS34 chống cắm ngược nguồn 12 V |

### Sơ đồ đấu nối

```
NGUỒN
  Adapter 12V (+) ──[cầu chì 2–3A]──[SS34 ►|]──► 12V
  12V ──┬────────────────────────────────────────► VM   TMC2209  (tụ 100µF 25V sát VM–GND)
        └──► IN+  Buck 12V→5V (chỉnh 5,0V)  OUT+ ──┬──► 5V   dải LED    (tụ 1000µF sát đầu dải)
                                                   └──► 5V   SuperMini  (tụ 100µF sát chân)
  Adapter 12V (−) ──┬────────────────────────────► GND  TMC2209  (chân cạnh VM)
                    └──► IN−  Buck          OUT− ──┬──► GND  dải LED
                                                   └──► GND  SuperMini
  Mọi GND nối chung về một điểm (IN− và OUT− của buck thường đã thông nhau).

TÍN HIỆU
  SuperMini GPIO4 ──[330Ω]─────────────────► DIN   dải LED (LED đầu tiên ở dưới)
  SuperMini GPIO5 ─────────────────────────► STEP  TMC2209
  SuperMini GPIO6 ─────────────────────────► DIR   TMC2209
  SuperMini GPIO7 ──┬──────────────────────► EN    TMC2209
                    └──[10kΩ]──► 3V3
  SuperMini 3V3   ─────────────────────────► VIO   TMC2209
  SuperMini GND   ─────────────────────────► GND   TMC2209  (chân cạnh VIO)
  SuperMini GPIO3 ──┬──────────────────────► NC    công tắc hành trình
                    ├──[10kΩ]──► 3V3
                    └──[100nF]─► GND
  SuperMini GND   ─────────────────────────► COM   công tắc hành trình

TMC2209 CÒN LẠI
  A1, A2 ── cuộn A của motor        B1, B2 ── cuộn B của motor
  MS1, MS2: để trống (1/8 vi bước)   PDN_UART: nối GND
```

Bảng chân ESP32-C3 SuperMini (đổi được trong `idf.py menuconfig`):

| Chân ESP32-C3 | Nối tới | Ghi chú |
|---|---|---|
| GPIO4 | DIN dải LED, qua điện trở 330 Ω | |
| GPIO5 | STEP (TMC2209) | |
| GPIO6 | DIR (TMC2209) | Sai chiều thì bật *Invert the direction* |
| GPIO7 | EN (TMC2209) + 10 kΩ kéo lên 3V3 | Mức thấp = chạy. Điện trở kéo lên giữ driver tắt lúc ESP32-C3 khởi động |
| GPIO3 | Chân NC công tắc hành trình + 10 kΩ kéo lên 3V3 (+ tụ 100 nF xuống GND nếu dây dài) | Chân COM công tắc nối GND |
| GPIO9 | Nút BOOT có sẵn | Bật/tắt, giữ 5 s để reset |
| 3V3 | VIO (TMC2209) | Mức logic 3,3 V |
| 5V, GND | Từ mạch buck 5 V, dây riêng | **Không** lấy điện LED từ chân 5V hay 3V3 của board |

Tránh GPIO2, GPIO8, GPIO9 (chân strapping, quyết định chế độ khởi động), GPIO18/19 (USB) và GPIO20/21 (sát ăng-ten của SuperMini).

### Nguồn: quan trọng nhất

Motor và LED là hai tải lớn nhất. Ở đèn RGB trước, chỉ riêng dải LED cũng đủ làm ESP32-C3 sụt áp và khởi động lại liên tục. Vì vậy:

0. **Chỉnh mạch buck trước**: chỉ nối adapter → buck, vặn biến trở tới khi đồng hồ đo đúng **5,0 V** (không quá 5,2 V) ở OUT+/OUT−, rồi mới nối dải LED và board. WS2812 chịu tối đa khoảng 5,3 V; module buck mới mua thường đặt sẵn điện áp cao hơn.
1. **Motor dùng 12 V riêng**, đi từ adapter (qua cầu chì và diode chống ngược) tới chân VM của TMC2209. Đặt tụ 100 µF 25 V sát VM–GND của module: TMC2209 có thể hỏng vì xung điện áp nếu thiếu tụ này. Đo đúng cực tính adapter trước khi cắm.
2. **LED và board dùng 5 V từ mạch buck**, kéo dây riêng tới từng nơi (nối hình sao), tụ 1000 µF ở đầu dải LED, tụ 100 µF sát chân 5V/GND của SuperMini.
3. **GND chung một điểm**: GND adapter, GND buck, GND TMC2209 (cả GND công suất và GND logic), GND board và GND LED nối về cùng một chỗ. Dòng motor không được chạy qua dây GND của board hay của LED.
4. **Không cắm hay rút dây motor khi TMC2209 đang có điện 12 V**: dễ cháy driver.
5. Cắm USB vào máy tính để nạp firmware hay xem log: rút dây 5V của buck khỏi chân 5V board, giữ nguyên GND.

### Cài đặt TMC2209

- **Vi bước**: để trống MS1 và MS2 (module có điện trở kéo xuống), driver chạy 1/8 bước, khớp với mặc định `Microsteps per full step = 8`. Nếu đổi MS1/MS2, xem bảng của module và đặt lại cho khớp.
- **PDN_UART nối GND**: chạy standalone và tự giảm dòng khi motor đứng yên, đỡ nóng khi giữ vị trí.
- **Dòng motor (Vref)**: với module dùng điện trở sense 0,11 Ω (BIGTREETECH, FYSETC, MKS), dòng hiệu dụng xấp xỉ **I_rms ≈ 0,71 × Vref**. Đặt khoảng 70 % dòng định mức của motor. Ví dụ motor 1 A: Vref ≈ 1,0 V. Đo Vref giữa biến trở (hoặc chân VREF) và GND khi đã cấp **12 V vào VM** và 3,3 V vào VIO, motor chưa nối. Đo được 0 V nghĩa là VM chưa có điện: đừng vặn biến trở lên tối đa. Bắt đầu thấp rồi tăng dần tới khi nâng ổn định; motor không nên nóng quá khoảng 60 °C. Vref càng thấp, lực ép khi kẹt càng nhỏ (xem **An toàn**). Kiểm tra lại công thức theo tài liệu của đúng module bạn mua.
- **Cuộn dây motor**: đo điện trở để tìm 2 cặp dây thông nhau (mỗi cặp là một cuộn): một cặp vào A1/A2, cặp kia vào B1/B2. Motor chạy ngược chiều thì đảo hai dây của một cuộn, hoặc bật *Invert the direction*.

### Tín hiệu LED

Mức 3,3 V của ESP32-C3 thường đủ cho WS2812 khi dải chạy 5,0 V, nhưng thấp hơn mức "chắc chắn nhận" trong datasheet. Nếu LED nháy sai màu: thêm IC chuyển mức (74AHCT125 / SN74AHCT1G125, cấp 5 V) giữa GPIO4 và điện trở 330 Ω.

### Cơ khí

- Dải LED dán dọc cột, **LED đầu tiên (DIN) ở dưới**, ngay mép trên của đế, nơi bắt đầu lộ ra khi ống che nhấc lên. Nếu DIN ở trên, bật *Light LEDs from the end of the strip*.
- **Hành trình phải bằng chiều dài phần dải LED**: `Travel = số LED × khoảng cách giữa 2 LED`. Ví dụ dải 144 LED/m (khoảng cách 6,9 mm): 30 LED = 208 mm; dải 60 LED/m (16,7 mm): 30 LED = 500 mm. Đặt `LED_COUNT` và `MOTOR_TRAVEL_MM` cho khớp.
- **Trục vít và ray phải dài hơn hành trình ít nhất 5–10 mm** (đo trước khi đặt `MOTOR_TRAVEL_MM`): firmware cần thêm 3 mm phía trên khi kiểm tra công tắc, và công tắc có hành trình vượt quá điểm nhấn.
- **Bước vít**: T8 bước 8 mm (4 đầu ren) chạy nhanh nhưng **không tự hãm**: để mặc định *Driver off at rest = 0* (giữ dòng) để ống che không tự trôi xuống. T8 bước 2 mm chạy chậm hơn 4 lần nhưng tự hãm: có thể đặt *Driver off at rest* = 2000 ms để motor nghỉ, đỡ nóng. Nhớ đặt `MOTOR_LEAD_UM` = 2000 cho trục bước 2 mm.

### An toàn

- **Chỗ kẹt tay**: mép ống che và mặt đế. Motor NEMA17 qua trục vít ép được hàng chục tới vài trăm newton. Firmware không đo được kẹt (không dùng StallGuard): nếu có vật chắn khi đóng, motor tiếp tục ép tới khi chạy hết quãng đường lệnh (vài giây), rồi nhấc lên 10 mm và nhả. Mô phỏng với vật cản ở 30 mm: ép khoảng 3,5 giây. Khi về gốc lúc khởi động, nếu có vật chắn, motor ép suốt quãng tìm (tối đa 125 % hành trình). Giữ tay và đồ vật khỏi khe khi đèn chạy, đặt Vref vừa đủ nâng, và dùng ống che nhẹ.
- Lần chạy đầu, hãy để tay gần công tắc nguồn 12 V.

## Cấu hình (`idf.py menuconfig`)

| Menu | Tùy chọn | Mặc định |
|---|---|---|
| Lift motor | STEP / DIR / EN / End stop GPIO | 5 / 6 / 7 / 3 |
| | End stop is normally closed (NC) | có (khuyến nghị: dây đứt = coi như bị nhấn, firmware phát hiện và tắt motor thay vì đâm xuống đế hay chạy quá đỉnh) |
| | Full steps per revolution / Microsteps | 200 / 8 |
| | Lead screw travel per revolution | 8000 µm (T8 bước 8 mm) |
| | Travel from closed to fully open | 150 mm |
| | Speed / Acceleration | 10 mm/s / 20 mm/s² |
| | Homing speed | 8 mm/s, cũng dùng cho 10 mm cuối mỗi lần đóng (lúc khởi động, chạm lần hai bằng 1/4) |
| | Driver off at rest after | 0 (giữ dòng) |
| WS2812 RGB Light | GPIO, số LED, cường độ, chiều sáng | 4, 30, 30 %, từ DIN |
| Lift motor | Step motor moves the upper part | có (tắt đi = đèn thanh LED thường) |

Tốc độ bị giới hạn ở 10 000 bước/giây (ngắt 20 kHz). Với 200 bước/mm (1/8 vi bước, T8 bước 8 mm) là tối đa 50 mm/s.

## Build và nạp

Giống đèn `matter-rgb-light-ws2812`: ESP-IDF v6.0.x, esp-matter `release/v1.6`, hoặc Docker `espressif/esp-matter:release-v1.6.1_idf_v6.0.2`.

```bash
cd ESP32-C3/matter-wifi/matter-lift-lamp
idf.py set-target esp32c3
idf.py menuconfig      # hành trình, bước vít, chân GPIO
idf.py build
```

| File trong `build/` | Dùng khi | Nạp tại |
|---|---|---|
| `matter_lift_lamp_esp32c3_merged_<phiên bản>.bin` | Đèn mới / xóa sạch | `0x0` |
| `matter_lift_lamp_esp32c3_update_<phiên bản>.bin` | Cập nhật, giữ ghép nối | `0x1D000` |
| `matter_lift_lamp_esp32c3_<phiên bản>.bin` | Chỉ app (OTA) | |

Ghép nối: tên Bluetooth `VibeLift-3840`, mã QR `MT:Y.K9042C00KA0648G00`, mã thủ công `3497-011-2332` (dữ liệu test của esp-matter, giống đèn RGB).

## Chạy thử lần đầu (theo thứ tự)

0. **Chỉnh buck 5,0 V** (mục Nguồn), kiểm tra cực tính 12 V.
1. **Chưa nối motor**: nạp firmware, xem log (`idf.py -p PORT monitor`). Log về gốc báo lỗi là **bình thường** khi chưa có motor; đèn chạy như đèn thanh LED: kiểm tra LED, ghép nối, app. Đèn mới nạp ở trạng thái tắt.
2. **Nối công tắc hành trình**: khởi động lại một lần khi công tắc nhả, một lần khi giữ nhấn. Dòng log `stepper: ... end stop GPIO3 (NC, now released/pressed)` phải đổi theo.
3. **Nối motor, chỉnh Vref**. Trong app để đèn **tắt** (hoặc 10 %). Để ống che ở giữa hành trình, khởi động lại: ống che phải **đi xuống** tìm công tắc. Nếu đi lên, tắt nguồn 12 V ngay, đổi chiều (*Invert the direction*).
4. Tăng độ sáng từng nấc trong app (10 % → 25 % → 50 % → 100 %), kiểm tra ống che dừng đúng mép LED cuối cùng và không chạm đỉnh; sai thì chỉnh `MOTOR_TRAVEL_MM` / `MOTOR_LEAD_UM` / vi bước.

## Cấu trúc mã nguồn

| File | Nội dung |
|---|---|
| `main/motion.c` | Lập quỹ đạo hình thang (tăng tốc, chạy đều, giảm tốc) chạy trong ngắt 20 kHz; đổi đích giữa chừng thì phanh rồi quay lại, không dừng giật |
| `main/stepper.c` | Ngắt GPTimer (trong IRAM, chạy cả khi đang ghi flash), xung STEP/DIR, chân EN, công tắc hành trình (lọc nhiễu 1 ms, chỉ tin trong 3 mm quanh 0), về gốc, đóng có kiểm tra công tắc |
| `main/lift.c`, `main/lift_map.h` | Độ sáng/bật tắt → độ cao; vị trí thật → số LED lộ ra |
| `main/led_bar.c` | Dải WS2812: chỉ sáng phần lộ ra (chế độ lift), màu, cường độ cố định, nháy Identify |
| `main/app_driver.cpp`, `main/app_main.cpp` | Matter: Extended Color Light, nút BOOT, NVS, watchdog (`app_health.cpp`) |
| `test/host/` | Test trên máy tính: quỹ đạo motor (về đích chính xác, không vượt tốc, không dừng gấp), độ cao ↔ số LED, driver LED, đổi màu, và mô phỏng `stepper.c` với timer, GPIO, TMC2209, ống che và công tắc giả lập: về gốc từ mọi vị trí, dây công tắc đứt, thiếu motor, mất bước, vật kẹt khi đóng, nhiễu công tắc (5 cấu hình) |

```bash
test/host/run.sh   # chỉ cần gcc
```

## Giới hạn

- Chưa có cảm biến VL53L; điều khiển bằng app, nút BOOT.
- Mỗi lần khởi động đèn phải về gốc (đóng hẳn rồi mở lại), vì step motor không tự biết vị trí.
- Chứng chỉ Matter test, chưa bật Secure Boot / mã hóa flash (như đèn RGB).
- Không phát hiện kẹt/quá tải (TMC2209 UART + StallGuard chưa dùng); xem mục **An toàn**.
- Chưa thử trên phần cứng thật (chỉ mô phỏng); xem mục **Chạy thử lần đầu**.
