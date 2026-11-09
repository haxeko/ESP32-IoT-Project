# matter-ws2812-3ch: bo điều khiển LED WS2812 3 kênh (ESP32-C3-WROOM-02, nguồn 5–12 V)

Bo mạch KiCad 9 dùng ESP32-C3-WROOM-02 để điều khiển dải LED WS2812/WS2812B (5 V) hoặc WS2811/WS2815 (12 V), dự kiến chạy firmware **Matter over Wi‑Fi** (viết sau). Bo có:

- **3 kênh ra độc lập**, mỗi kênh có cầu đấu `+ D −`.
- **1 nút** để vào chế độ kết nối và factory reset.
- **Công tắc gạt 3 nấc** để chọn số endpoint Matter (1, 2 hoặc 3).
- **Header J7** đưa ra 3 GPIO trống (IO2, IO7, IO10) kèm 3V3 và GND, để điều khiển thêm sau này.
- **6 pad nạp UART0** ở mặt dưới, và cổng USB‑C để nạp code/debug.

![Render 3D](docs/images/render_iso.png)

### Thay đổi ở r1.1

- Bỏ 3 cầu chì: chân `+` của cả 3 cầu đấu nối thẳng vào đường VLED.
- Thêm header J7 (1×5, 2,54 mm): `3V3 IO2 IO7 IO10 GND`.
- Công tắc 3 nấc chỉ còn dùng 3 GPIO (IO0, IO1, IO3): chân 1 của công tắc nối GND, nhờ đó giải phóng IO2.
- Board gọn lại còn **84 × 63 mm** (bản trước 84 × 74 mm), thêm khoảng 130 via khâu mảng GND lớp trên xuống mặt GND lớp dưới.

## Thông số

| Mục | Giá trị |
|---|---|
| Vi điều khiển | ESP32-C3-WROOM-02-N4 (flash 4 MB, đủ cho Matter) |
| Nguồn vào | 5–12 V DC, cầu đấu 5,08 mm. Nguồn này cấp thẳng cho dải LED: dải 5 V cắm nguồn 5 V, dải 12 V cắm nguồn 12 V |
| Dòng | Tổng tối đa 10 A, mỗi kênh nên ≤ 6 A liên tục. Trên bo **không có cầu chì**: dùng nguồn có bảo vệ ngắn mạch (xem mục nguồn) |
| Bảo vệ | Chống cắm ngược bằng MOSFET P (SUD50P04-08), TVS SMBJ15A, tụ lọc 470 µF |
| 3,3 V | Buck AP63203 (vào 3,8–32 V, 2 A) |
| Mức tín hiệu DATA | 5 V, qua 74AHCT125 và điện trở 100 Ω. IC này được cấp từ LDO AP7381‑5.0 lấy điện từ nguồn LED |
| USB-C | Nạp code và debug qua USB Serial/JTAG của C3. Có chống ESD USBLC6-2SC6. USB chỉ nuôi phần logic, không bao giờ cấp điện cho dải LED |
| Giao diện | Nút LINK/RESET (GPIO9), LED xanh báo trạng thái (GPIO8), công tắc 3 nấc (GPIO0, GPIO1, GPIO3), header J7 (GPIO2, GPIO7, GPIO10), 6 pad UART0 ở mặt dưới |
| PCB | 84 × 63 mm, 2 lớp, dày 1,6 mm, 4 lỗ M3 |

## Cấu trúc thư mục

| Thư mục | Nội dung |
|---|---|
| `hardware/` | Project KiCad 9: `c3_ws2812_3ch.kicad_pro/.kicad_sch/.kicad_pcb`, thư viện riêng `.kicad_sym` và `.pretty` (công tắc gạt, pad nạp, module ESP với via 0,3 mm) |
| `fab/` | `c3_ws2812_3ch_gerbers.zip` để upload, thư mục `gerbers/`, BOM, file toạ độ (CPL), PDF sơ đồ |
| `docs/images/` | Ảnh render 3D, lớp đồng trên/dưới, sơ đồ nguyên lý |
| `tools/` | Script Python sinh toàn bộ project: `design.py` (linh kiện và net), `layout.py` (vị trí), `build_all.sh` |

## Sơ đồ nguyên lý

![Sơ đồ](docs/images/schematic.png)

Bản đầy đủ ở `fab/c3_ws2812_3ch_schematic.pdf`. Sơ đồ có các khối sau:

1. **Nguồn vào:** J1 → Q1 (MOSFET P chống cắm ngược, D4 kẹp Vgs ở 15 V, R1 10k kéo gate xuống GND) → đường **VLED**. TVS D3, C1 470 µF và C2 100 nF đặt trên VLED.
2. **Ngõ ra:** VLED nối thẳng vào chân `+` của J2/J3/J4.
3. **3,3 V:** VLED qua D1 và VBUS (USB) qua D2 cùng đổ vào V_SYS (hai diode Schottky B5819W). V_SYS vào buck AP63203 (L1 3,9 µH, tụ vào 10 µF và 100 nF, tụ ra 2 × 22 µF, theo bảng 2 trong datasheet).
4. **5 V cho IC dịch mức:** LDO AP7381‑5.0 chịu được 40 V. Khi nguồn vào là 5 V, LDO ra khoảng 4,8–4,9 V (vẫn trong dải 4,5–5,5 V của 74AHCT125). Khi chỉ cắm USB, IC dịch mức không có điện, nên không có dòng chạy ngược vào chân DATA của dải LED đang tắt.
5. **Dịch mức:** GPIO4/5/6 → 74AHCT125 (mỗi ngõ vào có 10k kéo xuống để LED không nhận rác khi ESP đang khởi động) → 100 Ω → chân `D`. Cổng thứ 4 không dùng: ngõ vào nối GND.
6. **ESP32-C3:** tụ 10 µF + 100 nF ở chân 3V3, mạch RC 10k + 1 µF ở chân EN, điện trở kéo lên cho các chân strap IO2, IO8 và IO9.
7. **USB-C:** CC1/CC2 nối điện trở 5,1k xuống GND, D+/D− đi qua USBLC6-2SC6.
8. **Mở rộng:** J7 đưa ra `3V3, IO2, IO7, IO10, GND`.

## Gán chân

| GPIO | Chân module | Chức năng | Ghi chú |
|---|---|---|---|
| IO4 | 3 | DATA kênh 1 | Trùng mặc định `CONFIG_LED_GPIO=4` của `ESP32-C3/matter-wifi/matter-rgb-light-ws2812` |
| IO5 | 4 | DATA kênh 2 | |
| IO6 | 5 | DATA kênh 3 | |
| IO9 | 8 | Nút LINK/RESET (cũng là BOOT) | 10k kéo lên, nhấn = 0. Trùng `CONFIG_APP_BUTTON_GPIO=9` |
| IO8 | 7 | LED trạng thái, 0 = sáng | Chân strap, có 10k kéo lên |
| IO3 / IO1 / IO0 | 15 / 17 / 18 | Chân 2 / 3 / 4 của công tắc SW2 (qua RN1 1k) | Chân 1 của SW2 nối GND qua RN1 |
| IO2 | 16 | J7 chân 2 | Chân strap, có R5 10k kéo lên |
| IO7 | 6 | J7 chân 3 | |
| IO10 | 10 | J7 chân 4 | |
| IO18 / IO19 | 13 / 14 | USB D− / D+ | |
| IO20 / IO21 | 11 / 12 | UART0 RX / TX | Ra pad nạp |

Các chân strap đều an toàn khi khởi động:

- **IO8:** có R6 kéo lên. Mạch LED nối từ 3V3 qua R8 vào IO8 nên không kéo chân này xuống.
- **IO2:** chỉ nối ra J7, có R5 giữ ở mức 1. Công tắc gạt không còn chạm vào IO2, nên vị trí công tắc không ảnh hưởng lúc khởi động.

## Header J7: 3 GPIO tự do

| Chân J7 | Tín hiệu | Lưu ý |
|---|---|---|
| 1 | 3V3 | Lấy từ buck AP63203; nên dùng dưới khoảng 300 mA cho thiết bị ngoài |
| 2 | IO2 | **Chân strap:** đã có R5 10k kéo lên. Mạch ngoài không được kéo IO2 xuống 0 trong lúc ESP khởi động hoặc reset. Dùng làm ngõ ra, hoặc ngõ vào kéo lên, là an toàn |
| 3 | IO7 | GPIO thường (MTDO, JTAG đã chạy qua USB) |
| 4 | IO10 | GPIO thường. Datasheet ghi có xung thấp khoảng 5 ns lúc cấp nguồn, nên tránh dùng để kích thiết bị nhạy với xung lúc khởi động |
| 5 | GND | |

- Mức logic 3,3 V, **không chịu 5 V**. Mỗi chân nên dùng dưới khoảng 20 mA. Tải lớn hơn (relay, quạt, đèn) thì qua transistor/MOSFET.
- Có thể dùng làm I2C (ví dụ IO7 = SDA, IO10 = SCL, thêm điện trở kéo lên 4,7k), UART phụ, PWM (LEDC), ngõ vào nút bấm hoặc cảm biến.
- Header bước 2,54 mm ở mép phải board, dưới công tắc gạt. Tên chân in trên silkscreen.

## Nút LINK/RESET (gợi ý cho firmware)

Giữ cách dùng nút của các firmware có sẵn trong repo:

- **Nhấn nhanh:** bật/tắt đèn, hoặc mở lại cửa sổ kết nối Matter.
- **Giữ ≥ 5 giây rồi nhả:** factory reset, xoá Wi‑Fi và thông tin ghép nối.
- **Giữ nút trong lúc cắm điện:** ESP32‑C3 vào chế độ nạp ROM (GPIO9 = 0 khi reset). Dùng để cứu mạch khi firmware lỗi.

LED xanh (GPIO8) dùng để báo trạng thái, ví dụ: nháy chậm khi chờ kết nối, nháy nhanh khi đang giữ nút để reset, sáng một lúc khi kết nối xong.

## Công tắc gạt 3 nấc: đọc số endpoint

Mỗi loại công tắc SP3T 4 chân dùng một chân khác làm chân chung. Ví dụ Same Sky SLW‑156646 dùng chân đầu hàng (chân 1); C&K OS103011 dùng chân 2 (nấc 1 nối 1‑2, nấc 2 nối 2‑3, nấc 3 nối 2‑4). Bo nối như sau:

- **Chân 1** của SW2 nối GND qua một điện trở của RN1.
- **Chân 2, 3, 4** qua RN1 vào **IO3, IO1, IO0**.

Firmware bật pull-up nội cho 3 GPIO rồi đọc:

1. **Cặp chứa chân 1:** chân nào đọc 0 là đang nối với chân 1 (GND).
2. **Cặp không chứa chân 1:** nếu không chân nào đọc 0, lần lượt kéo một chân xuống 0 và đọc các chân còn lại.
3. **Kết quả:** cặp đang thông luôn là (1,2), (x,3) hoặc (x,4). **Số endpoint = số thứ tự chân lớn nhất trong cặp − 1.**

Cách này đúng cho công tắc có chân chung là chân 1, là chân 2, và cả kiểu cầu trượt (1‑2, 2‑3, 3‑4).

```c
#include "driver/gpio.h"
#include "esp_rom_sys.h"

/* SW_PIN[n] = GPIO của chân n của SW2 (chân 1 nối GND qua RN1) */
static const gpio_num_t SW_PIN[5] = {GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_3, GPIO_NUM_1, GPIO_NUM_0};

/* Trả về 1, 2 hoặc 3 endpoint; 0 nếu không đọc được (công tắc nằm giữa hai nấc) */
int read_endpoint_switch(void)
{
    for (int p = 2; p <= 4; p++) {
        gpio_reset_pin(SW_PIN[p]);
        gpio_set_direction(SW_PIN[p], GPIO_MODE_INPUT);
        gpio_set_pull_mode(SW_PIN[p], GPIO_PULLUP_ONLY);
    }
    esp_rom_delay_us(20);
    int top = 0;                                /* chân lớn nhất trong cặp đang thông */
    for (int p = 2; p <= 4; p++) {              /* cặp (1, p): chân p đọc 0 */
        if (gpio_get_level(SW_PIN[p]) == 0) {
            top = p;
        }
    }
    for (int a = 2; a <= 3 && top == 0; a++) {  /* cặp (a, b) không chứa chân 1 */
        gpio_set_direction(SW_PIN[a], GPIO_MODE_OUTPUT);
        gpio_set_level(SW_PIN[a], 0);
        esp_rom_delay_us(20);
        for (int b = a + 1; b <= 4; b++) {
            if (gpio_get_level(SW_PIN[b]) == 0) {
                top = b;
            }
        }
        gpio_set_direction(SW_PIN[a], GPIO_MODE_INPUT);
    }
    return top ? top - 1 : 0;
}
```

Nên đọc công tắc **một lần lúc khởi động**. Danh sách endpoint là một phần của thiết bị đã ghép nối Matter, nên khi số endpoint đổi so với lần trước (lưu trong NVS), firmware nên yêu cầu factory reset và ghép lại thay vì đổi ngầm. Vạch `1 2 3` in cạnh công tắc ghi theo thứ tự cặp chân (1,2), (·,3), (·,4). Nếu loại công tắc mua được cho kết quả đảo chiều, chỉ cần đảo bảng tra trong firmware.

## Gợi ý cho firmware 3 kênh

- ESP32‑C3 chỉ có **2 kênh RMT TX**. Để chạy 3 dải độc lập, có thể dùng RMT cho 2 kênh và `led_strip` backend **SPI** (SPI2 + DMA) cho kênh thứ 3. Cách khác là dùng chung một kênh RMT, đổi GPIO giữa các lần gửi khung, vì dữ liệu chỉ phải gửi lại khi trạng thái đèn thay đổi.
- Firmware `matter-rgb-light-ws2812` có sẵn dùng GPIO4 và GPIO9, nên chạy được ngay trên kênh 1 của bo này (1 endpoint).

## Nguồn và dòng

- **Chọn nguồn theo dải LED:** WS2812/WS2812B cần 5 V, WS2811/WS2815 loại 12 V cần 12 V. Mức DATA luôn là 5 V, hợp với cả hai loại.
- **Không có cầu chì trên bo.** Nếu dây ra dải LED bị chập, dòng chỉ bị giới hạn bởi nguồn. Nên dùng nguồn có bảo vệ ngắn mạch (phần lớn nguồn tổ ong có, nhưng nên xem thông số của nguồn). Với nguồn lớn hoặc dây ra mảnh, nên gắn cầu chì trên dây (cầu chì ống hoặc cầu chì ô tô kèm đế đấu dây) cho từng dải.
- **Tổn hao Q1:** khoảng 1 W khi chạy 5 V/10 A, khoảng 0,6 W khi 5 V/8 A. Q1 hàn trên mảng đồng VIN lớn để toả nhiệt. Nếu thay bằng **AOD4185** (cùng vỏ DPAK, 20 mΩ ở 4,5 V) thì nên giới hạn khoảng 6 A ở nguồn 5 V.
- **Lớp đồng:** thanh VLED (rộng khoảng 6 mm) và 3 nhánh xuống chân `+` của cầu đấu (rộng 4,4 mm) nằm ở lớp trên; GND là mặt đồng liền ở lớp dưới. Theo ước tính IPC‑2221 với đồng 1 oz: thanh VLED nóng thêm khoảng 14 °C khi chạy 10 A; một nhánh chạy 6 A nóng thêm khoảng 7 °C. Nếu một kênh cần trên 6 A, hoặc tổng chạy liên tục từ 8 A trở lên, nên đặt **đồng 2 oz**.
- **Dải LED dài:** cấp nguồn thêm ở giữa hoặc ở cuối dải (power injection) trực tiếp từ nguồn. Không kéo toàn bộ dòng qua bo.
- **Cắm USB khi đã có nguồn LED:** an toàn. Diode D1/D2 chọn nguồn cao hơn cho phần logic, và USB không bao giờ nối vào VLED.

## Bố trí PCB

| Lớp trên | Lớp dưới |
|---|---|
| ![top](docs/images/copper_top.png) | ![bottom](docs/images/copper_bottom.png) |

- **Anten ESP** nằm sát mép trên. Vùng 28 × 11 mm quanh anten không có đồng ở cả hai lớp (theo vùng cấm của footprint).
- **Đường công suất:** đi theo vùng đồng, không dùng đường mảnh: J1 → Q1 → thanh VLED → 3 nhánh xuống chân `+` của J2/J3/J4. Chân cầu đấu và Q1 nối đặc vào vùng đồng (không dùng thermal relief).
- **Buck:** tụ vào nằm ngay dưới U2, nối thẳng chân VIN và GND. Nút SW ngắn, tụ BST sát chân.
- **USB:** D+/D− đi song song, ngắn, qua USBLC6 rồi vào chân 13/14 của module.
- **Đường DATA:** chỉ chui xuống lớp dưới đúng đoạn băng qua thanh VLED. Mặt GND dưới gần như liền, nhất là ở vùng dòng lớn sát cầu đấu. Khoảng 130 via khâu (lưới 4 mm) nối mảng GND lớp trên xuống mặt GND lớp dưới.
- **Header J7:** ở mép phải, dưới công tắc gạt.
- **Pad nạp UART0:** 6 pad Ø1,5 mm, bước 2,54 mm, ở **mặt dưới, dưới thân module**, cùng bộ tín hiệu với bo cảm biến c3-radar (`3V3 EN GND IO9 RX0 TX0`). Tên từng pad in trên silkscreen mặt dưới; nhìn từ mặt dưới, thứ tự từ trái sang phải là TX0 → 3V3.

![Render mặt trên](docs/images/render_top.png)

![Render mặt dưới (pad nạp dưới thân module)](docs/images/render_bottom.png)

## Nạp firmware

1. **Qua USB-C (khuyên dùng):** cắm cáp, chạy `idf.py -p /dev/ttyACM0 flash monitor`. Chip mới hoặc firmware treo: giữ nút LINK/RESET rồi mới cắm cáp.
2. **Qua pad UART0:** dùng mạch USB‑UART 3,3 V nối vào pad mặt dưới (TX0 → RX, RX0 → TX, chung GND). Có thể dùng kim pogo hoặc hàn tạm dây. Nối IO9 với GND, chạm EN xuống GND rồi nạp.
3. Không cần nguồn LED khi nạp; USB tự nuôi phần logic. Khi nạp qua pad UART, cấp 3,3 V vào pad `3V3` hoặc cắm USB‑C, không cấp cả hai cùng lúc.

## Đặt làm mạch

- Upload `fab/c3_ws2812_3ch_gerbers.zip`: 2 lớp, 1,6 mm, đồng 1 oz (hoặc 2 oz như ở trên).
- Luật dùng: đường và khoảng hở tối thiểu 0,2 mm, via 0,6/0,3 mm (via công suất 0,8/0,4 mm), đồng cách mép board 0,4 mm. Đều nằm trong khả năng tiêu chuẩn của JLCPCB/PCBWay.
- Lắp ráp SMT: dùng `fab/c3_ws2812_3ch-BOM.csv` và `fab/c3_ws2812_3ch-pos.csv`. Gốc toạ độ là góc dưới trái board. Một số linh kiện có thể bị lệch góc xoay trong trình xem của nhà mạch; kiểm tra lại ở bước preview.
- Linh kiện cắm (cột `Mount = THT`: cầu đấu, nút, công tắc gạt, header J7) hàn tay sau.
- Cột LCSC có mã cho các linh kiện đã đối chiếu (AP63203WU‑7 C780769, USBLC6‑2SC6 C7519, HRO TYPE‑C‑31‑M‑12 C165948, B5819W C8598) và các điện trở/tụ 0603 loại basic thông dụng. Các dòng còn trống thì chọn theo MPN.

## Cần kiểm tra trước khi đặt

1. **Công tắc gạt SW2:** footprint vẽ theo bản vẽ của **Same Sky SLW‑156646‑5A‑N‑D** (4 chân dẹt, bước 3,2 mm, lỗ 1,1 mm). Nếu dùng loại khác (SK‑13D07, C&K OS103011…), đo lại bước chân. Cách đọc ở trên đúng cho công tắc có chân chung là chân 1 hoặc 2, nhưng lỗ phải khớp.
2. **Q1:** SUD50P04‑08 (Vishay). Có thể không có sẵn ở LCSC. Xem ghi chú về AOD4185 ở mục nguồn.
3. **Cầu đấu:** footprint Phoenix MKDS 1,5 bước 5,08 mm. Loại KF128/DG128 phổ biến có cùng bước chân và lỗ luồn dây quay ra mép dưới board.
4. **Module ESP:** dùng footprint ESP32‑C3‑WROOM‑02 của KiCad, chỉ mở 12 via tản nhiệt dưới EPAD từ 0,2 lên 0,3 mm cho hợp quy trình 2 lớp tiêu chuẩn. Chân GND 9 của module nối vào mảng GND lớp trên dưới bụng module (nối đặc), mảng này xuống mặt GND qua EPAD.
5. Ảnh render không có mô hình 3D của USB‑C và công tắc gạt (chỉ hiện pad).

## Trạng thái kiểm tra

- **ERC (KiCad 9.0.9):** 0 lỗi, 0 cảnh báo.
- **Netlist:** `tools/check_netlist.py` so netlist KiCad xuất ra với `design.py`. Khớp 39/39 net, mọi chân đúng net, các chân bỏ trống có cờ no-connect.
- **DRC (KiCad 9.0.9, `--schematic-parity`):** 0 vi phạm, 0 kết nối thiếu, 0 lệch giữa PCB và sơ đồ.
- **Đi dây:** dùng bộ đi dây lưới 2 lớp viết riêng (`tools/autoroute.py` + `tools/astar.c`). Khoảng hở được tính chính xác từ đa giác pad của KiCad, sau đó DRC kiểm lại toàn bộ. Nên mở PCB trong KiCad xem lại một lượt trước khi đặt, vì vài đường tín hiệu còn rút ngắn được bằng tay.

## Tái tạo / chỉnh sửa

```bash
cd matter-ws2812-3ch/tools
./build_all.sh      # sơ đồ -> ERC -> so netlist -> PCB -> đi dây -> DRC -> file sản xuất (khoảng 6 phút)
```

Cần KiCad 9.0 (`kicad-cli` và module Python `pcbnew`), Python 3 có `numpy` và `Pillow`, và `gcc`.

- Linh kiện và net sửa trong `tools/design.py`.
- Vị trí linh kiện, vùng đồng và chữ silkscreen sửa trong `tools/layout.py`.

Có thể sửa trực tiếp trong KiCad, nhưng chạy lại script sẽ ghi đè các sửa tay.
