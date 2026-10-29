# Checklist kiểm thử: nút Matter over Thread CR2032

Áp dụng cho bo theo [README](../README.md) mục 3 và firmware bản phát hành (mặc định SIT, hoặc `lit`). **Không đo dòng với bản debug** (bản debug không Light-sleep). Ghi kết quả vào cột cuối; các số [AS] trong `tools/battery_model.py` được thay bằng số đo ở mục F, G.

Log: UART0 115200 baud (TP3/TP4). Các dòng log được nhắc tới: `id_diag` (danh tính, Thread, parent RSSI, subscription), `power` (công suất phát, tạm dừng Thread, LIT/SIT), `battery` (áp pin, mức pin), `switch` (cử chỉ).

## A. Thiết bị đo

| Thiết bị | Dùng cho |
|---|---|
| Nordic PPK2 hoặc Joulescope JS220 (≥ 100 kS/s, dải nA–100 mA) | dòng ngủ, điện tích mỗi poll/lần nhấn, trung bình dài hạn |
| Oscilloscope 2 kênh, đầu dò 10×; tuỳ chọn đầu dò dòng hoặc shunt 1 Ω | dòng đỉnh, áp pin khi phát, độ trễ |
| Nguồn DC chỉnh được + điện trở công suất 10/15/20/25/30/40 Ω | giả lập pin yếu (mục I) |
| Bộ chuyển USB-UART 3,3 V (hoặc ESP-Prog) | nạp, log |
| Apple Home hub có Thread (HomePod mini / Apple TV 4K) + iPhone | mục C, D, K |
| Home Assistant + Matter Server add-on ≥ 9.1.0 + OpenThread Border Router (ZBT-1/SkyConnect) | mục C, D, J, K, L |
| Tuỳ chọn: nRF52840 dongle + Wireshark (sniffer 802.15.4) | kiểm tra poll, công suất khung, re-attach |
| Pin CR2032 mới của ít nhất 2 hãng (Panasonic, Energizer, Murata) | H, L |

Điểm đo trên bo: **JP1** (cắt cầu hàn, nối ampe kế) = dòng từ pin, đã qua tụ bulk; **JP2** = dòng đường 3,3 V (dòng thật của module, có xung). Cấp nguồn thay pin: nguồn/PPK2 ở chế độ source 3,0 V vào cọc pin BT1.

## B. Bring-up phần cứng

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| B1 | Lắp pin ngược chiều | không có dòng (< 1 µA), không nóng; lắp đúng chạy bình thường | |
| B2 | Đo 3V3 (TP1) lúc nghỉ và lúc phát | 3,30 V ± 2 %, sụt < 100 mV khi phát | |
| B3 | Kiểm tra TPS63900 đọc đúng cấu hình: cấp 2,0 V và 3,2 V vào cọc pin | 3V3 vẫn 3,30 V ở cả hai mức | |
| B4 | **Rò khi chip tắt**: nối TP5 (EN) xuống GND, đo ở JP1 sau 5 phút có áp (tụ MLCC cần thời gian ổn định) | ≤ 2 µA tổng (≈ 1 µA module tắt [DS] + 75 nA TPS63900 + rò tụ). Nếu lớn hơn: tháo C1/C2 đo lại để tách rò tụ | |
| B5 | Rò riêng tụ bulk (tháo U2, cấp 3,0 V vào VBAT) | ≤ 0,5 µA sau 5 phút [AS]; ghi số đo vào `I_LEAK` | |
| B6 | Lắp pin mới: dòng xung nạp tụ bulk | không reset lặp, khởi động trong < 1 s | |
| B7 | Nạp firmware qua UART (tự reset hoặc BOOT/EN bằng tay) | `esptool` nạp và verify được | |
| B8 | Log khởi động | có `battery: VBAT at boot ...`, `power: 802.15.4 TX power 0 dBm ...`, mã QR khi chưa ghép | |
| B9 | Lắp pin / reset EN 10 lần **không chạm nút** | không lần nào có `Button held at boot`; thiết bị đã ghép vẫn giữ ghép nối (hồi quy: chân nút phải được cấu hình input trước khi đọc) | |

## C. Ghép nối Apple Home / Home Assistant

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| C1 | Ghép vào **Apple Home** bằng QR (bản SIT) | ghép xong < 2 phút; phụ kiện có 3 mục Single / Double / Long Press; pin hiển thị | |
| C2 | Ghép vào **Home Assistant** | có Event entity với `multi_press_1`, `multi_press_2`, `long_press`, `long_release`; Battery %, Battery voltage, Identify | |
| C3 | **Multi-Admin**: Apple → chia sẻ → HA (và ngược lại); thử cả cách giữ nút 10 s | cả hai hệ nhận sự kiện | |
| C4 | Log `id_diag: SUB established ... min X s max Y s` của từng controller | max = 3600 s (IdleModeDuration). Ghi lại nếu controller ép ngắn hơn (ảnh hưởng tuổi pin, README 9.3) | |
| C5 | Log `id_diag: ot parent ... rssi_avg=` tại vị trí lắp thật, đã có vỏ | ≥ −80 dBm (nếu thấp hơn: thêm router gần hoặc tăng `THREAD_TX_POWER_DBM`) | |
| C6 | Identify từ HA / Apple | LED nháy 200/300 ms; dừng đúng lệnh | |
| C7 | Bản `lit` chỉ với HA (Battery Saving bật) | log `power: Every fabric has a Check-In client: LIT allowed`; HA vẫn nhận sự kiện | |
| C8 | Bản `lit` thêm Apple Home | log `staying in SIT mode`; Apple vẫn nhận sự kiện | |
| C9 | Xoá khỏi hệ cuối cùng | tự factory reset, mở lại cửa sổ BLE | |
| C10 | Bản `lit`: HA huỷ đăng ký Check-In client (xoá thiết bị khỏi HA khi còn hệ khác, hoặc tắt Battery Saving) | log `staying in SIT mode` ngay sau thao tác, không phải chờ lần nhấn kế tiếp | |
| C11 | Xoá khỏi hệ cuối cùng trong lúc cửa sổ ghép nối đang mở (giữ nút 10 s trước) | log `factory reset when it closes`; reset khi cửa sổ đóng (15 phút) | |

## D. Sự kiện nút

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| D1 | Nhấn một lần × 20 | 20 lần `multi_press_1` (HA), 20 lần cảnh Single Press (Apple); log `switch: Gesture` khớp | |
| D2 | Nhấn đúp × 20 (khoảng cách 150–300 ms) | 20 lần `multi_press_2`, không lẫn `multi_press_1` | |
| D3 | Giữ 1,5 s × 20 | `long_press` rồi `long_release`; không có `multi_press_*` | |
| D4 | Nhấn 3 lần nhanh | không tạo `multi_press_*` (count 0, bị bỏ qua), không treo | |
| D5 | Nhấn rất nhanh liên tục 30 s | không reset, không treo, chuỗi cuối kết thúc đúng | |
| D6 | Giữ 10 s rồi nhả | LED nháy 100/400 ms sau 10 s; cửa sổ ghép nối mở (`switch: Commissioning window open`) | |
| D7 | Giữ 20 s rồi nhả | LED nháy 50/100 ms sau 20 s; 3 nháy xác nhận; factory reset | |
| D8 | Giữ > 40 s rồi nhả | thao tác bảo trì bị huỷ, chỉ có `long_release` | |
| D9 | Giữ nút khi lắp pin 5 s | factory reset lúc khởi động | |
| D10 | **Không sót lần nhấn**: 100 lần nhấn đơn cách nhau 1–3 s, đếm trên HA | 100/100 | |
| D11 | Nhấn ngay sau khi chip vừa ngủ (chờ ≥ 20 s giữa các lần) × 20 | 20/20, đánh thức bằng EXT1 | |

## E. Độ trễ

Oscilloscope: CH1 trên chân GPIO10 (cạnh nhấn), CH2 dòng ở JP2 (shunt 1 Ω hoặc đầu dò dòng).

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| E1 | Cạnh nhấn → xung phát đầu tiên (report `InitialPress`) | 21–50 ms (debounce 20 ms + xử lý) | |
| E2 | Cạnh nhả → xung phát `MultiPressComplete` | 350–600 ms | |
| E3 | Đầu-cuối HA: automation bật một đèn nhanh khi `multi_press_1`; quay 240 fps cả nút và đèn; trừ độ trễ riêng của đèn (bật đèn từ HA bằng tay) | lúc nhả + ≤ 450 ms | |
| E4 | Như E3 với `SWITCH_MULTI_PRESS = n` (trigger `initial_press`) | lúc nhấn + ≤ 300 ms | |
| E5 | Apple Home: cảnh Single Press | ghi lại (tham khảo) | |
| E6 | Thời gian thức khỏi Light-sleep: cạnh nhấn → dòng tăng | ≤ 2 ms | |

## F. Dòng ngủ và năng lượng

PPK2/Joulescope ở JP1 (phía pin, nguồn 3,0 V) và ở JP2 (phía 3,3 V). Đã ghép nối, không thao tác ≥ 10 phút trước khi đo.

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| F1 | Dòng sàn giữa hai lần poll, JP2 | ≤ 30 µA (dự tính 27 µA) → ghi vào `I_SLEEP` | |
| F2 | Dòng sàn, JP1 (pin 3,0 V) | ≤ 40 µA | |
| F3 | Điện tích một lần poll, JP2 (tích phân một xung) | ≤ 0,3 mC (dự tính 0,25 mC) → ghi vào mô hình | |
| F4 | Chu kỳ poll | 15 s (SIT) / 120 s (LIT) ± 5 % | |
| F5 | Trung bình 1 giờ, không nhấn, JP1 | SIT ≤ 65 µA; LIT ≤ 50 µA | |
| F6 | Điện tích một lần nhấn đơn (từ cạnh nhấn tới khi về sàn), 1 controller và 2 controller | SIT ~3,8 mC / 1 controller; ghi `Q_REPORT`, `Q_BUTTON` | |
| F7 | Điện tích keep-alive mỗi giờ | ghi lại | |
| F8 | Dòng khi đang giữ nút 30 s | sàn + ~3,3 µA (qua R6), chip vẫn ngủ | |
| F9 | Dòng khi cửa sổ ghép nối mở (đã ghép, giữ 10 s) | poll 200 ms; ghi lại; trở về sàn khi đóng cửa sổ | |
| F10 | Đo pin có tải hằng ngày | xung CPU 500 ms ~10 mA, log `battery: Load test ...` | |
| F11 | Thiết bị **chưa ghép**, sau khi cửa sổ BLE 15 phút đã đóng | chỉ còn dòng sàn, không có lần thức 10 s (log `thread_diag: Thread diagnostics stopped`) | |

## G. Dòng đỉnh khi phát

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| G1 | Dòng đỉnh ở JP2 trong lúc poll và gửi report | TX ≤ 40 mA (0 dBm, 36 mA [DS]); RX ~25 mA | |
| G2 | **2 giây đầu sau khi lắp pin / reset EN, thiết bị đã ghép** (Child Update tới parent cũ) | **không có xung > 45 mA** (xung 140 mA nghĩa là khung +20 dBm lọt qua: kiểm tra linker wrap) | |
| G3 | Dòng đỉnh BLE khi ghép nối | ≤ 40 mA (BLE 0 dBm) | |
| G4 | Sniffer: công suất/RSSI khung từ nút ở 1 m so với một thiết bị biết công suất | khớp 0 dBm | |

## H. Áp pin CR2032 khi phát

Oscilloscope DC trên TP9 (VBAT), độ phân giải ≤ 10 mV/div (dùng offset), kích theo sườn xuống 50 mV dưới mức nghỉ.

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| H1 | Pin mới, sụt áp khi poll và khi gửi report | ≤ 0,3 V | |
| H2 | Pin mới, re-attach (tắt/bật Border Router) | V_pin_min ≥ 2,3 V | |
| H3 | Pin đã dùng (hoặc giả lập mục I) | V_pin_min ≥ 1,9 V khi phát; ≥ 1,75 V khi re-attach | |
| H4 | So với tính toán README 1.4 (C hiệu dụng, R_trong) | sai khác ≤ 30 %; nếu tụ hiệu dụng thấp → thêm tụ | |
| H5 | Ghép nối BLE với pin mới | không brownout | |

## I. Pin gần cạn (giả lập)

Nguồn DC + điện trở nối tiếp vào cọc pin, thay cho CR2032:

| Điểm giả lập | V nguồn | R nối tiếp | Tương ứng (đường Energizer) |
|---|---|---|---|
| I-a | 2,90 V | 10 Ω | pin mới |
| I-b | 2,80 V | 15 Ω | ~150 mAh |
| I-c | 2,75 V | 20 Ω | ~175 mAh (giới hạn mô hình 80 %) |
| I-d | 2,60 V | 25 Ω | ~200 mAh |
| I-e | 2,40 V | 30 Ω | gần cạn |
| I-f | 2,20 V | 40 Ω | cạn |

| # | Bước (mỗi điểm) | Đạt khi | Kết quả |
|---|---|---|---|
| I1 | Khởi động, chờ đo pin đầu tiên (2 phút), đọc log `battery: Load test ...` và `BatChargeLevel` trên HA | mức pin chuyển **Warning trước điểm đầu tiên có brownout** | |
| I2 | 20 lần nhấn | không reset ở I-a … I-d | |
| I3 | Re-attach (tắt/bật Border Router) | thành công ở I-a … I-c; ghi lại kết quả I-d | |
| I4 | Khởi động ở I-f | log `Battery too weak ... Deep-sleep`; ≤ 3 brownout liên tiếp rồi ngủ; nhấn nút thử lại được | |
| I5 | OTA ở mức Warning | log `OTA download cancelled: battery level not OK` | |
| I6 | Hiệu chỉnh ngưỡng `BATTERY_*_MV`, `BATTERY_BURST_RATIO_X100` theo kết quả | ghi lại giá trị chọn | |

## J. Mất Border Router

Mạng có 1 Border Router (trường hợp xấu nhất). PPK2 ghi liên tục ở JP1.

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| J1 | Tắt BR 5 phút rồi bật | nút detach (≤ 15 s + vài giây ở SIT), tự attach lại khi BR lên; subscription khôi phục; không mất fabric | |
| J2 | Tắt BR 1 giờ | sau 30 phút log `Thread detached for too long: paused`; thử lại ở +30 phút (`Thread resumed (retry)`) | |
| J3 | Dòng trung bình trong giờ thứ 2–6 lúc mất mạng | ≤ dòng bình thường + 25 µA | |
| J4 | Nhấn nút khi đang tạm dừng | log `Thread resumed (button)`; nếu BR đã lên thì attach trong ≤ 20 s | |
| J5 | Tắt BR 6 giờ rồi bật, không nhấn | attach lại ở lần thử kế tiếp (≤ 4 giờ) | |
| J6 | Parent khởi động lại nhanh (router quên child) | re-attach trong ≤ 190 s (SIT) nhờ child supervision | |
| J7 | Có 2 Border Router, tắt 1 | nút chuyển parent, không gián đoạn quá 1 phút | |
| J8 | Tắt BR, rồi tháo/lắp pin nút (khởi động khi không có mạng) | sau 30 phút vẫn có `Thread detached for too long: paused`; nhấn nút sau > 60 s thì thử attach lại | |

## K. Nhiều nút

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| K1 | 3 nút bản `unique_setup` (hoặc `factory`), ghép lần lượt | mỗi nút một QR riêng | |
| K2 | So log `id_diag` | `efuse_mac`, `serial`, `unique_id`, `ext_addr`, `srp host` khác nhau; `ext_panid`, `partition` giống nhau | |
| K3 | Nhấn cả 3 nút cùng lúc × 10 | mỗi sự kiện đúng thực thể, không mất | |
| K4 | Factory reset 1 nút | 2 nút còn lại không bị ảnh hưởng | |
| K5 | (Kiểm tra lỗi cố ý) chép NVS từ nút A sang nút B | tài liệu hoá triệu chứng trùng danh tính; không làm ở bo thật dùng lâu dài | |

## L. Chạy liên tục 7 ngày

Bản phát hành, 2 controller (Apple + HA), ~20 lần nhấn/ngày (bằng tay hoặc cơ cấu nhấn tự động), log UART ghi file, PPK2/Joulescope ghi trung bình.

| # | Theo dõi | Đạt khi | Kết quả |
|---|---|---|---|
| L1 | Số lần reset (`id_diag ... reset=`) | 0 reset ngoài ý muốn | |
| L2 | Sự kiện đến HA/Apple so với số lần nhấn | 100 % (khi mạng bình thường) | |
| L3 | `SUB terminated` | ≤ 1 lần/ngày/controller | |
| L4 | Dòng trung bình 7 ngày | trong ±15 % mô hình (SIT ~60 µA) | |
| L5 | `BatVoltage` và `BatPercentRemaining` | giảm đều, không nhảy | |
| L6 | Heap tối thiểu (`id_diag hw ... min_heap`) | không giảm dần (không rò bộ nhớ) | |
| L7 | Cập nhật `tools/battery_model.py` bằng số đo F và L4, tính lại tuổi pin | ghi lại | |

## M. OTA

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| M1 | OTA từ provider (chip-ota-provider-app hoặc HA) với pin OK | cập nhật xong, log `OTA image confirmed` sau 60 s ở trạng thái child | |
| M2 | Điện tích của một lần OTA (JP1) | ghi lại (ước lượng 1–5 mAh) | |
| M3 | OTA image hỏng/không vào mạng được | rút pin/lắp lại → quay về bản cũ | |
| M4 | OTA khi pin Warning | bị huỷ (I5) | |
| M5 | OTA lên image không vào được mạng (ví dụ tắt BR ngay sau khi tải xong) | 60 phút sau khởi động: `OTA image not confirmed ... rolling back`, chạy lại bản cũ | |

## N. Factory reset

| # | Cách | Đạt khi | Kết quả |
|---|---|---|---|
| N1 | Giữ 20 s | xoá fabric/Thread, mở cửa sổ BLE; `fctry` giữ nguyên (QR của `unique_setup`/`factory` không đổi) | |
| N2 | Giữ khi lắp pin 5 s | như N1 | |
| N3 | Xoá khỏi controller cuối cùng | như N1 | |
| N4 | Giữ nút khi lắp pin với pin yếu (boot guard Deep-sleep) | không thức dậy liên tục khi nút đang bị giữ; thức bằng timer sau 1 giờ | |

## O. Bo chip (`hardware-chip/`, firmware `sdkconfig.defaults.chip`)

Thêm cho bản không dùng module (chip ESP32-H2, anten in trên bo, LED RGB). Các mục A–N vẫn áp dụng; điểm đo và pad test như bản module (vị trí khác, xem [`hardware-chip/README.md`](../hardware-chip/README.md) mục 4).

| # | Bước | Đạt khi | Kết quả |
|---|---|---|---|
| O1 | Log khởi động: đồng hồ RTC | không có cảnh báo chuyển về RC trong (thạch anh 32,768 kHz Y2 dao động) | |
| O2 | Tần số 32 MHz: TX tone (ESP RF Test Tool), máy phân tích phổ | lệch ≤ ±10 ppm; nếu không, đổi C18/C19 (lệch dương → tăng) | |
| O3 | Anten: S11 tại chân feed (tháo R11, cáp đồng trục), bo trong vỏ, có pin | S11 < −10 dB trên 2,40–2,48 GHz sau khi chọn C24/R11/C25 | |
| O4 | Công suất phát dẫn (cáp ở chân 32 sau lọc C22/L3/C23) | đúng mức đặt (0 dBm) ± 2 dB, hài trong giới hạn | |
| O5 | RSSI/LQI của parent so với bo module ở cùng vị trí | không kém hơn quá 6 dB | |
| O6 | LED RGB: mở cửa sổ ghép nối, Identify, giữ nút 10 s / 20 s, nhấn khi pin yếu (giả lập) | lần lượt xanh dương, trắng, xanh dương rồi đỏ, cam (bảng màu ở README mục 7.8, dòng `STATUS_LED_WS2812`) | |
| O7 | **Dòng ngủ khi LED tắt** (JP2) | như bản module ± 2 µA: Q2 cắt nguồn LED (LED kiểu WS2812 tự tiêu thụ 0,7 mA nếu còn nguồn) | |
| O8 | Dòng khi LED sáng (JP2, độ sáng 40) | ~2 mA mỗi màu, ~6 mA màu trắng | |
