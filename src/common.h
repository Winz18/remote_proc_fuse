#ifndef COMMON_H
#define COMMON_H

#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <libssh2.h>
#include <libssh2_sftp.h>

typedef struct {
    char *remote_host;
    char *remote_user;
    char *remote_pass;
    char *ssh_key_path;
    int remote_port;
    char *remote_proc_path;

    int sock;
    LIBSSH2_SESSION *ssh_session;
    LIBSSH2_SFTP *sftp_session;

} remote_conn_info_t;

extern remote_conn_info_t *ssh_cli_conn;

static inline remote_conn_info_t* get_conn_info() {
    struct fuse_context *fc = fuse_get_context();
    if (fc && fc->private_data)
        return (remote_conn_info_t*)fc->private_data;
    return ssh_cli_conn;
}

#define LOG_ERR(fmt, ...) fprintf(stderr, "[ERROR] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) fprintf(stderr, "[WARNING] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) fprintf(stderr, "[INFO] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#endif
