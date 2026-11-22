# Smartroom ESP32 – Firmware giai đoạn 1

Firmware C++ (Arduino framework, PlatformIO) cho ESP32: đo nhiệt độ/độ ẩm, quạt PWM tự động, đèn hẹn giờ, Auto/Manual, LED trạng thái, OTA. Giai đoạn này **chưa có MQTT/Telegram/backend**; để kiểm thử phần cứng, ESP32 tự phục vụ một trang web cục bộ, REST API và lệnh Serial.

## 1. Sơ đồ chân

| Chức năng | GPIO |
|---|---|
| I2C SDA / SCL (DS3231) | 21 / 22 |
| DHT11 (DATA) | 4 |
| LED trạng thái (xanh dương) | 18 |
| LED chính | 19 |
| PWM quạt → module MOSFET | 23 |

Đổi chân hoặc ngưỡng mặc định: sửa `include/config.h`.

## 2. Cài đặt và nạp code

1. Cài VS Code + PlatformIO (hoặc `pip install platformio`).
2. Copy `include/secrets.example.h` thành `include/secrets.h`, điền tên Wi-Fi, mật khẩu Wi-Fi, mật khẩu OTA. File này đã nằm trong `.gitignore`.
3. Nạp qua USB: `pio run -t upload`, xem log: `pio device monitor` (115200).
4. Nạp qua Wi-Fi (sau lần nạp USB đầu tiên):
   ```bash
   export OTA_PASSWORD="mat-khau-giong-trong-secrets.h"
   pio run -e esp32dev_ota -t upload
   ```
5. Trên Ubuntu nếu không thấy cổng `/dev/ttyUSB0`: `sudo apt remove brltty` và `sudo usermod -aG dialout $USER`, rồi đăng xuất/đăng nhập lại.

Khi ESP32 vào Wi-Fi, Serial in ra địa chỉ `http://<IP>/` (hoặc `http://smartroom.local/`).

## 3. Quy tắc hoạt động

**Quạt tự động** (nhiệt độ **hoặc** độ ẩm vượt ngưỡng):

| | Mặc định |
|---|---|
| Nhiệt độ bật / tắt | 30 °C / 28 °C |
| Độ ẩm bật / tắt | 80 % / 75 % |
| Mức quạt khi bật | 100 % (không chia mức) |

Quạt Auto chỉ có hai trạng thái: chạy 100 % hoặc tắt. Code vẫn giữ logic chia 3 mức theo độ vượt ngưỡng; muốn dùng lại thì đổi `levelLow/levelMid/levelHigh` trong `include/logic.h` (ví dụ 40/70/100). Tần số PWM là 1 kHz vì ở 20 kHz quạt không quay nổi ở mức thấp với module MOSFET thực tế. Hysteresis: đã bật thì chỉ tắt khi xuống tới ngưỡng tắt. PWM tối thiểu 30 % (quạt thường không quay dưới mức này) và có xung 100 % ngắn (400 ms) lúc khởi động quạt. Nếu DHT11 lỗi liên tiếp 3 lần, quạt ở chế độ Auto **tắt** (LED trạng thái báo lỗi).

**Đèn:** tối đa 6 khung giờ, mặc định có 1 khung 18:00–22:00. Khung vắt qua nửa đêm được hỗ trợ (22:00 → 06:00). Chưa có giờ hợp lệ (RTC chưa có giờ và chưa có NTP) thì đèn Auto không tự bật.

**Auto/Manual:** riêng cho quạt và đèn. Điều khiển tay (web, Serial) chuyển sang Manual và tạm dừng tự động đến khi chuyển lại Auto. Khởi động lại luôn về Auto. (Phần cứng không có nút nhấn.)

**Thời gian:** NTP (UTC+7) → ghi vào DS3231. Khi mất mạng/khởi động lại, giờ được nạp từ DS3231.

**LED trạng thái (GPIO18)** – hiện trạng thái ưu tiên cao nhất:

| Ưu tiên | Trạng thái | Mẫu LED |
|---|---|---|
| (cấp nguồn) | Khởi động | Sáng đứng 1 giây |
| 1 | OTA đang cập nhật | Nháy đều 1 Hz – không rút nguồn |
| 2 | Lỗi DHT11 | 3 nháy, nghỉ 2 giây |
| 3 | Không có RTC / chưa có giờ | 4 nháy, nghỉ 2 giây |
| 4 | Đang vượt ngưỡng | Sáng đứng |
| 5 | Đang kết nối Wi-Fi | Nháy nhanh 5 Hz |
| 6 | Mất backend (giai đoạn sau) | 2 nháy, nghỉ 2 giây |
| 7 | Bình thường | 1 nháy ngắn mỗi 3 giây |

Cấu hình được lưu vào flash (NVS): ngưỡng quạt và lịch hẹn giờ.

## 4. Lệnh Serial (115200 baud, gõ `help`)

```
status                          in trạng thái
fan auto | off | <30..100>      quạt: tự động / tắt / thủ công theo %
led auto | on | off             đèn
set tempon|tempoff|humon|humoff <số>
sched                           xem lịch;  sched 2 22:00 06:00;  sched 2 off
sim <nhiệt_độ> <độ_ẩm>          giả lập cảm biến để thử quạt;  sim off
reboot
```

`sim` giả lập giá trị cảm biến (không lưu) nên thử được 3 mức PWM mà không cần làm nóng DHT11, rất tiện khi demo.

## 5. REST API (chỉ dùng trong mạng LAN, chưa có xác thực)

| Phương thức | Đường dẫn | Nội dung |
|---|---|---|
| GET | `/api/status` | Toàn bộ trạng thái (JSON) |
| POST | `/api/fan` | `{"mode":"auto"}` hoặc `{"mode":"manual","percent":70}` |
| POST | `/api/led` | `{"mode":"auto"}` hoặc `{"mode":"manual","on":true}` |
| POST | `/api/config` | `{"temp_on":30,"temp_off":28,"hum_on":80,"hum_off":75}` (gửi một phần cũng được) |
| GET / POST | `/api/schedule` | `{"slots":[{"enabled":true,"start":"18:00","end":"22:00"}]}` (thay toàn bộ lịch) |
| POST | `/api/sim` | `{"temp":33,"hum":60}` hoặc `{"off":true}` |

Ví dụ: `curl -X POST http://smartroom.local/api/fan -H 'Content-Type: application/json' -d '{"mode":"manual","percent":70}'`

## 6. Kiểm tra trên phần cứng (làm theo thứ tự)

1. **LED trạng thái:** cấp nguồn → sáng đứng 1 giây rồi nháy theo trạng thái.
2. **LED chính:** `led on` / `led off` / `led auto`.
3. **DHT11:** `status` thấy nhiệt độ/độ ẩm hợp lý; rút dây DATA ~6 giây → LED trạng thái 3 nháy + log "LỖI DHT11".
4. **RTC:** log `[rtc]` và `[ntp]`; rút nguồn vài phút rồi cắm lại, giờ phải còn (chỉ đúng khi đã lắp pin phù hợp).
5. **Quạt:** `fan 30`, `fan 70`, `fan 100`, `fan off`; ghi lại mức PWM thấp nhất mà quạt quay.
6. **Quạt tự động:** `sim 31 50` (bật 100 %), `sim 25 85` (bật 100 %), `sim 25 50` (tắt), rồi `sim off`.
7. **Hẹn giờ:** đặt khung quanh giờ hiện tại (`sched 1 HH:MM HH:MM`), và khung qua nửa đêm (`sched 2 22:00 06:00`).
8. **Cấu hình bền vững:** đổi ngưỡng, `reboot`, kiểm tra ngưỡng/lịch vẫn còn.
9. **OTA**.

## 7. Kiểm thử logic trên máy tính

Phần logic thuần (`include/logic.h`) không phụ thuộc phần cứng:

```bash
g++ -std=c++11 -Wall -Wextra -o tools/test_logic tools/test_logic.cpp && ./tools/test_logic
```

## 8. Giới hạn hiện tại

- Firmware chưa được nạp thử trên ESP32 thật; đã kiểm tra logic bằng test trên máy tính và kiểm tra biên dịch với thư viện giả lập. Khi chạy thật có thể phải chỉnh nhỏ (đặc biệt tần số PWM `FAN_PWM_FREQ_HZ` và mức quạt tối thiểu `FAN_MIN_PERCENT`).
- Web/REST cục bộ chưa có xác thực, chỉ dùng trong mạng nhà/ký túc xá. OTA có mật khẩu.
- DHT11 sai số ±2 °C và ±5 % RH, độ phân giải 1 đơn vị.
- Chưa có: MQTT, Telegram, lưu đệm dữ liệu khi mất mạng, OLED, backend/dashboard (các giai đoạn sau).

## 9. Cấu trúc

```
include/config.h          chân, hằng số
include/logic.h           logic thuần (hysteresis, mức quạt, khung giờ, LED trạng thái)
include/web_page.h        trang web cục bộ
include/secrets.example.h mẫu thông tin Wi-Fi/OTA
src/main.cpp              chương trình chính
tools/test_logic.cpp      kiểm thử logic
platformio.ini
```
