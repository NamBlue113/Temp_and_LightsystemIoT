#pragma once
// Quản lý Wi-Fi: nhớ nhiều mạng, tự kết nối lại, phát Wi-Fi riêng + captive portal
// để đổi mạng từ điện thoại. Không dùng delay dài nên không làm đứng vòng loop.
#include <Arduino.h>

namespace net {

// Kết quả của lần thử mạng do người dùng nhập trên trang cài đặt
enum class Trial : uint8_t { None, Connecting, Ok, Fail };

struct ScanItem {
  String ssid;
  int    rssi;
  bool   open;
  uint8_t channel;
};

void begin();
void poll(uint32_t now);

bool     connected();
uint32_t connectedAt();          // millis() của lần kết nối gần nhất
bool     portalActive();
void     openPortal();           // mở bằng tay (nút BOOT, dashboard, Serial); bật sau vài trăm ms

// Thử mạng mới; kết quả xem bằng trialState(). Trả về false nếu dữ liệu sai.
bool          requestConnect(const String& ssid, const String& pass, const char** err);
Trial         trialState();
const String& trialSsid();

void            requestScan();
bool            scanning();
int             scanCount();
const ScanItem& scanItem(int i);

// Lưu mạng mà không thử ngay (vd hotspot điện thoại đang tắt); sẽ tự kết nối khi thấy mạng
bool          saveForLater(const String& ssid, const String& pass, const char** err);
bool          forgetNetwork(const String& ssid);

#ifdef SMARTROOM_TEST
void          testBlackout(uint32_t seconds);   // chỉ để thử: giả lập mất hết mạng
void          testBlackoutAtBoot(uint32_t seconds);
#endif

int           savedCount();
const String& savedSsid(int i);
void          forgetAll();       // xóa các mạng đã lưu (mạng trong secrets.h vẫn còn làm dự phòng)

}  // namespace net
