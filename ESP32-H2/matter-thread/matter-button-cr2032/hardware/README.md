# PCB KiCad: nút nhấn Matter CR2032 (ESP32-H2-WROOM-02C)

Dự án KiCad 9 của mạch trong [README chính](../README.md) (mục 2–4): schematic, PCB 2 lớp đã đi dây, file sản xuất. Mọi file được sinh bằng các script trong `scripts/` từ một nguồn dữ liệu duy nhất (`scripts/design.py`), rồi kiểm tra bằng KiCad:

- **ERC: 0 lỗi, 0 cảnh báo.**
- **DRC: 0 vi phạm, 0 pad chưa nối, 0 lỗi footprint, 0 lệch giữa PCB và schematic** (`kicad-cli pcb drc --severity-all --schematic-parity`, KiCad 9.0.9).

Chưa đặt mạch, chưa đo trên bo thật: kiểm tra lại footprint module với datasheet trước khi đặt và chạy [checklist](../docs/test-checklist.md) khi có bo.

Bản không dùng module (chip ESP32-H2, thạch anh, anten in trên PCB, LED RGB WS2812B, bo 4 lớp 29,8 × 29,8 mm): [`../hardware-chip`](../hardware-chip/README.md).

| Mặt trên | Mặt dưới |
|---|---|
| ![Mặt trên](docs/pcb-top.png) | ![Mặt dưới](docs/pcb-bottom.png) |

Đồng và silkscreen (mặt dưới nhìn từ dưới lên, đã lật):

![Layout](docs/pcb-layout.png)

## 1. File

| File | Nội dung |
|---|---|
| `matter-button-cr2032.kicad_pro/.kicad_sch/.kicad_pcb` | dự án KiCad 9 (mở bằng KiCad ≥ 9.0) |
| `lib/matter_button.kicad_sym`, `lib/matter_button.pretty/` | linh kiện riêng: symbol + footprint ESP32-H2-WROOM-02C, power symbol VBAT, đế Keystone 3034 (bản sửa) |
| `fab/matter-button-cr2032-gerber.zip` | Gerber (RS-274X, không thuộc tính X2, theo hướng dẫn KiCad của JLCPCB) + khoan Excellon: gửi thẳng cho nhà sản xuất PCB |
| `fab/bom-vi.csv` | BOM theo tên và thông số linh kiện (tiếng Việt), kèm từ khóa tìm mua, mã LCSC và linh kiện thay thế |
| `fab/bom-jlcpcb.csv`, `fab/cpl-jlcpcb.csv` | BOM (mã LCSC + MPN) và file vị trí linh kiện (tên cột theo JLCPCB) cho dịch vụ lắp ráp; không có U1, BT1 (hàn tay) |
| `fab/bom.csv` | BOM đầy đủ (MPN, mã LCSC, mô tả), C7 ghi DNP |
| `docs/schematic.pdf` | schematic KiCad |
| `docs/assembly-top.svg`, `docs/assembly-bottom.svg` | bản vẽ lắp ráp: viền linh kiện + ký hiệu (C7 gạch chéo: không lắp) |
| `docs/pcb-*.png` | ảnh render 3D (không có model linh kiện) và layout 2D |
| `scripts/` | các script sinh toàn bộ dự án (mục 6) |

## 2. Thông số bo

| Mục | Giá trị |
|---|---|
| Kích thước | **29,8 × 23,9 mm**, góc bo R0,5; anten module chìa ra 6 mm ở mép trên → cả cụm **29,8 × 29,9 mm** (dưới 30 × 30 mm); không có lỗ bắt vít |
| Lớp | 2 lớp FR-4 1,6 mm, đồng 1 oz |
| Đường mạch | 0,2 mm tín hiệu, 0,3 mm nguồn và GND, 0,4 mm cực + pin; khe hở 0,2 mm (tối thiểu 0,15 mm ở pad của linh kiện) |
| Via | 0,6 / 0,3 mm, phủ mask; via nhiệt trong pad GND của module theo land pattern datasheet |
| Cách mép bo | đồng ≥ 0,3 mm |
| Bề mặt | **ENIG** khuyến nghị: pad tiếp xúc âm của pin là đồng trần, chân WSON bước 0,5 mm |
| GND | phủ đồng cả hai mặt (trừ vành quanh mép pin ở mặt dưới), mỗi pad GND có via riêng xuống pad tiếp xúc âm của pin hoặc mặt phẳng dưới; module nối mặt dưới qua 6 via nhiệt của pad 29 |
| Linh kiện | mặt trên tất cả, trừ đế pin BT1 ở mặt dưới; 10 pad test ở mặt dưới |

Các thông số nằm trong quy trình tiêu chuẩn của JLCPCB / PCBWay (track/space 0,127 mm, lỗ 0,3 mm), không cần tùy chọn đặc biệt.

## 3. Bố trí

| Vùng | Linh kiện | Lý do |
|---|---|---|
| Mép trên, sát mép phải | U1 ESP32-H2-WROOM-02C, vùng anten 6 mm **nhô ra ngoài mép bo** | không đồng, không FR-4 dưới anten (datasheet WROOM-02C mục 11, ESP32-H2 Hardware Design Guidelines); mặt phẳng GND chạy tới sát mép, đúng chân anten |
| Trái module | C6 100 nF sát chân 1; R4 (+ C7 không lắp) ở EN; C8, R7, R6 (nút: lọc RC, kéo lên 1 MΩ) ở GPIO10; R5 kéo lên GPIO8 | trục 3V3 chạy dọc mép trái từ JP2 lên chân 1, nhánh x = 6,5 mm cho các điện trở kéo lên; đường nút đi trong lề trái của module (giữa hàng chân và vùng cấm dưới module) xuống SW1 |
| Phải module | không có linh kiện (module sát mép phải) | UART, USB và đo pin đổi lớp ngay cạnh chân module, xuống pad test ở mặt dưới |
| Dưới module | SW1 PTS810 ở giữa bo, C2 dưới nút; LED D1 + R8 (GPIO22) bên phải nút | đặt ống dẫn sáng ở tọa độ D1 |
| Góc dưới trái | TPS63900: R1–R3 phía trên, C4 (VOUT) bên trái, C3 (VIN) bên phải, L1 phía dưới; C5, JP2 sát mép trái | vòng VIN và VOUT ngắn trên lớp trên, cuộn cảm sát LX1/LX2 |
| Góc dưới phải | Q1, JP1, C1; R9 1 MΩ + C9 100 nF đo pin ở góc | chuỗi nguồn phải → trái: tai + của đế pin → Q1 → JP1 → C1 → đường VBAT dọc mép dưới → C2, C3/U2 → C4 → JP2 → 3V3 |
| Mặt dưới | đế pin Keystone 3034 nằm dưới module và mạch; mép pin cách mép dưới 0,54 mm, cách chân anten 3,4 mm | pad tiếp xúc âm Ø17,8 mm là một phần GND; vành 8,95–10,3 mm quanh tâm pin không có đồng (mép vỏ pin là cực +) |
| Mặt dưới, ngoài đế pin | 10 pad test Ø1,5 mm: hai nhóm 2 × 2 ở hai góc trên (bước 2,54 × 2,7 mm), RX và VBAT bên phải đế pin; chữ "Made by Haxeko" và số rev ở dải giữa mép trên | kẹp pogo-pin; nhãn in ở mặt dưới; đường VBAT_SENSE đi dọc mép phải ở mặt dưới |

Chữ in mặt trên: "Thread Button" (dọc mép trái), U1, JP1, JP2. Mặt dưới: "Made by Haxeko", rev, nhãn pad test, dấu + ở tai dương của đế pin.

Tọa độ (mm, gốc ở góc trên trái, nhìn từ mặt trên; nhìn từ mặt dưới thì trái/phải đảo lại) để thiết kế vỏ và jig:

| Vật | Tọa độ |
|---|---|
| Tâm nút SW1 (thân 4,2 × 3,2 mm) | (14,9; 17,4) |
| Tâm LED D1 | (19,4; 16,8) |
| Tâm đế pin BT1 (mặt dưới) | (14,9; 13,36); pin Ø20 mm từ y = 3,36 tới 23,36 |
| Pad test, góc trên trái | y = 1,6: x = 2,00 GND · 4,54 3V3; y = 4,3: x = 2,00 BOOT · 4,54 EN |
| Pad test, góc trên phải | y = 1,6: x = 25,44 TXD0 · 27,98 VBAT_SENSE; y = 4,3: x = 25,44 USB D− · 27,98 USB D+ |
| Pad test, bên phải đế pin | RXD0 (26,9; 18,5) · VBAT (27,2; 21,2) |
| Cầu hàn đo dòng JP1 / JP2 | (24,0; 17,2) / (1,65; 18,62) |
| Module | thân x = 11 … 29, y = −6 … 14; mép anten y = −6, chân anten y = 0 (mép bo) |

Chiều cao: module 3,2 mm trên bo 1,6 mm; chiều cao nút PTS810 và đế pin Keystone 3034 lấy theo datasheet của chúng. Trong vỏ: không đặt kim loại (vít, lò xo, tấm che) trong ~15 mm quanh anten; kiểm tra RSSI/LQI của parent khi đã đóng vỏ (checklist mục C).

**Đánh đổi của bo nhỏ:** để cả cụm (tính anten) dưới 30 × 30 mm, bo chỉ cao 23,9 mm nên pin chỉ còn cách chân anten 3,4 mm (bố trí 25 × 27 mm: 6,5 mm; 30 × 38 mm: ~16 mm). Pin nằm sau mặt phẳng GND, không nằm dưới anten, nhưng Espressif khuyến nghị khoảng trống ~15 mm quanh anten, nên tầm phủ có thể giảm. Đo RSSI/LQI trước khi làm vỏ; nếu yếu, dùng lại bố trí 25 × 27 mm (commit `af26218`) hoặc 30 × 38 mm (commit `6649456`), cùng schematic.

## 4. Đặt mạch và lắp ráp

1. **PCB**: tải `fab/matter-button-cr2032-gerber.zip` lên JLCPCB/PCBWay: 2 lớp, 1,6 mm, ENIG, các mục khác để mặc định.
2. **Lắp ráp JLCPCB (tùy chọn)**: chọn lắp mặt trên (Top side), tải `fab/bom-jlcpcb.csv` và `fab/cpl-jlcpcb.csv`. Mỗi dòng BOM đã có mã LCSC nên được ghép tự động: 10 loại Basic, 5 loại Extended (U2 C1518762, L1 C391305, C1/C2 C469553, R1 C49010116, SW1 C116501; phí nạp tính theo loại). Nếu JLCPCB báo hết hàng: tụ C1/C2 thay bằng Samsung CL32A227MQVNNNE (C400951); SW1 phải là bản **SJM** (160 gf), đừng để trang ghép tự chọn bản SJG (C221895, 400 gf). Kiểm tra góc xoay ở bản xem trước (nhất là U2, Q1, D1, L1, SW1: chấm chân 1 và cực LED). C7 không lắp (DNP).
   - **U1 và BT1 không có ở LCSC** nên không có trong hai file trên: hàn tay sau khi nhận bo. BT1: 2 tai lớn ở mặt dưới, dễ hàn. U1: pad ở mép module hàn bằng mỏ hàn; pad GND giữa (29) có 6 via nhiệt, hàn từ mặt dưới qua via (hoặc khò/bếp gia nhiệt với kem hàn). Muốn JLCPCB lắp U1 thì dùng Global Sourcing hoặc gửi linh kiện (consigned) rồi thêm dòng U1 vào BOM/CPL.
3. **JP1, JP2** là cầu hàn đã nối sẵn bằng đồng: cắt đường đồng nhỏ giữa hai pad để đo dòng (nối ampe kế vào hai pad), đo xong hàn nối lại.
4. Cấp nguồn lần đầu, nạp firmware: [README chính mục 8.3](../README.md#83-nạp-firmware) và [checklist](../docs/test-checklist.md). Khi có pin, **không** cấp 3V3 vào pad 3V3.

## 5. Ghi chú thiết kế

- **Footprint ESP32-H2-WROOM-02C** tự vẽ theo land pattern datasheet v1.2 mục 11.1: pad 1,5 × 0,9 mm bước 1,5 mm; chân 1–9 trái, 10–19 dưới, 20–28 phải; pad GND 29 gồm hai phần 2,8 × 0,55 và 2,8 × 1,6 mm với 6 via nhiệt (phủ mask mặt dưới). Gốc tọa độ là tâm module, vùng anten y = −10 … −4 có keep-out mọi lớp đồng. Silkscreen chỉ vẽ góc dưới và chấm chân 1 (vùng anten nằm ngoài bo).
- **TPS63900**: symbol chuẩn của KiCad đã có chân 11 (pad tản nhiệt) ẩn, chồng lên chân GND 8. Footprint WSON-10 không có via trong pad (TI: via là tùy chọn); pad tản nhiệt nối chân 8 bằng đường mạch, rồi ra hai via GND hai bên.
- **Keystone 3034**: footprint chuẩn có hai pad cùng số "1" (hai tai của cùng một lá kim loại); KiCad 9 sẽ đòi nối đồng giữa chúng vòng qua pin. Bản trong `lib/` bỏ số của tai trái (nhìn từ trên), tai này chỉ để hàn giữ.
- **Ký hiệu linh kiện** trên silkscreen chỉ có U1, JP1, JP2 (bo quá dày cho chữ); vị trí từng linh kiện xem `docs/assembly-*.svg`.
- **Đi dây**: mọi đường trừ GND được vẽ trong script (khóa): phần TPS63900 bước 0,5 mm, đường nguồn (cực + pin, VBAT, 3V3), nút nhấn, LED, đo pin, UART/USB và các đường xuống pad test ở mặt dưới. FreeRouting chỉ chạy khi còn kết nối chưa đi (bố trí này: không còn, bước `route` bỏ qua nó). GND không qua autorouter: phủ đồng hai mặt nối tất cả.
- **Dưới pin**: mặt dưới chỉ đi dây trong phần không bị pin che (hai góc trên và hai dải bên); vành quanh mép pin là vùng cấm (rule area: không đường mạch, không via, không phủ đồng) vì mép vỏ CR2032 là cực + và cọ vào bo khi lắp pin. Via GND chỉ đặt bên trong pad tiếp xúc âm hoặc ngoài pin.

## 6. Sinh lại dự án từ script

Cần KiCad 9 (`kicad-cli` và module Python `pcbnew`); Java 21 và [FreeRouting 2.1.0](https://github.com/freerouting/freerouting/releases) (`freerouting-2.1.0.jar`) chỉ cần khi sửa bố trí mà để lại kết nối chưa vẽ tay. Trên Windows chạy trong "KiCad 9.0 Command Prompt" (Python kèm KiCad có `pcbnew`): gõ `python` thay cho `python3`, và đặt biến bằng `set FREEROUTING_JAR=C:\đường\dẫn\freerouting-2.1.0.jar` trước lệnh `route`.

```bash
cd ESP32-H2/matter-thread/matter-button-cr2032/hardware
python3 scripts/gen_project.py          # .kicad_pro (luật, net class), lib/, bảng thư viện
python3 scripts/gen_sch.py              # .kicad_sch
python3 scripts/gen_pcb.py place        # .kicad_pcb: viền bo, vị trí, đường vẽ tay, vùng cấm
FREEROUTING_JAR=/đường/dẫn/freerouting-2.1.0.jar python3 scripts/gen_pcb.py route
python3 scripts/gen_fab.py              # ERC + DRC (dừng nếu có lỗi), fab/, docs/
```

Chỉ đổi giá trị hoặc mã linh kiện (cùng footprint, cùng net) trong `design.py`: chạy `gen_sch.py`, rồi `python3 scripts/gen_pcb.py sync` (cập nhật giá trị và trường MPN/LCSC trên bo đã đi dây, không đặt hay đi dây lại), rồi `gen_fab.py`.

| Script | Việc |
|---|---|
| `design.py` | dữ liệu duy nhất: linh kiện, giá trị, MPN, mã LCSC, footprint, chân → net, vị trí trên schematic |
| `gen_project.py` | file dự án (luật thiết kế, net class Default 0,2 mm / Power 0,3 mm / Ground), thư viện riêng |
| `gen_sch.py` | schematic A3 một trang: mỗi chân có đoạn dây ngắn tới nhãn net hoặc ký hiệu nguồn |
| `gen_pcb.py` | board từ netlist do `kicad-cli` xuất ra (giống "Update PCB from Schematic"); bảng `PLACE` (vị trí), `ROUTES`/`VIAS` (đi dây tay); bước `route` gọi FreeRouting nếu còn kết nối chưa đi, dọn đoạn thừa, phủ đồng GND, via khâu; bước `sync` chỉ cập nhật giá trị và mã linh kiện |
| `gen_fab.py` | kiểm tra và xuất file sản xuất (ảnh layout 2D cần thêm `pip install cairosvg pillow`) |

FreeRouting (khi phải chạy) không cho cùng một kết quả mỗi lần: sau `route`, `gen_fab.py` chỉ xuất file khi DRC sạch. Sửa tay trong KiCad cũng được (sau đó không chạy lại `gen_pcb.py`, vì nó ghi đè `.kicad_pcb`); chạy `python3 scripts/gen_pcb.py pour` để phủ đồng và via khâu lại, rồi `gen_fab.py`.
