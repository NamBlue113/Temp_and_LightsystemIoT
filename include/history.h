#pragma once
// Lịch sử cảm biến: 24 giờ gần nhất giữ trong RAM để vẽ biểu đồ, đồng thời ghi vào
// flash (LittleFS) mỗi ngày một file, giữ HIST_KEEP_DAYS ngày. Không mất khi khởi động lại.
#include <Arduino.h>

namespace history {

struct Sample {
  uint32_t ts;      // thời điểm (epoch, giây)
  int16_t  t10;     // nhiệt độ x10
  uint8_t  h;       // độ ẩm %
  uint16_t light;   // mức sáng 0..4095
};

typedef void (*Visitor)(const Sample& s, void* ctx);

void begin();
bool flashOk();

void add(const Sample& s);

int           count();            // số mẫu trong RAM
const Sample& at(int i);          // 0 = cũ nhất
uint32_t      lastTs();           // 0 nếu chưa có mẫu

void forEachStored(uint32_t fromTs, Visitor fn, void* ctx);   // đọc từ flash, cũ -> mới
int    storedDays();
size_t usedBytes();
size_t totalBytes();
void   clear();

}  // namespace history
