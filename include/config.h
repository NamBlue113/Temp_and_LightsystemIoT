#pragma once
// =====================================================================
//  config.h - toàn bộ hằng số cấu hình của firmware Smartroom (ESP32)
//  Muốn đổi chân, ngưỡng mặc định, thời gian... chỉ cần sửa ở file này.
// =====================================================================
#include <stdint.h>

#define FW_VERSION "1.0.0"
#define DEVICE_HOSTNAME "smartroom"   // truy cập: http://smartroom.local/

// ---------------------------------------------------------------------
//  Gán chân (khớp với sơ đồ mạch trên breadboard)
// ---------------------------------------------------------------------
constexpr uint8_t PIN_I2C_SDA    = 21;  // DS3231 (và OLED nếu thêm sau này)
constexpr uint8_t PIN_I2C_SCL    = 22;
constexpr uint8_t PIN_DHT        = 4;   // DATA của DHT11
constexpr uint8_t PIN_LED_STATUS = 18;  // LED xanh dương: báo trạng thái mạch
constexpr uint8_t PIN_LED_MAIN   = 19;  // LED chính (đèn được hẹn giờ)
constexpr uint8_t PIN_FAN_PWM    = 23;  // PWM vào module MOSFET điều khiển quạt
constexpr uint8_t PIN_LIGHT      = 34;  // ngõ ra analog (AO) của cảm biến ánh sáng - GPIO34 chỉ làm ngõ vào
// GPIO26 (buzzer) chưa dùng trong giai đoạn này.

// ---------------------------------------------------------------------
//  Cảm biến ánh sáng: đèn Auto chỉ bật khi ĐANG trong khung giờ VÀ phòng tối
//  Mức sáng = giá trị ADC 0..4095 (0 = tối hẳn). Đo thực tế trên mạch này:
//  che kín cảm biến ~0-70, phòng bật đèn buổi tối ~270-295.
// ---------------------------------------------------------------------
constexpr bool     LIGHT_DARK_WHEN_HIGH   = false; // false: càng tối điện áp AO càng THẤP (đúng với cảm biến đang dùng)
constexpr int      LIGHT_DARK_LEVEL       = 120;   // mức sáng <= giá trị này -> coi là TỐI
constexpr int      LIGHT_BRIGHT_LEVEL     = 200;   // mức sáng >= giá trị này -> coi là SÁNG (ở giữa: giữ nguyên)
constexpr uint32_t LIGHT_READ_INTERVAL_MS = 500;

// ---------------------------------------------------------------------
//  Lịch sử cho biểu đồ: 24 giờ trong RAM, đồng thời ghi flash (LittleFS)
//  288 mẫu x 5 phút = 24 giờ
// ---------------------------------------------------------------------
constexpr int      HIST_MAX         = 288;
#ifndef HIST_INTERVAL_SEC
#define HIST_INTERVAL_SEC 300          // chu kỳ ghi mẫu (giây); chỉ đổi khi kiểm thử
#endif
constexpr uint32_t HIST_INTERVAL_S  = HIST_INTERVAL_SEC;
constexpr int      HIST_KEEP_DAYS   = 30;     // số ngày lịch sử giữ trong flash
constexpr uint32_t LIGHT_HOLD_MS          = 3000;  // phải tối/sáng liên tục chừng này mới đổi trạng thái (chống nhấp nháy)

// ---------------------------------------------------------------------
//  PWM quạt
// ---------------------------------------------------------------------
// Module MOSFET dạng trigger thường chịu được tới khoảng 20 kHz.
// Nếu quạt kêu rít hoặc chạy không mượt, thử hạ xuống 1000 - 5000.
constexpr uint32_t FAN_PWM_FREQ_HZ  = 1000;   // 20 kHz: quạt không quay nổi ở 40% với module MOSFET thực tế
constexpr uint8_t  FAN_PWM_BITS     = 8;     // duty 0..255
constexpr uint8_t  FAN_PWM_CHANNEL  = 0;     // chỉ dùng với Arduino-ESP32 2.x
constexpr int      FAN_MIN_PERCENT  = 30;    // dưới mức này quạt thường không quay
constexpr uint32_t FAN_KICK_MS      = 400;   // chạy 100% ngắn khi khởi động để quạt vượt ma sát

// ---------------------------------------------------------------------
//  Cảm biến DHT11
// ---------------------------------------------------------------------
constexpr uint32_t SENSOR_READ_INTERVAL_MS = 2000;  // DHT11 đọc tối đa 1 lần/giây
constexpr uint8_t  SENSOR_FAIL_LIMIT       = 3;     // đọc lỗi liên tiếp bao nhiêu lần thì báo lỗi cảm biến

// ---------------------------------------------------------------------
//  Lịch hẹn giờ LED
// ---------------------------------------------------------------------
constexpr int MAX_SLOTS = 6;             // số khung giờ tối đa

// ---------------------------------------------------------------------
//  Thời gian / mạng
// ---------------------------------------------------------------------
#define TZ_VIETNAM "<+07>-7"             // UTC+7, Việt Nam không có giờ mùa hè
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define NTP_SERVER_3 "time.cloudflare.com"
constexpr uint32_t VALID_TIME_EPOCH = 1735689600UL;  // 2025-01-01, mốc để coi giờ hệ thống là hợp lệ
constexpr uint32_t WIFI_RETRY_MS    = 20000;         // thử kết nối lại Wi-Fi sau mỗi 20 giây

// ---------------------------------------------------------------------
//  Cấu hình Wi-Fi qua điện thoại: ESP32 tự phát Wi-Fi riêng để chọn mạng
//  (mật khẩu của Wi-Fi riêng: SETUP_AP_PASSWORD trong secrets.h)
// ---------------------------------------------------------------------
#define SETUP_AP_SSID "Smartroom-Setup"
constexpr uint8_t  PIN_BOOT_BUTTON       = 0;       // nút BOOT có sẵn trên board
constexpr uint32_t BOOT_HOLD_MS          = 3000;    // giữ nút BOOT chừng này (khi mạch đã chạy) để mở cài đặt
constexpr int      WIFI_MAX_SAVED        = 3;       // số mạng được nhớ
constexpr int      WIFI_SCAN_MAX         = 20;      // số mạng tối đa hiển thị khi quét
constexpr uint32_t WIFI_ATTEMPT_MS       = 12000;   // thời gian thử một mạng đã lưu
constexpr uint32_t WIFI_TRIAL_MS         = 15000;   // thời gian thử mạng vừa nhập trên điện thoại
constexpr uint32_t WIFI_LOST_PORTAL_MS   = 120000;  // mất mạng quá lâu thì phát Wi-Fi riêng
constexpr uint32_t WIFI_PORTAL_RETRY_MS  = 60000;   // đang phát Wi-Fi riêng: thử lại mạng đã lưu (khi không ai kết nối)
constexpr uint32_t WIFI_PORTAL_LINGER_MS = 30000;   // đã có mạng: giữ Wi-Fi riêng thêm chừng này để xem kết quả
constexpr uint32_t WIFI_PORTAL_IDLE_MS   = 300000;  // mở bằng tay mà không ai kết nối: tự tắt
constexpr uint32_t WIFI_SCAN_GAP_MS      = 300;     // nghỉ giữa hai kênh khi quét lúc đang phát Wi-Fi riêng
constexpr uint32_t WIFI_SCAN_DWELL_MS    = 120;     // thời gian nghe mỗi kênh trong trường hợp đó
constexpr uint32_t WIFI_OPEN_DELAY_MS    = 500;     // chờ gửi xong phản hồi web rồi mới bật Wi-Fi riêng
constexpr uint32_t LCD_SHOW_IP_MS        = 10000;   // LCD hiện địa chỉ IP sau khi kết nối

// ---------------------------------------------------------------------
//  LED trạng thái
// ---------------------------------------------------------------------
constexpr uint32_t BOOT_LED_SOLID_MS    = 1000;  // LED trạng thái sáng đứng 1 giây khi cấp nguồn

// ---------------------------------------------------------------------
//  Màn hình LCD I2C (LCD1602/2004 + mạch PCF8574), chung bus I2C với DS3231
//  Địa chỉ được tự dò lúc khởi động (thường là 0x27 hoặc 0x3F).
// ---------------------------------------------------------------------
constexpr uint8_t  LCD_COLS = 16;
constexpr uint8_t  LCD_ROWS = 2;
constexpr uint32_t LCD_UPDATE_INTERVAL_MS = 1000;

// ---------------------------------------------------------------------
//  Điều khiển từ xa qua Firebase (tài khoản đặt trong secrets.h)
// ---------------------------------------------------------------------
constexpr uint32_t CLOUD_STATE_INTERVAL_MS = 10000;  // gửi trạng thái lên cơ sở dữ liệu
constexpr int      CLOUD_KEY_MAX        = 40;        // độ dài tối đa của mã lệnh
constexpr int      CLOUD_CMD_MAX        = 1024;      // lệnh nhận về dài tối đa (byte)
constexpr int      CLOUD_LINE_MAX       = 4096;      // một dòng của luồng nhận lệnh
constexpr int      CLOUD_CMD_MAX_AGE_S  = 30;        // lệnh cũ hơn mức này bị bỏ qua
constexpr uint32_t CLOUD_SILENCE_MS     = 75000;     // im lặng quá lâu thì mở lại luồng
constexpr uint32_t CLOUD_RETRY_MS       = 5000;      // thử lại sau lỗi mạng
constexpr uint32_t CLOUD_AUTH_RETRY_MS  = 60000;     // thử lại sau khi bị từ chối
constexpr uint32_t CLOUD_TASK_STACK     = 14336;

// ---------------------------------------------------------------------
//  Khác
// ---------------------------------------------------------------------
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 10000;  // in 1 dòng trạng thái ra Serial mỗi 10 giây
#define NVS_NAMESPACE "smartroom"
