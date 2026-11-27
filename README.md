# Smartroom ESP32

Firmware C++ (Arduino framework, PlatformIO) cho ESP32: đo nhiệt độ/độ ẩm/ánh sáng, quạt PWM tự động, đèn theo lịch và ánh sáng, Auto/Manual, LED trạng thái, OTA. ESP32 tự phục vụ trang web và REST API trong mạng nhà, đổi Wi-Fi được từ điện thoại, lưu lịch sử vào flash (xuất CSV) và điều khiển từ xa qua Firebase.

## 1. Sơ đồ chân

| Chức năng | GPIO |
|---|---|
| I2C SDA / SCL (DS3231, LCD) | 21 / 22 |
| DHT11 (DATA) | 4 |
| LED trạng thái (xanh dương) | 18 |
| LED chính | 19 |
| PWM quạt → module MOSFET | 23 |
| Cảm biến ánh sáng (AO) | 34 |
| Nút BOOT có sẵn trên board (mở cài đặt Wi-Fi) | 0 |

Đổi chân hoặc ngưỡng mặc định: sửa `include/config.h`.

## 2. Cài đặt và nạp code

1. Cài VS Code + PlatformIO (hoặc `pip install platformio`).
2. Copy `include/secrets.example.h` thành `include/secrets.h`, điền tên Wi-Fi, mật khẩu Wi-Fi, mật khẩu OTA, mật khẩu web (`WEB_PASSWORD`) và mật khẩu Wi-Fi cài đặt (`SETUP_AP_PASSWORD`, ít nhất 8 ký tự). File này đã nằm trong `.gitignore`.
3. Nạp qua USB: `pio run -t upload`, xem log: `pio device monitor` (115200).
4. Nạp qua Wi-Fi (sau lần nạp USB đầu tiên):
   ```bash
   export OTA_PASSWORD="mat-khau-giong-trong-secrets.h"
   pio run -e esp32dev_ota -t upload
   ```
5. Trên Ubuntu nếu không thấy cổng `/dev/ttyUSB0`: `sudo apt remove brltty` và `sudo usermod -aG dialout $USER`, rồi đăng xuất/đăng nhập lại.

Khi ESP32 vào Wi-Fi, Serial in ra địa chỉ `http://<IP>/` (hoặc `http://smartroom.local/`).

### Đổi mạng Wi-Fi từ điện thoại (không cần nạp lại code)

Khi không vào được mạng đã nhớ, ESP32 tự phát Wi-Fi `Smartroom-Setup` (mật khẩu `SETUP_AP_PASSWORD`).

1. Nối điện thoại vào `Smartroom-Setup`; trang cài đặt tự bật, hoặc mở `http://192.168.4.1/`.
2. Chọn mạng, nhập mật khẩu, bấm **Kết nối**. Đúng thì mạng được lưu và LCD hiện địa chỉ IP mới; sai thì các mạng cũ vẫn giữ nguyên.
3. Nối điện thoại vào cùng mạng đó rồi mở dashboard theo IP trên LCD.

| Tình huống | Cách xử lý |
|---|---|
| Khởi động, không thấy mạng đã nhớ | Phát Wi-Fi cài đặt sau lượt thử đầu tiên |
| Đang chạy thì mất mạng | Tự thử lại; quá 2 phút thì phát Wi-Fi cài đặt |
| Muốn đổi mạng khi đang kết nối | Nút **Đổi Wi-Fi** trên dashboard, giữ nút **BOOT** 3 giây, hoặc lệnh `wifi setup` |
| Chưa có mạng nhưng muốn điều khiển | `http://192.168.4.1/dashboard` (cần mật khẩu web) |

ESP32 nhớ tối đa 3 mạng (lưu không mã hóa trong NVS); mạng trong `secrets.h` luôn là mạng dự phòng. Chỉ hỗ trợ Wi-Fi 2,4 GHz; không dùng được mạng phải đăng nhập qua trang web hoặc cần tên tài khoản.

### Lịch sử và CSV

Cứ 5 phút ESP32 ghi một mẫu (nhiệt độ, độ ẩm, ánh sáng) vào flash, mỗi ngày một file, giữ 30 ngày; khởi động lại không mất. Dashboard vẽ 24 giờ gần nhất (khoảng trống là lúc thiết bị tắt), nút **Xuất CSV** tải toàn bộ dữ liệu đang lưu. Mẫu không được ghi khi đang giả lập, cảm biến lỗi hoặc chưa có giờ.

### Điều khiển từ xa qua Firebase

```
Điện thoại ── trang web ──HTTPS──> Firebase Realtime Database <──HTTPS── ESP32
```

1. Trong dự án Firebase: bật **Authentication → Email/Password**, tạo **hai người dùng** (một cho ESP32, một cho trang web) với mật khẩu mạnh, và tạo **Realtime Database**.
2. Điền `FIREBASE_API_KEY`, `FIREBASE_DB_HOST` vào `include/firebase_config.h` (thông tin công khai).
3. Thêm `FIREBASE_EMAIL`, `FIREBASE_PASSWORD` (tài khoản của **ESP32**) vào `include/secrets.h`, rồi nạp lại.
4. **Khóa cơ sở dữ liệu:** thay hai UID trong `database.rules.json` bằng UID của hai tài khoản trên, rồi dán vào Realtime Database → Rules → **Publish**.
5. Đưa trang web lên mạng (chọn một cách):
   - Firebase Hosting: cài Firebase CLI, `firebase login`, rồi `firebase deploy` (đăng cả luật bảo mật và thư mục `docs/`). Trang ở `https://<project-id>.web.app`.
   - GitHub Pages: Settings → Pages → Deploy from a branch → `main` / `/docs`.
6. Mở trang trên điện thoại, đăng nhập bằng tài khoản dành cho **trang web**.

| Nhánh dữ liệu | Nội dung | Ai ghi |
|---|---|---|
| `/devices/esp32_01/state` | Trạng thái, 10 giây một lần và ngay sau mỗi lệnh; `at` là giờ máy chủ | ESP32 |
| `/devices/esp32_01/cmd/<mã>` | `{"op":"..","body":"<JSON>","at":..}`; `op`: `fan`, `led`, `config`, `light`, `schedule_get`, `schedule_set`, `history` | Trang web |
| `/devices/esp32_01/reply/<mã>` | Chuỗi JSON `{"id":"..","ok":true,...}` | ESP32 |

- ESP32 đăng nhập bằng REST API, nhận lệnh tức thời qua luồng Server-Sent Events, chạy trong tác vụ riêng. Kết nối TLS được xác minh bằng chứng chỉ gốc GTS (`include/google_ca.h`).
- Lệnh cũ hơn 30 giây bị bỏ qua, nên lệnh gửi lúc thiết bị mất mạng không chạy muộn.
- Qua Internet không đổi được Wi-Fi và không giả lập cảm biến. CSV tải từ xa chứa 24 giờ gần nhất.
- Trang web chỉ lưu mã làm mới phiên (không lưu mật khẩu); bấm **Đăng xuất** để xóa.
- Không khai báo `FIREBASE_EMAIL` thì chức năng này tắt hẳn.

Sửa giao diện trong `include/web_page.h` xong, chạy `python tools/build_remote.py` để cập nhật `docs/index.html`.

## 3. Quy tắc hoạt động

**Quạt tự động** (nhiệt độ **hoặc** độ ẩm vượt ngưỡng):

| | Mặc định |
|---|---|
| Nhiệt độ bật / tắt | 30 °C / 28 °C |
| Độ ẩm bật / tắt | 80 % / 75 % |
| Mức quạt khi bật | 100 % (không chia mức) |

Quạt Auto chỉ có hai trạng thái: chạy 100 % hoặc tắt. Code vẫn giữ logic chia 3 mức theo độ vượt ngưỡng; muốn dùng lại thì đổi `levelLow/levelMid/levelHigh` trong `include/logic.h` (ví dụ 40/70/100). Tần số PWM là 1 kHz vì ở 20 kHz quạt không quay nổi ở mức thấp với module MOSFET thực tế. Hysteresis: đã bật thì chỉ tắt khi xuống tới ngưỡng tắt. PWM tối thiểu 30 % (quạt thường không quay dưới mức này) và có xung 100 % ngắn (400 ms) lúc khởi động quạt. Nếu DHT11 lỗi liên tiếp 3 lần, quạt ở chế độ Auto **tắt** (LED trạng thái báo lỗi).

**Đèn:** chế độ Auto chỉ bật khi đang trong khung giờ **và** phòng tối. Tối đa 6 khung giờ, mặc định có 1 khung 18:00–22:00. Khung vắt qua nửa đêm được hỗ trợ (22:00 → 06:00). Chưa có giờ hợp lệ (RTC chưa có giờ và chưa có NTP) thì đèn Auto không tự bật.

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
| 6 | Đã bật điều khiển từ xa nhưng mất kết nối Firebase | 2 nháy, nghỉ 2 giây |
| 7 | Bình thường | 1 nháy ngắn mỗi 3 giây |

Cấu hình được lưu vào flash (NVS): ngưỡng quạt, ngưỡng ánh sáng, lịch hẹn giờ và các mạng Wi-Fi đã nhớ.

## 4. Lệnh Serial (115200 baud, gõ `help`)

```
status                          in trạng thái
fan auto | off | <30..100>      quạt: tự động / tắt / thủ công theo %
led auto | on | off             đèn
set tempon|tempoff|humon|humoff <số>
sched                           xem lịch;  sched 2 22:00 06:00;  sched 2 off
sim <nhiệt_độ> <độ_ẩm>          giả lập cảm biến để thử quạt;  sim off
wifi | wifi setup | wifi forget xem mạng đã nhớ / đổi mạng / xóa mạng đã nhớ
hist | hist clear               xem dung lượng / xóa lịch sử trong flash
reboot
```

`sim` giả lập giá trị cảm biến (không lưu) nên thử được 3 mức PWM mà không cần làm nóng DHT11, rất tiện khi demo.

## 5. REST API (chỉ dùng trong mạng LAN)

Mọi lệnh `/api/*` dưới đây cần header `X-Auth: <WEB_PASSWORD>`; sai 5 lần bị khóa 60 giây.

| Phương thức | Đường dẫn | Nội dung |
|---|---|---|
| GET | `/api/status` | Toàn bộ trạng thái (JSON) |
| POST | `/api/fan` | `{"mode":"auto"}` hoặc `{"mode":"manual","percent":70}` |
| POST | `/api/led` | `{"mode":"auto"}` hoặc `{"mode":"manual","on":true}` |
| POST | `/api/config` | `{"temp_on":30,"temp_off":28,"hum_on":80,"hum_off":75}` (gửi một phần cũng được) |
| GET / POST | `/api/schedule` | `{"slots":[{"enabled":true,"start":"18:00","end":"22:00"}]}` (thay toàn bộ lịch) |
| POST | `/api/sim` | `{"temp":33,"hum":60}` hoặc `{"off":true}` |
| GET | `/api/history` | Lịch sử 24 giờ: các mảng `ts`, `t`, `h`, `l` |
| GET | `/api/history.csv` | CSV toàn bộ dữ liệu đang lưu (`?days=N` để lấy N ngày gần nhất) |
| POST | `/api/light` | `{"dark":120,"bright":200}` |
| POST | `/api/wifi/portal` | Phát Wi-Fi cài đặt |

Các lệnh `/api/wifi/info`, `scan`, `connect`, `save`, `forget` chỉ nhận từ thiết bị đang nối vào Wi-Fi cài đặt (không dùng mật khẩu web).

Ví dụ: `curl -X POST http://smartroom.local/api/fan -H 'X-Auth: MAT_KHAU_WEB' -H 'Content-Type: application/json' -d '{"mode":"manual","percent":70}'`

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

- Web/REST trong mạng nhà dùng HTTP không mã hóa. OTA có mật khẩu.
- Mật khẩu Wi-Fi lưu trong NVS và thông tin trong `secrets.h` không được mã hóa trên chip.
- Mỗi lần đổi mạng, ESP32 có thể đổi kênh nên điện thoại đang nối vào Wi-Fi cài đặt có thể rớt một lúc.
- Lịch sử trong flash bị xóa nếu nạp lại toàn bộ flash (`pio run -t erase`).
- Điều khiển từ xa cần Internet; gói miễn phí của Firebase có giới hạn dung lượng tải và số kết nối.
- Chương trình chiếm khoảng 92 % phân vùng ứng dụng, không còn nhiều chỗ cho tính năng lớn.
- DHT11 sai số ±2 °C và ±5 % RH, độ phân giải 1 đơn vị.
- Chưa có: cảnh báo đẩy về điện thoại, cảm biến phát hiện người, còi.

## 9. Cấu trúc

```
include/config.h          chân, hằng số
include/logic.h           logic thuần (hysteresis, mức quạt, khung giờ, LED trạng thái)
include/web_page.h        trang dashboard
include/wifi_page.h       trang cài đặt Wi-Fi
include/net.h             giao diện phần quản lý Wi-Fi
include/history.h         lịch sử cảm biến
include/remote.h          điều khiển từ xa
include/google_ca.h       chứng chỉ gốc để xác minh máy chủ Google
include/firebase_config.h thông tin dự án Firebase (công khai)
include/secrets.example.h mẫu thông tin Wi-Fi/OTA/web/Firebase
src/main.cpp              chương trình chính
src/net.cpp               kết nối Wi-Fi, Wi-Fi cài đặt, captive portal
src/history.cpp           ghi lịch sử vào flash (LittleFS)
src/remote.cpp            điều khiển từ xa qua Firebase
docs/index.html           trang điều khiển từ xa (sinh tự động)
tools/build_remote.py     sinh docs/index.html
tools/remote_transport.js phần giao tiếp Firebase của trang đó
tools/test_logic.cpp      kiểm thử logic
database.rules.json       luật bảo mật Realtime Database
firebase.json             cấu hình đăng luật và trang web bằng Firebase CLI
platformio.ini
```
