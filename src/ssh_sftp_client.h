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

// Ghi dữ liệu vào file đã mở
ssize_t sftp_write_remote(LIBSSH2_SFTP_HANDLE *handle, const char *buffer, size_t count);

// Tạo file mới trên máy remote
LIBSSH2_SFTP_HANDLE* sftp_create_remote(const char *remote_path, long mode);

// Xóa file từ xa
int sftp_unlink_remote(const char *remote_path);

// Tạo thư mục từ xa
int sftp_mkdir_remote(const char *remote_path, long mode);

// Xóa thư mục từ xa
int sftp_rmdir_remote(const char *remote_path);

// Cắt ngắn file từ xa
int sftp_truncate_remote(const char *remote_path, off_t size);

// Đóng file từ xa
int sftp_close_remote(LIBSSH2_SFTP_HANDLE *handle);

// Chuyển đổi mã lỗi SFTP sang mã lỗi POSIX errno
int sftp_error_to_errno(unsigned long sftp_err);

// Các hàm hỗ trợ tính năng mv và cp
// Sao chép file từ local vào remote
int sftp_copy_local_to_remote(const char *local_path, const char *remote_path);

// Sao chép file từ remote về local
int sftp_copy_remote_to_local(const char *remote_path, const char *local_path);

// Di chuyển file từ local vào remote (copy và xóa local)
int sftp_move_local_to_remote(const char *local_path, const char *remote_path);

// Di chuyển file từ remote về local (copy và xóa remote)
int sftp_move_remote_to_local(const char *remote_path, const char *local_path);

#endif // SSH_SFTP_CLIENT_H