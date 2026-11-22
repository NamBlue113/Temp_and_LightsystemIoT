// Kiểm thử phần logic thuần (logic.h) ngay trên máy tính, không cần ESP32.
// Biên dịch:  g++ -std=c++11 -Wall -Wextra -o tools/test_logic tools/test_logic.cpp && ./tools/test_logic
#include <cstdio>
#include <cstring>
#include "../include/logic.h"

static int g_total = 0, g_fail = 0;
#define CHECK(cond) do { g_total++; if (!(cond)) { g_fail++; \
  std::printf("FAIL  dong %d: %s\n", __LINE__, #cond); } } while (0)

using namespace logic;

static void testHysteresis() {
  CHECK(hysteresis(false, 29.0f, 30.0f, 28.0f) == false);  // chưa tới ngưỡng bật
  CHECK(hysteresis(false, 30.0f, 30.0f, 28.0f) == true);   // chạm ngưỡng bật
  CHECK(hysteresis(true,  29.0f, 30.0f, 28.0f) == true);   // vùng giữa: giữ nguyên
  CHECK(hysteresis(false, 29.0f, 30.0f, 28.0f) == false);  // vùng giữa: giữ nguyên
  CHECK(hysteresis(true,  28.0f, 30.0f, 28.0f) == false);  // chạm ngưỡng tắt
}

static void testFanLevels() {
  FanAutoParams p;
  p.levelLow = 40; p.levelMid = 70; p.levelHigh = 100;   // thử logic chia mức (mặc định firmware là 100/100/100)
  FanAutoState st;
  CHECK(fanAutoPercent(st, 25.0f, 50.0f, p) == 0);
  CHECK(fanAutoPercent(st, 29.9f, 50.0f, p) == 0);
  CHECK(fanAutoPercent(st, 30.0f, 50.0f, p) == 40);    // vượt 0
  CHECK(fanAutoPercent(st, 31.9f, 50.0f, p) == 40);
  CHECK(fanAutoPercent(st, 32.0f, 50.0f, p) == 70);    // vượt 2
  CHECK(fanAutoPercent(st, 33.9f, 50.0f, p) == 70);
  CHECK(fanAutoPercent(st, 34.0f, 50.0f, p) == 100);   // vượt 4
  CHECK(fanAutoPercent(st, 29.0f, 50.0f, p) == 40);    // đang nóng, 28 < 29 < 30: giữ mức thấp
  CHECK(fanAutoPercent(st, 28.0f, 50.0f, p) == 0);     // xuống ngưỡng tắt
  CHECK(fanAutoPercent(st, 29.0f, 50.0f, p) == 0);     // nguội rồi, 29 chưa đủ để bật lại

  FanAutoState sh;
  CHECK(fanAutoPercent(sh, 25.0f, 80.0f, p) == 40);    // độ ẩm vượt 0
  CHECK(fanAutoPercent(sh, 25.0f, 85.0f, p) == 70);    // vượt 5
  CHECK(fanAutoPercent(sh, 25.0f, 90.0f, p) == 100);   // vượt 10
  CHECK(fanAutoPercent(sh, 25.0f, 78.0f, p) == 40);    // vùng giữa: còn chạy
  CHECK(fanAutoPercent(sh, 25.0f, 75.0f, p) == 0);     // chạm ngưỡng tắt

  FanAutoState sb;
  CHECK(fanAutoPercent(sb, 31.0f, 90.0f, p) == 100);   // lấy mức cao hơn của 2 đại lượng
  CHECK(fanAutoPercent(sb, 35.0f, 82.0f, p) == 100);   // nhiệt độ quyết định
}

static void testThresholds() {
  const char* err = nullptr;
  CHECK(validThresholds(30, 28, 80, 75, &err));
  CHECK(!validThresholds(30, 30, 80, 75, &err));       // tắt = bật
  CHECK(!validThresholds(30, 29.5f, 80, 75, &err));    // chênh < 1 độ
  CHECK(!validThresholds(70, 28, 80, 75, &err));       // quá cao
  CHECK(!validThresholds(30, 28, 80, 79, &err));       // chênh ẩm < 2
  CHECK(!validThresholds(30, 28, 101, 75, &err));
  CHECK(!validThresholds(NAN, 28, 80, 75, &err));
  CHECK(validThresholds(30, 28, 80, 75, nullptr));     // err null vẫn chạy
}

static void testSlots() {
  Slot day;   day.enabled = true; day.startMin = 18 * 60; day.endMin = 22 * 60;
  CHECK(!slotActive(day, 17 * 60 + 59));
  CHECK(slotActive(day, 18 * 60));
  CHECK(slotActive(day, 21 * 60 + 59));
  CHECK(!slotActive(day, 22 * 60));
  CHECK(!slotActive(day, 0));

  Slot night; night.enabled = true; night.startMin = 22 * 60; night.endMin = 6 * 60;  // qua nửa đêm
  CHECK(!night.enabled == false);
  CHECK(!slotActive(night, 21 * 60 + 59));
  CHECK(slotActive(night, 22 * 60));
  CHECK(slotActive(night, 23 * 60 + 59));
  CHECK(slotActive(night, 0));
  CHECK(slotActive(night, 5 * 60 + 59));
  CHECK(!slotActive(night, 6 * 60));
  CHECK(!slotActive(night, 12 * 60));

  Slot off = day;  off.enabled = false;
  CHECK(!slotActive(off, 19 * 60));
  Slot empty; empty.enabled = true; empty.startMin = 600; empty.endMin = 600;
  CHECK(!slotActive(empty, 600));

  Slot all[3] = { day, night, off };
  CHECK(scheduleActive(all, 3, 19 * 60));
  CHECK(scheduleActive(all, 3, 23 * 60));
  CHECK(scheduleActive(all, 3, 1 * 60));
  CHECK(!scheduleActive(all, 3, 12 * 60));
  CHECK(!scheduleActive(all, 0, 19 * 60));
}

static void testParse() {
  uint16_t m = 0;
  CHECK(parseHHMM("18:00", m) && m == 1080);
  CHECK(parseHHMM("6:30", m) && m == 390);
  CHECK(parseHHMM("00:00", m) && m == 0);
  CHECK(parseHHMM("23:59", m) && m == 1439);
  CHECK(!parseHHMM("24:00", m));
  CHECK(!parseHHMM("12:60", m));
  CHECK(!parseHHMM("12:5", m));
  CHECK(!parseHHMM("ab:cd", m));
  CHECK(!parseHHMM("", m));
  CHECK(!parseHHMM(nullptr, m));
  CHECK(!parseHHMM("123:00", m));
  CHECK(!parseHHMM("12:000", m));
  CHECK(!parseHHMM("1800", m));
  char buf[6];
  formatHHMM(1080, buf);  CHECK(std::strcmp(buf, "18:00") == 0);
  formatHHMM(395, buf);   CHECK(std::strcmp(buf, "06:35") == 0);
}

static void testStatus() {
  StatusInputs in;
  CHECK(pickStatusPattern(in) == StatusPattern::Normal);
  in.wifiConnecting = true;
  CHECK(pickStatusPattern(in) == StatusPattern::WifiConnecting);
  in.overThreshold = true;
  CHECK(pickStatusPattern(in) == StatusPattern::OverThreshold);
  in.rtcFault = true;
  CHECK(pickStatusPattern(in) == StatusPattern::RtcFault);
  in.dhtError = true;
  CHECK(pickStatusPattern(in) == StatusPattern::DhtError);
  in.ota = true;
  CHECK(pickStatusPattern(in) == StatusPattern::Ota);

  StatusInputs b;  b.backendEnabled = true; b.backendLost = true;
  CHECK(pickStatusPattern(b) == StatusPattern::BackendLost);
  b.backendEnabled = false;
  CHECK(pickStatusPattern(b) == StatusPattern::Normal);   // backend tắt thì không báo
  b.backendEnabled = true; b.overThreshold = true;
  CHECK(pickStatusPattern(b) == StatusPattern::OverThreshold);

  // DhtError: 3 nháy (150 sáng/150 tắt), nghỉ 2000 -> chu kỳ 2900 ms
  StatusPattern d = StatusPattern::DhtError;
  CHECK(statusLedLevel(d, 0));    CHECK(statusLedLevel(d, 149));
  CHECK(!statusLedLevel(d, 150)); CHECK(!statusLedLevel(d, 299));
  CHECK(statusLedLevel(d, 300));  CHECK(!statusLedLevel(d, 450));
  CHECK(statusLedLevel(d, 600));  CHECK(!statusLedLevel(d, 750));
  CHECK(!statusLedLevel(d, 900)); CHECK(!statusLedLevel(d, 2899));
  CHECK(statusLedLevel(d, 2900));  // sang chu kỳ mới

  // RtcFault: 4 nháy -> chu kỳ 3200 ms
  StatusPattern r = StatusPattern::RtcFault;
  CHECK(statusLedLevel(r, 900));  CHECK(!statusLedLevel(r, 1050));
  CHECK(!statusLedLevel(r, 1200)); CHECK(statusLedLevel(r, 3200));

  // Backend: 2 nháy -> chu kỳ 2600 ms
  StatusPattern k = StatusPattern::BackendLost;
  CHECK(statusLedLevel(k, 0));    CHECK(statusLedLevel(k, 300));
  CHECK(!statusLedLevel(k, 450)); CHECK(!statusLedLevel(k, 600));
  CHECK(statusLedLevel(k, 2600));

  // Bình thường: 1 nháy 100 ms mỗi 3 giây
  StatusPattern n = StatusPattern::Normal;
  CHECK(statusLedLevel(n, 0));    CHECK(statusLedLevel(n, 99));
  CHECK(!statusLedLevel(n, 100)); CHECK(!statusLedLevel(n, 2999));
  CHECK(statusLedLevel(n, 3000));

  StatusPattern o = StatusPattern::Ota;
  CHECK(statusLedLevel(o, 0));  CHECK(statusLedLevel(o, 499));
  CHECK(!statusLedLevel(o, 500)); CHECK(!statusLedLevel(o, 999));
  CHECK(statusLedLevel(o, 1000));

  StatusPattern w = StatusPattern::WifiConnecting;
  CHECK(statusLedLevel(w, 0));  CHECK(statusLedLevel(w, 99));
  CHECK(!statusLedLevel(w, 100)); CHECK(statusLedLevel(w, 200));

  CHECK(statusLedLevel(StatusPattern::OverThreshold, 0));
  CHECK(statusLedLevel(StatusPattern::OverThreshold, 123456));
  CHECK(std::strcmp(statusPatternName(StatusPattern::DhtError), "dht_error") == 0);
}

int main() {
  testHysteresis();
  testFanLevels();
  testThresholds();
  testSlots();
  testParse();
  testStatus();
  std::printf("%d/%d kiem tra dat%s\n", g_total - g_fail, g_total, g_fail ? "  -> CO LOI" : "");
  return g_fail ? 1 : 0;
}
