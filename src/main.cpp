/*
 * Smartroom ESP32
 *
 *  - DHT11: nhiệt độ + độ ẩm
 *  - Quạt 5V qua module MOSFET, PWM nhiều mức; tự động theo nhiệt độ HOẶC độ ẩm (có hysteresis)
 *  - LED chính: hẹn giờ nhiều khung (kể cả qua nửa đêm), giờ từ NTP + RTC DS3231
 *  - Auto/Manual riêng cho quạt và đèn
 *  - LED trạng thái (xanh dương): mã nháy theo độ ưu tiên
 *  - Lưu cấu hình vào flash (NVS), cập nhật firmware OTA
 *  - Giao diện web + REST API cục bộ, lệnh Serial để kiểm thử
 *  - Đổi mạng Wi-Fi từ điện thoại qua Wi-Fi riêng của ESP32 (xem net.cpp)
 *  - Lịch sử cảm biến lưu flash, xuất CSV (xem history.cpp)
 *  - Điều khiển từ xa qua Firebase (xem remote.cpp)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <DHT.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include "esp_sntp.h"
#if __has_include("esp_arduino_version.h")
#include "esp_arduino_version.h"
#endif

#include "config.h"
#include "logic.h"
#if !__has_include("secrets.h")
#error "Thieu include/secrets.h - hay copy include/secrets.example.h thanh include/secrets.h roi dien thong tin Wi-Fi"
#endif
#include "secrets.h"
#ifndef WEB_PASSWORD
#error "Thieu WEB_PASSWORD - hay them dong #define WEB_PASSWORD \"...\" vao include/secrets.h (xem secrets.example.h)"
#endif
#include "web_page.h"
#include "wifi_page.h"
#include "net.h"
#include "history.h"
#include "remote.h"

// =====================================================================
//  Trạng thái toàn cục
// =====================================================================
enum class Mode : uint8_t { Auto = 0, Manual = 1 };

static DHT         dht(PIN_DHT, DHT11);
static RTC_DS3231  rtc;
static WebServer   server(80);
static Preferences prefs;

static logic::FanAutoParams params;
static logic::FanAutoState  fanState;
static logic::Slot          slots[MAX_SLOTS];

// Cảm biến
static float    g_temp = NAN, g_hum = NAN;
static bool     g_haveReading = false;
static uint8_t  g_dhtFail = 0;
static bool     g_dhtError = false;
static uint32_t g_lastSensorMs = 0;
static bool     g_simActive = false;

// Quạt
static Mode     g_fanMode = Mode::Auto;
static int      g_fanManualPercent = 0;
static int      g_fanAutoPercent = 0;
static int      g_fanPercent = 0;        // % đang xuất ra thật sự (đã áp mức tối thiểu)
static uint32_t g_fanKickUntil = 0;

// LED chính
static Mode     g_ledMode = Mode::Auto;
static bool     g_ledManualOn = false;
static bool     g_ledAutoOn = false;
static bool     g_ledOutput = false;
static bool     g_ledDirty = true;
static uint32_t g_lastLedCalcMs = 0;

// Thời gian
static bool          g_rtcFound = false;
static volatile bool g_ntpSynced = false;

// Mạng
static bool     g_otaStarted = false;
static bool     g_otaActive = false;

// LED trạng thái
static logic::StatusPattern g_pattern = logic::StatusPattern::Normal;
static uint32_t             g_patternStartMs = 0;

// Cảm biến ánh sáng
static int  g_lightRaw = 0;      // giá trị ADC đã làm mượt (0..4095)
static int  g_lightPct = -1;     // mức sáng 0..100 (-1 = chưa đọc)
static bool g_isDark = false;    // phòng đang tối?

// Ngưỡng ánh sáng (đổi được từ web, lưu trong NVS) và mức sáng hiện tại 0..4095
static int g_lightDarkLevel   = LIGHT_DARK_LEVEL;
static int g_lightBrightLevel = LIGHT_BRIGHT_LEVEL;
static int g_lightLevel       = 0;

// Chống dò mật khẩu web
static uint8_t  g_authFails = 0;
static uint32_t g_lockUntil = 0;

// LCD I2C (nullptr = không tìm thấy màn hình)
static LiquidCrystal_I2C* g_lcd = nullptr;

// Nguyên mẫu hàm
static void updateStatusLed(uint32_t now);

// =====================================================================
//  Tiện ích
// =====================================================================
static bool timeIsValid() {
  return (uint32_t)time(nullptr) > VALID_TIME_EPOCH;
}

static void formatLocalTime(char* buf, size_t n) {
  if (!timeIsValid()) {
    snprintf(buf, n, "--");
    return;
  }
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(buf, n, "%Y-%m-%d %H:%M:%S", &lt);
}

static int splitTokens(char* line, char* tok[], int maxTok) {
  int n = 0;
  char* p = strtok(line, " \t\r\n");
  while (p && n < maxTok) {
    tok[n++] = p;
    p = strtok(nullptr, " \t\r\n");
  }
  return n;
}

// =====================================================================
//  Lưu / đọc cấu hình (NVS)
//  Lưu: ngưỡng quạt + lịch hẹn giờ. Chế độ Auto/Manual KHÔNG lưu: khởi động lại luôn về Auto.
// =====================================================================
static void setDefaultSlots() {
  for (int i = 0; i < MAX_SLOTS; i++) slots[i] = logic::Slot();
  slots[0].enabled = true;          // mặc định: bật đèn 18:00 -> 22:00
  slots[0].startMin = 18 * 60;
  slots[0].endMin = 22 * 60;
}

static void loadSettings() {
  setDefaultSlots();
  prefs.begin(NVS_NAMESPACE, false);

  float tOn  = prefs.getFloat("tOn",  params.tempOn);
  float tOff = prefs.getFloat("tOff", params.tempOff);
  float hOn  = prefs.getFloat("hOn",  params.humOn);
  float hOff = prefs.getFloat("hOff", params.humOff);
  if (logic::validThresholds(tOn, tOff, hOn, hOff, nullptr)) {
    params.tempOn = tOn;  params.tempOff = tOff;
    params.humOn = hOn;   params.humOff = hOff;
  }

  char k[8];
  for (int i = 0; i < MAX_SLOTS; i++) {
    snprintf(k, sizeof(k), "se%d", i);
    bool en = prefs.getBool(k, slots[i].enabled);
    snprintf(k, sizeof(k), "sa%d", i);
    uint16_t a = prefs.getUShort(k, slots[i].startMin);
    snprintf(k, sizeof(k), "sb%d", i);
    uint16_t b = prefs.getUShort(k, slots[i].endMin);
    if (a < 1440 && b < 1440) {
      slots[i].enabled = en;
      slots[i].startMin = a;
      slots[i].endMin = b;
    }
  }
  prefs.end();
}

static void saveThresholds() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putFloat("tOn",  params.tempOn);
  prefs.putFloat("tOff", params.tempOff);
  prefs.putFloat("hOn",  params.humOn);
  prefs.putFloat("hOff", params.humOff);
  prefs.end();
}

static void saveSlots() {
  prefs.begin(NVS_NAMESPACE, false);
  char k[8];
  for (int i = 0; i < MAX_SLOTS; i++) {
    snprintf(k, sizeof(k), "se%d", i);
    prefs.putBool(k, slots[i].enabled);
    snprintf(k, sizeof(k), "sa%d", i);
    prefs.putUShort(k, slots[i].startMin);
    snprintf(k, sizeof(k), "sb%d", i);
    prefs.putUShort(k, slots[i].endMin);
  }
  prefs.end();
}

// =====================================================================
//  Quạt (PWM)
// =====================================================================
static void pwmWriteDuty(uint32_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_FAN_PWM, duty);
#else
  ledcWrite(FAN_PWM_CHANNEL, duty);
#endif
}

static void fanApplyDuty() {
  int eff = g_fanPercent;
  // Vài trăm ms đầu khi quạt đang tắt chuyển sang chạy: cho 100% để quạt thắng ma sát
  if (g_fanPercent > 0 && (int32_t)(g_fanKickUntil - millis()) > 0) eff = 100;
  pwmWriteDuty((uint32_t)eff * ((1u << FAN_PWM_BITS) - 1) / 100);
}

static void fanPwmInit() {
  pinMode(PIN_FAN_PWM, OUTPUT);
  digitalWrite(PIN_FAN_PWM, LOW);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_FAN_PWM, FAN_PWM_FREQ_HZ, FAN_PWM_BITS);
#else
  ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ_HZ, FAN_PWM_BITS);
  ledcAttachPin(PIN_FAN_PWM, FAN_PWM_CHANNEL);
#endif
  pwmWriteDuty(0);
}

// p = 0 -> tắt; 1..FAN_MIN_PERCENT-1 được nâng lên FAN_MIN_PERCENT để quạt chắc chắn quay
static void setFanPercent(int p) {
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  if (p > 0 && p < FAN_MIN_PERCENT) p = FAN_MIN_PERCENT;
  if (p == g_fanPercent) return;
  if (g_fanPercent == 0 && p > 0) g_fanKickUntil = millis() + FAN_KICK_MS;
  g_fanPercent = p;
}

static void setFanAuto() {
  g_fanMode = Mode::Auto;
  Serial.println(F("[quạt] -> AUTO"));
}

static void setFanManual(int percent) {
  g_fanMode = Mode::Manual;
  g_fanManualPercent = constrain(percent, 0, 100);
  Serial.printf("[quạt] -> THỦ CÔNG %d%%\n", g_fanManualPercent);
}

// =====================================================================
//  LED chính
// =====================================================================
static void setLedAuto() {
  g_ledMode = Mode::Auto;
  g_ledDirty = true;
  Serial.println(F("[đèn] -> AUTO"));
}

static void setLedManual(bool on) {
  g_ledMode = Mode::Manual;
  g_ledManualOn = on;
  g_ledDirty = true;
  Serial.printf("[đèn] -> THỦ CÔNG %s\n", on ? "BẬT" : "TẮT");
}

// =====================================================================
//  Cảm biến
// =====================================================================
static void processReading(float t, float h) {
  g_temp = t;
  g_hum = h;
  g_haveReading = true;
  g_fanAutoPercent = logic::fanAutoPercent(fanState, t, h, params);   // luôn cập nhật hysteresis, kể cả khi quạt đang Manual
}

static void setSim(bool active, float t, float h) {
  fanState = logic::FanAutoState();     // bỏ trạng thái hysteresis cũ để không dính giá trị giả lập
  g_fanAutoPercent = 0;
  if (active) {
    g_simActive = true;
    g_dhtFail = 0;
    g_dhtError = false;
    processReading(t, h);
    Serial.printf("[sim] giả lập T=%.1f H=%.0f\n", t, h);
  } else {
    g_simActive = false;
    g_haveReading = false;
    g_lastSensorMs = millis() - SENSOR_READ_INTERVAL_MS;   // đọc cảm biến thật ngay vòng sau
    Serial.println(F("[sim] tắt giả lập"));
  }
}

static void handleSensor(uint32_t now) {
  if ((uint32_t)(now - g_lastSensorMs) < SENSOR_READ_INTERVAL_MS) return;
  g_lastSensorMs = now;

  if (g_simActive) return;   // đang giả lập: giữ nguyên giá trị giả

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    if (g_dhtFail < 255) g_dhtFail++;
    if (!g_dhtError && g_dhtFail >= SENSOR_FAIL_LIMIT) {
      g_dhtError = true;
      Serial.println(F("[dht] LỖI: không đọc được DHT11 - quạt tự động tạm dừng"));
    }
    return;
  }
  if (g_dhtError) Serial.println(F("[dht] cảm biến đã đọc lại bình thường"));
  g_dhtFail = 0;
  g_dhtError = false;
  processReading(t, h);
}

// =====================================================================
//  Cảm biến ánh sáng (analog): xác định phòng tối/sáng, có vùng trễ + thời gian giữ
// =====================================================================
static void loadLight() {
  prefs.begin(NVS_NAMESPACE, false);
  int d = prefs.getInt("ld", LIGHT_DARK_LEVEL);
  int b = prefs.getInt("lb", LIGHT_BRIGHT_LEVEL);
  prefs.end();
  if (d >= 0 && b <= 4095 && d < b) { g_lightDarkLevel = d; g_lightBrightLevel = b; }
}

static void saveLight() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putInt("ld", g_lightDarkLevel);
  prefs.putInt("lb", g_lightBrightLevel);
  prefs.end();
}

static void handleLight(uint32_t now) {
  static uint32_t last = 0, pendingSince = 0;
  static bool first = true, pending = false;
  if (!first && (uint32_t)(now - last) < LIGHT_READ_INTERVAL_MS) return;
  last = now;

  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_LIGHT);
  int raw = (int)(sum / 8);
  g_lightRaw = first ? raw : (g_lightRaw * 3 + raw) / 4;
  int level = LIGHT_DARK_WHEN_HIGH ? (4095 - g_lightRaw) : g_lightRaw;   // mức sáng 0..4095
  int pct = level * 100 / 4095;
  g_lightPct = pct;
  g_lightLevel = level;

  bool want = g_isDark;
  if (level <= g_lightDarkLevel)        want = true;
  else if (level >= g_lightBrightLevel) want = false;

  if (first) {
    first = false;
    g_isDark = want;
    g_ledDirty = true;
    return;
  }
  if (want == g_isDark) { pending = false; return; }
  if (!pending) { pending = true; pendingSince = now; return; }
  if ((uint32_t)(now - pendingSince) >= LIGHT_HOLD_MS) {
    pending = false;
    g_isDark = want;
    g_ledDirty = true;
    Serial.printf("[ánh sáng] phòng %s (mức sáng %d/4095)\n", g_isDark ? "TỐI" : "SÁNG", level);
  }
}

// =====================================================================
//  Cập nhật ngõ ra
// =====================================================================
// Ghi 1 mẫu (nhiệt độ, độ ẩm, ánh sáng) mỗi HIST_INTERVAL_S giây để vẽ biểu đồ.
// Không ghi khi đang giả lập hoặc cảm biến lỗi hoặc chưa có giờ hợp lệ.
static void handleHistory(uint32_t now) {
  static uint32_t lastCheck = 0;
  if ((uint32_t)(now - lastCheck) < 1000) return;
  lastCheck = now;
  if (g_simActive || g_dhtError || !g_haveReading || g_lightPct < 0 || !timeIsValid()) return;
  // So theo giờ thật để khởi động lại nhiều lần không ghi mẫu dày hơn chu kỳ.
  // Nếu giờ bị chỉnh lùi hơn 1 giờ thì ghi tiếp bình thường.
  uint32_t t = (uint32_t)time(nullptr);
  uint32_t last = history::lastTs();
  if (last != 0 && t >= last && t - last < HIST_INTERVAL_S) return;
  if (last != 0 && t < last && last - t < 3600) return;
  history::Sample s;
  s.ts = t;
  s.t10 = (int16_t)lroundf(g_temp * 10.0f);
  s.h = (uint8_t)constrain((int)lroundf(g_hum), 0, 100);
  s.light = (uint16_t)constrain(g_lightLevel, 0, 4095);
  history::add(s);
}

static void updateOutputs(uint32_t now) {
  // --- Quạt ---
  int desired;
  if (g_fanMode == Mode::Manual)  desired = g_fanManualPercent;
  else                            desired = g_dhtError ? 0 : g_fanAutoPercent;
  setFanPercent(desired);
  fanApplyDuty();

  // --- LED chính (tính lại mỗi giây hoặc khi có thay đổi) ---
  if (g_ledDirty || (uint32_t)(now - g_lastLedCalcMs) >= 1000) {
    g_lastLedCalcMs = now;
    g_ledDirty = false;
    bool valid = timeIsValid();
    if (valid) {
      time_t t = time(nullptr);
      struct tm lt;
      localtime_r(&t, &lt);
      // Auto: phải đang trong khung giờ VÀ phòng tối (tránh bật đèn khi phòng còn sáng)
      g_ledAutoOn = logic::scheduleActive(slots, MAX_SLOTS, lt.tm_hour * 60 + lt.tm_min) && g_isDark;
    } else {
      g_ledAutoOn = false;     // chưa có giờ hợp lệ thì không tự bật
    }
    g_ledOutput = (g_ledMode == Mode::Manual) ? g_ledManualOn : g_ledAutoOn;
    digitalWrite(PIN_LED_MAIN, g_ledOutput ? HIGH : LOW);
  }
}

static void updateStatusLed(uint32_t now) {
  if (now < BOOT_LED_SOLID_MS) {
    digitalWrite(PIN_LED_STATUS, HIGH);
    return;
  }
  logic::StatusInputs in;
  in.ota            = g_otaActive;
  in.dhtError       = g_dhtError;
  in.rtcFault       = (!g_rtcFound || !timeIsValid());
  in.overThreshold  = !g_dhtError && g_haveReading && (fanState.tempHot || fanState.humHigh);
  in.wifiConnecting = (WiFi.status() != WL_CONNECTED);
  in.backendEnabled = remote::enabled();
  in.backendLost    = !remote::connected();

  logic::StatusPattern pat = logic::pickStatusPattern(in);
  if (pat != g_pattern) {
    g_pattern = pat;
    g_patternStartMs = now;
  }
  digitalWrite(PIN_LED_STATUS, logic::statusLedLevel(g_pattern, now - g_patternStartMs) ? HIGH : LOW);
}

// =====================================================================
//  LCD I2C: dò bus I2C, tìm màn hình, hiển thị nhiệt độ/độ ẩm/giờ/quạt/đèn
// =====================================================================
static void lcdInit() {
  uint8_t lcdAddr = 0;
  Serial.print(F("[i2c] thiết bị tìm thấy:"));
  uint32_t scanStart = millis();
  int busErrors = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    uint8_t rc = Wire.endTransmission();
    if (rc != 0) {
      // 2 = không có thiết bị ở địa chỉ này (bình thường); mã khác = lỗi bus
      if (rc != 2 && ++busErrors >= 3) {
        Serial.printf("\n[i2c] LỖI bus (mã %u tại 0x%02X, %lu ms) - dừng dò, bỏ qua LCD\n",
                      rc, a, (unsigned long)(millis() - scanStart));
        return;
      }
      continue;
    }
    Serial.printf(" 0x%02X", a);
    // PCF8574: 0x20..0x27, PCF8574A: 0x38..0x3F (trừ 0x3C/0x3D là OLED SSD1306)
    bool pcf = (a >= 0x20 && a <= 0x27) || (a >= 0x38 && a <= 0x3F && a != 0x3C && a != 0x3D);
    if (pcf && lcdAddr == 0) lcdAddr = a;
  }
  Serial.println();
  if (lcdAddr == 0) {
    Serial.println(F("[lcd] KHÔNG tìm thấy LCD I2C - kiểm tra SDA/SCL/VCC(5V)/GND"));
    return;
  }
  g_lcd = new LiquidCrystal_I2C(lcdAddr, LCD_COLS, LCD_ROWS);
  g_lcd->init();
  g_lcd->backlight();
  g_lcd->clear();
  g_lcd->setCursor(0, 0);
  g_lcd->print(F("Smartroom v" FW_VERSION));
  Serial.printf("[lcd] tìm thấy LCD tại 0x%02X\n", lcdAddr);
}

static void updateLcd(uint32_t now) {
  static uint32_t last = 0;
  static char prev[2][LCD_COLS + 1] = { "", "" };
  if (!g_lcd || now < 1500 || (uint32_t)(now - last) < LCD_UPDATE_INTERVAL_MS) return;
  last = now;

  char line[2][32];
  char hhmm[6] = "--:--";
  if (timeIsValid()) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", lt.tm_hour, lt.tm_min);
  }
  // LCD không có dấu tiếng Việt nên dùng chữ không dấu
  // Vừa kết nối: hiện địa chỉ IP. Đang phát Wi-Fi riêng: xen kẽ 4 giây hướng dẫn / 4 giây số liệu.
  if (net::connected() && net::connectedAt() != 0 && (uint32_t)(millis() - net::connectedAt()) < LCD_SHOW_IP_MS) {
    snprintf(line[0], sizeof(line[0]), "WiFi OK");
    snprintf(line[1], sizeof(line[1]), "%s", WiFi.localIP().toString().c_str());
  } else if (net::portalActive() && !net::connected() && (now / 4000) % 2 == 0) {
    snprintf(line[0], sizeof(line[0]), "%s", SETUP_AP_SSID);
    snprintf(line[1], sizeof(line[1]), "%s", WiFi.softAPIP().toString().c_str());
  } else {
    if (g_dhtError)          snprintf(line[0], sizeof(line[0]), "DHT loi    %s", hhmm);
    else if (!g_haveReading) snprintf(line[0], sizeof(line[0]), "Dang doc.. %s", hhmm);
    else                     snprintf(line[0], sizeof(line[0]), "%4.1fC %2.0f%% %s", g_temp, g_hum, hhmm);
    snprintf(line[1], sizeof(line[1]), "Fan:%3d%% Led:%s", g_fanPercent, g_ledOutput ? "ON" : "OFF");
  }

  for (int r = 0; r < 2; r++) {
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), "%-*s", (int)LCD_COLS, line[r]);   // đệm khoảng trắng cho đủ dòng
    if (strcmp(buf, prev[r]) == 0) continue;                      // không đổi thì không ghi lại
    strcpy(prev[r], buf);
    g_lcd->setCursor(0, r);
    g_lcd->print(buf);
  }
}

// =====================================================================
//  Thời gian: RTC DS3231 + NTP
// =====================================================================
static void onNtpSync(struct timeval* tv) {
  (void)tv;
  g_ntpSynced = true;
}

static void loadTimeFromRtc() {
  if (!g_rtcFound) {
    Serial.println(F("[rtc] KHÔNG tìm thấy DS3231 - kiểm tra dây SDA/SCL/VCC/GND"));
    return;
  }
  if (rtc.lostPower()) {
    Serial.println(F("[rtc] RTC chưa có giờ (mất nguồn/chưa đặt) - chờ NTP"));
    return;
  }
  DateTime n = rtc.now();
  if (n.year() < 2025) {
    Serial.println(F("[rtc] giờ RTC không hợp lệ - chờ NTP"));
    return;
  }
  struct timeval tv;
  tv.tv_sec = (time_t)n.unixtime();
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  char buf[24];
  formatLocalTime(buf, sizeof(buf));
  Serial.printf("[rtc] đã nạp giờ từ RTC: %s\n", buf);
}

static void handleRtcSync() {
  if (!g_ntpSynced) return;
  g_ntpSynced = false;
  time_t now = time(nullptr);
  char buf[24];
  formatLocalTime(buf, sizeof(buf));
  Serial.printf("[ntp] đồng bộ xong: %s\n", buf);
  if (g_rtcFound) {
    long diff = (long)now - (long)rtc.now().unixtime();
    if (diff > 1 || diff < -1 || rtc.lostPower()) {
      rtc.adjust(DateTime((uint32_t)now));
      Serial.printf("[rtc] đã ghi giờ NTP vào RTC (lệch %ld giây)\n", diff);
    }
  }
  g_ledDirty = true;
}

// =====================================================================
//  Trạng thái dạng JSON / dòng chữ
// =====================================================================
static String statusJson() {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["uptime_s"] = millis() / 1000;
  d["heap_free"] = ESP.getFreeHeap();
  d["heap_min"] = ESP.getMinFreeHeap();

  char tbuf[24];
  formatLocalTime(tbuf, sizeof(tbuf));
  JsonObject tm = d["time"].to<JsonObject>();
  tm["local"] = String(tbuf);
  tm["valid"] = timeIsValid();
  tm["rtc_found"] = g_rtcFound;
  if (g_rtcFound) tm["rtc_temp"] = rtc.getTemperature();

  JsonObject w = d["wifi"].to<JsonObject>();
  bool conn = (WiFi.status() == WL_CONNECTED);
  w["connected"] = conn;
  w["portal"] = net::portalActive();
  w["setup_ssid"] = SETUP_AP_SSID;
  if (conn) {
    w["ssid"] = WiFi.SSID();
    w["rssi"] = WiFi.RSSI();
    w["ip"] = WiFi.localIP().toString();
  }

  JsonObject s = d["sensor"].to<JsonObject>();
  if (g_haveReading && !g_dhtError) {
    s["temperature"] = g_temp;
    s["humidity"] = g_hum;
  } else {
    s["temperature"] = nullptr;
    s["humidity"] = nullptr;
  }
  s["error"] = g_dhtError;
  s["sim"] = g_simActive;

  JsonObject f = d["fan"].to<JsonObject>();
  f["mode"] = (g_fanMode == Mode::Auto) ? "auto" : "manual";
  f["percent"] = g_fanPercent;
  f["auto_percent"] = g_fanAutoPercent;
  f["manual_percent"] = g_fanManualPercent;
  f["temp_hot"] = fanState.tempHot;
  f["hum_high"] = fanState.humHigh;

  JsonObject l = d["led"].to<JsonObject>();
  l["mode"] = (g_ledMode == Mode::Auto) ? "auto" : "manual";
  l["on"] = g_ledOutput;
  l["auto_on"] = g_ledAutoOn;

  JsonObject li = d["light"].to<JsonObject>();
  li["percent"] = g_lightPct;
  li["raw"] = g_lightRaw;
  li["dark"] = g_isDark;
  li["level"] = g_lightLevel;
  li["dark_level"] = g_lightDarkLevel;
  li["bright_level"] = g_lightBrightLevel;

  JsonObject c = d["config"].to<JsonObject>();
  c["temp_on"] = params.tempOn;
  c["temp_off"] = params.tempOff;
  c["hum_on"] = params.humOn;
  c["hum_off"] = params.humOff;

  JsonObject cl = d["cloud"].to<JsonObject>();
  cl["enabled"] = remote::enabled();
  cl["connected"] = remote::connected();

  JsonObject hi = d["history"].to<JsonObject>();
  hi["flash"] = history::flashOk();
  hi["samples"] = history::count();

  d["status_led"] = logic::statusPatternName(g_pattern);

  String out;
  serializeJson(d, out);
  return out;
}

static void printStatusLine() {
  char tbuf[24];
  formatLocalTime(tbuf, sizeof(tbuf));
  char th[24];
  if (g_haveReading && !g_dhtError) snprintf(th, sizeof(th), "T=%.1fC H=%.0f%%", g_temp, g_hum);
  else                              snprintf(th, sizeof(th), "T=-- H=--");
  Serial.printf("[%s] %s%s | quạt: %s %d%% | đèn: %s %s | ánh sáng: %d%% raw=%d (%s) | wifi: %s | LED trạng thái: %s\n",
                tbuf, th, g_simActive ? " (giả lập)" : "",
                g_fanMode == Mode::Auto ? "AUTO" : "THỦ CÔNG", g_fanPercent,
                g_ledMode == Mode::Auto ? "AUTO" : "THỦ CÔNG", g_ledOutput ? "sáng" : "tắt",
                g_lightPct, g_lightRaw, g_isDark ? "tối" : "sáng",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "chưa kết nối",
                logic::statusPatternName(g_pattern));
}

static void printSlots() {
  char a[6], b[6];
  for (int i = 0; i < MAX_SLOTS; i++) {
    logic::formatHHMM(slots[i].startMin, a);
    logic::formatHHMM(slots[i].endMin, b);
    Serial.printf("  khung %d: %s  %s -> %s%s\n", i + 1, slots[i].enabled ? "BẬT" : "tắt", a, b,
                  (slots[i].enabled && slots[i].startMin > slots[i].endMin) ? "  (qua nửa đêm)" : "");
  }
}

// =====================================================================
//  Thao tác dùng chung cho Serial và REST
// =====================================================================
static bool applyThresholds(float tOn, float tOff, float hOn, float hOff, const char** err) {
  if (!logic::validThresholds(tOn, tOff, hOn, hOff, err)) return false;
  params.tempOn = tOn;  params.tempOff = tOff;
  params.humOn = hOn;   params.humOff = hOff;
  saveThresholds();
  Serial.printf("[cấu hình] ngưỡng: nhiệt độ %.1f/%.1f C, độ ẩm %.0f/%.0f %%\n", tOn, tOff, hOn, hOff);
  return true;
}

static bool applySlots(const logic::Slot* in, const char** err) {
  for (int i = 0; i < MAX_SLOTS; i++) {
    if (in[i].enabled && in[i].startMin == in[i].endMin) {
      if (err) *err = "Gio bat va gio tat khong duoc trung nhau";
      return false;
    }
  }
  for (int i = 0; i < MAX_SLOTS; i++) slots[i] = in[i];
  saveSlots();
  g_ledDirty = true;
  Serial.println(F("[lịch] đã lưu lịch hẹn giờ:"));
  printSlots();
  return true;
}

// =====================================================================
//  REST API
// =====================================================================
static void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

static void sendOk() {
  sendJson(200, "{\"ok\":true}");
}

static void sendErr(int code, const char* msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  String out;
  serializeJson(d, out);
  sendJson(code, out);
}

// Xác thực: client gửi mật khẩu trong header X-Auth. Sai 5 lần -> khóa 60 giây.
static bool authOk() {
  uint32_t now = millis();
  if ((int32_t)(g_lockUntil - now) > 0) { sendErr(429, "Thu sai qua nhieu, doi 1 phut"); return false; }
  String p = server.header("X-Auth");
  const char* pw = WEB_PASSWORD;
  size_t n = strlen(pw);
  uint8_t diff = (p.length() != n) ? 1 : 0;
  for (size_t i = 0; i < n; i++) diff |= (uint8_t)(((i < p.length()) ? p[i] : 0) ^ pw[i]);   // so sánh không rò thời gian
  if (diff == 0) { g_authFails = 0; return true; }
  if (++g_authFails >= 5) { g_lockUntil = now + 60000; g_authFails = 0; }
  sendErr(401, "Sai mat khau");
  return false;
}

static bool readBody(JsonDocument& doc) {
  if (!server.hasArg("plain")) return false;
  DeserializationError e = deserializeJson(doc, server.arg("plain"));
  return !e;
}

// ---------------------------------------------------------------------
//  Lệnh dùng chung cho REST và Firebase. Trả về false và đặt *err nếu dữ liệu sai.
// ---------------------------------------------------------------------
static bool cmdFan(JsonVariantConst d, const char** err) {
  const char* mode = d["mode"] | "";
  if (strcmp(mode, "auto") == 0) { setFanAuto(); return true; }
  if (strcmp(mode, "manual") != 0) { *err = "mode phai la auto hoac manual"; return false; }
  int p = d["percent"] | -1;
  if (p < 0 || p > 100) { *err = "percent phai trong 0..100"; return false; }
  setFanManual(p);
  return true;
}

static bool cmdLed(JsonVariantConst d, const char** err) {
  const char* mode = d["mode"] | "";
  if (strcmp(mode, "auto") == 0) { setLedAuto(); return true; }
  if (strcmp(mode, "manual") != 0) { *err = "mode phai la auto hoac manual"; return false; }
  if (!d["on"].is<bool>()) { *err = "thieu truong on (true/false)"; return false; }
  setLedManual(d["on"].as<bool>());
  return true;
}

static bool cmdConfig(JsonVariantConst d, const char** err) {
  float tOn  = d["temp_on"]  | params.tempOn;
  float tOff = d["temp_off"] | params.tempOff;
  float hOn  = d["hum_on"]   | params.humOn;
  float hOff = d["hum_off"]  | params.humOff;
  return applyThresholds(tOn, tOff, hOn, hOff, err);
}

static bool cmdLight(JsonVariantConst d, const char** err) {
  int dk = d["dark"] | g_lightDarkLevel;
  int br = d["bright"] | g_lightBrightLevel;
  if (dk < 0 || br > 4095 || dk + 20 > br) { *err = "Nguong TOI phai nho hon nguong SANG it nhat 20 (0..4095)"; return false; }
  g_lightDarkLevel = dk;
  g_lightBrightLevel = br;
  saveLight();
  g_ledDirty = true;
  Serial.printf("[ánh sáng] ngưỡng: tối <= %d, sáng >= %d\n", dk, br);
  return true;
}

static bool cmdScheduleSet(JsonVariantConst d, const char** err) {
  if (!d["slots"].is<JsonArrayConst>()) { *err = "thieu mang slots"; return false; }
  JsonArrayConst arr = d["slots"].as<JsonArrayConst>();
  if ((int)arr.size() > MAX_SLOTS) { *err = "qua nhieu khung gio (toi da 6)"; return false; }
  logic::Slot tmp[MAX_SLOTS];     // khung không được gửi lên sẽ là tắt
  int i = 0;
  for (JsonObjectConst o : arr) {
    uint16_t a, b;
    if (!logic::parseHHMM(o["start"] | "", a) || !logic::parseHHMM(o["end"] | "", b)) {
      *err = "gio phai co dang HH:MM";
      return false;
    }
    tmp[i].enabled = o["enabled"] | false;
    tmp[i].startMin = a;
    tmp[i].endMin = b;
    i++;
  }
  return applySlots(tmp, err);
}

static void fillSchedule(JsonDocument& d) {
  JsonArray arr = d["slots"].to<JsonArray>();
  char a[6], b[6];
  for (int i = 0; i < MAX_SLOTS; i++) {
    logic::formatHHMM(slots[i].startMin, a);
    logic::formatHHMM(slots[i].endMin, b);
    JsonObject o = arr.add<JsonObject>();
    o["enabled"] = slots[i].enabled;
    o["start"] = String(a);
    o["end"] = String(b);
  }
}

// Lịch sử 24 giờ gần nhất: mảng cũ -> mới, "ts" là thời điểm (epoch) của từng mẫu
static void fillHistory(JsonDocument& d) {
  uint32_t from = timeIsValid() ? (uint32_t)time(nullptr) - 86400UL : 0;
  d["interval"] = HIST_INTERVAL_S;
  JsonArray ts = d["ts"].to<JsonArray>();
  JsonArray t = d["t"].to<JsonArray>();
  JsonArray h = d["h"].to<JsonArray>();
  JsonArray l = d["l"].to<JsonArray>();
  int n = 0;
  uint32_t end = 0;
  for (int i = 0; i < history::count(); i++) {
    const history::Sample& s = history::at(i);
    if (s.ts < from) continue;
    ts.add(s.ts);
    t.add(serialized(String(s.t10 / 10.0f, 1)));
    h.add(s.h);
    l.add(s.light);
    end = s.ts;
    n++;
  }
  d["n"] = n;
  d["end"] = end;
}

// ---------------------------------------------------------------------
//  REST handler
// ---------------------------------------------------------------------
static void handleApiStatus() {
  if (!authOk()) return;
  sendJson(200, statusJson());
}

static void handleCommand(bool (*fn)(JsonVariantConst, const char**)) {
  if (!authOk()) return;
  JsonDocument d;
  if (!readBody(d)) { sendErr(400, "JSON khong hop le"); return; }
  const char* err = nullptr;
  if (!fn(d.as<JsonVariantConst>(), &err)) { sendErr(400, err); return; }
  sendOk();
}

static void sendDoc(const JsonDocument& d) {
  String out;
  serializeJson(d, out);
  sendJson(200, out);
}

static void handleApiHistory() {
  if (!authOk()) return;
  JsonDocument d;
  fillHistory(d);
  sendDoc(d);
}

// Xuất CSV toàn bộ dữ liệu trong flash (hoặc ?days=N ngày gần nhất), gửi theo từng khối
struct CsvCtx { String buf; uint32_t rows; };

static void csvRow(const history::Sample& s, void* p) {
  CsvCtx* c = static_cast<CsvCtx*>(p);
  time_t t = (time_t)s.ts;
  struct tm lt;
  localtime_r(&t, &lt);
  char when[24], row[80];
  strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &lt);
  snprintf(row, sizeof(row), "%s,%lu,%.1f,%u,%u\r\n", when, (unsigned long)s.ts,
           s.t10 / 10.0f, (unsigned)s.h, (unsigned)s.light);
  c->buf += row;
  c->rows++;
  if (c->buf.length() >= 1400) {
    server.sendContent(c->buf);
    c->buf = "";
  }
}

static void handleApiHistoryCsv() {
  if (!authOk()) return;
  uint32_t from = 0;
  int days = server.arg("days").toInt();
  if (days > 0 && days <= HIST_KEEP_DAYS && timeIsValid()) from = (uint32_t)time(nullptr) - (uint32_t)days * 86400UL;
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "attachment; filename=\"smartroom-history.csv\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv; charset=utf-8", "");
  CsvCtx c;
  c.rows = 0;
  c.buf.reserve(1600);
  c.buf = "thoi_gian,epoch,nhiet_do_c,do_am_pct,anh_sang\r\n";
  if (history::flashOk()) {
    history::forEachStored(from, csvRow, &c);
  } else {
    for (int i = 0; i < history::count(); i++) csvRow(history::at(i), &c);
  }
  if (c.buf.length()) server.sendContent(c.buf);
  server.sendContent("");
}

static void handleApiScheduleGet() {
  if (!authOk()) return;
  JsonDocument d;
  fillSchedule(d);
  sendDoc(d);
}

static void handleApiSim() {
  if (!authOk()) return;
  JsonDocument d;
  if (!readBody(d)) { sendErr(400, "JSON khong hop le"); return; }
  if (d["off"] | false) {
    setSim(false, 0, 0);
    sendOk();
    return;
  }
  if (!d["temp"].is<float>() || !d["hum"].is<float>()) { sendErr(400, "can truong temp va hum"); return; }
  float t = d["temp"].as<float>();
  float h = d["hum"].as<float>();
  if (t < -10 || t > 80 || h < 0 || h > 100) { sendErr(400, "gia tri ngoai khoang cho phep"); return; }
  setSim(true, t, h);
  sendOk();
}

// ---------------------------------------------------------------------
//  Cài đặt Wi-Fi: chỉ nhận yêu cầu đến từ thiết bị đang nối vào Wi-Fi riêng
//  (đã phải biết mật khẩu của Wi-Fi riêng), nên không dùng thêm mật khẩu web.
// ---------------------------------------------------------------------
static bool fromSetupAp() {
  if (!net::portalActive()) return false;
  IPAddress ap = WiFi.softAPIP();
  IPAddress local = server.client().localIP();
  IPAddress remote = server.client().remoteIP();
  return local == ap && remote[0] == ap[0] && remote[1] == ap[1] && remote[2] == ap[2];
}

static bool requireSetupAp() {
  if (fromSetupAp()) return true;
  sendErr(403, "Chi dung duoc khi noi vao Wi-Fi cai dat cua thiet bi");
  return false;
}

static void sendPage(const char* html) {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", html);
}

static void handleApiWifiInfo() {
  if (!requireSetupAp()) return;
  JsonDocument d;
  bool conn = net::connected();
  d["connected"] = conn;
  d["current"] = conn ? WiFi.SSID() : String();
  d["ip"] = conn ? WiFi.localIP().toString() : String();
  const char* st = "idle";
  switch (net::trialState()) {
    case net::Trial::Connecting: st = "connecting"; break;
    case net::Trial::Ok:         st = "ok";         break;
    case net::Trial::Fail:       st = "fail";       break;
    default: break;
  }
  d["state"] = st;
  d["target"] = net::trialSsid();
  JsonArray saved = d["saved"].to<JsonArray>();
  for (int i = 0; i < net::savedCount(); i++) saved.add(net::savedSsid(i));
  String out;
  serializeJson(d, out);
  sendJson(200, out);
}

static void handleApiWifiScan() {
  if (!requireSetupAp()) return;
  if (server.hasArg("new")) net::requestScan();
  JsonDocument d;
  d["scanning"] = net::scanning();
  JsonArray arr = d["list"].to<JsonArray>();
  for (int i = 0; i < net::scanCount(); i++) {
    const net::ScanItem& it = net::scanItem(i);
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = it.ssid;
    o["rssi"] = it.rssi;
    o["open"] = it.open;
  }
  String out;
  serializeJson(d, out);
  sendJson(200, out);
}

static void handleApiWifiConnect() {
  if (!requireSetupAp()) return;
  JsonDocument d;
  if (!readBody(d)) { sendErr(400, "JSON khong hop le"); return; }
  const char* err = nullptr;
  if (!net::requestConnect(d["ssid"] | "", d["password"] | "", &err)) { sendErr(400, err); return; }
  sendOk();
}

static void handleApiWifiSave() {
  if (!requireSetupAp()) return;
  JsonDocument d;
  if (!readBody(d)) { sendErr(400, "JSON khong hop le"); return; }
  const char* err = nullptr;
  if (!net::saveForLater(d["ssid"] | "", d["password"] | "", &err)) { sendErr(400, err); return; }
  sendOk();
}

static void handleApiWifiForget() {
  if (!requireSetupAp()) return;
  JsonDocument d;
  if (!readBody(d)) { sendErr(400, "JSON khong hop le"); return; }
  if (!net::forgetNetwork(d["ssid"] | "")) { sendErr(404, "khong co mang nay"); return; }
  sendOk();
}

// Mở Wi-Fi riêng từ dashboard (đang ở mạng LAN) - cần mật khẩu web
static void handleApiWifiPortal() {
  if (!authOk()) return;
  net::openPortal();
  sendOk();
}

static void handleNotFound() {
  // Điện thoại vừa nối vào Wi-Fi riêng sẽ thử tải một địa chỉ bất kỳ -> chuyển về trang cài đặt
  if (fromSetupAp() && server.method() == HTTP_GET && !server.uri().startsWith("/api/")) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
    return;
  }
  sendErr(404, "khong tim thay");
}

static void setupWeb() {
  static const char* kHeaders[] = {"X-Auth"};
  server.collectHeaders(kHeaders, 1);
  server.on("/", HTTP_GET, []() { sendPage(fromSetupAp() ? WIFI_HTML : INDEX_HTML); });
  server.on("/dashboard", HTTP_GET, []() { sendPage(INDEX_HTML); });
  server.on("/api/wifi/info", HTTP_GET, handleApiWifiInfo);
  server.on("/api/wifi/scan", HTTP_GET, handleApiWifiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleApiWifiConnect);
  server.on("/api/wifi/portal", HTTP_POST, handleApiWifiPortal);
  server.on("/api/wifi/save", HTTP_POST, handleApiWifiSave);
  server.on("/api/wifi/forget", HTTP_POST, handleApiWifiForget);
  server.on("/manifest.json", HTTP_GET, []() { server.send_P(200, "application/manifest+json", MANIFEST_JSON); });
  server.on("/api/light", HTTP_POST, []() { handleCommand(cmdLight); });
  server.on("/api/history", HTTP_GET, handleApiHistory);
  server.on("/api/history.csv", HTTP_GET, handleApiHistoryCsv);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/fan", HTTP_POST, []() { handleCommand(cmdFan); });
  server.on("/api/led", HTTP_POST, []() { handleCommand(cmdLed); });
  server.on("/api/config", HTTP_POST, []() { handleCommand(cmdConfig); });
  server.on("/api/schedule", HTTP_GET, handleApiScheduleGet);
  server.on("/api/schedule", HTTP_POST, []() { handleCommand(cmdScheduleSet); });
  server.on("/api/sim", HTTP_POST, handleApiSim);
  server.onNotFound(handleNotFound);
  server.begin();
}

// =====================================================================
//  OTA (Wi-Fi do net.cpp quản lý)
// =====================================================================
static void setupOta() {
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    g_otaActive = true;
    Serial.println(F("[ota] bắt đầu cập nhật firmware - KHÔNG rút nguồn"));
  });
  ArduinoOTA.onEnd([]() {
    g_otaActive = false;
    Serial.println(F("\n[ota] xong, khởi động lại"));
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    (void)done; (void)total;
    updateStatusLed(millis());      // quá trình nạp chặn vòng loop nên cập nhật LED tại đây
  });
  ArduinoOTA.onError([](ota_error_t e) {
    g_otaActive = false;
    Serial.printf("[ota] lỗi %u\n", (unsigned)e);
  });
  ArduinoOTA.begin();
}

// In địa chỉ mỗi lần có kết nối mới; OTA + mDNS khởi động ở lần kết nối đầu tiên
static void handleServices() {
  static uint32_t seen = 0;
  if (!net::connected() || net::connectedAt() == seen) return;
  seen = net::connectedAt();
  if (!g_otaStarted) {
    setupOta();
    g_otaStarted = true;
  }
  Serial.printf("[wifi] đã kết nối \"%s\", IP = %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  Serial.printf("[web] http://%s.local/  hoặc  http://%s/\n", DEVICE_HOSTNAME, WiFi.localIP().toString().c_str());
}

// =====================================================================
//  Điều khiển từ xa qua Firebase. Không nhận lệnh giả lập cảm biến hay đổi Wi-Fi.
// =====================================================================
static String runRemoteCommand(const String& payload, bool* changed) {
  *changed = false;
  JsonDocument in, out;
  const char* err = nullptr;
  bool ok = false;
  if (deserializeJson(in, payload)) {
    err = "JSON khong hop le";
  } else {
    out["id"] = in["id"] | "";
    const char* op = in["op"] | "";
    JsonVariantConst body = in["body"];
    if      (!strcmp(op, "fan"))          { ok = cmdFan(body, &err); *changed = ok; }
    else if (!strcmp(op, "led"))          { ok = cmdLed(body, &err); *changed = ok; }
    else if (!strcmp(op, "config"))       { ok = cmdConfig(body, &err); *changed = ok; }
    else if (!strcmp(op, "light"))        { ok = cmdLight(body, &err); *changed = ok; }
    else if (!strcmp(op, "schedule_set")) { ok = cmdScheduleSet(body, &err); *changed = ok; }
    else if (!strcmp(op, "schedule_get")) { fillSchedule(out); ok = true; }
    else if (!strcmp(op, "history"))      { fillHistory(out); ok = true; }
    else                                  { err = "lenh khong ho tro"; }
  }
  out["ok"] = ok;
  if (!ok) out["error"] = err ? err : "loi khong ro";
  String s;
  serializeJson(out, s);
  return s;
}

static void handleRemote(uint32_t now) {
  static uint32_t lastState = 0;
  static bool wasOnline = false;
  remote::poll();
  bool publishNow = false;
  String payload;
  if (remote::popCommand(payload)) {
    bool changed = false;
    String reply = runRemoteCommand(payload, &changed);
    if (changed) {
      updateOutputs(millis());       // áp dụng ngay để trạng thái gửi đi là trạng thái mới
      publishNow = true;
    }
    remote::publishReply(reply);
  }
  bool online = remote::connected();
  if (online && (!wasOnline || publishNow || (uint32_t)(now - lastState) >= CLOUD_STATE_INTERVAL_MS)) {
    lastState = now;
    remote::publishState(statusJson());
  }
  wasOnline = online;
}

// =====================================================================
//  Lệnh Serial (115200 baud) - gõ "help" để xem danh sách
// =====================================================================
static void printHelp() {
  Serial.println(F(
    "Lệnh:\n"
    "  status                       in trạng thái hiện tại\n"
    "  fan auto | off | <30..100>   quạt: tự động / tắt / chạy thủ công theo %\n"
    "  led auto | on | off          đèn: tự động theo lịch / bật / tắt\n"
    "  set tempon|tempoff|humon|humoff <số>   đổi ngưỡng quạt\n"
    "  sched                        xem lịch hẹn giờ\n"
    "  sched <1..6> HH:MM HH:MM     đặt khung giờ (vd: sched 2 22:00 06:00)\n"
    "  sched <1..6> off             tắt một khung giờ\n"
    "  sim <nhiệt_độ> <độ_ẩm>       giả lập giá trị cảm biến (để thử quạt)\n"
    "  sim off                      tắt giả lập\n"
    "  wifi                         xem các mạng đã nhớ\n"
    "  wifi setup                   phát Wi-Fi riêng để đổi mạng từ điện thoại\n"
    "  wifi forget                  xóa các mạng đã nhớ\n"
    "  hist                         xem dung lượng lịch sử trong flash\n"
    "  hist clear                   xóa toàn bộ lịch sử\n"
    "  reboot                       khởi động lại"));
}

static void processCommand(char* line) {
  char* tok[5];
  int n = splitTokens(line, tok, 5);
  if (n == 0) return;
  const char* c = tok[0];

  if (!strcmp(c, "help")) {
    printHelp();
  } else if (!strcmp(c, "status")) {
    printStatusLine();
  } else if (!strcmp(c, "fan") && n >= 2) {
    if (!strcmp(tok[1], "auto")) {
      setFanAuto();
    } else if (!strcmp(tok[1], "off")) {
      setFanManual(0);
    } else {
      int p = atoi(tok[1]);
      if (p >= FAN_MIN_PERCENT && p <= 100) setFanManual(p);
      else Serial.printf("fan: nhập auto, off hoặc số từ %d đến 100\n", FAN_MIN_PERCENT);
    }
  } else if (!strcmp(c, "led") && n >= 2) {
    if (!strcmp(tok[1], "auto"))     setLedAuto();
    else if (!strcmp(tok[1], "on"))  setLedManual(true);
    else if (!strcmp(tok[1], "off")) setLedManual(false);
    else Serial.println(F("led: nhập auto, on hoặc off"));
  } else if (!strcmp(c, "set") && n >= 3) {
    float v = atof(tok[2]);
    float tOn = params.tempOn, tOff = params.tempOff, hOn = params.humOn, hOff = params.humOff;
    bool known = true;
    if      (!strcmp(tok[1], "tempon"))  tOn = v;
    else if (!strcmp(tok[1], "tempoff")) tOff = v;
    else if (!strcmp(tok[1], "humon"))   hOn = v;
    else if (!strcmp(tok[1], "humoff"))  hOff = v;
    else known = false;
    if (!known) {
      Serial.println(F("set: tên phải là tempon, tempoff, humon hoặc humoff"));
    } else {
      const char* err = nullptr;
      if (!applyThresholds(tOn, tOff, hOn, hOff, &err)) Serial.printf("set: bị từ chối - %s\n", err);
    }
  } else if (!strcmp(c, "sched")) {
    if (n == 1) {
      printSlots();
    } else {
      int idx = atoi(tok[1]);
      if (idx < 1 || idx > MAX_SLOTS) {
        Serial.printf("sched: số khung phải từ 1 đến %d\n", MAX_SLOTS);
      } else {
        logic::Slot tmp[MAX_SLOTS];
        for (int i = 0; i < MAX_SLOTS; i++) tmp[i] = slots[i];
        if (n >= 3 && !strcmp(tok[2], "off")) {
          tmp[idx - 1].enabled = false;
        } else if (n >= 4) {
          uint16_t a, b;
          if (!logic::parseHHMM(tok[2], a) || !logic::parseHHMM(tok[3], b)) {
            Serial.println(F("sched: giờ phải có dạng HH:MM"));
            return;
          }
          tmp[idx - 1].enabled = true;
          tmp[idx - 1].startMin = a;
          tmp[idx - 1].endMin = b;
        } else {
          Serial.println(F("sched: dùng 'sched <1..6> HH:MM HH:MM' hoặc 'sched <1..6> off'"));
          return;
        }
        const char* err = nullptr;
        if (!applySlots(tmp, &err)) Serial.printf("sched: bị từ chối - %s\n", err);
      }
    }
  } else if (!strcmp(c, "sim") && n >= 2) {
    if (!strcmp(tok[1], "off")) {
      setSim(false, 0, 0);
    } else if (n >= 3) {
      setSim(true, atof(tok[1]), atof(tok[2]));
    } else {
      Serial.println(F("sim: dùng 'sim <nhiệt_độ> <độ_ẩm>' hoặc 'sim off'"));
    }
  } else if (!strcmp(c, "wifi")) {
    if (n == 1) {
      Serial.printf("  đang dùng: %s | Wi-Fi riêng: %s\n",
                    net::connected() ? WiFi.SSID().c_str() : "chưa kết nối", net::portalActive() ? "BẬT" : "tắt");
      for (int i = 0; i < net::savedCount(); i++) Serial.printf("  đã nhớ %d: %s\n", i + 1, net::savedSsid(i).c_str());
    } else if (!strcmp(tok[1], "setup")) {
      net::openPortal();
    } else if (!strcmp(tok[1], "forget")) {
      net::forgetAll();
    } else {
      Serial.println(F("wifi: dùng 'wifi', 'wifi setup' hoặc 'wifi forget'"));
    }
  } else if (!strcmp(c, "hist")) {
    if (n == 1) {
      Serial.printf("  flash: %s | %d ngày | %d mẫu trong RAM | dùng %u/%u KB\n",
                    history::flashOk() ? "OK" : "LỖI", history::storedDays(), history::count(),
                    (unsigned)(history::usedBytes() / 1024), (unsigned)(history::totalBytes() / 1024));
    } else if (!strcmp(tok[1], "clear")) {
      history::clear();
#ifdef SMARTROOM_TEST
    } else if (!strcmp(tok[1], "fill") && n >= 3) {
      // chỉ để thử: tạo <n> mẫu giả cách nhau 5 phút, kết thúc ở hiện tại
      uint32_t t = (uint32_t)time(nullptr), t0 = millis();
      for (int k = atoi(tok[2]) - 1; k >= 0; k--) history::add({t - (uint32_t)k * 300UL, (int16_t)(250 + k % 80), (uint8_t)(60 + k % 30), (uint16_t)(k % 4096)});
      Serial.printf("[test] đã tạo xong trong %lu ms\n", (unsigned long)(millis() - t0));
    } else if (!strcmp(tok[1], "fake") && n >= 3) {
      // chỉ để thử: tạo mỗi ngày một mẫu giả cho <n> ngày trước
      uint32_t t = (uint32_t)time(nullptr);
      for (int k = atoi(tok[2]); k >= 1; k--) history::add({t - (uint32_t)k * 86400UL, 250, 50, 1000});
      Serial.printf("[test] đã tạo dữ liệu giả, còn %d ngày\n", history::storedDays());
#endif
    } else {
      Serial.println(F("hist: dùng 'hist' hoặc 'hist clear'"));
    }
#ifdef SMARTROOM_TEST
  } else if (!strcmp(c, "blackout") && n >= 2) {
    if (n >= 3 && !strcmp(tok[2], "boot")) net::testBlackoutAtBoot(atoi(tok[1]));
    else net::testBlackout(atoi(tok[1]));
  } else if (!strcmp(c, "cloud") && n >= 2) {
    if (!strcmp(tok[1], "expire")) {
      remote::testExpireToken();
      Serial.println(F("[test] buộc làm mới phiên đăng nhập"));
    } else {
      bool changed = false;
      String r = runRemoteCommand(String(tok[1]), &changed);
      Serial.printf("[test] cloud (changed=%d, %u byte): %s\n", changed ? 1 : 0, r.length(), r.c_str());
    }
#endif
  } else if (!strcmp(c, "reboot")) {
    Serial.println(F("Khởi động lại..."));
    delay(200);
    ESP.restart();
  } else {
    Serial.println(F("Lệnh không hợp lệ. Gõ 'help' để xem danh sách."));
  }
}

static void handleSerial() {
  static char buf[96];
  static size_t len = 0;
  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (len > 0) {
        buf[len] = '\0';
        len = 0;
        processCommand(buf);
      }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = ch;
    }
  }
}

// =====================================================================
//  setup / loop
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== Smartroom ESP32 v%s ===\n", FW_VERSION);

  // Ngõ ra: tắt hết trước, LED trạng thái sáng đứng trong lúc khởi động
  pinMode(PIN_LED_MAIN, OUTPUT);
  digitalWrite(PIN_LED_MAIN, LOW);
  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, HIGH);
  fanPwmInit();
  pinMode(PIN_LIGHT, INPUT);
  loadLight();

  loadSettings();
  Serial.printf("[cấu hình] ngưỡng: nhiệt độ %.1f/%.1f C, độ ẩm %.0f/%.0f %%\n",
                params.tempOn, params.tempOff, params.humOn, params.humOff);
  printSlots();

  dht.begin();
  history::begin();

  // RTC (I2C)
  // Kiểm tra bus trước: lúc rảnh SDA và SCL phải ở mức cao. Nếu bị giữ thấp
  // (chạm GND, module không có nguồn...) thì bỏ qua I2C để firmware không bị treo.
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delay(10);
  bool sdaLow = (digitalRead(PIN_I2C_SDA) == LOW);
  bool sclLow = (digitalRead(PIN_I2C_SCL) == LOW);
  // Thử chạm chập SDA-SCL: kéo SCL xuống thấp, nếu SDA xuống theo thì hai dây đang chạm nhau
  bool shorted = false;
  if (!sdaLow && !sclLow) {
    pinMode(PIN_I2C_SCL, OUTPUT);
    digitalWrite(PIN_I2C_SCL, LOW);
    delay(2);
    shorted = (digitalRead(PIN_I2C_SDA) == LOW);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delay(2);
  }
  Serial.printf("[i2c] kiểm tra bus: SDA %s, SCL %s, chạm nhau: %s\n",
                sdaLow ? "THẤP" : "cao", sclLow ? "THẤP" : "cao", shorted ? "CÓ" : "không");
  if (shorted) {
    Serial.println(F("[i2c] LỖI: SDA và SCL đang chạm nhau - bỏ qua RTC và LCD"));
  } else if (sdaLow || sclLow) {
    Serial.printf("[i2c] LỖI: bus bị kẹt mức thấp (SDA/GPIO%d: %s, SCL/GPIO%d: %s) - bỏ qua RTC và LCD\n",
                  PIN_I2C_SDA, sdaLow ? "THẤP" : "ok", PIN_I2C_SCL, sclLow ? "THẤP" : "ok");
  } else {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setTimeOut(50);
    g_rtcFound = rtc.begin(&Wire);
    lcdInit();
  }

  // Múi giờ + NTP (đồng bộ khi có mạng), rồi nạp giờ từ RTC để dùng được ngay cả khi chưa có mạng
  sntp_set_time_sync_notification_cb(onNtpSync);
  configTzTime(TZ_VIETNAM, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  loadTimeFromRtc();

  // Wi-Fi + web (web chạy ngay để dùng được cả khi chỉ có Wi-Fi riêng)
  net::begin();
  setupWeb();

  Serial.println(F("Gõ 'help' để xem lệnh kiểm thử."));
}

void loop() {
  uint32_t now = millis();

  net::poll(now);
  handleServices();
  if (g_otaStarted) ArduinoOTA.handle();
  server.handleClient();
  now = millis();                    // phần mạng / web có thể tốn thời gian
  handleSerial();
  handleSensor(now);
  handleLight(now);
  handleHistory(now);
  handleRtcSync();
  updateOutputs(now);
  handleRemote(now);
  updateStatusLed(now);
  updateLcd(now);

  static uint32_t lastPrint = 0;
  if ((uint32_t)(now - lastPrint) >= STATUS_PRINT_INTERVAL_MS) {
    lastPrint = now;
    printStatusLine();
  }
}
