#pragma once
// Copy file này thành include/secrets.h rồi điền thông tin thật.
// KHÔNG commit secrets.h lên GitHub (đã có trong .gitignore).

#define WIFI_SSID     "TEN_WIFI"
#define WIFI_PASSWORD "MAT_KHAU_WIFI"

// Mật khẩu cập nhật firmware qua Wi-Fi (OTA). Hãy đổi thành chuỗi riêng của bạn.
#define OTA_PASSWORD  "doi-mat-khau-ota"

// Mật khẩu đăng nhập trang web điều khiển (điện thoại/laptop). Chỉ dùng chữ, số.
#define WEB_PASSWORD  "doi-mat-khau-web"

// Mật khẩu của Wi-Fi "Smartroom-Setup" do ESP32 phát ra để đổi mạng từ điện thoại.
// Bắt buộc dài ít nhất 8 ký tự.
#define SETUP_AP_PASSWORD "doi-mat-khau-setup"

// Điều khiển từ xa qua Firebase (không bắt buộc). Bỏ dấu // ở hai dòng dưới và điền
// tài khoản Firebase Authentication dành riêng cho thiết bị.
// #define FIREBASE_EMAIL    "device@example.com"
// #define FIREBASE_PASSWORD "mat-khau-manh-cua-thiet-bi"
