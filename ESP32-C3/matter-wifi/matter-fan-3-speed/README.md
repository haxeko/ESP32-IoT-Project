# Haxeko Smart Fan: quạt 3 số có đảo gió, Matter over Wi-Fi + mắt hồng ngoại (ESP32-C3)

Firmware ESP-IDF + [esp-matter](https://github.com/espressif/esp-matter) cho **ESP32-C3** (SuperMini hoặc DevKitM-1) biến một quạt đứng/quạt treo **3 số có trục xoay (đảo gió)** thành thiết bị **Quạt Matter** (device type Fan, 0x002B). Thêm trực tiếp vào Apple Home, Google Home, Home Assistant, SmartThings, Alexa qua **Wi-Fi**, ghép nối bằng **BLE**.

- **4 relay**: 3 relay chọn số (1 / 2 / 3) và 1 relay bật/tắt động cơ trục xoay (đảo gió).
- **Theo đúng chuẩn Matter Fan Control**: thanh tốc độ 0–100 % ra số theo quy tắc của chuẩn (1–33 % số 1, 34–66 % số 2, 67–100 % số 3), FanMode Off/Low/Medium/High, lệnh Step.
- **Đảo gió**: nút **Dao động (Oscillate)** của Apple Home / Home Assistant (Matter Rocking) bật/tắt relay trục xoay.
- **Gió tự nhiên**: quạt tự đổi số ngẫu nhiên như gió ngoài trời (Matter Wind; preset *natural_wind* trong Home Assistant, nút trên remote, nút ngoài). Apple Home không hiển thị chức năng này.
- **Mắt thu hồng ngoại**: dùng lại remote gốc của quạt (học lệnh ngay trên thiết bị, nhận mọi loại remote).
- Nút BOOT trên mạch: bật/tắt, ghép nối, học remote, factory reset. Thêm được nút số, nút đảo gió và nút gió tự nhiên ngoài.

## Cách quạt hoạt động

### Tốc độ

Tốc độ theo đúng quy tắc của cluster Matter Fan Control (SpeedMax = 3): quạt chạy ở `SpeedSetting`, do cluster tính từ giá trị ứng dụng ghi vào.

| Ứng dụng ghi | Quạt chạy | Relay đóng | Matter (SpeedSetting / FanMode) |
|---|---|---|---|
| 0 %, FanMode Off | tắt | không relay nào | 0 / Off |
| 1–33 %, FanMode Low, SpeedSetting 1 | **số 1** | số 1 | 1 / Low |
| 34–66 %, FanMode Medium, SpeedSetting 2 | **số 2** | số 2 | 2 / Medium |
| 67–100 %, FanMode High hoặc On, SpeedSetting 3 | **số 3** | số 3 | 3 / High |

- `SpeedSetting` = làm tròn lên (3 × `PercentSetting` / 100), như chuẩn quy định. `PercentSetting` giữ nguyên giá trị ứng dụng đã ghi (ví dụ 45 % thì vẫn là 45 %, quạt chạy số 2); `PercentCurrent` / `SpeedCurrent` cho biết số đang chạy thật (33 / 66 / 100 %).
- Đổi số bằng remote hay nút trên mạch thì firmware ghi `SpeedSetting` mới (cluster tự tính `PercentSetting` = 33 / 66 / 100 % và FanMode), ứng dụng cập nhật ngay.
- **Bật từ ứng dụng** mà không chọn số (FanMode On; Apple Home bật quạt bằng cách gửi 100 %): **số 3**, theo chuẩn.
- **Bật lại bằng remote hoặc nút trên mạch** (nút nguồn, nút BOOT): quạt chạy lại **số trước khi tắt**. Lần bật đầu tiên (chưa có gì lưu): số 1.
- **Đổi số an toàn cho motor**: relay số cũ nhả ra, chờ **200 ms** (`FAN_RELAY_DEAD_TIME_MS`) rồi relay số mới mới đóng, kể cả khi tắt rồi bật lại thật nhanh. Không bao giờ có 2 relay số cùng đóng. Nên đấu thêm **khoá liên động bằng dây** (xem [Sơ đồ đấu nối](#sơ-đồ-đấu-nối)) để kể cả khi hỏng hóc cũng không thể cấp điện 2 đầu dây motor cùng lúc.

### Đảo gió (trục xoay)

- Relay trục xoay chỉ đóng **khi quạt đang chạy và đảo gió đang bật**. Tắt quạt thì động cơ trục xoay dừng theo, nhưng trạng thái đảo gió được nhớ: bật quạt lại là quạt đảo gió tiếp.
- Vì vậy lúc quạt tắt, nút **Dao động** trong Apple Home vẫn có thể hiện **Có**: đó là cài đặt (`RockSetting`) cho lần bật sau, còn **relay trục xoay vẫn mở**. Firmware chỉ đóng relay này khi quạt đang bật (có số đặt); test trên máy tính thử với mọi trạng thái và mọi lệnh. Log serial in trạng thái relay thật mỗi khi có thay đổi, ví dụ lúc tắt quạt: `Relays: all speed relays open, oscillation open`.
- Tắt quạt thì trục xoay dừng trước, bật quạt thì trục xoay chạy sau. Lúc đổi số trục xoay vẫn chạy (motor quạt chỉ trôi theo quán tính trong 200 ms chờ đổi số).
- Trên Matter, đảo gió là **Rocking** (`RockSupport` = trái-phải): Apple Home hiện nút **Dao động** (*Oscillate*), Home Assistant hiện *Oscillate*. Tắt `FAN_MATTER_ROCKING` thì đảo gió chỉ còn ở remote và nút.

### Gió tự nhiên

Khi bật gió tự nhiên và quạt đang chạy, quạt tự đổi số ngẫu nhiên **quanh số đang đặt** (số đặt là mức gió):

| Số đang đặt | Gió tự nhiên chạy |
|---|---|
| 1 (nhẹ) | số 1, thỉnh thoảng lên số 2 một lúc ngắn |
| 2 (vừa) | số 1–3: phần lớn thời gian số 1–2, thỉnh thoảng lên số 3 một lúc ngắn |
| 3 (mạnh) | số 2, thỉnh thoảng lên số 3 một lúc ngắn |

- Motor không bao giờ dừng khi đang chạy gió tự nhiên.
- Mỗi số giữ ngẫu nhiên **8–30 s**; cơn gió mạnh (số cao nhất của mức) tối đa 19 s. Chỉnh bằng `FAN_NATURAL_MIN_S` / `FAN_NATURAL_MAX_S`. Đổi số vẫn theo quy tắc ngắt trước, chờ 200 ms rồi mới đóng, như khi đổi số bằng tay. Đảo gió vẫn chạy độc lập.
- Gió tự nhiên là một **cài đặt riêng**, độc lập với số (Matter `WindSetting`): đổi số trong lúc chạy gió tự nhiên là đổi mức gió, gió tự nhiên vẫn chạy; tắt quạt vẫn nhớ gió tự nhiên, bật lại là chạy tiếp. Gió tự nhiên cũng được nhớ khi mất điện / khởi động lại.

| Bật / tắt gió tự nhiên bằng | Ghi chú |
|---|---|
| **Home Assistant** | preset `natural_wind` (bật), preset low / medium / high hoặc *none* (tắt) |
| Remote | nút đã học ở bước 8 (**NATURAL**: nút *Nhịp gió*, *Tự nhiên*, *Mode*...). Quạt đang tắt thì nút này bật quạt lên luôn |
| Nút ngoài (tuỳ chọn) | `BUTTON_NATURAL_GPIO`, giống nút trên remote |
| chip-tool / ứng dụng Matter khác | ghi `WindSetting` (2 = gió tự nhiên, 0 = tắt); không tự bật quạt |
| **Apple Home** | không có: Apple Home không hiển thị Matter Wind. Gió tự nhiên bật từ remote / nút / Home Assistant vẫn chạy bình thường, thanh tốc độ của Apple Home lúc đó là mức gió |

- `PercentCurrent` / `SpeedCurrent` luôn cho biết số đang chạy thật lúc đó. Preset *sleep_wind* mà Home Assistant luôn liệt kê kèm thì thiết bị không hỗ trợ (báo lỗi khi chọn).
- ⚠️ **Tuổi thọ relay**: với 8–30 s quạt đổi số khoảng 200 lần một giờ (mỗi relay số đóng cắt khoảng 70–100 lần một giờ). Relay cơ 10 A tải motor quạt nhỏ thường chịu được hàng trăm nghìn lần, nhưng nếu bật gió tự nhiên nhiều giờ mỗi ngày nên tăng thời gian giữ số (ví dụ 20–60 s), hoặc dùng [relay rắn](#dùng-relay-rắn-ssr) (không mòn, không kêu). Không muốn có gió tự nhiên trên Matter thì tắt `FAN_MATTER_WIND_NATURAL` (khi đó chỉ còn ở remote và nút ngoài).

### Mất điện, khởi động lại

- **Mất điện rồi có điện lại**: quạt quay về trạng thái cuối (số, đảo gió và gió tự nhiên), chỉ chậm khoảng 0,5 s sau khi cấp điện, không cần chờ Wi-Fi. Đổi được thành "luôn tắt khi có điện lại" trong menuconfig (`FAN_POWER_ON_STATE`); khi đó số và đảo gió vẫn được nhớ cho lần bật tiếp theo.
- **Khởi động lại mềm** (OTA, factory reset, crash, watchdog): luôn khôi phục trạng thái cuối. Riêng khi thiết bị bị crash, watchdog hoặc sụt áp trong vòng **30 s** sau khi khởi động, lần khởi động sau quạt ở trạng thái **tắt** (và lưu luôn trạng thái tắt), để một lỗi lặp lúc khởi động (ví dụ nguồn yếu sụt áp mỗi khi relay đóng) không bật tắt motor liên tục.
- Trạng thái quạt được ghi vào flash khi đã đứng yên **3 giây**, và ngay trước mỗi lần khởi động lại mềm (OTA, factory reset). Rút điện trong 3 giây sau khi vừa đổi thì lần đổi đó không được nhớ.

## Phần cứng

- **ESP32-C3 SuperMini** (hoặc ESP32-C3-DevKitM-1), flash 4 MB.
- **Module 4 relay** 5 V (3 relay số + 1 relay trục xoay), tiếp điểm chịu được dòng motor quạt (thường 10 A / 250 VAC là dư).
- **Mắt thu hồng ngoại 38 kHz**: VS1838B, TSOP38238, TSOP1838... hoặc module KY-022. Cấp **3,3 V**.
- **Nguồn 220 V → 5 V** (ví dụ HLK-PM01) cho ESP32-C3 và module relay.

### Sơ đồ chân

| Chức năng | Chân ESP32-C3 | Ghi chú |
|---|---|---|
| Relay **số 1** (thấp) | **GPIO3** | |
| Relay **số 2** | **GPIO4** | |
| Relay **số 3** (cao) | **GPIO5** | |
| Relay **trục xoay** (đảo gió) | **GPIO7** | |
| Mắt hồng ngoại, chân OUT | **GPIO10** | VCC 3,3 V, GND |
| Nút BOOT | GPIO9 | có sẵn trên mạch |
| LED trạng thái | GPIO8 | SuperMini: LED xanh dương (sáng ở mức thấp). DevKitM-1: WS2812 |
| Nút số / đảo gió / gió tự nhiên ngoài (tuỳ chọn) | ví dụ GPIO0 / GPIO1 / GPIO6 | nối xuống GND, bật trong menuconfig |

- Các chân relay được chọn là chân **không có điện trở kéo lên lúc reset** (GPIO0, 1, 3, 4, 5, 7, 10 trên ESP32-C3): từ lúc cấp điện tới khi firmware chạy (~0,3 s) chân relay để hở, mạch kích relay active-high không bị kích nhầm. Không dùng cho relay: GPIO2/8/9 (chân strapping), GPIO6 (có kéo lên lúc reset), GPIO11–17 (flash), GPIO18/19 (USB), GPIO20/21 (UART0, ROM in log ra GPIO21 mỗi lần khởi động). Firmware báo lỗi khi build nếu hai chức năng dùng trùng chân hoặc dùng chân flash/USB.
- **Mức kích relay**: mặc định `FAN_RELAY_ACTIVE_LEVEL = 1` (active-high, mạch kích transistor/MOSFET). **Module relay opto 4 kênh phổ biến thường là active-low**: đặt `FAN_RELAY_ACTIVE_LEVEL = 0`. Đặt sai thì lúc khởi động **cả 4 relay cùng đóng**: luôn thử với nguồn USB, chưa nối motor, nghe/nhìn đèn relay trước (tắt hết khi quạt tắt, chỉ một relay số đóng khi quạt chạy).
- Mạch kích active-high (NPN/MOSFET) nên có điện trở kéo xuống ≤ 10 kΩ từ chân IN / cực G xuống GND.

### Sơ đồ đấu nối

```
                    ESP32-C3 SuperMini
 HLK-PM01 +5V ───► 5V            GPIO3 ───► IN1  (relay số 1)
 HLK-PM01 GND ───► GND           GPIO4 ───► IN2  (relay số 2)
                                 GPIO5 ───► IN3  (relay số 3)
                                 GPIO7 ───► IN4  (relay trục xoay)
 Module relay VCC ◄── 5V, GND ◄── GND
 Mắt IR: OUT ──► GPIO10,  VCC ◄── 3V3,  GND ◄── GND
```

Phía điện lưới, đấu **3 relay số nối tiếp qua tiếp điểm thường đóng (NC)** để khoá liên động: dù có bao nhiêu relay số đóng cùng lúc thì cũng chỉ một đầu dây số của motor có điện (relay số 1 được ưu tiên).

```
 Pha (L) ──► COM relay 1 ── NO ──► dây số 1 của motor
                          └ NC ──► COM relay 2 ── NO ──► dây số 2 của motor
                                                └ NC ──► COM relay 3 ── NO ──► dây số 3 của motor
                                                                      └ NC     (để trống)
 Pha (L) ──► COM relay 4 ── NO ──► dây động cơ trục xoay (đảo gió)

 Trung tính (N) ──► dây chung của motor quạt và dây còn lại của động cơ trục xoay
 Tụ quạt: giữ nguyên như mạch gốc của quạt
```

- Mạch này thay cho phím bấm cơ (hoặc mạch điều khiển) gốc của quạt: các dây số trước đây nối vào phím số thì nay nối vào NO của relay tương ứng. Tháo hẳn phím cơ khỏi mạch, không để song song.
- Quạt có trục xoay bằng núm kéo cơ (không có động cơ đảo gió riêng) thì không dùng relay 4.

### Dùng relay rắn (SSR)

Dùng được relay rắn (SSR) cho 3 relay số, firmware không cần sửa gì ngoài đặt đúng `FAN_RELAY_ACTIVE_LEVEL`. Số liệu dưới đây lấy theo Omron **G3MB-202P** (loại hay gặp trên module SSR 4 kênh: zero-cross, có snubber, 2 A).

- **Gió tự nhiên không làm hại SSR.** SSR không có tiếp điểm nên không mòn: đổi số 200 lần một giờ hay nhiều hơn cũng không giảm tuổi thọ. Loại zero-cross đóng ở điểm 0 V nên ít xung nhiễu. SSR cũng **không kêu "tách"** mỗi lần đổi số như relay cơ (gió tự nhiên đổi số mỗi 8–30 s, ban đêm nghe khá rõ). Có thể giảm `FAN_NATURAL_MIN_S` / `FAN_NATURAL_MAX_S` nếu muốn gió đổi nhanh hơn.
- **SSR nóng theo dòng tải, không theo số lần đóng cắt.** Sụt áp tối đa 1,6 V: motor quạt 0,2–0,5 A chỉ toả dưới 1 W, không cần tản nhiệt. G3MB-202P chịu 2 A ở 25 °C (giảm dần khi nóng hơn), dư cho motor quạt.
- **Không đấu được khoá liên động NC**, vì SSR không có tiếp điểm thường đóng. Chỉ còn firmware giữ cho 2 số không cùng đóng: SSR nhả chậm nhất ½ chu kỳ + 1 ms (~11 ms ở 50 Hz), thời gian chết 200 ms là dư. SSR hỏng thường bị **chập luôn** (đóng mãi), khi đó đổi sang số khác sẽ cấp điện 2 đầu dây motor cùng lúc: nên lắp **cầu chì** (1–2 A) trên dây pha của motor, và giữ nguyên cầu chì nhiệt trong cuộn dây motor.
- **Relay trục xoay nên giữ relay cơ.** Động cơ trục xoay là động cơ đồng bộ nhỏ (vài W), chỉ ăn vài chục mA, dưới **dòng tải tối thiểu 0,1 A** của G3MB-202P, nên SSR có thể đóng không ổn định. Dòng rò của SSR khi tắt (tối đa 1,5 mA ở 200 VAC, qua mạch snubber) cũng có thể làm động cơ nhỏ này kêu ù nhẹ lúc lẽ ra phải đứng yên. Motor quạt chính thì không bị ảnh hưởng.
- **Kiểm tra mức kích với GPIO 3,3 V.** G3MB-202P loại 5 V cần 4–6 V ở đầu vào, nên GPIO 3,3 V nối thẳng vào đầu vào SSR là không đủ: module phải có transistor đệm. Module kích mức thấp cấp 5 V thì khi GPIO ở mức cao vẫn còn ~1,7 V giữa VCC và chân IN, có module vì vậy tắt không hết. Đặt `FAN_RELAY_ACTIVE_LEVEL` theo module, rồi thử bằng nguồn USB trước khi nối motor: khi quạt tắt, đèn của cả 4 kênh phải tắt; khi quạt chạy, chỉ một kênh số sáng.

⚠️ Mạch dùng điện 220 V: cách ly, đi dây an toàn, cầu chì cho nguồn. Không cắm USB vào máy tính khi mạch đang nối điện lưới (nguồn HLK-PM01 không chung mass với lưới, nhưng phía relay thì có).

## Remote hồng ngoại

### Học lệnh remote

Remote mỗi hãng một mã, nên firmware **học** các nút của remote ngay trên thiết bị (không cần máy tính):

1. **Nhấn nút BOOT 3 lần liên tiếp**. LED bắt đầu nháy **vàng**: số lần nháy trong mỗi chu kỳ = bước đang học.
2. Ở mỗi bước, bấm nút tương ứng trên remote (chĩa vào mắt thu). Học được thì LED chớp **xanh lá** và chuyển sang bước sau.

| Bước | LED nháy | Nút trên remote | Tác dụng |
|---|---|---|---|
| 1 | 1 lần | **Bật/Tắt** (ON/OFF, POWER) | bật (số trước đó) / tắt. Nếu học cả nút Tắt riêng ở bước 4 thì nút này chỉ còn **bật** |
| 2 | 2 lần | **Tốc độ** (SPEED) | quạt tắt: bật lên; đang chạy: số 1 → 2 → 3 → 1 |
| 3 | 3 lần | **Đảo gió** (SWING, OSC) | bật/tắt đảo gió |
| 4 | 4 lần | **Tắt** (OFF) riêng, nếu remote có | tắt |
| 5 | 5 lần | **Số 1**, nếu remote có | chạy số 1 |
| 6 | 6 lần | **Số 2**, nếu remote có | chạy số 2 |
| 7 | 7 lần | **Số 3**, nếu remote có | chạy số 3 |
| 8 | 8 lần | **Gió tự nhiên** (*Nhịp gió*, *Tự nhiên*, *Rhythm*, *Nature*, *Mode*), nếu remote có | bật/tắt [gió tự nhiên](#gió-tự-nhiên) |

- **Nhấn BOOT 1 lần**: bỏ qua bước hiện tại (giữ mã đã học trước đó cho nút này, nếu có).
- **Nhấn BOOT 2 lần**: kết thúc và lưu ngay (remote thông thường chỉ có 3 nút đầu: học xong bước 3 thì nhấn đúp).
- Không bấm gì trong **30 giây** (`IR_LEARN_TIMEOUT_S`): tự kết thúc và lưu những nút đã học.
- Một nút remote chỉ làm một việc: học lại nút đó cho chức năng khác thì nó bị bỏ khỏi chức năng cũ.
- Học lại lúc nào cũng được (nhấn BOOT 3 lần); các bước bỏ qua giữ nguyên mã cũ (cập nhật từ bản trước chưa có bước 8: mã đã học vẫn còn, chỉ cần học thêm bước 8). Mã đã học được lưu trong flash, **factory reset không xoá** (muốn xoá hết: `idf.py erase-flash`).

Trong lúc học, remote không điều khiển quạt. Log in từng bước, ví dụ:

```
I ir_rx: IR learning 1/8: press the POWER button on the remote (not learned yet; click: skip, double click: finish)
I ir_rx: IR NEC address 0x00 command 0x45 (0xBA45FF00)
I ir_rx: Learned POWER
...
I ir_rx: IR learning finished (double click): 3 of 8 remote buttons learned
```

### Remote nào dùng được

- Mắt thu 38 kHz (phần lớn remote quạt, tivi, đầu thu).
- Remote giao thức **NEC** (phổ biến nhất ở remote giá rẻ) được giải mã đầy đủ. Mọi giao thức khác (Panasonic, Sony, Samsung, các loại riêng của hãng quạt...) được nhận theo **hình dạng khung tín hiệu**, nên vẫn học và dùng được.
- Giữ nút trên remote không lặp lệnh: mỗi lần bấm là một lệnh.
- Remote dùng bit đảo (Philips RC5/RC6, hiếm gặp ở quạt) gửi hai mã khác nhau xen kẽ ở mỗi lần bấm, nên chỉ ăn cách một lần bấm.
- Mỗi lần nhận mã, log in `IR NEC ...` hoặc `IR code 0x... (N pulses)`; mã chưa học thì log `Not a learned button`. Lệnh remote được nhận thì LED chớp xanh lá.

## Nút nhấn

| Thao tác (nút BOOT, GPIO9) | Tác dụng |
|---|---|
| Nhấn 1 lần | bật/tắt quạt (bật lại đúng số trước đó) |
| Nhấn 2 lần | mở chế độ ghép nối 15 phút (thêm vào hệ thứ hai), LED nháy xanh dương |
| Nhấn 3 lần | học remote hồng ngoại |
| Giữ 5 s (LED chuyển đỏ) rồi thả | LED nháy đỏ, factory reset: xoá Wi-Fi và ghép nối Matter (giữ trạng thái quạt và mã remote) |

- Có thể nối thêm một nút nhấn ra ngoài vỏ quạt song song với nút BOOT (GPIO9 xuống GND). Đừng giữ nút này lúc cấp điện: chip sẽ vào chế độ nạp firmware.
- **Nút số** (`BUTTON_SPEED_GPIO`), **nút đảo gió** (`BUTTON_SWING_GPIO`) và **nút gió tự nhiên** (`BUTTON_NATURAL_GPIO`) ngoài, tuỳ chọn: mỗi lần nhấn chuyển số (quạt tắt thì bật lên) / bật-tắt đảo gió / bật-tắt gió tự nhiên.

## LED trạng thái

| LED | Ý nghĩa |
|---|---|
| Nháy xanh dương (0,5 s sáng / 0,5 s tắt) | đang chờ ghép nối / đang ghép nối, hoặc Identify (nút *Nhận dạng* trong Apple Home, *Identify* trong Home Assistant) |
| Nháy vàng N lần, nghỉ | đang học remote, bước N |
| Chớp xanh lá | nhận lệnh remote / học xong một nút |
| Đỏ đứng | giữ nút đủ 5 s: thả ra để factory reset |
| Nháy đỏ nhanh | đang factory reset |
| Tắt | bình thường (để không chói khi ngủ). Muốn LED sáng khi quạt chạy (WS2812: xanh lá mờ, LED một màu: sáng hẳn): `STATUS_LED_SHOW_RUNNING` |

SuperMini chỉ có LED một màu: các trạng thái trên chỉ khác nhau ở cách nháy. Mạch không có LED: chọn `STATUS_LED_TYPE_NONE` (khi đó Identify báo "không có đèn").

## Trên ứng dụng

Thiết bị là **Quạt** (Fan, 0x002B) với cluster Fan Control:

| Thuộc tính | Giá trị |
|---|---|
| FanModeSequence | Off / Low / Medium / High (không có Auto) |
| SpeedMax / SpeedSetting / SpeedCurrent | 3 / số đặt 0–3 / số đang chạy 0–3 (MultiSpeed) |
| PercentSetting / PercentCurrent | giá trị ứng dụng ghi (0–100) / số đang chạy: 0, 33, 66, 100 |
| RockSupport / RockSetting | trái-phải / đảo gió (Rocking, tuỳ chọn) |
| WindSupport / WindSetting | gió tự nhiên / gió tự nhiên bật-tắt (Wind, tuỳ chọn) |
| Lệnh Step | tăng/giảm một số |

- **Apple Home**: chạm ô quạt để bật/tắt (bật từ ô quạt: Apple Home gửi 100 %, tức số 3); mở ra là thanh tốc độ và nút **Dao động** (đảo gió). Apple Home không hiển thị Matter Wind (gió tự nhiên). Xem [Đảo gió](#đảo-gió-trục-xoay), [Gió tự nhiên](#gió-tự-nhiên).
- **Home Assistant**: thực thể `fan` có thanh tốc độ, preset low / medium / high / natural_wind và *Oscillate*.
- **Bật quạt mà không chọn số** (FanMode On, Apple Home gửi 100 %): **số 3**, đúng chuẩn Matter. Nút nguồn remote và nút BOOT thì bật lại số trước khi tắt.
- Điều khiển bằng remote hay nút trên mạch thì ứng dụng cập nhật ngay.
- **Cập nhật từ bản cũ** (qua OTA hoặc nạp lại app): bản này bỏ các chức năng không theo chuẩn của các bản trước (nút hướng quay dùng làm đảo gió, công tắc *Tự động* dùng làm gió tự nhiên, thanh tốc độ tự nhảy về 33 / 66 / 100 %). Ứng dụng đã thêm quạt từ trước có thể vẫn hiện các nút cũ: xoá quạt khỏi ứng dụng rồi thêm lại (mở ghép nối bằng nút BOOT nhấn đúp, hoặc factory reset). Trạng thái quạt và mã remote đã học vẫn giữ nguyên.

Với chip-tool (node id `0x7283`, endpoint 1):

```bash
chip-tool fancontrol write percent-setting 45 0x7283 1     # -> SpeedSetting 2: số 2 (PercentSetting vẫn 45)
chip-tool fancontrol write percent-setting 70 0x7283 1     # -> SpeedSetting 3: số 3
chip-tool fancontrol write speed-setting 1 0x7283 1        # -> số 1, PercentSetting 33, FanMode Low
chip-tool fancontrol write fan-mode 2 0x7283 1             # Medium -> số 2
chip-tool fancontrol write fan-mode 4 0x7283 1             # On -> High, số 3
chip-tool fancontrol write fan-mode 0 0x7283 1             # tắt
chip-tool fancontrol write rock-setting 1 0x7283 1         # đảo gió bật (0 = tắt)
chip-tool fancontrol write wind-setting 2 0x7283 1         # gió tự nhiên bật (0 = tắt)
chip-tool fancontrol step 0 0x7283 1 --Wrap false --LowestOff true   # tăng một số
chip-tool fancontrol read percent-current 0x7283 1
```

## Cấu hình

`idf.py menuconfig`:

| Menu | Tuỳ chọn | Mặc định | Ý nghĩa |
|---|---|---|---|
| Fan Relays | `FAN_RELAY_SPEED1_GPIO` / `2` / `3` | 3 / 4 / 5 | chân relay số 1, 2, 3 |
| | `FAN_RELAY_SWING_GPIO` | 7 | chân relay trục xoay |
| | `FAN_RELAY_ACTIVE_LEVEL` | 1 | 1 = active-high, 0 = active-low (module relay opto thường là 0) |
| | `FAN_RELAY_DEAD_TIME_MS` | 200 | thời gian chờ giữa nhả relay số cũ và đóng relay số mới |
| Fan Behavior | `FAN_POWER_ON_STATE` | khôi phục | trạng thái khi có điện lại: khôi phục / luôn tắt |
| | `FAN_MATTER_ROCKING` | y | đảo gió dạng Rocking (nút *Dao động* / *Oscillate*) |
| | `FAN_MATTER_WIND_NATURAL` | y | gió tự nhiên trên Matter: Wind / `WindSetting` (preset *natural_wind* của Home Assistant) |
| | `FAN_NATURAL_MIN_S` / `FAN_NATURAL_MAX_S` | 8 / 30 | gió tự nhiên: thời gian giữ một số ngắn nhất / dài nhất (giây) |
| IR Receiver | `IR_RX_GPIO` | 10 | chân OUT của mắt thu, -1 = không có mắt thu |
| | `IR_LEARN_TIMEOUT_S` | 30 | tự kết thúc học remote sau chừng này giây không bấm gì |
| Buttons | `APP_BUTTON_GPIO` | 9 | nút chính (BOOT), -1 = không có |
| | `BUTTON_SPEED_GPIO` / `BUTTON_SWING_GPIO` / `BUTTON_NATURAL_GPIO` | -1 | nút số / nút đảo gió / nút gió tự nhiên ngoài |
| Status LED | `STATUS_LED_TYPE` | GPIO | không có / LED một màu / WS2812 |
| | `STATUS_LED_GPIO` | 8 | |
| | `STATUS_LED_ACTIVE_LEVEL` | 0 | LED một màu: 0 = sáng ở mức thấp (SuperMini) |
| | `STATUS_LED_SHOW_RUNNING` | n | LED sáng khi quạt chạy (WS2812: xanh lá mờ) |
| Device Information | `DEVICE_VENDOR_NAME` | Haxeko | nhà sản xuất |
| | `DEVICE_PRODUCT_NAME` | Smart Fan | kiểu máy |
| | `DEVICE_DISPLAY_NAME` | Smart Fan | tên mặc định khi thêm vào ứng dụng, tên quảng bá trước khi ghép nối |
| | `DEVICE_HARDWARE_VERSION_STRING` | ESP32-C3 SuperMini | phiên bản phần cứng |

Hoặc sửa `sdkconfig.defaults` rồi xoá `sdkconfig` để áp dụng lại. ESP32-C3-DevKitM-1: chọn `STATUS_LED_TYPE_WS2812`.

## Build

Yêu cầu: ESP-IDF v6.0.2 + esp-matter release/v1.6 (đã `export.sh` cả hai). Cách nhanh nhất là dùng Docker. Mount **cả repo** (không chỉ thư mục dự án) để mỗi commit mới tự lấy giờ build mới cho số phiên bản:

```bash
# chạy từ bất kỳ thư mục nào trong repo
docker run --rm -it -v "$(git rev-parse --show-toplevel)":/repo \
  espressif/esp-matter:release-v1.6.1_idf_v6.0.2 \
  bash -c "git config --global --add safe.directory /repo && cd /repo/ESP32-C3/matter-wifi/matter-fan-3-speed && { [ -f sdkconfig ] || idf.py set-target esp32c3; } && idf.py build"
```

Hoặc cài local:

```bash
. ~/esp/esp-idf/export.sh
. ~/esp/esp-matter/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 erase-flash flash monitor
```

SuperMini nạp và xem log qua cổng USB có sẵn (`/dev/ttyACM0`, Windows `COMx`). Nếu không nạp được: giữ BOOT, nhấn RST, thả BOOT.

### File firmware

| File trong `build/` | Dùng để |
|---|---|
| `matter_fan_3speed-<phiên bản>.bin` | nạp một lần ở địa chỉ `0x0` (bootloader + bảng phân vùng + app). Xoá cả vùng NVS nên phải ghép nối lại và học lại remote |
| `matter_fan_3speed.bin` | file app, dùng với `idf.py flash` / `write-flash @flash_args` (giữ ghép nối và mã remote) |
| `matter_fan_3speed-ota.bin` | cập nhật OTA qua Matter |

```bash
esptool --chip esp32c3 -p /dev/ttyACM0 write-flash 0x0 build/matter_fan_3speed-1.0.2610141530.bin
```

GitHub Actions (`.github/workflows/matter-fan-3-speed.yml` ở gốc repo) chạy test trên máy tính, build và upload artifact `matter_fan_3speed-<phiên bản>` (các file trên cùng `flash_args`, `bootloader/`, `partition_table/`) và `matter_fan_3speed-<phiên bản>-debug` (`.elf`/`.map` để giải mã địa chỉ khi crash). Vào tab **Actions** → lần chạy mới nhất → **Artifacts**.

### Phiên bản firmware

Mỗi lần build, firmware tự có phiên bản dạng `1.0.<yyMMddHHmm>` (giờ build theo UTC), ví dụ `1.0.2610141530`. Xem ở Apple Home (phụ kiện → cài đặt → **Firmware**) hoặc dòng `App version:` trong log khởi động. Số phiên bản Matter dùng cho OTA (`SoftwareVersion`) là phần `yyMMddHHmm`, nên luôn tăng. Build local: commit mới (hoặc checkout) tự lấy phiên bản mới ở lần build sau; chỉ sửa file thì chạy `idf.py reconfigure`.

### OTA

- Mỗi bản build tạo thêm `build/matter_fan_3speed-ota.bin` để đưa vào một OTA provider (ví dụ `chip-ota-provider-app`).
- **Rollback**: image mới khởi động ở trạng thái chờ xác nhận, và chỉ được xác nhận khi đã kết nối Wi-Fi liên tục 60 s mà vẫn còn các ghép nối. Image mới crash hoặc khởi động lại trước đó thì bootloader tự quay về bản cũ.
- Bản ghi trạng thái quạt và mã remote trong NVS chỉ được thêm trường ở cuối; từ bản này trở đi, firmware đọc được cả bản ghi do bản mới hơn ghi (rollback không mất dữ liệu). Rollback từ bản này về các bản cũ hơn (trước khi có gió tự nhiên) thì bản cũ không đọc được bản ghi mới: quạt khởi động như lần đầu và phải học lại remote.

## Ghép nối

Firmware dùng chứng chỉ test (DAC test) của esp-matter, mã ghép nối mặc định:

| | |
|---|---|
| Mã QR | `MT:Y.K9042C00KA0648G00` |
| Mã thủ công | `3497-011-2332` |
| Passcode / Discriminator | `20202021` / `3840` |
| Tên Bluetooth khi chờ ghép nối | `SmartFan-3840` |

Tạo ảnh QR: `https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00`.

1. Cấp điện lần đầu (hoặc sau factory reset): LED nháy xanh dương, cửa sổ ghép nối BLE mở 15 phút.
2. Apple Home: **+ → Thêm phụ kiện → quét mã QR** (hoặc nhập mã thủ công). Gặp cảnh báo phụ kiện chưa được chứng nhận thì chọn **Vẫn thêm**. iPhone gửi tên/mật khẩu Wi-Fi 2,4 GHz cho quạt qua Bluetooth.
3. Home Assistant / Google Home / SmartThings / Alexa: *Add Matter device*, quét cùng mã. Google/SmartThings/Alexa cần bật chế độ cho thiết bị chưa chứng nhận (developer/test) theo hướng dẫn của từng hãng.

- **Thêm vào hệ thứ hai** (ví dụ đã có Apple Home, muốn thêm Home Assistant): nhấn đúp BOOT, LED nháy xanh dương, rồi trong 15 phút thêm thiết bị ở hệ mới bằng mã gốc ở trên (điện thoại cùng mạng Wi-Fi với quạt). Hoặc dùng chức năng chia sẻ của hệ đang quản lý (Apple Home: *Bật chế độ ghép nối*; HA: *Share device*).
- **Xoá khỏi hệ cuối cùng**: quạt tự factory reset và mở lại cửa sổ ghép nối BLE 15 phút với mã gốc. Nếu lúc đó đang mở chế độ ghép nối (để thêm vào hệ khác) thì quạt chờ cửa sổ ghép nối đóng: có hệ mới ghép vào thì giữ nguyên, không có thì factory reset. Quạt vẫn chạy như trước, mã remote vẫn còn.
- Mọi mạch nạp firmware này dùng **chung mã ghép nối**: có nhiều mạch thì ghép **từng mạch một**.
- Firmware dùng chứng chỉ test (VID `0xFFF1`, PID `0x8000`), chỉ hợp cho DIY.

Với chip-tool:

```bash
chip-tool pairing ble-wifi 0x7283 <SSID> <mật-khẩu> 20202021 3840
chip-tool fancontrol write fan-mode 2 0x7283 1     # Medium = số 2
chip-tool basicinformation read serial-number 0x7283 0
```

## Kiểm thử trên máy tính

`test/host/` chạy logic quạt và bộ giải mã hồng ngoại trên máy tính (chỉ cần `gcc`), không cần ESP32 hay ESP-IDF:

```bash
test/host/run.sh
```

| Test | Kiểm tra |
|---|---|
| `test_fan_logic.c` | số ↔ % khớp với quy tắc Fan Control mà cluster Matter áp dụng (SpeedSetting = làm tròn lên, PercentSetting = làm tròn xuống, FanMode Low/Medium/High); bật/tắt nhớ số cũ, chuyển số vòng, đặt số, relay đảo gió không bao giờ đóng khi quạt tắt (thử mọi trạng thái × mọi lệnh), lệnh Step (wrap, lowest off), dữ liệu lưu bị hỏng; gió tự nhiên độc lập với số (WindSetting không bật quạt, đổi số vẫn giữ gió tự nhiên, tắt quạt vẫn nhớ), nhịp gió tự nhiên (trong khoảng của số đang đặt, motor không dừng, luôn đổi số, thời gian giữ trong giới hạn); bản ghi NVS (bố cục byte, phiên bản 1, bản ghi dài hơn của bản mới, bản ghi hỏng) |
| `test_ir.c` | chuyển dữ liệu RMT thành xung (bỏ mức nghỉ đầu khung, gộp đoạn cùng mức, cắt khung quá dài); giải mã NEC với méo của mắt thu và nhiễu thời gian, khung lặp NEC, khung hỏng; mã theo hình dạng (Sony, Samsung) ổn định với nhiễu và khác nhau giữa các nút; lọc nhiễu; giữ nút không lặp lệnh; bảng nút đã học (POWER thành ON khi có nút OFF, học lại chuyển chức năng, nút NATURAL); bản ghi mã remote (bố cục, phiên bản 1 có 7 nút, bản ghi dài hơn, bản ghi hỏng) |

Phần relay, Matter, Wi-Fi và BLE vẫn phải thử trên thiết bị thật.

## Cấu trúc

```
CMakeLists.txt             project esp-matter (esp32c3), phiên bản theo giờ build, file firmware đầy đủ mang tên phiên bản
sdkconfig.defaults         Matter over Wi-Fi, BLE tối giản, OTA + rollback, watchdog, cluster không dùng
partitions.csv             4 MB, 2 phân vùng OTA
main/app_main.cpp          khởi tạo, node Matter, sự kiện ghép nối/LED, Identify, xác nhận OTA, factory reset
main/fan_matter.cpp        endpoint Fan, delegate Fan Control <-> task quạt (SpeedSetting, Rocking, Wind)
main/fan_ctrl.cpp          task quạt: hàng đợi lệnh, gió tự nhiên, lưu trạng thái sau 3 s, chống khởi động lỗi lặp lại
main/relays.cpp            relay số và relay trục xoay (ngắt trước - đóng sau, thời gian chết), kiểm tra chân GPIO khi build
main/fan_store.cpp         đọc/ghi trạng thái quạt trong NVS
main/fan_record.c          định dạng bản ghi trạng thái quạt: phiên bản 2, đọc được phiên bản 1 và bản ghi mới hơn (thuần C, có test)
main/fan_logic.c           máy trạng thái quạt (thuần C, có test)
main/ir_rx.cpp             mắt thu hồng ngoại (RMT), học remote, lưu mã
main/ir_decode.c           giải mã NEC, mã theo hình dạng khung, lọc lặp phím (thuần C, có test)
main/ir_table.c            bảng nút remote đã học -> lệnh quạt, định dạng bản ghi mã remote (thuần C, có test)
main/buttons.cpp           nút BOOT (1/2/3 lần, giữ 5 s), nút số / đảo gió / gió tự nhiên ngoài
main/status_led.cpp        LED trạng thái (GPIO hoặc WS2812)
main/device_info.cpp       nhà sản xuất, kiểu máy, tên, phần cứng, số sê-ri (MAC Wi-Fi)
main/chip_project_config.h tên và loại thiết bị khi quảng bá trước ghép nối (DNS-SD)
main/Kconfig.projbuild     menu Fan Relays / Fan Behavior / IR Receiver / Buttons / Status LED / Device Information
test/host/                 test trên máy tính (run.sh)
```
