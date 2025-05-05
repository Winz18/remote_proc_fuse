#include "ssh_sftp_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "common.h" // Đảm bảo include common.h
remote_conn_info_t *ssh_cli_conn = NULL;

// Helper function to log libssh2 errors
static void log_libssh2_error(LIBSSH2_SESSION *session, const char *prefix) {
    char *errmsg;
    int errcode = libssh2_session_last_error(session, &errmsg, NULL, 0);
    LOG_ERR("%s: libssh2 error %d: %s", prefix, errcode, errmsg);
}

int sftp_connect_and_auth(remote_conn_info_t *conn) {
    conn->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sock == -1) {
        LOG_ERR("Failed to create socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(conn->remote_port);
    if (inet_pton(AF_INET, conn->remote_host, &sin.sin_addr) != 1) {
        LOG_ERR("Invalid remote host address: %s", conn->remote_host);
        close(conn->sock); conn->sock = -1; // Clean up socket
        return -1;
    }

    if (connect(conn->sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
        LOG_ERR("Failed to connect to remote host %s:%d: %s", conn->remote_host, conn->remote_port, strerror(errno));
        close(conn->sock); conn->sock = -1; // Clean up socket
        return -1;
    }

    conn->ssh_session = libssh2_session_init();
    if (!conn->ssh_session) {
        LOG_ERR("Failed to initialize SSH session");
        close(conn->sock); conn->sock = -1; // Clean up socket
        return -1;
    }

    // Set non-blocking for handshake to potentially add timeout later
    // libssh2_session_set_blocking(conn->ssh_session, 0);

    int rc = libssh2_session_handshake(conn->ssh_session, conn->sock);
    if (rc) {
        log_libssh2_error(conn->ssh_session, "SSH handshake failed");
        libssh2_session_free(conn->ssh_session); conn->ssh_session = NULL;
        close(conn->sock); conn->sock = -1;
        return -1;
    }

    // Restore blocking mode
    // libssh2_session_set_blocking(conn->ssh_session, 1);

    const char *fingerprint = libssh2_hostkey_hash(conn->ssh_session, LIBSSH2_HOSTKEY_HASH_SHA1);
    // Fingerprint check is informational, not strictly an error if missing for now
    if (fingerprint) {
         char fingerprint_hex[60]; // SHA1 is 20 bytes, hex is 40 chars + colons + null
         for(int i = 0; i < 20; i++) {
             sprintf(fingerprint_hex + (i*3), "%02X:", (unsigned char)fingerprint[i]);
         }
         fingerprint_hex[59] = '\0'; // Remove last colon
         LOG_INFO("Host key fingerprint (SHA1): %s", fingerprint_hex);
    } else {
        log_libssh2_error(conn->ssh_session, "Could not get host key fingerprint");
        // Not returning -1, as it might not be critical for all use cases
    }

    // Try authentication methods
    const char *userauthlist = libssh2_userauth_list(conn->ssh_session, conn->remote_user, strlen(conn->remote_user));
    LOG_INFO("Supported authentication methods for user %s: %s", conn->remote_user, userauthlist ? userauthlist : "(unknown)");

    rc = -1; // Default to failure
    if (conn->ssh_key_path && (!userauthlist || strstr(userauthlist, "publickey"))) {
        LOG_INFO("Attempting public key authentication (key: %s, passphrase: %s)",
                 conn->ssh_key_path, conn->remote_pass ? "yes" : "no");
        rc = libssh2_userauth_publickey_fromfile(conn->ssh_session, conn->remote_user,
                                                 NULL, // No specific public key file needed if private key implies it
                                                 conn->ssh_key_path, conn->remote_pass);
        if (rc == 0) {
            LOG_INFO("Public key authentication successful.");
        } else {
            log_libssh2_error(conn->ssh_session, "Public key authentication failed");
        }
    }

    // If key auth failed or wasn't attempted, try password
    if (rc != 0 && conn->remote_pass && (!userauthlist || strstr(userauthlist, "password"))) {
        LOG_INFO("Attempting password authentication.");
        rc = libssh2_userauth_password(conn->ssh_session, conn->remote_user, conn->remote_pass);
        if (rc == 0) {
            LOG_INFO("Password authentication successful.");
        } else {
            log_libssh2_error(conn->ssh_session, "Password authentication failed");
        }
    }

    if (rc != 0) {
        LOG_ERR("All attempted authentication methods failed for user %s.", conn->remote_user);
        libssh2_session_disconnect(conn->ssh_session, "Auth failed");
        libssh2_session_free(conn->ssh_session); conn->ssh_session = NULL;
        close(conn->sock); conn->sock = -1;
        return -1;
    }

    conn->sftp_session = libssh2_sftp_init(conn->ssh_session);
    if (!conn->sftp_session) {
        log_libssh2_error(conn->ssh_session, "Unable to initialize SFTP session");
        libssh2_session_disconnect(conn->ssh_session, "SFTP init failed");
        libssh2_session_free(conn->ssh_session); conn->ssh_session = NULL;
        close(conn->sock); conn->sock = -1;
        return -1;
    }

    LOG_INFO("SFTP session initialized successfully.");
    return 0;
}

void sftp_disconnect(remote_conn_info_t *conn) {
    if (!conn) return;

    if (conn->sftp_session) {
        libssh2_sftp_shutdown(conn->sftp_session);
        conn->sftp_session = NULL;
    }

    if (conn->ssh_session) {
        libssh2_session_disconnect(conn->ssh_session, "Normal Shutdown");
        libssh2_session_free(conn->ssh_session);
        conn->ssh_session = NULL;
    }

    if (conn->sock != -1) {
        close(conn->sock);
        conn->sock = -1;
    }
}

int sftp_stat_remote(const char *remote_path, LIBSSH2_SFTP_ATTRIBUTES *attrs) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -1;

    int rc = libssh2_sftp_stat(conn->sftp_session, remote_path, attrs);
    if (rc < 0) {
        LOG_DEBUG("sftp_stat_remote failed for %s with rc=%d", remote_path, rc);
    }
    return rc;
}

LIBSSH2_SFTP_HANDLE* sftp_opendir_remote(const char *remote_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return NULL;

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(conn->sftp_session, remote_path);
    if (!handle) {
        LOG_DEBUG("sftp_opendir_remote failed for %s", remote_path);
    }
    return handle;
}

int sftp_readdir_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t buffer_len, LIBSSH2_SFTP_ATTRIBUTES *attrs) {
    int rc = libssh2_sftp_readdir(handle, buffer, buffer_len, attrs);
    return rc;
}

int sftp_closedir_remote(LIBSSH2_SFTP_HANDLE *handle) {
    if (!handle) return -1;
    return libssh2_sftp_closedir(handle);
}

LIBSSH2_SFTP_HANDLE* sftp_open_remote(const char *remote_path, unsigned long flags, long mode) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return NULL;

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(conn->sftp_session, remote_path, flags, mode);
    if (!handle) {
        LOG_DEBUG("sftp_open_remote failed for %s with flags=0x%lx mode=0%lo", remote_path, flags, mode);
    }
    return handle;
}

LIBSSH2_SFTP_HANDLE* sftp_create_remote(const char *remote_path, long mode) {
    unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;
    return sftp_open_remote(remote_path, flags, mode);
}

ssize_t sftp_read_remote(LIBSSH2_SFTP_HANDLE *handle, char *buffer, size_t count) {
    if (!handle) return -EBADF; // Use errno code
    ssize_t rc = libssh2_sftp_read(handle, buffer, count);
    if (rc < 0 && rc != LIBSSH2_ERROR_EAGAIN) { // Check for actual errors, ignore EAGAIN for now
        remote_conn_info_t *conn = get_conn_info();
        if (conn && conn->sftp_session) {
            unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
            LOG_ERR("sftp_read_remote failed: libssh2 rc=%zd, sftp_err=%lu -> errno=%d", rc, sftp_err, sftp_error_to_errno(sftp_err));
            return -sftp_error_to_errno(sftp_err); // Return negative errno
        } else {
            LOG_ERR("sftp_read_remote failed: libssh2 rc=%zd (no session info)", rc);
            return -EIO; // Generic I/O error
        }
    }
    return rc;
}

ssize_t sftp_write_remote(LIBSSH2_SFTP_HANDLE *handle, const char *buffer, size_t count) {
    if (!handle) return -EBADF; // Use errno code
    ssize_t rc = libssh2_sftp_write(handle, buffer, count);
     if (rc < 0 && rc != LIBSSH2_ERROR_EAGAIN) { // Check for actual errors
        remote_conn_info_t *conn = get_conn_info();
        if (conn && conn->sftp_session) {
            unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
            LOG_ERR("sftp_write_remote failed: libssh2 rc=%zd, sftp_err=%lu -> errno=%d", rc, sftp_err, sftp_error_to_errno(sftp_err));
            return -sftp_error_to_errno(sftp_err); // Return negative errno
        } else {
            LOG_ERR("sftp_write_remote failed: libssh2 rc=%zd (no session info)", rc);
            return -EIO; // Generic I/O error
        }
    }
    return rc;
}

int sftp_close_remote(LIBSSH2_SFTP_HANDLE *handle) {
    if (!handle) return -1;
    return libssh2_sftp_close(handle);
}

int sftp_unlink_remote(const char *remote_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -1;

    return libssh2_sftp_unlink(conn->sftp_session, remote_path);
}

int sftp_mkdir_remote(const char *remote_path, long mode) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -1;

    return libssh2_sftp_mkdir(conn->sftp_session, remote_path, mode);
}

int sftp_rmdir_remote(const char *remote_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -1;

    return libssh2_sftp_rmdir(conn->sftp_session, remote_path);
}

int sftp_error_to_errno(unsigned long sftp_err) {
    switch (sftp_err) {
        case LIBSSH2_FX_OK:               return 0;
        case LIBSSH2_FX_EOF:              return 0; // Thường không phải lỗi thực sự
        case LIBSSH2_FX_NO_SUCH_FILE:     return ENOENT;
        case LIBSSH2_FX_PERMISSION_DENIED: return EACCES; // <--- Sửa thành EACCES (13)
        case LIBSSH2_FX_FAILURE:          // Mã lỗi chung, EIO có thể phù hợp
        case LIBSSH2_FX_BAD_MESSAGE:      // Lỗi giao thức, EIO
        case LIBSSH2_FX_NO_CONNECTION:    // EIO hoặc ENOTCONN? EIO an toàn hơn
        case LIBSSH2_FX_CONNECTION_LOST:  // EIO hoặc ENOTCONN? EIO an toàn hơn
        case LIBSSH2_FX_OP_UNSUPPORTED:   return ENOSYS; // Giữ nguyên
        case LIBSSH2_FX_INVALID_HANDLE:   return EBADF;
        case LIBSSH2_FX_NO_SUCH_PATH:     return ENOENT;
        case LIBSSH2_FX_FILE_ALREADY_EXISTS: return EEXIST;
        case LIBSSH2_FX_WRITE_PROTECT:    return EROFS;
        case LIBSSH2_FX_NO_MEDIA:         return ENODEV; // Hoặc EIO
        case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM: return ENOSPC;
        case LIBSSH2_FX_QUOTA_EXCEEDED:   return EDQUOT;
        case LIBSSH2_FX_UNKNOWN_PRINCIPAL: return EINVAL; // Có thể?
        case LIBSSH2_FX_LOCK_CONFLICT:    return EDEADLK; // Hoặc EAGAIN?
        case LIBSSH2_FX_DIR_NOT_EMPTY:    return ENOTEMPTY;
        case LIBSSH2_FX_NOT_A_DIRECTORY:  return ENOTDIR;
        case LIBSSH2_FX_INVALID_FILENAME: return EINVAL;
        case LIBSSH2_FX_LINK_LOOP:        return ELOOP;
        default:                          return EIO; // Lỗi I/O chung cho các trường hợp khác
    }
}

int sftp_copy_local_to_remote(const char *local_path, const char *remote_path) {
    FILE *local_file = fopen(local_path, "rb");
    if (!local_file) {
        LOG_ERR("Failed to open local file '%s': %s", local_path, strerror(errno));
        return -errno;
    }

    unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;
    LIBSSH2_SFTP_HANDLE *remote_handle = sftp_open_remote(remote_path, flags, 0644);
    if (!remote_handle) {
        // sftp_open_remote already logs the specific SFTP error
        LOG_ERR("Failed to open/create remote file '%s'", remote_path);
        fclose(local_file);
        // Get last sftp error and convert to errno
        remote_conn_info_t *conn = get_conn_info();
        if (conn && conn->sftp_session) {
             unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
             return -sftp_error_to_errno(sftp_err);
        }
        return -EIO; // Generic error if no session
    }

    char buffer[32768];
    size_t bytes_read;
    ssize_t bytes_written;
    int result = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), local_file)) > 0) {
        char *buf_ptr = buffer;
        size_t remaining = bytes_read;
        while (remaining > 0) {
            bytes_written = sftp_write_remote(remote_handle, buf_ptr, remaining);
            if (bytes_written < 0) {
                 // sftp_write_remote logs the error
                 LOG_ERR("Failed to write to remote file '%s'", remote_path);
                 result = (int)bytes_written; // Already negative errno
                 goto cleanup_copy_local_to_remote;
            }
            remaining -= bytes_written;
            buf_ptr += bytes_written;
        }
    }

    if (ferror(local_file)) {
        LOG_ERR("Error reading from local file '%s': %s", local_path, strerror(errno));
        result = -errno;
    }

cleanup_copy_local_to_remote:
    fclose(local_file);
    sftp_close_remote(remote_handle); // sftp_close_remote logs errors

    return result;
}

int sftp_copy_remote_to_local(const char *remote_path, const char *local_path) {
    LIBSSH2_SFTP_HANDLE *remote_handle = sftp_open_remote(remote_path, LIBSSH2_FXF_READ, 0);
    if (!remote_handle) {
        // sftp_open_remote logs the specific SFTP error
        LOG_ERR("Failed to open remote file '%s' for reading", remote_path);
        remote_conn_info_t *conn = get_conn_info();
        if (conn && conn->sftp_session) {
             unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
             return -sftp_error_to_errno(sftp_err);
        }
        return -EIO;
    }

    FILE *local_file = fopen(local_path, "wb");
    if (!local_file) {
        LOG_ERR("Failed to open local file '%s' for writing: %s", local_path, strerror(errno));
        sftp_close_remote(remote_handle);
        return -errno;
    }

    char buffer[32768];
    ssize_t bytes_read;
    size_t bytes_written;
    int result = 0;

    while (1) {
        bytes_read = sftp_read_remote(remote_handle, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            bytes_written = fwrite(buffer, 1, bytes_read, local_file);
            if (bytes_written < (size_t)bytes_read) {
                LOG_ERR("Failed to write to local file '%s': %s", local_path, strerror(errno));
                result = -errno;
                break;
            }
        } else if (bytes_read == 0) { // EOF
            break;
        } else { // Error
            // sftp_read_remote logs the error
            LOG_ERR("Error reading from remote file '%s'", remote_path);
            result = (int)bytes_read; // Already negative errno
            break;
        }
    }

    fclose(local_file);
    sftp_close_remote(remote_handle); // sftp_close_remote logs errors

    return result;
}

int sftp_move_local_to_remote(const char *local_path, const char *remote_path) {
    int result = sftp_copy_local_to_remote(local_path, remote_path);
    if (result == 0) {
        if (unlink(local_path) != 0) {
            result = -errno;
            LOG_ERR("Failed to remove local file '%s' after copy: %s", local_path, strerror(errno));
            // Attempt to clean up remote file, but prioritize reporting the unlink error
            int unlink_rc = sftp_unlink_remote(remote_path);
            if (unlink_rc != 0) {
                 LOG_WARN("Also failed to remove remote file '%s' during cleanup", remote_path);
            }
        }
    }
    return result;
}

int sftp_move_remote_to_local(const char *remote_path, const char *local_path) {
    int result = sftp_copy_remote_to_local(remote_path, local_path);
    if (result == 0) {
        int unlink_rc = sftp_unlink_remote(remote_path);
        if (unlink_rc != 0) {
            // sftp_unlink_remote logs the error
            remote_conn_info_t *conn = get_conn_info();
            if (conn && conn->sftp_session) {
                 unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
                 result = -sftp_error_to_errno(sftp_err);
            } else {
                 result = -EIO;
            }
            LOG_ERR("Failed to remove remote file '%s' after copy (errno %d)", remote_path, -result);
            // Attempt to clean up local file
            if (unlink(local_path) != 0) {
                 LOG_WARN("Also failed to remove local file '%s' during cleanup: %s", local_path, strerror(errno));
            }
        }
    }
    return result;
}

// Add sftp_rename_remote function
int sftp_rename_remote(const char *old_path, const char *new_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -ENOTCONN;

    long rename_flags = LIBSSH2_SFTP_RENAME_OVERWRITE |
                        LIBSSH2_SFTP_RENAME_ATOMIC |
                        LIBSSH2_SFTP_RENAME_NATIVE;

    int rc = libssh2_sftp_rename_ex(conn->sftp_session,
                                   old_path, strlen(old_path),
                                   new_path, strlen(new_path),
                                   rename_flags);

    if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("sftp_rename_remote failed for '%s' -> '%s', sftp_err=%lu -> errno=%d",
                old_path, new_path, sftp_err, err);
        return -err ? -err : -EIO;
    }
    return 0;
}

// Add sftp_setstat_remote function
int sftp_setstat_remote(const char *remote_path, LIBSSH2_SFTP_ATTRIBUTES *attrs) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -ENOTCONN;

    int rc = libssh2_sftp_setstat(conn->sftp_session, remote_path, attrs);

    if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("sftp_setstat_remote failed for '%s', rc=%d, sftp_err=%lu -> errno=%d",
                remote_path, rc, sftp_err, err);
        return -err ? -err : -EIO;
    }
    return 0;
}
