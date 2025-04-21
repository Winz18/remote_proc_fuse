#ifndef COMMON_H
#define COMMON_H

#define FUSE_USE_VERSION 31 // Sử dụng FUSE API version 3.1

#include <fuse.h>
#include <libssh2.h>
#include <libssh2_sftp.h>

// Cấu trúc lưu trữ thông tin kết nối và session
typedef struct {
    char *remote_host;
    char *remote_user;
    char *remote_pass; // Cẩn thận: không an toàn khi lưu pass thế này
    char *ssh_key_path; // Đường dẫn đến private key nếu dùng xác thực key
    int remote_port;
    char *remote_proc_path; // Đường dẫn cơ sở trên máy remote (thường là "/proc")

    // --- Trạng thái runtime ---
    int sock; // Socket descriptor
    LIBSSH2_SESSION *ssh_session;
    LIBSSH2_SFTP *sftp_session;

} remote_conn_info_t;

// Hàm helper để lấy thông tin kết nối từ FUSE context
static inline remote_conn_info_t* get_conn_info() {
    return (remote_conn_info_t*)fuse_get_context()->private_data;
}

// Macro tiện ích cho logging (in ra stderr để xem khi chạy FUSE với -f)
#define LOG_ERR(fmt, ...) fprintf(stderr, "[ERROR] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) fprintf(stderr, "[INFO] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__) // Chỉ hiện khi biên dịch với DEBUG flag

#endif // COMMON_H
