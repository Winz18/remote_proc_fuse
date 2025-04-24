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

int sftp_connect_and_auth(remote_conn_info_t *conn) {
    conn->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sock == -1) {
        LOG_ERR("Failed to create socket");
        return -1;
    }

    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(conn->remote_port);
    if (inet_pton(AF_INET, conn->remote_host, &sin.sin_addr) != 1) {
        LOG_ERR("Invalid remote host address: %s", conn->remote_host);
        return -1;
    }

    if (connect(conn->sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
        LOG_ERR("Failed to connect to remote host");
        return -1;
    }

    conn->ssh_session = libssh2_session_init();
    if (!conn->ssh_session) {
        LOG_ERR("Failed to initialize SSH session");
        return -1;
    }

    if (libssh2_session_handshake(conn->ssh_session, conn->sock)) {
        LOG_ERR("Failed to establish SSH session");
        return -1;
    }

    const char *fingerprint = libssh2_hostkey_hash(conn->ssh_session, LIBSSH2_HOSTKEY_HASH_SHA1);
    if (!fingerprint) {
        LOG_ERR("Unable to get host key fingerprint");
        return -1;
    }

    if (conn->ssh_key_path) {
        if (libssh2_userauth_publickey_fromfile(conn->ssh_session, conn->remote_user,
                                               NULL, conn->ssh_key_path, conn->remote_pass)) {
            LOG_ERR("Authentication by public key failed");
            return -1;
        }
    } else {
        if (libssh2_userauth_password(conn->ssh_session, conn->remote_user, conn->remote_pass)) {
            LOG_ERR("Authentication by password failed");
            return -1;
        }
    }

    conn->sftp_session = libssh2_sftp_init(conn->ssh_session);
    if (!conn->sftp_session) {
        LOG_ERR("Unable to init SFTP session");
        return -1;
    }

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
    if (!handle) return -1;
    return libssh2_sftp_read(handle, buffer, count);
}

ssize_t sftp_write_remote(LIBSSH2_SFTP_HANDLE *handle, const char *buffer, size_t count) {
    if (!handle) return -1;
    return libssh2_sftp_write(handle, buffer, count);
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
        case LIBSSH2_FX_EOF:              return 0;
        case LIBSSH2_FX_NO_SUCH_FILE:     return ENOENT;
        case LIBSSH2_FX_PERMISSION_DENIED: return EACCES;
        case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM: return ENOSPC;
        case LIBSSH2_FX_FILE_ALREADY_EXISTS: return EEXIST;
        case LIBSSH2_FX_WRITE_PROTECT:    return EROFS;
        case LIBSSH2_FX_NO_MEDIA:         return ENODEV;
        case LIBSSH2_FX_NO_SUCH_PATH:     return ENOENT;
        case LIBSSH2_FX_NOT_A_DIRECTORY:  return ENOTDIR;
        case LIBSSH2_FX_INVALID_HANDLE:   return EBADF;
        case LIBSSH2_FX_INVALID_FILENAME: return EINVAL;
        case LIBSSH2_FX_DIR_NOT_EMPTY:    return ENOTEMPTY;
        case LIBSSH2_FX_OP_UNSUPPORTED:   return ENOSYS;
        default:                          return EIO;
    }
}

int sftp_copy_local_to_remote(const char *local_path, const char *remote_path) {
    FILE *local_file = fopen(local_path, "rb");
    if (!local_file) {
        LOG_ERR("Failed to open local file: %s", local_path);
        return -errno;
    }

    unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;
    LIBSSH2_SFTP_HANDLE *remote_handle = sftp_open_remote(remote_path, flags, 0644);
    if (!remote_handle) {
        LOG_ERR("Failed to open remote file: %s", remote_path);
        fclose(local_file);
        return -EIO;
    }

    char buffer[32768];
    size_t bytes_read;
    ssize_t bytes_written;
    int result = 0;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), local_file)) > 0) {
        bytes_written = sftp_write_remote(remote_handle, buffer, bytes_read);
        if (bytes_written < 0 || (size_t)bytes_written != bytes_read) {
            LOG_ERR("Failed to write to remote file");
            result = -EIO;
            break;
        }
    }

    if (ferror(local_file)) {
        LOG_ERR("Error reading from local file");
        result = -errno;
    }

    fclose(local_file);
    sftp_close_remote(remote_handle);

    return result;
}

int sftp_copy_remote_to_local(const char *remote_path, const char *local_path) {
    LIBSSH2_SFTP_HANDLE *remote_handle = sftp_open_remote(remote_path, LIBSSH2_FXF_READ, 0);
    if (!remote_handle) {
        LOG_ERR("Failed to open remote file: %s", remote_path);
        return -EIO;
    }

    FILE *local_file = fopen(local_path, "wb");
    if (!local_file) {
        LOG_ERR("Failed to open local file: %s", local_path);
        sftp_close_remote(remote_handle);
        return -errno;
    }

    char buffer[32768];
    ssize_t bytes_read;
    size_t bytes_written;
    int result = 0;

    while ((bytes_read = sftp_read_remote(remote_handle, buffer, sizeof(buffer))) > 0) {
        bytes_written = fwrite(buffer, 1, bytes_read, local_file);
        if (bytes_written < (size_t)bytes_read) {
            LOG_ERR("Failed to write to local file");
            result = -errno;
            break;
        }
    }

    if (bytes_read < 0) {
        LOG_ERR("Error reading from remote file");
        result = -EIO;
    }

    fclose(local_file);
    sftp_close_remote(remote_handle);

    return result;
}

int sftp_move_local_to_remote(const char *local_path, const char *remote_path) {
    int result = sftp_copy_local_to_remote(local_path, remote_path);
    if (result == 0) {
        if (unlink(local_path) != 0) {
            result = -errno;
            LOG_ERR("Failed to remove local file after copy");
            sftp_unlink_remote(remote_path);
        }
    }
    return result;
}

int sftp_move_remote_to_local(const char *remote_path, const char *local_path) {
    int result = sftp_copy_remote_to_local(remote_path, local_path);
    if (result == 0) {
        if (sftp_unlink_remote(remote_path) != 0) {
            result = -EIO;
            LOG_ERR("Failed to remove remote file after copy");
            unlink(local_path);
        }
    }
    return result;
}
