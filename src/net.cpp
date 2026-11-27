#include "net.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>

#include "config.h"
#include "secrets.h"

#ifndef SETUP_AP_PASSWORD
#error "Thieu SETUP_AP_PASSWORD - hay them dong #define SETUP_AP_PASSWORD \"...\" (it nhat 8 ky tu) vao include/secrets.h"
#endif
static_assert(sizeof(SETUP_AP_PASSWORD) - 1 >= 8 && sizeof(SETUP_AP_PASSWORD) - 1 <= 63,
              "SETUP_AP_PASSWORD phai dai tu 8 den 63 ky tu");

namespace net {
namespace {

struct Cred {
  String ssid;
  String pass;
};

// Việc đang làm với giao diện STA (chỉ một việc tại một thời điểm)
enum class Phase : uint8_t {
  Idle,        // không làm gì (đã kết nối, hoặc đang chờ lần thử sau)
  CycleScan,   // đang quét để chọn mạng đã lưu
  CycleTry,    // đang thử lần lượt các mạng đã lưu
  TrialWait,   // vừa nhận mạng mới, chờ trang web nhận xong phản hồi
  TrialTry     // đang thử mạng mới
};

Preferences store;
DNSServer   dns;

Cred saved[WIFI_MAX_SAVED];
int  nSaved = 0;

Cred cand[WIFI_MAX_SAVED + 1];     // mạng đã lưu + mạng mặc định trong secrets.h
int  nCand = 0;
int  order[WIFI_MAX_SAVED + 1];    // thứ tự thử trong lượt hiện tại
int  nOrder = 0;
int  orderPos = 0;

Phase    phase = Phase::Idle;
uint32_t phaseAt = 0;
uint32_t lastCycleAt = 0;
bool     bootCycle = true;         // lượt đầu thất bại thì mở cài đặt ngay

bool     wasConnected = false;
uint32_t connAt = 0;
uint32_t lostAt = 0;

bool     portal = false;
bool     portalManual = false;
uint32_t closeAt = 0;              // 0 = chưa hẹn tắt
uint32_t lastClientAt = 0;

Cred  trial;
Trial trialResult = Trial::None;

ScanItem found[WIFI_SCAN_MAX];
int      nFound = 0;
bool     scanBusy = false;         // đang trong một lượt quét
bool     scanRunning = false;      // phần cứng đang quét
bool     scanByChannel = false;    // quét từng kênh (khi đang phát Wi-Fi riêng)
bool     scanOk = false;
uint8_t  scanChannel = 0;
uint32_t scanAt = 0;
uint32_t scanNextAt = 0;
uint32_t openAt = 0;               // 0 = không có yêu cầu mở Wi-Fi riêng đang chờ

// --------------------------------------------------------------- lưu trữ NVS
void loadSaved() {
  nSaved = 0;
  store.begin(NVS_NAMESPACE, false);
  int n = store.isKey("wn") ? store.getInt("wn", 0) : 0;
  char ks[8], kp[8];
  for (int i = 0; i < n && i < WIFI_MAX_SAVED; i++) {
    snprintf(ks, sizeof(ks), "ws%d", i);
    snprintf(kp, sizeof(kp), "wp%d", i);
    if (!store.isKey(ks)) continue;
    saved[nSaved].ssid = store.getString(ks, "");
    saved[nSaved].pass = store.isKey(kp) ? store.getString(kp, "") : String();
    if (saved[nSaved].ssid.length() > 0) nSaved++;
  }
  store.end();
}

void writeSaved() {
  store.begin(NVS_NAMESPACE, false);
  store.putInt("wn", nSaved);
  char ks[8], kp[8];
  for (int i = 0; i < WIFI_MAX_SAVED; i++) {
    snprintf(ks, sizeof(ks), "ws%d", i);
    snprintf(kp, sizeof(kp), "wp%d", i);
    if (i < nSaved) {
      store.putString(ks, saved[i].ssid);
      store.putString(kp, saved[i].pass);
    } else {
      if (store.isKey(ks)) store.remove(ks);
      if (store.isKey(kp)) store.remove(kp);
    }
  }
  store.end();
}

// Đưa mạng vừa dùng được lên đầu danh sách (mạng cũ nhất bị loại khi đầy)
void remember(const Cred& c) {
  if (nSaved > 0 && saved[0].ssid == c.ssid && saved[0].pass == c.pass) return;
  Cred keep = c;                   // c có thể trỏ vào chính mảng saved
  int pos = -1;
  for (int i = 0; i < nSaved; i++) {
    if (saved[i].ssid == keep.ssid) { pos = i; break; }
  }
  if (pos < 0) {
    if (nSaved < WIFI_MAX_SAVED) nSaved++;
    pos = nSaved - 1;
  }
  for (int i = pos; i > 0; i--) saved[i] = saved[i - 1];
  saved[0] = keep;
  writeSaved();
  Serial.printf("[wifi] đã nhớ mạng \"%s\" (%d/%d)\n", keep.ssid.c_str(), nSaved, WIFI_MAX_SAVED);
}

#ifdef SMARTROOM_TEST
uint32_t blackoutUntil = 0;
bool blackout() { return blackoutUntil != 0 && (int32_t)(blackoutUntil - millis()) > 0; }
#endif

void buildCandidates() {
  nCand = 0;
#ifdef SMARTROOM_TEST
  if (blackout()) {                // mạng không tồn tại, để mọi lượt thử đều thất bại
    cand[nCand].ssid = "smartroom-test-no-such-network";
    cand[nCand].pass = "12345678";
    nCand++;
    return;
  }
#endif
  bool hasDefault = false;
  for (int i = 0; i < nSaved; i++) {
    cand[nCand++] = saved[i];
    if (saved[i].ssid == WIFI_SSID) hasDefault = true;
  }
  if (!hasDefault && strlen(WIFI_SSID) > 0) {
    cand[nCand].ssid = WIFI_SSID;
    cand[nCand].pass = WIFI_PASSWORD;
    nCand++;
  }
}

// --------------------------------------------------------------- quét mạng
// ESP32 chỉ có một bộ thu phát: quét liền tất cả các kênh sẽ làm Wi-Fi riêng biến mất
// vài giây và điện thoại bị rớt. Vì vậy khi đang phát Wi-Fi riêng thì quét từng kênh.
void startScan(uint32_t now) {
  if (scanBusy) return;
  scanBusy = true;
  scanRunning = false;
  scanOk = false;
  scanByChannel = portal;
  scanChannel = scanByChannel ? 1 : 0;       // 0 = tất cả các kênh
  scanNextAt = now;
  nFound = 0;
}

void cancelScan() {
  if (!scanBusy) return;
  WiFi.scanDelete();
  scanBusy = false;
  scanRunning = false;
}

// Gộp kết quả quét: bỏ trùng tên (giữ sóng mạnh nhất), sắp theo sóng giảm dần
void collectScan(int n) {
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    ScanItem x;
    x.ssid = s;
    x.rssi = WiFi.RSSI(i);
    x.open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    x.channel = (uint8_t)WiFi.channel(i);
    int dup = -1;
    for (int k = 0; k < nFound; k++) {
      if (found[k].ssid == s) { dup = k; break; }
    }
    if (dup >= 0) {
      if (x.rssi > found[dup].rssi) found[dup] = x;
    } else if (nFound < WIFI_SCAN_MAX) {
      found[nFound++] = x;
    }
  }
  for (int i = 1; i < nFound; i++) {
    ScanItem x = found[i];
    int k = i - 1;
    while (k >= 0 && found[k].rssi < x.rssi) { found[k + 1] = found[k]; k--; }
    found[k + 1] = x;
  }
}

// Kênh của mạng theo lần quét gần nhất (0 = không biết). Giúp việc kết nối ít phải rời kênh.
int channelOf(const String& ssid) {
  for (int i = 0; i < nFound; i++) {
    if (found[i].ssid == ssid) return found[i].channel;
  }
  return 0;
}

// --------------------------------------------------------------- thử mạng đã lưu
void tryCandidate(uint32_t now) {
  const Cred& c = cand[order[orderPos]];
  Serial.printf("[wifi] đang kết nối tới \"%s\"...\n", c.ssid.c_str());
  WiFi.disconnect();
  WiFi.begin(c.ssid.c_str(), c.pass.c_str(), channelOf(c.ssid));
  phase = Phase::CycleTry;
  phaseAt = now;
}

void startCycle(uint32_t now) {
  lastCycleAt = now;
  buildCandidates();
  if (nCand == 0) {                // không có mạng nào để thử: phát Wi-Fi riêng ngay
    phase = Phase::Idle;
    bootCycle = false;
    lostAt = now - WIFI_LOST_PORTAL_MS;
    return;
  }
  WiFi.disconnect();
  phase = Phase::CycleScan;
  phaseAt = now;
  startScan(now);
}

// Sau khi quét: chỉ thử các mạng đang thấy; không thấy mạng nào thì thử mạng ưu tiên nhất (mạng ẩn)
void orderAfterScan(bool scanOk, uint32_t now) {
  nOrder = 0;
  if (scanOk) {
    for (int i = 0; i < nCand; i++) {
      for (int k = 0; k < nFound; k++) {
        if (found[k].ssid == cand[i].ssid) { order[nOrder++] = i; break; }
      }
    }
    if (nOrder == 0) order[nOrder++] = 0;
  } else {
    for (int i = 0; i < nCand; i++) order[nOrder++] = i;
  }
  orderPos = 0;
  tryCandidate(now);
}

// --------------------------------------------------------------- Wi-Fi riêng
void startPortal(bool manual, uint32_t now) {
  if (portal) {
    portalManual = portalManual || manual;
    closeAt = 0;
    lastClientAt = now;
    return;
  }
  if (!connected() && phase == Phase::Idle) WiFi.disconnect();   // STA đứng yên để Wi-Fi riêng ổn định
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD)) {
    Serial.println(F("[wifi] LỖI: không phát được Wi-Fi riêng"));
    WiFi.mode(WIFI_STA);
    lostAt = now;                  // đợi thêm một chu kỳ rồi mới thử lại, tránh lặp liên tục
    return;
  }
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());     // mọi tên miền đều trỏ về ESP32 -> điện thoại tự bật trang cài đặt
  portal = true;
  portalManual = manual;
  closeAt = 0;
  lastClientAt = now;
  lastCycleAt = now;
  trialResult = Trial::None;
  Serial.printf("[wifi] đã phát Wi-Fi \"%s\" - nối vào rồi mở http://%s/\n",
                SETUP_AP_SSID, WiFi.softAPIP().toString().c_str());
  if (phase == Phase::Idle) startScan(now);
}

void closePortal() {
  if (!portal) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portal = false;
  portalManual = false;
  closeAt = 0;
  Serial.println(F("[wifi] đã tắt Wi-Fi riêng"));
}

void pollButton(uint32_t now) {
  static uint32_t downAt = 0;
  static bool fired = false;
  if (digitalRead(PIN_BOOT_BUTTON) != LOW) { downAt = 0; fired = false; return; }
  if (downAt == 0) { downAt = now ? now : 1; return; }
  if (!fired && (uint32_t)(now - downAt) >= BOOT_HOLD_MS) {
    fired = true;
    Serial.println(F("[wifi] giữ nút BOOT -> mở cài đặt Wi-Fi"));
    startPortal(true, now);
  }
}

void pollScan(uint32_t now) {
  if (!scanBusy) return;
  if (!scanRunning) {
    if ((int32_t)(now - scanNextAt) < 0) return;
    WiFi.scanDelete();
    WiFi.scanNetworks(true, false, false, scanByChannel ? WIFI_SCAN_DWELL_MS : 300, scanChannel);   // chạy nền
    scanRunning = true;
    scanAt = now;
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING && (uint32_t)(now - scanAt) < 10000) return;
  if (n == WIFI_SCAN_FAILED && (uint32_t)(now - scanAt) < 500) return;
  scanRunning = false;
  if (n >= 0) {
    scanOk = true;
    collectScan(n);
  }
  WiFi.scanDelete();
  if (scanByChannel && scanChannel < 13) {
    scanChannel++;
    scanNextAt = now + WIFI_SCAN_GAP_MS;
    return;
  }
  scanBusy = false;
  if (phase == Phase::CycleScan) orderAfterScan(scanOk, now);
}

void pollPhase(uint32_t now) {
  switch (phase) {
    case Phase::CycleTry:
      if (connected()) {
        phase = Phase::Idle;
        remember(cand[order[orderPos]]);
      } else if ((uint32_t)(now - phaseAt) >= WIFI_ATTEMPT_MS) {
        if (++orderPos < nOrder) {
          tryCandidate(now);
        } else {
          phase = Phase::Idle;
          WiFi.disconnect();
          lastCycleAt = now;
          Serial.println(F("[wifi] chưa kết nối được mạng đã lưu"));
          if (bootCycle) {
            bootCycle = false;
            startPortal(false, now);
          }
        }
      }
      break;

    case Phase::TrialWait:
      if ((uint32_t)(now - phaseAt) >= 600) {
        WiFi.disconnect();
        delay(100);
        WiFi.begin(trial.ssid.c_str(), trial.pass.c_str(), channelOf(trial.ssid));
        phase = Phase::TrialTry;
        phaseAt = now;
      }
      break;

    case Phase::TrialTry:
      if (connected() && WiFi.SSID() == trial.ssid) {
        phase = Phase::Idle;
        trialResult = Trial::Ok;
        remember(trial);
        closeAt = now + WIFI_PORTAL_LINGER_MS;
        connAt = now;
        wasConnected = true;
        bootCycle = false;
        trial.pass = "";
        Serial.printf("[wifi] mạng mới kết nối được, IP = %s\n", WiFi.localIP().toString().c_str());
      } else if ((uint32_t)(now - phaseAt) >= WIFI_TRIAL_MS) {
        trialResult = Trial::Fail;
        Serial.printf("[wifi] không kết nối được \"%s\" - giữ nguyên các mạng đã lưu\n", trial.ssid.c_str());
        trial.pass = "";
        startCycle(now);           // quay lại mạng cũ
      }
      break;

    default:
      break;
  }
}

}  // namespace

// --------------------------------------------------------------- hàm công khai
bool connected() { return WiFi.status() == WL_CONNECTED; }
uint32_t connectedAt() { return connAt; }
bool portalActive() { return portal; }

void begin() {
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  WiFi.persistent(false);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);    // tự quản lý việc kết nối lại để Wi-Fi riêng không bị ngắt quãng
  WiFi.onEvent([](arduino_event_id_t e, arduino_event_info_t) {
    if (e == ARDUINO_EVENT_WIFI_AP_STACONNECTED)    Serial.printf("[wifi] %lu ms: có thiết bị nối vào Wi-Fi cài đặt\n", (unsigned long)millis());
    if (e == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) Serial.printf("[wifi] %lu ms: thiết bị rời Wi-Fi cài đặt\n", (unsigned long)millis());
  });
  loadSaved();
#ifdef SMARTROOM_TEST
  store.begin(NVS_NAMESPACE, false);       // "blackout <giây> boot": lần khởi động này coi như không có mạng
  if (store.isKey("tbo")) {
    blackoutUntil = millis() + store.getUInt("tbo", 0) * 1000UL;
    store.remove("tbo");
    Serial.println(F("[test] khởi động trong trạng thái không có mạng"));
  }
  store.end();
#endif
  uint32_t now = millis();
  lostAt = now;
  startCycle(now);
}

void poll(uint32_t now) {
  pollButton(now);
  if (openAt != 0 && (int32_t)(now - openAt) >= 0) {
    openAt = 0;
    startPortal(true, now);
  }
  if (portal) dns.processNextRequest();
  pollScan(now);
  pollPhase(now);

  bool trialRunning = (phase == Phase::TrialWait || phase == Phase::TrialTry);
  bool conn = connected();

  if (!trialRunning) {
    if (conn && !wasConnected) {
      wasConnected = true;
      bootCycle = false;
      connAt = now;
    } else if (!conn && wasConnected) {
      wasConnected = false;
      lostAt = now;
      lastCycleAt = now - WIFI_RETRY_MS + 2000;     // thử lại sau 2 giây
      closeAt = 0;
      Serial.println(F("[wifi] mất kết nối"));
    }
  }

  if (!portal) {
    if (!conn && !trialRunning) {
      if (!bootCycle && (uint32_t)(now - lostAt) >= WIFI_LOST_PORTAL_MS) {
        startPortal(false, now);
      } else if (phase == Phase::Idle && (uint32_t)(now - lastCycleAt) >= WIFI_RETRY_MS) {
        startCycle(now);
      }
    }
    return;
  }

  // Đang phát Wi-Fi riêng
  int clients = WiFi.softAPgetStationNum();
  if (clients > 0) lastClientAt = now;
  if (trialRunning) return;

  if (!conn) {
    if (phase == Phase::Idle && clients == 0 && (uint32_t)(now - lastCycleAt) >= WIFI_PORTAL_RETRY_MS) startCycle(now);
    return;
  }
  if (closeAt == 0) {
    bool idle = (uint32_t)(now - lastClientAt) >= WIFI_PORTAL_IDLE_MS;
    if (!portalManual) closeAt = now + WIFI_PORTAL_LINGER_MS;
    else if (idle)     closeAt = now;
  }
  if (closeAt != 0 && (int32_t)(now - closeAt) >= 0) closePortal();
}

void openPortal() {
  openAt = millis() + WIFI_OPEN_DELAY_MS;
  if (openAt == 0) openAt = 1;
}

namespace {
bool validInput(const String& ssid, const String& pass, const char** err) {
  if (phase == Phase::TrialWait || phase == Phase::TrialTry) {
    if (err) *err = "Dang thu mot mang khac, doi vai giay";
    return false;
  }
  if (ssid.length() < 1 || ssid.length() > 32) {
    if (err) *err = "Ten mang phai dai 1..32 ky tu";
    return false;
  }
  if (pass.length() != 0 && (pass.length() < 8 || pass.length() > 64)) {
    if (err) *err = "Mat khau Wi-Fi phai de trong (mang mo) hoac dai 8..64 ky tu";
    return false;
  }
  return true;
}
}  // namespace

bool saveForLater(const String& ssid, const String& pass, const char** err) {
  if (!validInput(ssid, pass, err)) return false;
  Cred c;
  c.ssid = ssid;
  c.pass = pass;
  remember(c);
  // thử ngay khi không còn ai nối vào Wi-Fi riêng
  lastCycleAt = millis() - WIFI_PORTAL_RETRY_MS;
  return true;
}

bool forgetNetwork(const String& ssid) {
  for (int i = 0; i < nSaved; i++) {
    if (saved[i].ssid != ssid) continue;
    for (int k = i; k < nSaved - 1; k++) saved[k] = saved[k + 1];
    nSaved--;
    writeSaved();
    Serial.printf("[wifi] đã quên mạng \"%s\"\n", ssid.c_str());
    return true;
  }
  return false;
}

#ifdef SMARTROOM_TEST
void testBlackoutAtBoot(uint32_t seconds) {
  store.begin(NVS_NAMESPACE, false);
  store.putUInt("tbo", seconds);
  store.end();
  ESP.restart();
}

void testBlackout(uint32_t seconds) {
  blackoutUntil = millis() + seconds * 1000UL;
  Serial.printf("[test] giả lập mất mạng %lu giây\n", (unsigned long)seconds);
  WiFi.disconnect();
}
#endif

bool requestConnect(const String& ssid, const String& pass, const char** err) {
  if (!validInput(ssid, pass, err)) return false;
  trial.ssid = ssid;
  trial.pass = pass;
  trialResult = Trial::Connecting;
  closeAt = 0;
  cancelScan();
  phase = Phase::TrialWait;
  phaseAt = millis();
  return true;
}

Trial trialState() { return trialResult; }
const String& trialSsid() { return trial.ssid; }

void requestScan() {
  if (phase == Phase::TrialWait || phase == Phase::TrialTry) return;   // đang thử mạng mới: không quét
  if (phase == Phase::CycleTry) {      // đang thử mạng cũ thì dừng lại, ưu tiên người đang cài đặt
    WiFi.disconnect();
    phase = Phase::Idle;
    lastCycleAt = millis();
  }
  startScan(millis());
}

bool scanning() { return scanBusy; }
int scanCount() { return nFound; }
const ScanItem& scanItem(int i) { return found[i]; }

int savedCount() { return nSaved; }
const String& savedSsid(int i) { return saved[i].ssid; }

void forgetAll() {
  nSaved = 0;
  writeSaved();
  Serial.println(F("[wifi] đã xóa các mạng đã lưu"));
}

}  // namespace net
