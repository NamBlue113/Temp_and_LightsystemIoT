#pragma once
// Điều khiển từ xa qua Firebase Realtime Database (HTTPS). Chỉ hoạt động khi
// secrets.h có FIREBASE_EMAIL và FIREBASE_PASSWORD; thiếu thì module tự tắt.
//   /devices/<id>/state       trạng thái thiết bị, "at" là giờ máy chủ
//   /devices/<id>/cmd/<mã>    lệnh: {"op":"..","body":"<JSON>","at":<giờ máy chủ>}
//   /devices/<id>/reply/<mã>  phản hồi: chuỗi JSON {"id":"..","ok":true,...}
// Mọi việc mạng chạy trong tác vụ riêng nên không làm chậm vòng điều khiển.
#include <Arduino.h>

namespace remote {

bool enabled();
bool connected();
void poll();                                   // khởi động tác vụ khi đã có mạng
bool popCommand(String& out);                  // lấy một lệnh đang chờ (gọi từ loop)
bool publishState(const String& json);
bool publishReply(const String& json);         // trả lời cho lệnh vừa lấy ra
void testExpireToken();                        // kiểm thử: buộc làm mới phiên đăng nhập

}  // namespace remote
