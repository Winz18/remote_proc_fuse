#include "ssh_sftp_client.h"
#include <stdio.h>
#include <stdlib.h>

// --- Triển khai các hàm SSH/SFTP ---

int sftp_connect_and_auth(remote_conn_info_t *conn) {
    struct sockaddr_in sin;
    int rc;

    // --- Khởi tạo Socket ---
    conn->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sock == -1) {
        LOG_ERR("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(conn->remote_port);
    if (inet_pton(AF_INET, conn->remote_host, &sin.sin_addr) <= 0) {
        LOG_ERR("Failed to convert IP address '%s'", conn->remote_host);
        close(conn->sock);
        conn->sock = -1;
        return -1;
    }

    if (connect(conn->sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
        LOG_ERR("Failed to connect socket to %s:%d - %s", conn->remote_host, conn->remote_port, strerror(errno));
        close(conn->sock);
        conn->sock = -1;
        return -1;
    }
    LOG_INFO("Socket connected to %s:%d", conn->remote_host, conn->remote_port);

    // --- Khởi tạo libssh2 ---
    rc = libssh2_init(0);
    if (rc != 0) {
        LOG_ERR("libssh2 initialization failed (%d)", rc);
        close(conn->sock);
        conn->sock = -1;
        return -1;
    }

    // --- Tạo SSH Session ---
    conn->ssh_session = libssh2_session_init();
    if (!conn->ssh_session) {
        LOG_ERR("Failed to initialize SSH session");
        close(conn->sock);
        conn->sock = -1;
        libssh2_exit();
        return -1;
    }
    // libssh2_session_set_blocking(conn->ssh_session, 1); // Mặc định là blocking

    // --- Handshake ---
    rc = libssh2_session_handshake(conn->ssh_session, conn->sock);
    if (rc) {
        LOG_ERR("SSH handshake failed: %d", rc);
        sftp_disconnect(conn); // Dọn dẹp
        return -1;
    }
    LOG_INFO("SSH handshake successful");

    // *** THÊM ĐẶT TIMEOUT ***
    long session_timeout_ms = 60000; // Ví dụ: 60 giây
    libssh2_session_set_timeout(conn->ssh_session, session_timeout_ms);
    LOG_INFO("Set SSH session timeout to %ld ms", session_timeout_ms);
    // *** KẾT THÚC THÊM TIMEOUT ***

    // --- Xác thực ---
    const char *fingerprint = libssh2_hostkey_hash(conn->ssh_session, LIBSSH2_HOSTKEY_HASH_SHA1);
    LOG_INFO("Host key fingerprint (SHA1):");
    for(int i = 0; i < 20; i++) {
        fprintf(stderr, "%02X ", (unsigned char)fingerprint[i]);
    }
    fprintf(stderr, "\n");
    // !! Trong ứng dụng thực tế, bạn cần kiểm tra fingerprint này với known_hosts !!

    if (conn->ssh_key_path) {
        // Xác thực bằng Public Key
        // TODO: Cần thêm đường dẫn public key nếu cần
        // char *pubkey_path = ...; // Tìm file .pub tương ứng hoặc để NULL
        rc = libssh2_userauth_publickey_fromfile(conn->ssh_session, conn->remote_user,
                                                 NULL, // auto-detect pubkey file
                                                 conn->ssh_key_path,
                                                 conn->remote_pass); // Passphrase for key
    } else if (conn->remote_pass) {
        // Xác thực bằng Mật khẩu
        rc = libssh2_userauth_password(conn->ssh_session, conn->remote_user, conn->remote_pass);
    } else {
         LOG_ERR("No authentication method specified (password or key path needed).");
         rc = -1; // Indicate error
    }


    if (rc) {
        LOG_ERR("SSH authentication failed: %d", rc);
        char *errmsg;
        int errmsg_len;
        libssh2_session_last_error(conn->ssh_session, &errmsg, &errmsg_len, 0);
        if(errmsg) LOG_ERR("Last SSH error message: %.*s", errmsg_len, errmsg);
        sftp_disconnect(conn);
        return -1;
    }
    LOG_INFO("SSH authentication successful for user '%s'", conn->remote_user);

    // --- Khởi tạo SFTP Session ---
    conn->sftp_session = libssh2_sftp_init(conn->ssh_session);
    if (!conn->sftp_session) {
        LOG_ERR("Failed to initialize SFTP session: %d", libssh2_session_last_errno(conn->ssh_session));
        sftp_disconnect(conn);
        return -1;
    }
    LOG_INFO("SFTP session initialized");

    return 0; // Thành công
}

void sftp_disconnect(remote_conn_info_t *conn) {
    if (conn->sftp_session) {
        libssh2_sftp_shutdown(conn->sftp_session);
        conn->sftp_session = NULL;
        LOG_INFO("SFTP session shut down");
    }
    if (conn->ssh_session) {
        libssh2_session_disconnect(conn->ssh_session, "Normal Shutdown");
        libssh2_session_free(conn->ssh_session);
        conn->ssh_session = NULL;
        LOG_INFO("SSH session disconnected and freed");
    }
    if (conn->sock != -1) {
        close(conn->sock);
        conn->sock = -1;
        LOG_INFO("Socket closed");
    }
    libssh2_exit(); // Nên gọi khi chương trình kết thúc hẳn, không phải mỗi lần disconnect?
                   // Gọi ở đây có thể ảnh hưởng nếu có nhiều instance FUSE?
                   // Tạm để đây, nhưng xem xét lại nếu cần.
    LOG_INFO("libssh2 exited");
}

int sftp_stat_remote(const char *remote_path, LIBSSH2_SFTP_ATTRIBUTES *attrs) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return LIBSSH2_ERROR_SOCKET_DISCONNECT; // Hoặc lỗi phù hợp khác

    LOG_DEBUG("SFTP stat: %s", remote_path);
    int rc = libssh2_sftp_stat_ex(conn->sftp_session, remote_path, strlen(remote_path),
                                  LIBSSH2_SFTP_STAT, attrs);
    if (rc != 0) {
        // Không log lỗi ở đây vì file/dir không tồn tại là bình thường
        // LOG_ERR("libssh2_sftp_stat_ex failed for %s: %d", remote_path, rc);
    }
    return rc; // Trả về mã lỗi của libssh2
}

LIBSSH2_SFTP_HANDLE* sftp_opendir_remote(const char *remote_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return NULL;

    LOG_DEBUG("SFTP opendir: %s", remote_path);
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(conn->sftp_session, remote_path);
    if (!handle) {
         LOG_ERR("libssh2_sftp_opendir failed for %s: %d (SFTP Error: %lu)",
                 remote_path, libssh2_session_last_errno(conn->ssh_session),
                 libssh2_sftp_last_error(conn->sftp_session));
    }
    return handle;
}

int sftp_readdir_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t buffer_len, LIBSSH2_SFTP_ATTRIBUTES *attrs) {
    remote_conn_info_t *conn = get_conn_info(); // Cần conn để lấy sftp_session? Thực ra handle là đủ
    if (!conn || !conn->sftp_session || !handle) return LIBSSH2_ERROR_BAD_USE;

    LOG_DEBUG("SFTP readdir");
    // libssh2_sftp_readdir_ex trả về số byte đọc vào buffer (tên file/dir),
    // 0 nếu hết entry, < 0 nếu lỗi
    int rc = libssh2_sftp_readdir_ex(handle, buffer, buffer_len, NULL, 0, attrs); // Không cần longname
    if (rc < 0 && rc != LIBSSH2_ERROR_FILE) { // LIBSSH2_ERROR_FILE thường là EOF
         LOG_ERR("libssh2_sftp_readdir_ex failed: %d", rc);
    } else if (rc == LIBSSH2_ERROR_FILE || rc == 0) { // Xử lý EOF
        LOG_DEBUG("SFTP readdir: EOF");
        return 0; // Coi như là hết entry
    }
    LOG_DEBUG("SFTP readdir found: %s (size %d)", buffer, rc);
    return rc; // Trả về số byte của tên entry
}


int sftp_closedir_remote(LIBSSH2_SFTP_HANDLE *handle) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session || !handle) return LIBSSH2_ERROR_BAD_USE;

    LOG_DEBUG("SFTP closedir");
    int rc = libssh2_sftp_close_handle(handle);
    if (rc != 0) {
        LOG_ERR("libssh2_sftp_close_handle (dir) failed: %d", rc);
    }
    return rc;
}

LIBSSH2_SFTP_HANDLE* sftp_open_remote(const char *remote_path, unsigned long flags, long mode) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return NULL;

    LOG_DEBUG("SFTP open: %s (flags: %lu)", remote_path, flags);
    // Chỉ hỗ trợ đọc
    unsigned long sftp_flags = LIBSSH2_FXF_READ;
    // Chuyển đổi flags FUSE sang flags SFTP (nếu cần, ví dụ append, truncate...)
    // Hiện tại chỉ cần Read
    if (flags & O_WRONLY || flags & O_RDWR) {
        LOG_ERR("Write access not supported for %s", remote_path);
        return NULL; // Không hỗ trợ ghi
    }

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open_ex(conn->sftp_session, remote_path, strlen(remote_path),
                                                      sftp_flags, mode, LIBSSH2_SFTP_OPENFILE);
     if (!handle) {
         LOG_ERR("libssh2_sftp_open_ex failed for %s: %d (SFTP Error: %lu)",
                 remote_path, libssh2_session_last_errno(conn->ssh_session),
                 libssh2_sftp_last_error(conn->sftp_session));
     }
    return handle;
}

// Đọc dữ liệu từ file đã mở
ssize_t sftp_read_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t count) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session || !handle) return -EIO; // Lỗi I/O

    LOG_DEBUG("SFTP read (handle: %p, count: %zu)", handle, count);
    ssize_t bytes_read = libssh2_sftp_read(handle, buffer, count);

    if (bytes_read < 0 && bytes_read != LIBSSH2_ERROR_EAGAIN) { // EAGAIN là non-blocking, không nên xảy ra ở đây
        LOG_ERR("libssh2_sftp_read failed: %zd", bytes_read);
        return -EIO; // Lỗi I/O chung
    } else if (bytes_read == LIBSSH2_ERROR_EAGAIN) {
        LOG_ERR("libssh2_sftp_read returned EAGAIN unexpectedly");
        return -EAGAIN; // Thử lại? Hoặc lỗi khác
    } else {
        LOG_DEBUG("SFTP read successful: %zd bytes", bytes_read);
    }

    return bytes_read; // Trả về số byte đọc được (có thể là 0 nếu EOF)
}


int sftp_close_remote(LIBSSH2_SFTP_HANDLE *handle) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session || !handle) return LIBSSH2_ERROR_BAD_USE;

    LOG_DEBUG("SFTP close (handle: %p)", handle);
    int rc = libssh2_sftp_close_handle(handle);
    if (rc != 0) {
         LOG_ERR("libssh2_sftp_close_handle (file) failed: %d (SFTP Error: %lu)",
                 rc, libssh2_sftp_last_error(conn->sftp_session));
    }
    return rc;
}

// Hàm chuyển đổi mã lỗi SFTP sang mã lỗi POSIX errno
int sftp_error_to_errno(unsigned long sftp_err) {
    switch (sftp_err) {
        case LIBSSH2_FX_OK:
            return 0; // Thành công
        case LIBSSH2_FX_EOF:
            return 0; // End-of-file không phải là lỗi, nhưng read sẽ trả về 0 byte
        case LIBSSH2_FX_NO_SUCH_FILE:
        case LIBSSH2_FX_NO_SUCH_PATH:
            return ENOENT; // No such file or directory
        case LIBSSH2_FX_PERMISSION_DENIED:
            return EACCES; // Permission denied
        case LIBSSH2_FX_FAILURE:
            return EIO; // Generic failure -> I/O error
        case LIBSSH2_FX_BAD_MESSAGE:
            return EIO; // Bad message -> I/O error
        case LIBSSH2_FX_NO_CONNECTION:
        case LIBSSH2_FX_CONNECTION_LOST:
            return ENOTCONN; // Hoặc EIO? Connection lost
        case LIBSSH2_FX_OP_UNSUPPORTED:
            return ENOSYS; // Operation not supported
        case LIBSSH2_FX_INVALID_HANDLE:
            return EBADF; // Invalid file handle (descriptor)
        case LIBSSH2_FX_INVALID_PARAMETER:
            return EINVAL; // Invalid parameter
        case LIBSSH2_FX_FILE_ALREADY_EXISTS:
            return EEXIST; // File exists (cho create/mkdir)
        case LIBSSH2_FX_WRITE_PROTECT:
            return EROFS; // Read-only filesystem (cho write ops)
        case LIBSSH2_FX_NO_MEDIA:
            return ENOMEDIUM; // No media in drive
        case LIBSSH2_FX_DIR_NOT_EMPTY:
            return ENOTEMPTY; // Directory not empty (cho rmdir)
        // Thêm các mã lỗi khác nếu cần
        default:
            LOG_ERR("Unknown SFTP error code: %lu", sftp_err);
            return EIO; // Lỗi I/O chung cho các trường hợp không xác định
    }
}