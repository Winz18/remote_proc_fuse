#define FUSE_USE_VERSION 31

#include "common.h"
#include "remote_proc_fuse.h"
#include "ssh_sftp_client.h"
#include "mount_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <libssh2.h> // <-- Thêm include cho libssh2_init/exit

struct fuse_args cli_args = FUSE_ARGS_INIT(0, NULL);
remote_conn_info_t connection_info = {
    .remote_host = NULL,
    .remote_user = NULL,
    .remote_pass = NULL,
    .ssh_key_path = NULL,
    .remote_port = 22,
    .remote_proc_path = NULL,
    .sock = -1,
    .ssh_session = NULL,
    .sftp_session = NULL
};

static void show_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [FUSE options] <mountpoint> -o host=<hostname> -o user=<username> [-o port=<port>] [-o pass=<password> | -o key=<keyfile>] [-o remotepath=<path>]\n\n", progname);
    fprintf(stderr, "Mounts a remote directory via SSH/SFTP.\n\n");
    fprintf(stderr, "Required FUSE options (-o):\n");
    fprintf(stderr, "  host=hostname     Hostname or IP address of the remote machine.\n");
    fprintf(stderr, "  user=username     Username for SSH login.\n");
    fprintf(stderr, "\nOptional FUSE options (-o):\n");
    fprintf(stderr, "  port=port         SSH port on the remote machine (default: 22).\n");
    fprintf(stderr, "  pass=password     Password for SSH login (INSECURE!).\n");
    fprintf(stderr, "  key=keyfile       Path to the private SSH key file for authentication.\n");
    fprintf(stderr, "  remotepath=path   Path to mount on the remote system (default: /).\n");
    fprintf(stderr, "  readonly          Mount filesystem as read-only.\n");
    fprintf(stderr, "  allow_other       Allow other users to access the filesystem.\n");
    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s /mnt/remote -o host=192.168.1.100 -o user=myuser -o pass=mypassword -o remotepath=/home/myuser\n", progname);
    fprintf(stderr, "  %s /mnt/remote -o host=server.com -o user=admin -o key=~/.ssh/id_rsa -o remotepath=/etc\n", progname);
    fprintf(stderr, "\nStandard FUSE options (e.g., -f for foreground, -d for debug) are also accepted.\n");
}

enum {
     KEY_HELP,
     KEY_VERSION,
     KEY_OPT_HOST,
     KEY_OPT_USER,
     KEY_OPT_PASS,
     KEY_OPT_PORT,
     KEY_OPT_KEY,
     KEY_OPT_REMOTEPATH,
};

#define RP_OPT(t, p, v) { t, offsetof(remote_conn_info_t, p), v }

static struct fuse_opt rp_opts[] = {
     { "host=%s",    offsetof(remote_conn_info_t, remote_host), KEY_OPT_HOST },
     { "user=%s",    offsetof(remote_conn_info_t, remote_user), KEY_OPT_USER },
     { "pass=%s",    offsetof(remote_conn_info_t, remote_pass), KEY_OPT_PASS },
     { "port=%d",    offsetof(remote_conn_info_t, remote_port), KEY_OPT_PORT },
     { "key=%s",     offsetof(remote_conn_info_t, ssh_key_path), KEY_OPT_KEY },
     { "remotepath=%s", offsetof(remote_conn_info_t, remote_proc_path), KEY_OPT_REMOTEPATH },

     FUSE_OPT_KEY("-h",          KEY_HELP),
     FUSE_OPT_KEY("--help",      KEY_HELP),
     FUSE_OPT_KEY("-V",          KEY_VERSION),
     FUSE_OPT_KEY("--version",   KEY_VERSION),
     FUSE_OPT_END
};

// Hàm xử lý tùy chọn FUSE
static int rp_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    remote_conn_info_t *conn = (remote_conn_info_t*)data;
    char *value_start = NULL; // Khởi tạo để tránh cảnh báo

    switch (key) {
        case KEY_HELP:
            show_usage(outargs->argv[0]);
             // Trả về 1 để dừng xử lý tùy chọn và hiển thị trợ giúp FUSE
             return fuse_opt_add_arg(outargs, "-ho") ? -1 : 1;

        case KEY_VERSION:
             fprintf(stderr, "RemoteFS - FUSE-based Remote Filesystem version 1.0\n");
             // Trả về 1 để dừng xử lý tùy chọn và hiển thị phiên bản FUSE
             return fuse_opt_add_arg(outargs, "--version") ? -1 : 1;

        // Xử lý các tùy chọn chuỗi
        case KEY_OPT_HOST:
        case KEY_OPT_USER:
        case KEY_OPT_PASS:
        case KEY_OPT_KEY:
        case KEY_OPT_REMOTEPATH:
             {
                 size_t offset = 0;
                 // Tìm offset của trường tương ứng trong cấu trúc rp_opts
                 for (int i = 0; rp_opts[i].templ; ++i) {
                     if (rp_opts[i].value == key) {
                         offset = rp_opts[i].offset;
                         break;
                     }
                 }
                 if (offset > 0) {
                     // Lấy con trỏ đến trường char* trong connection_info
                     char **field_ptr = (char **)(((char *)conn) + offset);
                     // Tìm dấu '=' để lấy giá trị
                     value_start = strchr(arg, '=');
                     if (value_start) {
                         // Giải phóng bộ nhớ cũ nếu đã được cấp phát trước đó (quan trọng!)
                         free(*field_ptr);
                         // Sao chép giá trị mới
                         *field_ptr = strdup(value_start + 1);
                         if (!(*field_ptr)) {
                              perror("strdup failed");
                              return -1; // Lỗi cấp phát bộ nhớ
                         }
                         LOG_DEBUG("Parsed option: %s = %s", rp_opts[key].templ, *field_ptr);
                     } else {
                          fprintf(stderr, "Warning: Invalid format for option %s\n", arg);
                     }
                 } else {
                      fprintf(stderr, "Internal error: Option key %d not found in rp_opts\n", key);
                      return -1;
                 }
             }
             // Trả về 0 để báo rằng tùy chọn đã được xử lý và không cần chuyển cho FUSE
             return 0;

        // Xử lý tùy chọn số nguyên (port)
        case KEY_OPT_PORT:
            // Giá trị port đã được fuse_opt_parse gán trực tiếp vào conn->remote_port
            LOG_DEBUG("Parsed option: port = %d", conn->remote_port);
            // Trả về 0 để báo rằng tùy chọn đã được xử lý
            return 0;

        // Các tùy chọn khác không được xử lý bởi hàm này sẽ được chuyển cho FUSE
        default:
            // Trả về 1 để FUSE xử lý các tùy chọn chuẩn của nó (ví dụ: -f, -d)
            return 1;
    }
}

int main(int argc, char *argv[]) {
    // ---> KHỞI TẠO LIBSSH2 <---
    if (libssh2_init(0) != 0) {
        fprintf(stderr, "Error initializing libssh2\n");
        return 1;
    }

    int ret = 1; // Mặc định là lỗi

    struct fuse_operations rp_oper = {
        .init       = rp_init,
        .destroy    = rp_destroy,
        .getattr    = rp_getattr,
        .readdir    = rp_readdir,
        .open       = rp_open,
        .read       = rp_read,
        .release    = rp_release,
        .access     = rp_access,
        // ... các hoạt động khác ...
        .write      = rp_write,
        .create     = rp_create,
        .unlink     = rp_unlink,
        .mkdir      = rp_mkdir,
        .rmdir      = rp_rmdir,
        .truncate   = rp_truncate,
        .rename     = rp_rename,
        .fsync      = rp_fsync,
    };

    // Khởi tạo FUSE args
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    // Phân tích cú pháp các tùy chọn dòng lệnh
    // fuse_opt_parse sẽ điền vào cấu trúc connection_info và xử lý các tùy chọn FUSE chuẩn
    if (fuse_opt_parse(&args, &connection_info, rp_opts, rp_opt_proc) == -1) {
        fprintf(stderr, "Error parsing options.\n");
        // Không cần giải phóng connection_info ở đây vì fuse_main chưa chạy
        fuse_opt_free_args(&args);
        libssh2_exit(); // <-- Dọn dẹp trước khi thoát
        return 1;
    }

    // Đặt giá trị mặc định cho remote_proc_path nếu chưa được cung cấp
    if (!connection_info.remote_proc_path) {
        connection_info.remote_proc_path = strdup("/");
        if (!connection_info.remote_proc_path) {
             perror("strdup failed for default remote path");
             fuse_opt_free_args(&args);
             libssh2_exit(); // <-- Dọn dẹp trước khi thoát
             return 1;
        }
    }

    // Kiểm tra các tùy chọn bắt buộc
    if (!connection_info.remote_host || !connection_info.remote_user) {
        fprintf(stderr, "Error: Both host and user must be specified.\n");
        fprintf(stderr, "Use -h or --help for usage information.\n");
        // Giải phóng bộ nhớ đã cấp phát bởi strdup và fuse_opt_parse
        free(connection_info.remote_host);
        free(connection_info.remote_user);
        free(connection_info.remote_pass);
        free(connection_info.ssh_key_path);
        free(connection_info.remote_proc_path);
        fuse_opt_free_args(&args);
        libssh2_exit(); // <-- Dọn dẹp trước khi thoát
        return 1;
    }

    // Kiểm tra phương thức xác thực
    if (!connection_info.remote_pass && !connection_info.ssh_key_path) {
        fprintf(stderr, "Error: Either password (pass=) or SSH key (key=) must be specified.\n");
        fprintf(stderr, "Use -h or --help for usage information.\n");
        free(connection_info.remote_host);
        free(connection_info.remote_user);
        free(connection_info.remote_pass);
        free(connection_info.ssh_key_path);
        free(connection_info.remote_proc_path);
        fuse_opt_free_args(&args);
        libssh2_exit(); // <-- Dọn dẹp trước khi thoát
        return 1;
    }

    // --- Lưu thông tin mount point và kết nối ---
    // Tìm đối số không phải là tùy chọn (được cho là mount point)
    // Lưu ý: Đoạn mã này giả định mount point là đối số *đầu tiên* không phải tùy chọn.
    // FUSE thường yêu cầu mount point là đối số cuối cùng không phải tùy chọn.
    // Cần kiểm tra lại logic này nếu gặp vấn đề.
    char *mount_point = NULL;
    for (int i = 1; i < args.argc; i++) {
         if (args.argv[i][0] != '-') {
              // Tìm thấy đối số không phải tùy chọn đầu tiên
              // Tuy nhiên, cần đảm bảo nó không phải là giá trị của tùy chọn trước đó (-o value)
              if (i > 0 && args.argv[i-1][0] == '-' && args.argv[i-1][1] == 'o' && strlen(args.argv[i-1]) == 2) {
                   // Đây là giá trị của -o, bỏ qua
                   continue;
              }
              // Kiểm tra các trường hợp tùy chọn dài (--option=value) - phức tạp hơn
              // Cách đơn giản hơn là dựa vào việc fuse_main sẽ báo lỗi nếu mountpoint không hợp lệ.
              // Hoặc, giả định mountpoint là đối số cuối cùng.
              mount_point = args.argv[args.argc - 1]; // Giả định là đối số cuối cùng
              break; // Thoát vòng lặp sau khi tìm thấy
         }
    }


    if (mount_point && mount_point[0] != '-') { // Kiểm tra lại mount_point hợp lệ
        char real_path[PATH_MAX];
        if (realpath(mount_point, real_path) != NULL) {
            LOG_INFO("  Mount Point: %s", real_path);

            // Lưu đường dẫn remote tương ứng với mount point này
            if (save_mount_point(real_path, connection_info.remote_proc_path) != 0) {
                LOG_WARN("Failed to save mount point information to mounts.conf");
            } else {
                LOG_INFO("Saved mount point info for tools (mounts.conf)");
            }

            // Lưu toàn bộ thông tin kết nối
            if (save_connection_info(real_path, &connection_info) != 0) {
                LOG_WARN("Failed to save full connection information to connections.conf");
            } else {
                LOG_INFO("Saved full connection info for tools (connections.conf)");
            }
        } else {
            LOG_WARN("Could not resolve real path for mount point: %s (%s)", mount_point, strerror(errno));
            // Có thể tiếp tục mount, nhưng các công cụ cp/mv có thể không hoạt động đúng
        }
    } else {
        LOG_WARN("Could not reliably determine mount point from arguments for saving config.");
        // FUSE có thể vẫn hoạt động nếu mount point được truyền đúng cho fuse_main
    }
    // --- Kết thúc phần lưu thông tin ---


    // Bắt đầu vòng lặp chính của FUSE
    // connection_info được truyền làm private_data, có thể truy cập qua fuse_get_context()
    ret = fuse_main(args.argc, args.argv, &rp_oper, &connection_info);

    // Giải phóng bộ nhớ cho các đối số FUSE (không phải connection_info)
    fuse_opt_free_args(&args);

    // Giải phóng bộ nhớ đã cấp phát cho các trường trong connection_info
    // Lưu ý: Không giải phóng connection_info ở đây vì nó không được cấp phát động
    // mà là biến toàn cục tĩnh trong hàm này.
    // Tuy nhiên, các trường char* bên trong nó *đã* được cấp phát động bởi strdup.
    free(connection_info.remote_host);
    free(connection_info.remote_user);
    free(connection_info.remote_pass);
    free(connection_info.ssh_key_path);
    free(connection_info.remote_proc_path);
    // Không cần gọi sftp_disconnect ở đây vì rp_destroy sẽ làm điều đó

    if (ret != 0) {
        // fuse_main thường in lỗi của chính nó
        fprintf(stderr, "fuse_main returned an error: %d\n", ret);
    } else {
        LOG_INFO("Filesystem unmounted successfully (or fuse_main exited cleanly).");
    }

    // ---> DỌN DẸP LIBSSH2 <---
    libssh2_exit();
    return ret; // Trả về mã lỗi từ fuse_main
}
