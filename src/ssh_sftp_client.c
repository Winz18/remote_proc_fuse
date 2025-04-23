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

    LOG_DEBUG("SFTP open: %s (flags: 0x%lx)", remote_path, flags);
    
    // Xác định SFTP flags dựa trên flags POSIX
    unsigned long sftp_flags = 0;
    
    // Xác định quyền truy cập (READ/WRITE/READWRITE)
    int access_mode = flags & O_ACCMODE;
    if (access_mode == O_RDONLY) {
        sftp_flags = LIBSSH2_FXF_READ;
    } else if (access_mode == O_WRONLY) {
        sftp_flags = LIBSSH2_FXF_WRITE;
    } else if (access_mode == O_RDWR) {
        sftp_flags = LIBSSH2_FXF_READ | LIBSSH2_FXF_WRITE;
    }
    
    // Xử lý các flags khác
    if (flags & O_CREAT) {
        sftp_flags |= LIBSSH2_FXF_CREAT;
        
        if (flags & O_EXCL) {
            // Nếu file đã tồn tại, báo lỗi
            sftp_flags |= LIBSSH2_FXF_EXCL;
        }
    }
    
    if (flags & O_TRUNC) {
        // Cắt ngắn nội dung file về 0 khi mở
        sftp_flags |= LIBSSH2_FXF_TRUNC;
    }
    
    if (flags & O_APPEND) {
        // Ghi vào cuối file
        sftp_flags |= LIBSSH2_FXF_APPEND;
    }
    
    // Mở file
    LOG_DEBUG("SFTP open with flags: 0x%lx (translated to SFTP flags: 0x%lx)", 
             flags, sftp_flags);
             
    // Nếu mode không được chỉ định, sử dụng quyền mặc định
    if (mode == 0) {
        mode = 0644; // rw-r--r--
    }
    
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open_ex(
        conn->sftp_session, 
        remote_path, 
        strlen(remote_path),
        sftp_flags, 
        mode, 
        LIBSSH2_SFTP_OPENFILE
    );
    
    if (!handle) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        
        LOG_ERR("libssh2_sftp_open_ex failed for %s: %d (SFTP Error: %lu -> %s)",
                remote_path, libssh2_session_last_errno(conn->ssh_session),
                sftp_err, strerror(err));
                
        // Nếu file không tồn tại và không có flag O_CREAT
        if (err == ENOENT && !(flags & O_CREAT)) {
            LOG_ERR("File does not exist and O_CREAT flag not specified");
        }
        // Nếu không có quyền
        else if (err == EACCES) {
            LOG_ERR("Permission denied when opening file");
        }
    } else {
        LOG_DEBUG("SFTP open successful for %s, handle: %p", remote_path, handle);
    }
    
    return handle;
}

// Đọc dữ liệu từ file đã mở
ssize_t sftp_read_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t count) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session || !handle) return -EIO; // Lỗi I/O

    LOG_DEBUG("SFTP read (handle: %p, count: %zu)", handle, count);
    
    // Nếu không có gì để đọc, trả về 0
    if (count == 0) {
        LOG_DEBUG("SFTP read: Nothing to read (count=0)");
        return 0;
    }
    
    // Xử lý đọc dữ liệu lớn thành từng phần nhỏ để tránh timeout
    size_t bytes_remaining = count;
    size_t bytes_read_total = 0;
    char *buffer_pos = buffer;
    
    // Kích thước mỗi lần đọc (64KB giống như trong hàm write)
    const size_t CHUNK_SIZE = 65536;
    
    while (bytes_remaining > 0) {
        // Tính toán kích thước cho lần đọc tiếp theo
        size_t bytes_to_read = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;
        
        // Đọc một phần dữ liệu
        ssize_t bytes_read = libssh2_sftp_read(handle, buffer_pos, bytes_to_read);
        
        if (bytes_read < 0) {
            // Lỗi đọc
            if (bytes_read == LIBSSH2_ERROR_EAGAIN) {
                // Non-blocking mode, retry
                LOG_DEBUG("SFTP read: EAGAIN received, retrying after brief pause");
                usleep(10000); // Chờ 10ms
                continue;
            } else {
                LOG_ERR("SFTP read failed: %zd", bytes_read);
                if (bytes_read_total > 0) {
                    // Nếu đã đọc được một phần dữ liệu, trả về số byte đã đọc
                    return bytes_read_total;
                }
                return -EIO;
            }
        } else if (bytes_read == 0) {
            // EOF - Đã đọc hết dữ liệu từ file
            LOG_DEBUG("SFTP read: EOF reached after reading %zu bytes", bytes_read_total);
            break;
        }
        
        // Cập nhật các con trỏ và bộ đếm
        buffer_pos += bytes_read;
        bytes_remaining -= bytes_read;
        bytes_read_total += bytes_read;
        
        LOG_DEBUG("SFTP read progress: %zu/%zu bytes read", bytes_read_total, count);
    }
    
    LOG_DEBUG("SFTP read successful: %zu bytes", bytes_read_total);
    return bytes_read_total;
}

// Ghi dữ liệu vào file đã mở
ssize_t sftp_write_remote(LIBSSH2_SFTP_HANDLE *handle, const char *buffer, size_t count) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session || !handle) return -EIO; // Lỗi I/O

    LOG_DEBUG("SFTP write (handle: %p, count: %zu)", handle, count);

    // Nếu không có gì để ghi, trả về thành công với 0 byte
    if (count == 0) {
        LOG_DEBUG("SFTP write: Nothing to write (count=0)");
        return 0;
    }

    // Xử lý ghi dữ liệu lớn thành từng phần nhỏ để tránh bị timeout
    size_t bytes_remaining = count;
    size_t bytes_written_total = 0;
    const char *buffer_pos = buffer;
    
    // Kích thước mỗi lần ghi (ví dụ 64KB)
    const size_t CHUNK_SIZE = 65536;

    while (bytes_remaining > 0) {
        // Tính toán kích thước cho lần ghi tiếp theo
        size_t bytes_to_write = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;
        
        // Ghi một phần dữ liệu
        ssize_t bytes_written = libssh2_sftp_write(handle, buffer_pos, bytes_to_write);
        
        if (bytes_written < 0) {
            if (bytes_written == LIBSSH2_ERROR_EAGAIN) {
                // Trường hợp non-blocking, nên chờ một lúc rồi thử lại
                LOG_DEBUG("SFTP write: EAGAIN received, retrying after brief pause");
                usleep(10000); // Chờ 10ms
                continue;
            } else {
                // Lỗi khác, không thể tiếp tục
                LOG_ERR("SFTP write failed: %zd", bytes_written);
                if (bytes_written_total > 0) {
                    // Nếu đã ghi được một phần dữ liệu, trả về số byte đã ghi
                    return bytes_written_total;
                }
                return -EIO;
            }
        } else if (bytes_written == 0) {
            // Không thể ghi thêm, nhưng không phải lỗi
            LOG_DEBUG("SFTP write: Cannot write more data, stopping");
            break;
        }
        
        // Cập nhật các con trỏ và bộ đếm
        buffer_pos += bytes_written;
        bytes_remaining -= bytes_written;
        bytes_written_total += bytes_written;
        
        LOG_DEBUG("SFTP write progress: %zu/%zu bytes written", bytes_written_total, count);
    }

    LOG_DEBUG("SFTP write successful: %zu bytes", bytes_written_total);
    return bytes_written_total;
}

// Tạo file mới trên máy remote
LIBSSH2_SFTP_HANDLE* sftp_create_remote(const char *remote_path, long mode) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return NULL;

    LOG_DEBUG("SFTP create: %s (mode: %lo)", remote_path, mode);
    
    // Chuẩn bị flags cho việc tạo file mới
    unsigned long flags = LIBSSH2_FXF_CREAT | LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC;
    
    // Đảm bảo mode hợp lệ
    if (mode == 0) {
        mode = 0644; // mặc định rw-r--r--
    }
    
    // Thử open file với các flags và mode đã chỉ định
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open_ex(
        conn->sftp_session, 
        remote_path, 
        strlen(remote_path), 
        flags, 
        mode, 
        LIBSSH2_SFTP_OPENFILE
    );
    
    if (!handle) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        
        LOG_ERR("libssh2_sftp_open_ex (create) failed for %s: %d (SFTP Error: %lu -> %s)",
                remote_path, libssh2_session_last_errno(conn->ssh_session),
                sftp_err, strerror(err));
        
        // Chi tiết hơn về lỗi
        if (err == EACCES) {
            LOG_ERR("Permission denied when creating file: %s", remote_path);
        } else if (err == EEXIST) {
            LOG_ERR("File already exists and cannot be overwritten: %s", remote_path);
        }
        return NULL;
    }
    
    LOG_DEBUG("SFTP create successful: %s, handle: %p", remote_path, handle);
    return handle;
}

// Xóa file từ xa
int sftp_unlink_remote(const char *remote_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return LIBSSH2_ERROR_SOCKET_DISCONNECT;

    LOG_DEBUG("SFTP unlink: %s", remote_path);
    int rc = libssh2_sftp_unlink_ex(conn->sftp_session, remote_path, strlen(remote_path));
    if (rc != 0) {
        LOG_ERR("libssh2_sftp_unlink_ex failed for %s: %d (SFTP Error: %lu)",
                remote_path, rc, libssh2_sftp_last_error(conn->sftp_session));
    }
    return rc;
}

// Tạo thư mục từ xa
int sftp_mkdir_remote(const char *remote_path, long mode) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return LIBSSH2_ERROR_SOCKET_DISCONNECT;

    LOG_DEBUG("SFTP mkdir: %s (mode: %lo)", remote_path, mode);
    int rc = libssh2_sftp_mkdir_ex(conn->sftp_session, remote_path, strlen(remote_path), mode);
    if (rc != 0) {
        LOG_ERR("libssh2_sftp_mkdir_ex failed for %s: %d (SFTP Error: %lu)",
                remote_path, rc, libssh2_sftp_last_error(conn->sftp_session));
    }
    return rc;
}

// Xóa thư mục từ xa
int sftp_rmdir_remote(const char *remote_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return LIBSSH2_ERROR_SOCKET_DISCONNECT;

    LOG_DEBUG("SFTP rmdir: %s", remote_path);
    int rc = libssh2_sftp_rmdir_ex(conn->sftp_session, remote_path, strlen(remote_path));
    if (rc != 0) {
        LOG_ERR("libssh2_sftp_rmdir_ex failed for %s: %d (SFTP Error: %lu)",
                remote_path, rc, libssh2_sftp_last_error(conn->sftp_session));
    }
    return rc;
}

// Cắt ngắn file từ xa
int sftp_truncate_remote(const char *remote_path, off_t size) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return LIBSSH2_ERROR_SOCKET_DISCONNECT;

    LOG_DEBUG("SFTP truncate: %s (size: %ld)", remote_path, size);
    
    // Lấy thông tin file hiện tại
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int stat_rc = libssh2_sftp_stat_ex(conn->sftp_session, remote_path, strlen(remote_path),
                                      LIBSSH2_SFTP_STAT, &attrs);
    if (stat_rc != 0) {
        LOG_ERR("truncate: Failed to stat file: %s", remote_path);
        return LIBSSH2_ERROR_SFTP_PROTOCOL;
    }
    
    // Nếu kích thước hiện tại đã bằng kích thước yêu cầu, không cần làm gì
    if ((attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) && attrs.filesize == size) {
        LOG_DEBUG("truncate: File size already matches requested size: %ld", size);
        return 0;
    }
    
    // Trường hợp 1: Truncate với size = 0 (xóa nội dung)
    if (size == 0) {
        // Mở file với flag WRITE|TRUNC để xóa nội dung
        unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC;
        LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open_ex(conn->sftp_session, 
                                                          remote_path, 
                                                          strlen(remote_path), 
                                                          flags, 
                                                          attrs.permissions, 
                                                          LIBSSH2_SFTP_OPENFILE);
        if (!handle) {
            unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
            LOG_ERR("truncate: Failed to open file for truncate to 0: %s (SFTP error: %lu)", 
                   remote_path, sftp_err);
            return LIBSSH2_ERROR_SFTP_PROTOCOL;
        }
        
        // Đóng handle để áp dụng cờ TRUNC
        int rc = libssh2_sftp_close_handle(handle);
        if (rc != 0) {
            LOG_ERR("truncate: Failed to close handle after truncate to 0: %d", rc);
        }
        return rc;
    }
    
    // Trường hợp 2: Truncate với size > 0 (cắt bớt nội dung)
    // Đọc nội dung cần giữ lại
    LIBSSH2_SFTP_HANDLE *read_handle = libssh2_sftp_open_ex(conn->sftp_session, 
                                                           remote_path, 
                                                           strlen(remote_path), 
                                                           LIBSSH2_FXF_READ, 
                                                           0, 
                                                           LIBSSH2_SFTP_OPENFILE);
    if (!read_handle) {
        LOG_ERR("truncate: Failed to open file for reading: %s", remote_path);
        return LIBSSH2_ERROR_SFTP_PROTOCOL;
    }
    
    // Cấp phát bộ nhớ cho buffer đọc file
    char *buffer = NULL;
    
    // Chỉ cần đọc nếu kích thước yêu cầu nhỏ hơn kích thước hiện tại
    if (size < attrs.filesize) {
        buffer = malloc(size);
        if (!buffer) {
            LOG_ERR("truncate: Failed to allocate buffer: %s", strerror(errno));
            libssh2_sftp_close_handle(read_handle);
            return -ENOMEM;
        }
        
        // Đọc dữ liệu cần giữ lại
        ssize_t bytes_read = 0;
        size_t total_read = 0;
        
        while (total_read < size) {
            bytes_read = libssh2_sftp_read(read_handle, buffer + total_read, size - total_read);
            if (bytes_read <= 0) {
                // Error or EOF
                if (bytes_read < 0) {
                    LOG_ERR("truncate: Error reading file: %zd", bytes_read);
                    free(buffer);
                    libssh2_sftp_close_handle(read_handle);
                    return LIBSSH2_ERROR_SFTP_PROTOCOL;
                }
                break; // EOF
            }
            total_read += bytes_read;
        }
        
        // Lưu lại số byte thực tế đã đọc
        size = total_read;
    } else {
        // Nếu kích thước yêu cầu >= kích thước hiện tại, không cần đọc
        // Nhưng vẫn cần mở lại file với flag TRUNC để duy trì tính nhất quán
        buffer = malloc(attrs.filesize);
        if (!buffer) {
            LOG_ERR("truncate: Failed to allocate buffer: %s", strerror(errno));
            libssh2_sftp_close_handle(read_handle);
            return -ENOMEM;
        }
        
        ssize_t bytes_read = 0;
        size_t total_read = 0;
        
        while (total_read < attrs.filesize) {
            bytes_read = libssh2_sftp_read(read_handle, buffer + total_read, attrs.filesize - total_read);
            if (bytes_read <= 0) break; // Error or EOF
            total_read += bytes_read;
        }
        
        size = total_read;
    }
    
    // Đóng handle đọc
    libssh2_sftp_close_handle(read_handle);
    
    // Nếu không có dữ liệu đọc được (có thể do file rỗng), chỉ cần trả về thành công
    if (size == 0) {
        free(buffer);
        return 0;
    }
    
    // Mở lại file để ghi với flag TRUNC
    LIBSSH2_SFTP_HANDLE *write_handle = libssh2_sftp_open_ex(
        conn->sftp_session, 
        remote_path, 
        strlen(remote_path), 
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC | LIBSSH2_FXF_CREAT, 
        attrs.permissions, 
        LIBSSH2_SFTP_OPENFILE
    );
    
    if (!write_handle) {
        LOG_ERR("truncate: Failed to open file for writing: %s", remote_path);
        free(buffer);
        return LIBSSH2_ERROR_SFTP_PROTOCOL;
    }
    
    // Ghi dữ liệu đã đọc vào file
    ssize_t bytes_written = 0;
    size_t total_written = 0;
    
    while (total_written < size) {
        bytes_written = libssh2_sftp_write(write_handle, buffer + total_written, size - total_written);
        if (bytes_written < 0) {
            LOG_ERR("truncate: Error writing data: %zd", bytes_written);
            free(buffer);
            libssh2_sftp_close_handle(write_handle);
            return LIBSSH2_ERROR_SFTP_PROTOCOL;
        } else if (bytes_written == 0) {
            // Không thể ghi thêm
            break;
        }
        total_written += bytes_written;
    }
    
    // Giải phóng bộ nhớ
    free(buffer);
    
    // Đóng handle ghi
    int rc = libssh2_sftp_close_handle(write_handle);
    if (rc != 0) {
        LOG_ERR("truncate: Failed to close write handle: %d", rc);
        return rc;
    }
    
    LOG_DEBUG("truncate: Successfully truncated %s to size %ld", remote_path, size);
    return 0;
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
        // Case for invalid parameter - use EINVAL but don't rely on the constant
        // LIBSSH2_FX_INVALID_PARAMETER may not be defined in older libssh2 versions
        // Instead, we'll handle this kind of error with our default case
        case LIBSSH2_FX_FILE_ALREADY_EXISTS:
            return EEXIST; // File exists (cho create/mkdir)
        case LIBSSH2_FX_WRITE_PROTECT:
            return EROFS; // Read-only filesystem (cho write ops)
        // LIBSSH2_FX_NO_MEDIA might not be defined in all versions
        // case LIBSSH2_FX_NO_MEDIA:
        //    return ENOMEDIUM; // No media in drive
        // Directory not empty (case for rmdir)
        #ifdef LIBSSH2_FX_DIR_NOT_EMPTY
        case LIBSSH2_FX_DIR_NOT_EMPTY:
            return ENOTEMPTY;
        #endif
        // Thêm các mã lỗi khác nếu cần
        default:
            // Generic handling for all undefined error codes
            LOG_ERR("Unknown or unmapped SFTP error code: %lu", sftp_err);
            // For some common error values that might not be defined as constants
            if (sftp_err == 9) // Common value for INVALID_PARAMETER in SFTP
                return EINVAL;
            if (sftp_err == 10) // Common value for DIR_NOT_EMPTY in SFTP 
                return ENOTEMPTY;
            if (sftp_err == 11) // Common value for NO_MEDIA in SFTP
                return EIO; // Use EIO as a fallback if ENOMEDIUM is not available
                
            return EIO; // Lỗi I/O chung cho các trường hợp không xác định
    }
}

// Sao chép file từ local vào remote
int sftp_copy_local_to_remote(const char *local_path, const char *remote_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -ENOTCONN;

    LOG_DEBUG("Copy local to remote: %s -> %s", local_path, remote_path);
    
    // Mở file cục bộ để đọc
    FILE *local_file = fopen(local_path, "rb");
    if (!local_file) {
        int err = errno;
        LOG_ERR("Failed to open local file: %s - %s", local_path, strerror(err));
        return -err;
    }
    
    // Lấy thông tin file cục bộ
    struct stat local_stat;
    if (stat(local_path, &local_stat) != 0) {
        int err = errno;
        LOG_ERR("Failed to stat local file: %s - %s", local_path, strerror(err));
        fclose(local_file);
        return -err;
    }
    
    // Mở/tạo file trên remote để ghi
    unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;
    LIBSSH2_SFTP_HANDLE *remote_handle = libssh2_sftp_open_ex(
        conn->sftp_session,
        remote_path,
        strlen(remote_path),
        flags,
        local_stat.st_mode & 0777, // Giữ nguyên permission
        LIBSSH2_SFTP_OPENFILE
    );
    
    if (!remote_handle) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("Failed to open remote file: %s - %s", remote_path, strerror(err));
        fclose(local_file);
        return -err;
    }
    
    // Sao chép dữ liệu theo từng phần (chunked)
    char buffer[65536]; // 64KB chunks
    size_t bytes_read;
    ssize_t bytes_written;
    size_t total_written = 0;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), local_file)) > 0) {
        bytes_written = libssh2_sftp_write(remote_handle, buffer, bytes_read);
        
        if (bytes_written < 0) {
            LOG_ERR("Failed to write to remote file: %s - Error code: %zd", 
                   remote_path, bytes_written);
            fclose(local_file);
            libssh2_sftp_close_handle(remote_handle);
            return -EIO;
        }
        
        total_written += bytes_written;
        
        // Kiểm tra nếu không ghi đủ dữ liệu
        if ((size_t)bytes_written < bytes_read) {
            LOG_ERR("Incomplete write to remote file: %s (expected %zu, got %zd)", 
                   remote_path, bytes_read, bytes_written);
            fclose(local_file);
            libssh2_sftp_close_handle(remote_handle);
            return -EIO;
        }
    }
    
    // Kiểm tra lỗi đọc file
    if (ferror(local_file)) {
        LOG_ERR("Error reading local file: %s", local_path);
        fclose(local_file);
        libssh2_sftp_close_handle(remote_handle);
        return -EIO;
    }
    
    // Đóng file
    fclose(local_file);
    libssh2_sftp_close_handle(remote_handle);
    
    LOG_DEBUG("Copy local to remote completed: %s -> %s (%zu bytes)", 
             local_path, remote_path, total_written);
    return 0;
}

// Sao chép file từ remote về local
int sftp_copy_remote_to_local(const char *remote_path, const char *local_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -ENOTCONN;

    LOG_DEBUG("Copy remote to local: %s -> %s", remote_path, local_path);
    
    // Lấy thông tin file remote
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = sftp_stat_remote(remote_path, &attrs);
    if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("Failed to stat remote file: %s - %s", remote_path, strerror(err));
        return -err;
    }
    
    // Mở file remote để đọc
    LIBSSH2_SFTP_HANDLE *remote_handle = libssh2_sftp_open_ex(
        conn->sftp_session,
        remote_path,
        strlen(remote_path),
        LIBSSH2_FXF_READ,
        0,
        LIBSSH2_SFTP_OPENFILE
    );
    
    if (!remote_handle) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("Failed to open remote file: %s - %s", remote_path, strerror(err));
        return -err;
    }
    
    // Mở file cục bộ để ghi
    FILE *local_file = fopen(local_path, "wb");
    if (!local_file) {
        int err = errno;
        LOG_ERR("Failed to open local file: %s - %s", local_path, strerror(err));
        libssh2_sftp_close_handle(remote_handle);
        return -err;
    }
    
    // Sao chép dữ liệu theo từng phần
    char buffer[65536]; // 64KB chunks
    ssize_t bytes_read;
    size_t bytes_written;
    size_t total_read = 0;
    
    while (1) {
        bytes_read = libssh2_sftp_read(remote_handle, buffer, sizeof(buffer));
        
        if (bytes_read < 0) {
            LOG_ERR("Failed to read from remote file: %s - Error code: %zd", 
                   remote_path, bytes_read);
            fclose(local_file);
            libssh2_sftp_close_handle(remote_handle);
            return -EIO;
        }
        
        if (bytes_read == 0) {
            // Hết file
            break;
        }
        
        bytes_written = fwrite(buffer, 1, bytes_read, local_file);
        if (bytes_written < (size_t)bytes_read) {
            LOG_ERR("Incomplete write to local file: %s (expected %zd, got %zu)", 
                   local_path, bytes_read, bytes_written);
            fclose(local_file);
            libssh2_sftp_close_handle(remote_handle);
            return -EIO;
        }
        
        total_read += bytes_read;
    }
    
    // Đóng file
    fclose(local_file);
    libssh2_sftp_close_handle(remote_handle);
    
    // Thiết lập quyền tương tự như file remote
    if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        chmod(local_path, attrs.permissions & 0777);
    }
    
    LOG_DEBUG("Copy remote to local completed: %s -> %s (%zu bytes)", 
             remote_path, local_path, total_read);
    return 0;
}

// Di chuyển file từ local vào remote (copy và xóa local)
int sftp_move_local_to_remote(const char *local_path, const char *remote_path) {
    LOG_DEBUG("Move local to remote: %s -> %s", local_path, remote_path);
    
    // Sao chép file
    int rc = sftp_copy_local_to_remote(local_path, remote_path);
    if (rc != 0) {
        LOG_ERR("Failed to copy file from local to remote for move operation");
        return rc;
    }
    
    // Xóa file cục bộ
    if (unlink(local_path) != 0) {
        int err = errno;
        LOG_ERR("Failed to delete local file after move: %s - %s", 
               local_path, strerror(err));
        return -err;
    }
    
    LOG_DEBUG("Move local to remote completed: %s -> %s", local_path, remote_path);
    return 0;
}

// Di chuyển file từ remote về local (copy và xóa remote)
int sftp_move_remote_to_local(const char *remote_path, const char *local_path) {
    if (!remote_path || !local_path) {
        LOG_ERR("Move remote to local: NULL path provided");
        return -EINVAL;
    }
    
    LOG_DEBUG("Move remote to local: %s -> %s", remote_path, local_path);
    
    // Kết nối SFTP nếu chưa kết nối
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) {
        LOG_ERR("Move remote to local: No active SFTP connection");
        return -ENOTCONN;
    }
    
    // Sao chép file
    int rc = sftp_copy_remote_to_local(remote_path, local_path);
    if (rc != 0) {
        LOG_ERR("Failed to copy file from remote to local for move operation");
        return rc;
    }
    
    // Xóa file remote
    rc = sftp_unlink_remote(remote_path);
    if (rc != 0) {
        LOG_ERR("Failed to delete remote file after move: %s", remote_path);
        return -EIO; // Chuyển rc sang mã lỗi POSIX
    }
    
    LOG_DEBUG("Move remote to local completed: %s -> %s", remote_path, local_path);
    return 0;
}