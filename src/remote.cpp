#include "remote.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "config.h"
#include "secrets.h"
#include "firebase_config.h"
#include "google_ca.h"

#ifndef FIREBASE_EMAIL
#define FIREBASE_EMAIL ""
#endif
#ifndef FIREBASE_PASSWORD
#define FIREBASE_PASSWORD ""
#endif

#define DEVICE_PATH "/devices/" FIREBASE_DEVICE_ID

namespace remote {
namespace {

struct Command {
  char key[CLOUD_KEY_MAX + 1];
  char data[CLOUD_CMD_MAX + 1];
};

struct Reply {
  String path;
  String body;
};

// ---------- trao đổi giữa loop() và tác vụ mạng ----------
QueueHandle_t     cmdQueue = nullptr;
QueueHandle_t     replyQueue = nullptr;     // phần tử: Reply*
SemaphoreHandle_t stateLock = nullptr;
String            nextState;                // bảo vệ bởi stateLock
volatile bool     online = false;
volatile bool     expireRequested = false;  // chỉ dùng khi kiểm thử
String            lastKey;                  // chỉ loop() dùng

// ---------- chỉ tác vụ mạng dùng ----------
WiFiClientSecure restClient;                // ghi dữ liệu (giữ kết nối)
WiFiClientSecure streamClient;              // nhận lệnh tức thời (Server-Sent Events)
HTTPClient       rest;

String   idToken, refreshToken;
uint32_t tokenAt = 0, tokenLifeMs = 0;
uint32_t waitFrom = 0, waitMs = 0;          // tạm nghỉ sau lỗi

bool     streamOpen = false;
bool     chunked = false;
uint32_t lastDataAt = 0;
enum class Chunk : uint8_t { Size, Data, End };
Chunk    chunkState = Chunk::Size;
size_t   chunkLeft = 0;
String   chunkHeader;
char     line[CLOUD_LINE_MAX + 1];
size_t   lineLen = 0;
bool     lineTooLong = false;
char     eventName[16] = "";

String   doneKeys[8];                       // lệnh đã nhận, tránh chạy lại
uint8_t  donePos = 0;
String   toDelete[8];
uint8_t  toDeleteCount = 0;
bool     purgeCommands = false;
bool     repliesCleared = false;

void pause(uint32_t ms) {
  waitFrom = millis();
  waitMs = ms;
}

void closeStream() {
  if (streamOpen) Serial.println(F("[cloud] đóng luồng nhận lệnh"));
  streamOpen = false;
  online = false;
  streamClient.stop();
}

void closeAll() {
  closeStream();
  rest.end();
  restClient.stop();
}

// =====================================================================
//  Đăng nhập Firebase (lấy idToken, hết hạn sau 1 giờ)
// =====================================================================
int httpsPost(const String& url, const char* type, const String& body, String& response) {
  WiFiClientSecure client;
  client.setCACert(GOOGLE_CA_PEM);
  client.setHandshakeTimeout(15);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  if (!http.begin(client, url)) return -1;
  http.addHeader("Content-Type", type);
  int code = http.POST(body);
  if (code > 0) response = http.getString();
  http.end();
  return code;
}

bool signIn() {
  closeAll();                               // chỉ giữ một kết nối TLS khi đăng nhập
  bool useRefresh = refreshToken.length() > 0;
  String url, body, response;
  const char* type;
  if (useRefresh) {
    url = "https://securetoken.googleapis.com/v1/token?key=" FIREBASE_API_KEY;
    type = "application/x-www-form-urlencoded";
    body = "grant_type=refresh_token&refresh_token=" + refreshToken;
  } else {
    url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" FIREBASE_API_KEY;
    type = "application/json";
    JsonDocument d;
    d["email"] = FIREBASE_EMAIL;
    d["password"] = FIREBASE_PASSWORD;
    d["returnSecureToken"] = true;
    serializeJson(d, body);
  }

  int code = httpsPost(url, type, body, response);
  JsonDocument r;
  if (code == 200 && !deserializeJson(r, response)) {
    idToken      = r[useRefresh ? "id_token" : "idToken"] | "";
    refreshToken = r[useRefresh ? "refresh_token" : "refreshToken"] | "";
    long life    = atol(r[useRefresh ? "expires_in" : "expiresIn"] | "0");
    if (idToken.length() && life > 600) {
      tokenAt = millis();
      tokenLifeMs = (uint32_t)(life - 300) * 1000UL;   // làm mới sớm 5 phút
      Serial.println(useRefresh ? F("[cloud] đã làm mới phiên đăng nhập") : F("[cloud] đăng nhập Firebase thành công"));
      return true;
    }
  }

  idToken = "";
  if (useRefresh && code >= 400 && code < 500) {
    refreshToken = "";                      // lần sau đăng nhập lại bằng mật khẩu
    pause(1000);
  } else if (code >= 400 && code < 500) {
    deserializeJson(r, response);
    Serial.printf("[cloud] LỖI đăng nhập (%d %s). Kiểm tra FIREBASE_EMAIL / FIREBASE_PASSWORD\n",
                  code, (const char*)(r["error"]["message"] | ""));
    pause(CLOUD_AUTH_RETRY_MS);
  } else {
    Serial.printf("[cloud] không liên lạc được máy chủ đăng nhập (%d)\n", code);
    pause(CLOUD_RETRY_MS);
  }
  return false;
}

bool ensureToken() {
  if (expireRequested) {
    expireRequested = false;
    tokenLifeMs = 0;
  }
  if (idToken.length() && (uint32_t)(millis() - tokenAt) < tokenLifeMs) return true;
  return signIn();
}

// Máy chủ từ chối: phiên mới cấp mà vẫn bị từ chối nghĩa là do luật bảo mật
void rejected() {
  if ((uint32_t)(millis() - tokenAt) > 60000) {
    idToken = "";
  } else {
    Serial.println(F("[cloud] bị từ chối quyền - kiểm tra Rules của Realtime Database"));
    pause(CLOUD_AUTH_RETRY_MS);
  }
  closeStream();
}

// =====================================================================
//  Ghi / xóa dữ liệu
// =====================================================================
bool dbRequest(const char* method, const String& path, const String& body) {
  if (!idToken.length()) return false;
  String url = "https://" FIREBASE_DB_HOST + path + ".json?print=silent&auth=" + idToken;
  int code = -1;
  for (int attempt = 0; attempt < 2 && code <= 0; attempt++) {
    rest.setReuse(true);
    rest.setConnectTimeout(10000);
    rest.setTimeout(10000);
    if (!rest.begin(restClient, url)) break;
    if (body.length()) rest.addHeader("Content-Type", "application/json");
    code = rest.sendRequest(method, (uint8_t*)body.c_str(), body.length());
    if (code > 0 && code != 204) rest.getString();     // đọc hết để dùng lại kết nối
    rest.end();
    if (code <= 0) restClient.stop();                  // máy chủ đã đóng kết nối cũ
  }
  if (code == 200 || code == 204) return true;
  Serial.printf("[cloud] %s thất bại (%d)\n", method, code);
  if (code == 401 || code == 403) rejected();
  else pause(1000);                                     // không thử lại dồn dập
  return false;
}

// =====================================================================
//  Lệnh nhận về
// =====================================================================
String asJsonString(const String& json) {
  JsonDocument d;
  d.set(json);
  String out;
  serializeJson(d, out);
  return out;
}

void rejectCommand(const char* key, const char* why) {
  JsonDocument d;
  d["id"] = key;
  d["ok"] = false;
  d["error"] = why;
  String s;
  serializeJson(d, s);
  Reply* r = new Reply{String(DEVICE_PATH "/reply/") + key, asJsonString(s)};
  if (xQueueSend(replyQueue, &r, 0) != pdTRUE) delete r;
}

bool validKey(const char* key) {
  size_t n = strlen(key);
  if (n == 0 || n > CLOUD_KEY_MAX) return false;
  for (size_t i = 0; i < n; i++) {
    if (!isalnum((unsigned char)key[i]) && key[i] != '-' && key[i] != '_') return false;
  }
  return true;
}

void acceptCommand(const char* key, JsonVariantConst data) {
  if (!validKey(key) || toDeleteCount >= 8) {
    purgeCommands = true;
    return;
  }
  toDelete[toDeleteCount++] = key;
  for (const String& k : doneKeys) {
    if (k == key) return;
  }
  doneKeys[donePos++ % 8] = key;

  const char* op = data["op"] | "";
  int64_t at = data["at"] | (int64_t)0;       // giờ máy chủ Firebase (ms)
  int64_t now = (int64_t)time(nullptr) * 1000;
  if (at == 0 || llabs(now - at) > (int64_t)CLOUD_CMD_MAX_AGE_S * 1000) {
    Serial.println(F("[cloud] bỏ qua lệnh cũ"));
    rejectCommand(key, "lenh qua cu, hay gui lai");
    return;
  }

  JsonDocument body, payload;
  payload["id"] = key;
  payload["op"] = op;
  const char* text = data["body"] | "";
  if (*text && !deserializeJson(body, text)) payload["body"] = body.as<JsonVariantConst>();
  static Command c;
  if (measureJson(payload) > CLOUD_CMD_MAX) {
    rejectCommand(key, "lenh qua dai");
    return;
  }
  strlcpy(c.key, key, sizeof(c.key));
  serializeJson(payload, c.data, sizeof(c.data));
  if (xQueueSend(cmdQueue, &c, 0) != pdTRUE) rejectCommand(key, "thiet bi dang ban");
}

// Một sự kiện của luồng: put/patch kèm {"path":"/..","data":..}
void handleEvent(const char* name, const char* json) {
  if (!strcmp(name, "auth_revoked")) {
    idToken = "";
    closeStream();
    return;
  }
  if (!strcmp(name, "cancel")) {
    Serial.println(F("[cloud] bị từ chối quyền đọc lệnh - kiểm tra Rules"));
    closeStream();
    pause(CLOUD_AUTH_RETRY_MS);
    return;
  }
  if (strcmp(name, "put") && strcmp(name, "patch")) return;   // keep-alive

  JsonDocument d;
  if (deserializeJson(d, json)) {
    purgeCommands = true;
    return;
  }
  const char* path = d["path"] | "";
  JsonVariantConst data = d["data"];
  if (data.isNull() || path[0] != '/') return;                // lệnh vừa bị xóa
  if (path[1] == '\0') {
    for (JsonPairConst kv : data.as<JsonObjectConst>()) acceptCommand(kv.key().c_str(), kv.value());
  } else if (!strchr(path + 1, '/')) {
    acceptCommand(path + 1, data);
  }
}

void handleLine() {
  if (lineLen && line[lineLen - 1] == '\r') lineLen--;
  line[lineLen] = '\0';
  if (!strncmp(line, "event: ", 7)) {
    strlcpy(eventName, line + 7, sizeof(eventName));
  } else if (!strncmp(line, "data: ", 6)) {
    if (lineTooLong) purgeCommands = true;                    // quá nhiều lệnh tồn: xóa hết
    else handleEvent(eventName, line + 6);
  }
  lineLen = 0;
  lineTooLong = false;
}

void feedText(char c) {
  if (c == '\n') {
    handleLine();
  } else if (lineLen < CLOUD_LINE_MAX) {
    line[lineLen++] = c;
  } else {
    lineTooLong = true;
  }
}

void feedByte(char c) {
  if (!chunked) {
    feedText(c);
    return;
  }
  switch (chunkState) {
    case Chunk::Size:
      if (c == '\n') {
        chunkLeft = strtoul(chunkHeader.c_str(), nullptr, 16);
        chunkHeader = "";
        if (chunkLeft == 0) closeStream();
        else chunkState = Chunk::Data;
      } else if (chunkHeader.length() < 16) {
        chunkHeader += c;
      }
      break;
    case Chunk::Data:
      feedText(c);
      if (--chunkLeft == 0) chunkState = Chunk::End;
      break;
    case Chunk::End:
      if (c == '\n') chunkState = Chunk::Size;
      break;
  }
}

bool readHeaderLine(String& out) {
  out = "";
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < 10000) {
    if (streamClient.available()) {
      int c = streamClient.read();
      if (c < 0) return false;
      if (c == '\n') {
        out.trim();
        return true;
      }
      if (out.length() < 512) out += (char)c;
    } else if (!streamClient.connected()) {
      return false;
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  return false;
}

bool openStream() {
  String host = FIREBASE_DB_HOST;
  String path = String(DEVICE_PATH "/cmd.json?auth=") + idToken;
  for (int hop = 0; hop < 3; hop++) {
    streamClient.stop();
    streamClient.setCACert(GOOGLE_CA_PEM);
    streamClient.setHandshakeTimeout(15);
    if (!streamClient.connect(host.c_str(), 443, 10000)) break;
    streamClient.print("GET " + path + " HTTP/1.1\r\nHost: " + host +
                       "\r\nAccept: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
    String h, location;
    if (!readHeaderLine(h)) break;
    int code = h.substring(h.indexOf(' ') + 1).toInt();
    chunked = false;
    bool headersOk = false;
    while (readHeaderLine(h)) {
      if (!h.length()) {
        headersOk = true;
        break;
      }
      String low = h;
      low.toLowerCase();
      if (low.startsWith("transfer-encoding:") && low.indexOf("chunked") > 0) chunked = true;
      if (low.startsWith("location:")) {
        location = h.substring(9);
        location.trim();
      }
    }
    if (!headersOk) break;
    if (code == 200) {
      chunkState = Chunk::Size;
      chunkHeader = "";
      lineLen = 0;
      lineTooLong = false;
      eventName[0] = '\0';
      lastDataAt = millis();
      streamOpen = true;
      return true;
    }
    if (code == 307 && location.startsWith("https://")) {     // Firebase chuyển sang máy chủ khác
      int slash = location.indexOf('/', 8);
      if (slash < 0) break;
      host = location.substring(8, slash);
      path = location.substring(slash);
      continue;
    }
    Serial.printf("[cloud] không mở được luồng lệnh (%d)\n", code);
    streamClient.stop();
    if (code == 401 || code == 403) {
      rejected();
      return false;
    }
    break;
  }
  streamClient.stop();
  pause(CLOUD_RETRY_MS);
  return false;
}

void pumpStream() {
  uint8_t buf[256];
  while (streamOpen) {
    int avail = streamClient.available();
    if (avail <= 0) break;
    int n = streamClient.read(buf, min(avail, (int)sizeof(buf)));
    if (n <= 0) break;
    lastDataAt = millis();
    for (int i = 0; i < n && streamOpen; i++) feedByte((char)buf[i]);
  }
  if (streamOpen && (!streamClient.connected() || (uint32_t)(millis() - lastDataAt) > CLOUD_SILENCE_MS)) {
    closeStream();
    pause(1000);
  }
}

// =====================================================================
//  Tác vụ mạng
// =====================================================================
void task(void*) {
  restClient.setCACert(GOOGLE_CA_PEM);
  restClient.setHandshakeTimeout(15);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(20));
    // cần có mạng và giờ hợp lệ (để biết lệnh nào đã cũ)
    if (WiFi.status() != WL_CONNECTED || (uint32_t)time(nullptr) <= VALID_TIME_EPOCH) {
      if (streamOpen || restClient.connected()) closeAll();
      online = false;
      continue;
    }
    if (waitMs && (uint32_t)(millis() - waitFrom) < waitMs) continue;
    waitMs = 0;
    if (!ensureToken()) continue;

    if (!streamOpen) {
      if (!openStream()) continue;
      Serial.println(F("[cloud] đã kết nối Firebase"));
      if (!repliesCleared) repliesCleared = dbRequest("DELETE", DEVICE_PATH "/reply", "");
      if (streamOpen) online = true;
    }

    pumpStream();

    if (purgeCommands && dbRequest("DELETE", DEVICE_PATH "/cmd", "")) {
      purgeCommands = false;
      toDeleteCount = 0;
    }
    while (toDeleteCount && streamOpen) {
      if (!dbRequest("DELETE", String(DEVICE_PATH "/cmd/") + toDelete[toDeleteCount - 1], "")) break;
      toDeleteCount--;
    }

    Reply* r = nullptr;
    while (streamOpen && xQueueReceive(replyQueue, &r, 0) == pdTRUE) {
      dbRequest("PUT", r->path, r->body);
      delete r;
    }

    String state;
    xSemaphoreTake(stateLock, portMAX_DELAY);
    state = nextState;
    nextState = "";
    xSemaphoreGive(stateLock);
    if (streamOpen && state.length() > 2) {
      // "at" do máy chủ điền, trang web dựa vào đó để biết thiết bị còn trực tuyến
      dbRequest("PUT", DEVICE_PATH "/state", "{\"at\":{\".sv\":\"timestamp\"}," + state.substring(1));
    }
  }
}

}  // namespace

bool enabled() { return strlen(FIREBASE_EMAIL) > 0 && strlen(FIREBASE_PASSWORD) > 0; }
bool connected() { return online; }

void poll() {
  static bool started = false;
  if (started || !enabled() || WiFi.status() != WL_CONNECTED) return;
  started = true;
  cmdQueue = xQueueCreate(4, sizeof(Command));
  replyQueue = xQueueCreate(6, sizeof(Reply*));
  stateLock = xSemaphoreCreateMutex();
  if (!cmdQueue || !replyQueue || !stateLock ||
      // chạy chung nhân với loop(), cùng mức ưu tiên (chia thời gian). Đặt ở nhân 0 thì lúc
      // mã hóa TLS trùng với ghi flash liên tục sẽ làm watchdog khởi động lại chip
      xTaskCreatePinnedToCore(task, "cloud", CLOUD_TASK_STACK, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE) != pdPASS) {
    Serial.println(F("[cloud] LỖI: không khởi động được tác vụ"));
  }
}

bool popCommand(String& out) {
  if (!cmdQueue) return false;
  static Command c;
  if (xQueueReceive(cmdQueue, &c, 0) != pdTRUE) return false;
  lastKey = c.key;
  out = c.data;
  return true;
}

bool publishState(const String& json) {
  if (!online) return false;
  xSemaphoreTake(stateLock, portMAX_DELAY);
  nextState = json;
  xSemaphoreGive(stateLock);
  return true;
}

bool publishReply(const String& json) {
  if (!replyQueue || !lastKey.length()) return false;
  Reply* r = new Reply{String(DEVICE_PATH "/reply/") + lastKey, asJsonString(json)};
  lastKey = "";
  if (xQueueSend(replyQueue, &r, 0) == pdTRUE) return true;
  delete r;
  return false;
}

void testExpireToken() { expireRequested = true; }

}  // namespace remote
