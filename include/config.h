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
//  Lịch sử cho biểu đồ (lưu trong RAM, mất khi khởi động lại)
//  288 mẫu x 5 phút = 24 giờ
// ---------------------------------------------------------------------
constexpr int      HIST_MAX         = 288;
constexpr uint32_t HIST_INTERVAL_MS = 300000;
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

// Giai đoạn 1 chưa có MQTT/backend nên trạng thái "mất kết nối backend" chưa dùng.
constexpr bool BACKEND_ENABLED = false;

// ---------------------------------------------------------------------
//  Khác
// ---------------------------------------------------------------------
constexpr uint32_t STATUS_PRINT_INTERVAL_MS = 10000;  // in 1 dòng trạng thái ra Serial mỗi 10 giây
#define NVS_NAMESPACE "smartroom"
