#ifndef SSH_SFTP_CLIENT_H
#define SSH_SFTP_CLIENT_H

#include "common.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
// Cần include libssh2_sftp.h để định nghĩa kiểu LIBSSH2_SFTP_ATTRIBUTES
#include <libssh2_sftp.h>

// --- Khai báo các hàm ---
int sftp_connect_and_auth(remote_conn_info_t *conn);
void sftp_disconnect(remote_conn_info_t *conn);
int sftp_stat_remote(const char *remote_path, LIBSSH2_SFTP_ATTRIBUTES *attrs);
LIBSSH2_SFTP_HANDLE* sftp_opendir_remote(const char *remote_path);
int sftp_readdir_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t buffer_len, LIBSSH2_SFTP_ATTRIBUTES *attrs);
int sftp_closedir_remote(LIBSSH2_SFTP_HANDLE *handle);
LIBSSH2_SFTP_HANDLE* sftp_open_remote(const char *remote_path, unsigned long flags, long mode);
ssize_t sftp_read_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t count);
ssize_t sftp_write_remote(LIBSSH2_SFTP_HANDLE *handle, const char *buffer, size_t count);
int sftp_close_remote(LIBSSH2_SFTP_HANDLE *handle);
LIBSSH2_SFTP_HANDLE* sftp_create_remote(const char *remote_path, long mode);
int sftp_unlink_remote(const char *remote_path);
int sftp_mkdir_remote(const char *remote_path, long mode);
int sftp_rmdir_remote(const char *remote_path);
// int sftp_close_remote(LIBSSH2_SFTP_HANDLE *handle); // Khai báo bị lặp lại, bỏ đi
int sftp_error_to_errno(unsigned long sftp_err);
int sftp_copy_local_to_remote(const char *local_path, const char *remote_path);
int sftp_copy_remote_to_local(const char *remote_path, const char *local_path);
int sftp_move_local_to_remote(const char *local_path, const char *remote_path);
int sftp_move_remote_to_local(const char *remote_path, const char *local_path);

// ---> THÊM KHAI BÁO CHO HÀM RENAME <---
int sftp_rename_remote(const char *old_path, const char *new_path);

// ---> THÊM KHAI BÁO CHO HÀM SETSTAT <--- (Có thể cần sau này)
int sftp_setstat_remote(const char *remote_path, LIBSSH2_SFTP_ATTRIBUTES *attrs);

#endif // SSH_SFTP_CLIENT_H
