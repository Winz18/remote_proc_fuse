#ifndef SSH_SFTP_CLIENT_H
#define SSH_SFTP_CLIENT_H

#include "common.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> // close()
#include <string.h> // strerror
#include <errno.h>  // errno

// Khởi tạo kết nối SSH và session SFTP
int sftp_connect_and_auth(remote_conn_info_t *conn);

// Đóng kết nối SSH và giải phóng tài nguyên
void sftp_disconnect(remote_conn_info_t *conn);

// Lấy thông tin file/thư mục từ xa (stat)
int sftp_stat_remote(const char *remote_path, LIBSSH2_SFTP_ATTRIBUTES *attrs);

// Mở thư mục từ xa
LIBSSH2_SFTP_HANDLE* sftp_opendir_remote(const char *remote_path);

// Đọc một entry từ thư mục đã mở
// Trả về số byte của tên entry, 0 nếu hết, < 0 nếu lỗi
int sftp_readdir_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t buffer_len, LIBSSH2_SFTP_ATTRIBUTES *attrs);

// Đóng thư mục từ xa
int sftp_closedir_remote(LIBSSH2_SFTP_HANDLE *handle);

// Mở file từ xa để đọc
LIBSSH2_SFTP_HANDLE* sftp_open_remote(const char *remote_path, unsigned long flags, long mode);

// Đọc dữ liệu từ file đã mở
ssize_t sftp_read_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t count);

// Đóng file từ xa
int sftp_close_remote(LIBSSH2_SFTP_HANDLE *handle);

#endif // SSH_SFTP_CLIENT_H