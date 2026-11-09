# matter-ws2812-1ch: bo điều khiển LED WS2812 1 kênh (ESP32-C6-MINI-1, nguồn 5–12 V)

Bo mạch KiCad 9 dùng module **ESP32-C6-MINI-1** để điều khiển một dải LED WS2812/WS2812B (5 V) hoặc WS2811/WS2815 (12 V). ESP32-C6 có Wi‑Fi 6, BLE 5 và radio IEEE 802.15.4, nên firmware viết sau có thể chạy **Matter over Wi‑Fi** hoặc **Matter over Thread**. Bo có:

- **1 ngõ ra LED** với cầu đấu `+ D −`.
- **1 nút** để vào chế độ kết nối và factory reset.
- **Công tắc gạt 3 nấc** để chọn số endpoint Matter: chia dải LED thành 1, 2 hoặc 3 đoạn, mỗi đoạn là một đèn riêng.
- **Header J7** đưa ra 1 GPIO trống (IO18) kèm 3V3 và GND.
- **6 pad nạp UART0** ở mặt dưới, và cổng USB‑C để nạp code/debug.

![Render 3D](docs/images/render_iso.png)

### Thay đổi ở r3.0

- Chỉ còn **1 ngõ ra LED** (J2). Bỏ 2 cầu đấu kênh 2/3.
- Header J7 còn **1 GPIO**: 1×3 chân `3V3 IO18 GND`.
- **Board còn 39 × 33 mm**, giảm khoảng 33 % diện tích so với r2.0 (48 × 40 mm) và khoảng 76 % so với r1.1 (84 × 63 mm).
- IC dịch mức đổi sang **74AHCT1G125** (1 cổng, SOT-23-5), điện trở 100 Ω rời.
- TVS nhỏ hơn: **SMF15A** (200 W). Tụ lọc VLED **47 µF 25 V** (5 × 5,4 mm).
- Cầu đấu, công tắc gạt và header J7 cùng nằm ở mép dưới. Module vẫn ở mép trên, anten chìa ra ngoài.
- Thư mục project đổi từ `matter-ws2812-3ch` thành `matter-ws2812-1ch`; file KiCad đổi tên thành `c6_ws2812_1ch.*`.

## Thông số

| Mục | Giá trị |
|---|---|
| Vi điều khiển | ESP32-C6-MINI-1-N4: Wi‑Fi 6 2,4 GHz, BLE 5, 802.15.4 (Thread/Zigbee), flash 4 MB |
| Nguồn vào | 5–12 V DC, cầu đấu 3,5 mm. Nguồn này cấp thẳng cho dải LED: dải 5 V cắm nguồn 5 V, dải 12 V cắm nguồn 12 V |
| Dòng | Tối đa **5 A**. Trên bo **không có cầu chì**: dùng nguồn có bảo vệ ngắn mạch (xem mục nguồn) |
| Bảo vệ | Chống cắm ngược bằng MOSFET P SiSS27DN (−30 V, tối đa 9 mΩ ở −4,5 V), TVS SMF15A, tụ lọc 47 µF |
| 3,3 V | Buck AP63203 (vào 3,8–32 V, 2 A) |
| Mức tín hiệu DATA | 5 V, qua 74AHCT1G125 và điện trở 100 Ω. IC này được cấp từ LDO AP7381‑5.0 lấy điện từ nguồn LED |
| USB-C | Nạp code và debug qua USB Serial/JTAG của C6 (IO12/IO13). Có chống ESD USBLC6-2SC6. USB chỉ nuôi phần logic, không bao giờ cấp điện cho dải LED |
| Giao diện | Nút LINK/RESET (GPIO9), LED xanh báo trạng thái (GPIO8), công tắc 3 nấc (GPIO21/22/23), header J7 (GPIO18), 6 pad UART0 ở mặt dưới |
| PCB | 39 × 33 mm, 2 lớp, dày 1,6 mm. Kể cả phần anten chìa ra: 39 × 38,3 mm |

## Cấu trúc thư mục

| Thư mục | Nội dung |
|---|---|
| `hardware/` | Project KiCad 9: `c6_ws2812_1ch.kicad_pro/.kicad_sch/.kicad_pcb`, thư viện footprint riêng `.pretty` (module C6 có via GND, pad nạp, USB‑C và công tắc gạt đã cắt silkscreen ở mép board), mô hình 3D dạng hộp trong `.3dshapes` |
| `fab/` | `c6_ws2812_1ch_gerbers.zip` để upload, thư mục `gerbers/`, BOM, file toạ độ (CPL), PDF sơ đồ |
| `docs/images/` | Ảnh render 3D, lớp đồng trên/dưới, sơ đồ nguyên lý |
| `tools/` | Script Python sinh toàn bộ project: `design.py` (linh kiện và net), `layout.py` (vị trí), `build_all.sh` |

## Sơ đồ nguyên lý

![Sơ đồ](docs/images/schematic.png)

Bản đầy đủ ở `fab/c6_ws2812_1ch_schematic.pdf`. Sơ đồ có các khối sau:

1. **Nguồn vào:** J1 → Q1 (MOSFET P chống cắm ngược, D4 kẹp Vgs ở 15 V, R1 10k kéo gate xuống GND) → đường **VLED**. TVS D3 và C1 47 µF đặt trên VLED.
2. **Ngõ ra:** VLED nối thẳng vào chân `+` của J2.
3. **3,3 V:** VLED qua D1 và VBUS (USB) qua D2 cùng đổ vào V_SYS (hai diode Schottky B5819W). V_SYS vào buck AP63203 (L1 3,9 µH, tụ vào 10 µF và 100 nF, tụ ra 2 × 22 µF).
4. **5 V cho IC dịch mức:** LDO AP7381‑5.0 chịu được 40 V. Khi nguồn vào là 5 V, LDO ra khoảng 4,8–4,9 V (vẫn trong dải 4,5–5,5 V của 74AHCT1G125). Khi chỉ cắm USB, IC dịch mức không có điện, nên không có dòng chạy vào chân DATA của dải LED đang tắt. Ngõ vào họ AHCT chịu được 5,5 V cả khi IC không có điện.
5. **Dịch mức:** GPIO14 → 74AHCT1G125 (ngõ vào có R9 10k kéo xuống để LED không nhận rác khi ESP đang khởi động; chân OE nối GND) → R10 100 Ω → chân `D` của J2.
6. **ESP32-C6:** tụ 100 nF sát chân 3V3 (hai tụ 22 µF của buck nằm cách khoảng 4 mm), mạch RC 10k + 1 µF ở chân EN, R6 10k kéo IO8 lên.
7. **USB-C:** CC1/CC2 nối điện trở 5,1k xuống GND, D+/D− đi qua USBLC6-2SC6.
8. **Mở rộng:** J7 đưa ra `3V3, IO18, GND`.

## Gán chân

| GPIO | Chân module | Chức năng | Ghi chú |
|---|---|---|---|
| IO14 | 19 | DATA dải LED | Không có pull sau reset; R9 10k kéo xuống |
| IO9 | 23 | Nút LINK/RESET (cũng là BOOT) | Pull-up nội, nhấn = 0 |
| IO8 | 22 | LED trạng thái, 0 = sáng | Chân strap, R6 10k kéo lên |
| IO21 / IO22 / IO23 | 27 / 28 / 29 | Chân 1 / 2 / 4 của công tắc SW2 = nấc 1 / 2 / 3 | Chân 3 (chung) của SW2 nối GND |
| IO18 | 24 | J7 chân 2 | |
| IO12 / IO13 | 17 / 18 | USB D− / D+ | |
| RXD0 / TXD0 | 30 / 31 | UART0 RX / TX | Ra pad nạp |
| IO0–IO7, IO15, IO19, IO20 | | Để trống | |

Các chân strap của ESP32-C6 đều ở trạng thái an toàn khi khởi động:

- **IO9 = 1, IO8 = 1:** chạy firmware bình thường. Giữ nút (IO9 = 0) lúc reset thì vào chế độ nạp; lúc đó IO8 phải bằng 1, nên có R6 kéo lên. Mạch LED nối từ 3V3 qua R8 vào IO8, không kéo chân này xuống.
- **IO4/IO5 (MTMS/MTDI)** và **IO15** để trống. IO15 chỉ có tác dụng strap khi đã đốt eFuse JTAG (mặc định chưa đốt).

## Header J7: 1 GPIO tự do

| Chân J7 | Tín hiệu | Lưu ý |
|---|---|---|
| 1 | 3V3 | Lấy từ buck AP63203; nên dùng dưới khoảng 300 mA cho thiết bị ngoài |
| 2 | IO18 | GPIO thường |
| 3 | GND | |

- IO18 là chân SDIO của C6 (bo không dùng SDIO). Có pull-up yếu chỉ trong lúc reset; sau reset không có pull nào.
- Mức logic 3,3 V, **không chịu 5 V**. Nên dùng dưới khoảng 20 mA. Tải lớn hơn (relay, quạt, đèn) thì qua transistor/MOSFET.
- Dùng được làm ngõ vào nút bấm/cảm biến (ví dụ cảm biến chuyển động PIR, DS18B20), ngõ ra điều khiển relay qua transistor, PWM (LEDC), hoặc một dây 1-Wire.
- Header bước 2,54 mm ở mép dưới, giữa cầu đấu J2 và công tắc gạt. Chân 1 (3V3) là pad vuông, ở phía trên. Tên chân in ở mặt dưới (`J7: 3V3 IO18 GND`).

## Nút LINK/RESET (gợi ý cho firmware)

- **Nhấn nhanh:** bật/tắt đèn, hoặc mở lại cửa sổ kết nối Matter.
- **Giữ ≥ 5 giây rồi nhả:** factory reset, xoá Wi‑Fi/Thread và thông tin ghép nối.
- **Giữ nút trong lúc cắm điện:** ESP32‑C6 vào chế độ nạp ROM (GPIO9 = 0 khi reset). Dùng để cứu mạch khi firmware lỗi.

LED xanh (GPIO8) dùng để báo trạng thái, ví dụ: nháy chậm khi chờ kết nối, nháy nhanh khi đang giữ nút để reset, sáng một lúc khi kết nối xong.

## Công tắc gạt 3 nấc: số endpoint

Bo chỉ có một ngõ ra, nên công tắc dùng để **chia dải LED thành 1, 2 hoặc 3 đoạn**. Mỗi đoạn là một endpoint Matter (một đèn riêng trong app: bật/tắt, độ sáng, màu riêng). Ví dụ: một dải chạy quanh phòng chia thành 3 vùng. Số LED mỗi đoạn đặt trong firmware.

SW2 là **C&K PCM13SMTR**. Bảng "switch function" trong datasheet C&K: **nấc 1 nối chân 1‑3, nấc 2 nối 2‑3, nấc 3 nối 3‑4**, tức chân 3 là chân chung. Bo nối:

- **Chân 3** của SW2 nối GND.
- **Chân 1, 2, 4** vào **IO21, IO22, IO23**.

Firmware bật pull-up nội cho 3 GPIO rồi đọc: chân nào bằng 0 thì công tắc đang ở nấc tương ứng.

```c
#include "driver/gpio.h"
#include "esp_rom_sys.h"

/* SW2 chân 1, 2, 4 -> nấc 1, 2, 3 (chân 3 là chân chung, nối GND) */
static const gpio_num_t EP_PIN[3] = {GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23};

/* Trả về 1, 2 hoặc 3 endpoint; 0 nếu công tắc đang nằm giữa hai nấc */
int read_endpoint_switch(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << GPIO_NUM_21) | (1ULL << GPIO_NUM_22) | (1ULL << GPIO_NUM_23),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    esp_rom_delay_us(50);
    for (int i = 0; i < 3; i++) {
        if (gpio_get_level(EP_PIN[i]) == 0) {
            return i + 1;
        }
    }
    return 0;
}
```

- Nên đọc công tắc **một lần lúc khởi động**. Danh sách endpoint là một phần của thiết bị đã ghép nối Matter. Nên khi số endpoint đổi so với lần trước (lưu trong NVS), firmware nên yêu cầu factory reset và ghép lại thay vì đổi ngầm.
- Công tắc nằm ở mép dưới, góc phải. Vạch `1 2 3` in phía trên công tắc: gạt sang trái (về phía J7) là nấc 1, gạt sang phải là nấc 3.
- Cần gạt nhô ra khoảng 0,75 mm khỏi mép dưới board, đủ để gạt bằng móng tay hoặc đầu bút.
- Nếu không cần chia đoạn, chỉ cần để công tắc ở nấc 1. Muốn bo nhỏ hơn nữa thì có thể bỏ hẳn công tắc trong bản sau.

## Gợi ý cho firmware

- Một dải LED chỉ cần **1 kênh RMT TX** (`led_strip` backend RMT).
- Firmware `ESP32-C3/matter-wifi/matter-rgb-light-ws2812` trong repo có thể dùng cho bo này ở chế độ 1 endpoint: `idf.py set-target esp32c6`, đặt `CONFIG_LED_GPIO=14`, giữ `CONFIG_APP_BUTTON_GPIO=9`. Chưa build thử cho C6.
- Với Matter over Thread, cần một Thread Border Router trong nhà (ví dụ HomePod mini, Apple TV 4K, Google Nest Hub 2, hoặc OTBR tự dựng).

## Nguồn và dòng

- **Chọn nguồn theo dải LED:** WS2812/WS2812B cần 5 V, WS2811/WS2815 loại 12 V cần 12 V. Mức DATA luôn là 5 V, hợp với cả hai loại.
- **Không có cầu chì trên bo.** Nếu dây ra dải LED bị chập, dòng chỉ bị giới hạn bởi nguồn. Nên dùng nguồn có bảo vệ ngắn mạch (phần lớn nguồn tổ ong có, nhưng nên xem thông số của nguồn). Với nguồn lớn hoặc dây ra mảnh, nên gắn cầu chì trên dây.
- **Tụ ở đầu dải LED:** C1 trên bo chỉ 47 µF. Nên gắn thêm tụ 470–1000 µF ngay đầu vào dải LED, như khuyến cáo thông thường cho WS2812.
- **Đường đồng:** VIN và VLED là vùng đồng rộng khoảng 3,5–4 mm ở lớp trên (J1 → Q1 → chân `+` của J2). GND là mặt đồng liền ở lớp dưới. Theo ước tính IPC‑2221 với đồng 1 oz, đường VLED nóng thêm khoảng 7 °C khi chạy 5 A.
- **Tổn hao Q1:** SiSS27DN tối đa 9 mΩ ở Vgs = −4,5 V, nên ở 5 A chỉ khoảng 0,25 W.
- **Cầu đấu 3,5 mm:** loại phổ biến (KF350, Phoenix PT 1,5) chịu khoảng 8–10 A mỗi chân; xem datasheet loại mua được. Dây 0,5–1,5 mm².
- **Dải LED dài:** cấp nguồn thêm ở giữa hoặc ở cuối dải (power injection) trực tiếp từ nguồn. Không kéo toàn bộ dòng qua bo.
- **Cắm USB khi đã có nguồn LED:** an toàn. Diode D1/D2 chọn nguồn cao hơn cho phần logic, và USB không bao giờ nối vào VLED.

## Bố trí PCB

| Lớp trên | Lớp dưới |
|---|---|
| ![top](docs/images/copper_top.png) | ![bottom](docs/images/copper_bottom.png) |

- **Mép trên:** USB‑C (góc trái), buck 3,3 V, rồi module ESP32-C6. Mép trên của board trùng chỗ bắt đầu vùng anten của module, nên toàn bộ anten nằm ngoài board, không có đồng bên dưới. Khi làm hộp, không đặt kim loại hoặc dây điện sát phần anten.
- **Mép dưới:** cầu đấu nguồn J1, cầu đấu LED J2, header J7, công tắc gạt SW2. Mọi dây nối đều đi ra một phía.
- **Đường công suất:** đi theo vùng đồng, không dùng đường mảnh: J1 → Q1 → vùng VLED → chân `+` của J2. Chân cầu đấu và Q1 nối đặc vào vùng đồng (không dùng thermal relief).
- **Đường DATA:** từ IO14 qua 74AHCT1G125 (nằm phía trên cầu đấu J2) và R10 xuống chân `D`. Đoạn sau R10 đi thẳng ở lớp trên; hai đoạn ngắn trước đó chui xuống lớp dưới để băng qua đường nguồn.
- **GND:** mặt GND lớp dưới gần như liền. 4 via trong pad giữa của module, 11 via khâu và các via cạnh chân GND nối mảng GND lớp trên xuống lớp dưới.
- **Pad nạp UART0:** 6 pad Ø1,5 mm, bước 2,54 mm, ở **mặt dưới, dưới thân module** (`3V3 EN GND IO9 RX0 TX0`). Tên từng pad in ở mặt dưới; nhìn từ mặt dưới, thứ tự từ trái sang phải là TX0 → 3V3.

![Render mặt trên](docs/images/render_top.png)

![Render mặt dưới (pad nạp dưới thân module)](docs/images/render_bottom.png)

## Nạp firmware

1. **Qua USB-C (khuyên dùng):** cắm cáp, chạy `idf.py -p /dev/ttyACM0 flash monitor`. Chip mới hoặc firmware treo: giữ nút LINK/RESET rồi mới cắm cáp.
2. **Qua pad UART0:** dùng mạch USB‑UART 3,3 V nối vào pad mặt dưới (TX0 → RX, RX0 → TX, chung GND). Có thể dùng kim pogo hoặc hàn tạm dây. Nối IO9 với GND, chạm EN xuống GND rồi nạp.
3. Không cần nguồn LED khi nạp; USB tự nuôi phần logic. Khi nạp qua pad UART, cấp 3,3 V vào pad `3V3` hoặc cắm USB‑C, không cấp cả hai cùng lúc.

## Đặt làm mạch

- Upload `fab/c6_ws2812_1ch_gerbers.zip`: 2 lớp, 1,6 mm, đồng 1 oz.
- Luật dùng: đường tối thiểu 0,25 mm, khoảng hở tối thiểu 0,2 mm (VIN/VLED 0,25 mm), via 0,6/0,3 mm, đồng cách mép board 0,4 mm. Đều nằm trong khả năng tiêu chuẩn của JLCPCB/PCBWay.
- Lắp ráp SMT: dùng `fab/c6_ws2812_1ch-BOM.csv` và `fab/c6_ws2812_1ch-pos.csv`. Gốc toạ độ là góc dưới trái board. Một số linh kiện có thể bị lệch góc xoay trong trình xem của nhà mạch; kiểm tra lại ở bước preview.
- Linh kiện cắm (cột `Mount = THT`: 2 cầu đấu, header J7) hàn tay sau. Nút và công tắc gạt đều là linh kiện dán.
- Cột LCSC có mã cho các linh kiện đã đối chiếu: AP63203WU‑7 C780769, USBLC6‑2SC6 C7519, HRO TYPE‑C‑31‑M‑12 C165948, B5819W C8598, C&K PCM13SMTR C221842, XKB TS‑1187A C318884, LED xanh C72043, và các điện trở/tụ 0603 loại basic thông dụng. Các dòng còn trống thì chọn theo MPN.
- Bo dày đặc nên nhiều ký hiệu linh kiện (khối buck U2/L1/C3–C6, Q1, D1–D3, R1–R4, U3, …) không in được trên silkscreen; vị trí đầy đủ có trong file CPL và lớp Fab của PCB.

## Cần kiểm tra trước khi đặt

1. **Công tắc gạt SW2:** footprint `SW_SP3T_PCM13` của KiCad khớp bản vẽ "PC mounting" trong datasheet C&K (4 chân, cách nhau 1,5 / 3,0 / 1,5 mm). Kiểu nối 1‑3 / 2‑3 / 3‑4 lấy từ bảng "switch function" của datasheet. Nếu firmware đọc ra nấc bị đảo chiều, chỉ cần đảo bảng tra.
2. **Q1:** SiSS27DN (Vishay). Nếu thay, chọn MOSFET P vỏ PowerPAK 1212‑8 cùng sơ đồ chân (1–3 S, 4 G, pad lớn D), VDS ≥ 20 V, VGS ±20 V, RDS(on) ≤ 10 mΩ ở −4,5 V.
3. **Cầu đấu:** footprint Phoenix PT 1,5/x‑3,5‑H (lỗ 1,2 mm, bước 3,5 mm), lỗ luồn dây quay ra mép dưới board. Loại KF350‑3.5 phổ biến có cùng bước chân.
4. **Module ESP:** footprint ESP32‑C6‑MINI‑1 của KiCad, thêm 4 via GND 0,3 mm (phủ mask) ở khe giữa các ô của pad giữa. Vùng cấm đồng của anten thu hẹp còn ±12 mm quanh module và nằm hẳn ngoài board. Các chân GND quanh module nối qua mảng GND lớp trên dưới bụng module; riêng pad góc 51 (kẹp giữa IO18 và IO19) có đường đi riêng.
5. **Mô hình 3D:** thư viện 3D của KiCad 9.0.9 chưa có mô hình cho module C6, USB‑C HRO, nút TS‑1187A và MOSFET PowerPAK. Script sinh hộp đơn giản thay thế (`hardware/c6_ws2812_1ch.3dshapes`), chỉ để xem render.

## Trạng thái kiểm tra

- **ERC (KiCad 9.0.9):** 0 lỗi, 0 cảnh báo.
- **Netlist:** `tools/check_netlist.py` so netlist KiCad xuất ra với `design.py`. Khớp 27/27 net, mọi chân đúng net, các chân bỏ trống có cờ no-connect.
- **DRC (KiCad 9.0.9, `--schematic-parity`):** 0 vi phạm, 0 kết nối thiếu, 0 lệch giữa PCB và sơ đồ.
- **Đi dây:** dùng bộ đi dây lưới 2 lớp viết riêng (`tools/autoroute.py` + `tools/astar.c`). Khoảng hở được tính chính xác từ đa giác pad của KiCad, sau đó DRC kiểm lại toàn bộ. Nên mở PCB trong KiCad xem lại một lượt trước khi đặt, vì vài đường tín hiệu còn rút ngắn được bằng tay.

## Tái tạo / chỉnh sửa

```bash
cd matter-ws2812-1ch/tools
./build_all.sh      # sơ đồ -> ERC -> so netlist -> PCB -> đi dây -> DRC -> file sản xuất (vài phút)
```

Cần KiCad 9.0 (`kicad-cli` và module Python `pcbnew`), Python 3 có `numpy` và `Pillow`, và `gcc`.

- Linh kiện và net sửa trong `tools/design.py`.
- Vị trí linh kiện, vùng đồng và chữ silkscreen sửa trong `tools/layout.py`.

Có thể sửa trực tiếp trong KiCad, nhưng chạy lại script sẽ ghi đè các sửa tay.

Các bản trước vẫn còn trong lịch sử git: r2.0 (3 kênh, ESP32-C6, 48 × 40 mm, commit `ad3b83e`) và r1.1 (3 kênh, ESP32-C3-WROOM-02, 84 × 63 mm, commit `2404e9e`).
