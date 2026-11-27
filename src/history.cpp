#include "history.h"

#include <FS.h>
#include <LittleFS.h>

#include "config.h"

namespace history {
namespace {

constexpr size_t   REC_SIZE  = 9;              // ts(4) + t10(2) + h(1) + light(2)
constexpr uint32_t TZ_OFFSET = 7 * 3600;       // ngày tính theo giờ Việt Nam
constexpr int      MAX_FILES = HIST_KEEP_DAYS + 16;
const char* const  DIR_PATH  = "/hist";

Sample ring[HIST_MAX];
int    nRing = 0;
int    head = 0;                               // vị trí ghi tiếp theo
bool   mounted = false;

uint32_t dayOf(uint32_t ts) { return (ts + TZ_OFFSET) / 86400UL; }

void pathOf(uint32_t day, char* buf, size_t n) {
  snprintf(buf, n, "%s/%lu.bin", DIR_PATH, (unsigned long)day);
}

void pushRing(const Sample& s) {
  ring[head] = s;
  head = (head + 1) % HIST_MAX;
  if (nRing < HIST_MAX) nRing++;
}

void pack(const Sample& s, uint8_t* b) {
  memcpy(b, &s.ts, 4);
  memcpy(b + 4, &s.t10, 2);
  b[6] = s.h;
  memcpy(b + 7, &s.light, 2);
}

void unpack(const uint8_t* b, Sample& s) {
  memcpy(&s.ts, b, 4);
  memcpy(&s.t10, b + 4, 2);
  s.h = b[6];
  memcpy(&s.light, b + 7, 2);
}

// Danh sách ngày đang có file, sắp tăng dần
int listDays(uint32_t* days, int max) {
  if (!mounted) return 0;
  File dir = LittleFS.open(DIR_PATH);
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* name = f.name();
    const char* slash = strrchr(name, '/');
    uint32_t d = strtoul(slash ? slash + 1 : name, nullptr, 10);
    if (d == 0) continue;
    if (n < max) {
      days[n++] = d;
    } else {                                   // quá nhiều file: giữ lại các ngày mới nhất
      int oldest = 0;
      for (int i = 1; i < n; i++) if (days[i] < days[oldest]) oldest = i;
      if (d > days[oldest]) days[oldest] = d;
    }
  }
  for (int i = 1; i < n; i++) {
    uint32_t x = days[i];
    int k = i - 1;
    while (k >= 0 && days[k] > x) { days[k + 1] = days[k]; k--; }
    days[k + 1] = x;
  }
  return n;
}

void readDay(uint32_t day, uint32_t fromTs, Visitor fn, void* ctx) {
  char path[32];
  pathOf(day, path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) return;
  uint8_t buf[REC_SIZE * 32];
  for (;;) {
    int got = f.read(buf, sizeof(buf));
    if (got < (int)REC_SIZE) break;
    for (int off = 0; off + (int)REC_SIZE <= got; off += REC_SIZE) {
      Sample s;
      unpack(buf + off, s);
      if (dayOf(s.ts) != day || s.ts < fromTs) continue;      // bỏ dữ liệu hỏng
      fn(s, ctx);
    }
  }
  f.close();
}

// Xóa các ngày quá hạn; luôn giữ tối đa HIST_KEEP_DAYS file
void prune() {
  uint32_t days[MAX_FILES];
  int n = listDays(days, MAX_FILES);
  char path[32];
  for (int i = 0; i < n - HIST_KEEP_DAYS; i++) {
    pathOf(days[i], path, sizeof(path));
    LittleFS.remove(path);
    Serial.printf("[lịch sử] xóa dữ liệu cũ %s\n", path);
  }
}

void pushVisitor(const Sample& s, void*) { pushRing(s); }

}  // namespace

void begin() {
  mounted = LittleFS.begin(true);              // tự định dạng nếu phân vùng chưa dùng bao giờ
  if (!mounted) {
    Serial.println(F("[lịch sử] LỖI: không gắn được LittleFS - chỉ lưu trong RAM"));
    return;
  }
  if (!LittleFS.exists(DIR_PATH)) LittleFS.mkdir(DIR_PATH);
  prune();
  uint32_t days[MAX_FILES];
  int n = listDays(days, MAX_FILES);
  for (int i = (n > 2 ? n - 2 : 0); i < n; i++) readDay(days[i], 0, pushVisitor, nullptr);
  Serial.printf("[lịch sử] flash: %d ngày, nạp lại %d mẫu, đã dùng %u/%u KB\n",
                n, nRing, (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
}

bool flashOk() { return mounted; }

void add(const Sample& s) {
  pushRing(s);
  if (!mounted) return;
  char path[32];
  pathOf(dayOf(s.ts), path, sizeof(path));
  bool isNew = !LittleFS.exists(path);
  File f = LittleFS.open(path, "a");
  if (!f) {
    Serial.println(F("[lịch sử] LỖI: không ghi được flash"));
    return;
  }
  uint8_t b[REC_SIZE];
  pack(s, b);
  if (f.write(b, REC_SIZE) != REC_SIZE) Serial.println(F("[lịch sử] LỖI: ghi thiếu dữ liệu (đầy flash?)"));
  f.close();
  if (isNew) prune();
}

int count() { return nRing; }

const Sample& at(int i) { return ring[(head - nRing + i + HIST_MAX) % HIST_MAX]; }

uint32_t lastTs() { return nRing ? at(nRing - 1).ts : 0; }

void forEachStored(uint32_t fromTs, Visitor fn, void* ctx) {
  uint32_t days[MAX_FILES];
  int n = listDays(days, MAX_FILES);
  for (int i = 0; i < n; i++) {
    if (days[i] < dayOf(fromTs)) continue;
    readDay(days[i], fromTs, fn, ctx);
  }
}

int storedDays() {
  uint32_t days[MAX_FILES];
  return listDays(days, MAX_FILES);
}

size_t usedBytes()  { return mounted ? LittleFS.usedBytes() : 0; }
size_t totalBytes() { return mounted ? LittleFS.totalBytes() : 0; }

void clear() {
  uint32_t days[MAX_FILES];
  char path[32];
  for (;;) {
    int n = listDays(days, MAX_FILES);
    if (n == 0) break;
    int removed = 0;
    for (int i = 0; i < n; i++) {
      pathOf(days[i], path, sizeof(path));
      if (LittleFS.remove(path)) removed++;
    }
    if (removed == 0) break;
  }
  nRing = 0;
  head = 0;
  Serial.println(F("[lịch sử] đã xóa toàn bộ"));
}

}  // namespace history
