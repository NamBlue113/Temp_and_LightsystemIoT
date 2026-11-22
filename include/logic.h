#pragma once
// =====================================================================
//  logic.h - phần LOGIC THUẦN, không phụ thuộc Arduino/ESP32.
//  Nhờ vậy có thể biên dịch và kiểm thử trên máy tính (xem tools/test_logic.cpp).
//  Gồm: hysteresis, mức quạt PWM, khung giờ (kể cả qua nửa đêm), ưu tiên LED trạng thái.
// =====================================================================
#include <stdint.h>
#include <stdio.h>
#include <math.h>

namespace logic {

// ---------------------------------------------------------------------
//  1. Quạt tự động: hysteresis + mức PWM
// ---------------------------------------------------------------------
struct FanAutoParams {
  float tempOn  = 30.0f;   // °C: đạt ngưỡng này thì bật
  float tempOff = 28.0f;   // °C: xuống tới mức này thì tắt
  float humOn   = 80.0f;   // %RH
  float humOff  = 75.0f;
  // Vượt ngưỡng bật bao nhiêu thì lên mức cao hơn:
  //   vượt < Band1 -> mức thấp, < Band2 -> mức vừa, còn lại -> mức cao
  float tempBand1 = 2.0f, tempBand2 = 4.0f;   // °C
  float humBand1  = 5.0f, humBand2  = 10.0f;  // %RH
  // % PWM cho từng mức. Mặc định cả 3 mức đều 100%: quạt chỉ có bật hết cỡ hoặc tắt.
  // Muốn chia mức lại thì đổi thành ví dụ 40, 70, 100.
  int levelLow = 100, levelMid = 100, levelHigh = 100;
};

struct FanAutoState {
  bool tempHot  = false;   // đang trong vùng "nóng" (đã bật, chưa xuống tới ngưỡng tắt)
  bool humHigh  = false;   // đang trong vùng "ẩm cao"
};

// Hysteresis: chạm ngưỡng bật -> bật; chạm ngưỡng tắt -> tắt; ở giữa -> giữ trạng thái cũ.
inline bool hysteresis(bool active, float value, float onTh, float offTh) {
  if (value >= onTh)  return true;
  if (value <= offTh) return false;
  return active;
}

inline int bandLevel(float excess, float band1, float band2, const FanAutoParams& p) {
  if (excess < band1) return p.levelLow;
  if (excess < band2) return p.levelMid;
  return p.levelHigh;
}

// Trả về % PWM (0 = tắt). Mức theo nhiệt độ và theo độ ẩm được tính riêng, rồi lấy mức CAO HƠN.
// Hàm cập nhật st (trạng thái hysteresis) mỗi lần gọi.
inline int fanAutoPercent(FanAutoState& st, float temp, float hum, const FanAutoParams& p) {
  st.tempHot = hysteresis(st.tempHot, temp, p.tempOn, p.tempOff);
  st.humHigh = hysteresis(st.humHigh, hum,  p.humOn,  p.humOff);
  int lt = st.tempHot ? bandLevel(temp - p.tempOn, p.tempBand1, p.tempBand2, p) : 0;
  int lh = st.humHigh ? bandLevel(hum  - p.humOn,  p.humBand1,  p.humBand2,  p) : 0;
  return lt > lh ? lt : lh;
}

// Kiểm tra bộ ngưỡng người dùng nhập. Trả về false và gán *err nếu không hợp lệ.
inline bool validThresholds(float tOn, float tOff, float hOn, float hOff, const char** err) {
  const char* dummy = nullptr;
  if (!err) err = &dummy;
  if (isnan(tOn) || isnan(tOff) || isnan(hOn) || isnan(hOff)) { *err = "Gia tri khong hop le"; return false; }
  if (tOn < 15.0f || tOn > 60.0f)   { *err = "Nguong nhiet do bat phai trong 15..60 C"; return false; }
  if (tOff < 10.0f || tOff > tOn - 1.0f) { *err = "Nguong nhiet do tat phai >= 10 va thap hon nguong bat it nhat 1 C"; return false; }
  if (hOn < 30.0f || hOn > 95.0f)   { *err = "Nguong do am bat phai trong 30..95 %"; return false; }
  if (hOff < 20.0f || hOff > hOn - 2.0f) { *err = "Nguong do am tat phai >= 20 va thap hon nguong bat it nhat 2 %"; return false; }
  return true;
}

// ---------------------------------------------------------------------
//  2. Lịch hẹn giờ (khung giờ trong ngày, tính bằng phút từ 00:00)
// ---------------------------------------------------------------------
struct Slot {
  bool     enabled  = false;
  uint16_t startMin = 0;     // 0..1439
  uint16_t endMin   = 0;     // 0..1439 (không tính phút kết thúc)
};

// Khung [start, end). Nếu start > end thì khung vắt qua nửa đêm (vd 22:00 -> 06:00).
// start == end được coi là khung rỗng (không bật).
inline bool slotActive(const Slot& s, int minuteOfDay) {
  if (!s.enabled || s.startMin == s.endMin) return false;
  if (s.startMin < s.endMin) return minuteOfDay >= s.startMin && minuteOfDay < s.endMin;
  return minuteOfDay >= s.startMin || minuteOfDay < s.endMin;
}

inline bool scheduleActive(const Slot* slots, int n, int minuteOfDay) {
  for (int i = 0; i < n; i++) {
    if (slotActive(slots[i], minuteOfDay)) return true;
  }
  return false;
}

// "H:MM" hoặc "HH:MM" -> số phút. Trả về false nếu sai định dạng.
inline bool parseHHMM(const char* s, uint16_t& out) {
  if (!s) return false;
  int h = 0, digits = 0;
  while (*s >= '0' && *s <= '9') {
    h = h * 10 + (*s - '0');
    s++;
    if (++digits > 2) return false;
  }
  if (digits == 0 || *s != ':') return false;
  s++;
  if (!(s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' && s[2] == '\0')) return false;
  int m = (s[0] - '0') * 10 + (s[1] - '0');
  if (h > 23 || m > 59) return false;
  out = (uint16_t)(h * 60 + m);
  return true;
}

// buf phải có ít nhất 6 byte.
inline void formatHHMM(uint16_t minutes, char* buf) {
  snprintf(buf, 6, "%02d:%02d", (int)(minutes / 60) % 24, (int)(minutes % 60));
}

// ---------------------------------------------------------------------
//  3. LED trạng thái: chọn mẫu theo độ ưu tiên và tính mức sáng theo thời gian
// ---------------------------------------------------------------------
enum class StatusPattern : uint8_t {
  Ota,             // đang cập nhật firmware
  DhtError,        // lỗi cảm biến DHT11
  RtcFault,        // không có RTC hoặc chưa có giờ hợp lệ
  OverThreshold,   // đang vượt ngưỡng
  WifiConnecting,  // đang kết nối Wi-Fi
  BackendLost,     // mất kết nối backend (giai đoạn sau)
  Normal           // mọi thứ ổn
};

struct StatusInputs {
  bool ota = false;
  bool dhtError = false;
  bool rtcFault = false;
  bool overThreshold = false;
  bool wifiConnecting = false;
  bool backendEnabled = false;
  bool backendLost = false;
};

// Thứ tự ưu tiên (cao -> thấp): OTA > lỗi DHT > lỗi RTC > vượt ngưỡng > Wi-Fi > backend > bình thường
inline StatusPattern pickStatusPattern(const StatusInputs& in) {
  if (in.ota)            return StatusPattern::Ota;
  if (in.dhtError)       return StatusPattern::DhtError;
  if (in.rtcFault)       return StatusPattern::RtcFault;
  if (in.overThreshold)  return StatusPattern::OverThreshold;
  if (in.wifiConnecting) return StatusPattern::WifiConnecting;
  if (in.backendEnabled && in.backendLost) return StatusPattern::BackendLost;
  return StatusPattern::Normal;
}

// n lần nháy (mỗi lần sáng onMs, tắt offMs) rồi nghỉ pauseMs, lặp lại. t = thời gian từ lúc bắt đầu mẫu.
inline bool flashSequence(uint32_t t, int n, uint32_t onMs, uint32_t offMs, uint32_t pauseMs) {
  uint32_t unit  = onMs + offMs;
  uint32_t burst = (uint32_t)n * unit;
  uint32_t cycle = burst + pauseMs;
  uint32_t p = t % cycle;
  if (p >= burst) return false;
  return (p % unit) < onMs;
}

// true = LED sáng tại thời điểm t (ms kể từ khi mẫu bắt đầu)
inline bool statusLedLevel(StatusPattern pat, uint32_t t) {
  switch (pat) {
    case StatusPattern::Ota:            return (t % 1000) < 500;              // nháy đều 1 Hz
    case StatusPattern::DhtError:       return flashSequence(t, 3, 150, 150, 2000);
    case StatusPattern::RtcFault:       return flashSequence(t, 4, 150, 150, 2000);
    case StatusPattern::OverThreshold:  return true;                          // sáng đứng
    case StatusPattern::WifiConnecting: return (t % 200) < 100;               // nháy nhanh 5 Hz
    case StatusPattern::BackendLost:    return flashSequence(t, 2, 150, 150, 2000);
    case StatusPattern::Normal:
    default:                            return flashSequence(t, 1, 100, 0, 2900);  // 1 nháy ngắn mỗi 3 giây
  }
}

inline const char* statusPatternName(StatusPattern pat) {
  switch (pat) {
    case StatusPattern::Ota:            return "ota";
    case StatusPattern::DhtError:       return "dht_error";
    case StatusPattern::RtcFault:       return "rtc_fault";
    case StatusPattern::OverThreshold:  return "over_threshold";
    case StatusPattern::WifiConnecting: return "wifi_connecting";
    case StatusPattern::BackendLost:    return "backend_lost";
    case StatusPattern::Normal:
    default:                            return "normal";
  }
}

}  // namespace logic
