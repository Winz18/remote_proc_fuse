#define FUSE_USE_VERSION 31

#include "common.h"
#include "remote_proc_fuse.h"
#include "ssh_sftp_client.h" // Cần để truy cập struct conn info
#include "mount_config.h"    // Thêm header cho config mount
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // getopt
#include <libgen.h> // realpath
#include <limits.h> // PATH_MAX

// Cấu trúc lưu trữ các đối số dòng lệnh tùy chỉnh
struct fuse_args cli_args = FUSE_ARGS_INIT(0, NULL); // Cho FUSE xử lý arg của nó
remote_conn_info_t connection_info = { // Dữ liệu kết nối sẽ truyền vào FUSE
    .remote_host = NULL,
    .remote_user = NULL,
    .remote_pass = NULL,
    .ssh_key_path = NULL,
    .remote_port = 22, // Mặc định port 22
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
    fprintf(stderr, "                    (If key has passphrase, provide it with 'pass=' option).\n");
    fprintf(stderr, "  remotepath=path   Path to mount on the remote system (default: /).\n");
    fprintf(stderr, "  readonly          Mount filesystem as read-only.\n");
    fprintf(stderr, "  allow_other       Allow other users to access the filesystem.\n");
    fprintf(stderr, "\nExample:\n");
    fprintf(stderr, "  %s /mnt/remote -o host=192.168.1.100 -o user=myuser -o pass=mypassword -o remotepath=/home/myuser\n", progname);
    fprintf(stderr, "  %s /mnt/remote -o host=server.com -o user=admin -o key=~/.ssh/id_rsa -o remotepath=/etc\n", progname);
    fprintf(stderr, "\nStandard FUSE options (e.g., -f for foreground, -d for debug) are also accepted.\n");

    // Thêm fuse_lib_help để hiển thị các tùy chọn FUSE chuẩn
    // fuse_lib_help(&cli_args); // Cần tạo fuse_args đúng cách trước
}


// Định nghĩa các tùy chọn tùy chỉnh cho FUSE
enum {
     KEY_HELP,
     KEY_VERSION,
     // Các key cho tùy chọn -o của chúng ta
     KEY_OPT_HOST,
     KEY_OPT_USER,
     KEY_OPT_PASS,
     KEY_OPT_PORT,
     KEY_OPT_KEY,
     KEY_OPT_REMOTEPATH,
};

#define RP_OPT(t, p, v) { t, offsetof(remote_conn_info_t, p), v }

static struct fuse_opt rp_opts[] = {
     // Các tùy chọn -o tùy chỉnh
     { "host=%s",    offsetof(remote_conn_info_t, remote_host), KEY_OPT_HOST },
     { "user=%s",    offsetof(remote_conn_info_t, remote_user), KEY_OPT_USER },
     { "pass=%s",    offsetof(remote_conn_info_t, remote_pass), KEY_OPT_PASS },
     { "port=%d",    offsetof(remote_conn_info_t, remote_port), KEY_OPT_PORT },
     { "key=%s",     offsetof(remote_conn_info_t, ssh_key_path), KEY_OPT_KEY },
     { "remotepath=%s", offsetof(remote_conn_info_t, remote_proc_path), KEY_OPT_REMOTEPATH },

     // Các tùy chọn FUSE chuẩn (-h, --help, -V, --version)
     FUSE_OPT_KEY("-h",          KEY_HELP),
     FUSE_OPT_KEY("--help",      KEY_HELP),
     FUSE_OPT_KEY("-V",          KEY_VERSION),
     FUSE_OPT_KEY("--version",   KEY_VERSION),
     FUSE_OPT_END // Đánh dấu kết thúc mảng options
};


// Hàm xử lý tùy chọn FUSE tùy chỉnh
static int rp_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    remote_conn_info_t *conn = (remote_conn_info_t*)data;

    switch (key) {
        case KEY_HELP:
            show_usage(outargs->argv[0]);
            // Yêu cầu FUSE thoát sau khi xử lý tùy chọn này
             return fuse_opt_add_arg(outargs, "-ho") ? -1 : 1; // -ho để fuse chỉ in help của nó
            // exit(0);
        case KEY_VERSION:
             fprintf(stderr, "RemoteFS - FUSE-based Remote Filesystem version 1.0\n");
             // Yêu cầu FUSE hiển thị version của nó và thoát
             return fuse_opt_add_arg(outargs, "--version") ? -1 : 1;
            // exit(0);

        // Xử lý các tùy chọn -o của chúng ta
        case KEY_OPT_HOST:
        case KEY_OPT_USER:
        case KEY_OPT_PASS:
        case KEY_OPT_KEY:
        case KEY_OPT_REMOTEPATH:
             // Giá trị đã được fuse_opt_parse gán vào conn qua offsetof
             // Cần strdup để lưu trữ giá trị chuỗi vì arg có thể bị thay đổi
             {
                 // Tìm lại offset dựa trên key để biết trường nào cần strdup
                 size_t offset = 0;
                 for (int i = 0; rp_opts[i].templ; ++i) {
                     if (rp_opts[i].value == key) {
                         offset = rp_opts[i].offset;
                         break;
                     }
                 }
                 if (offset > 0) {
                     char **field_ptr = (char **)(((char *)conn) + offset);
                     const char *value_start = strchr(arg, '=');
                     if (value_start) {
                         *field_ptr = strdup(value_start + 1);
                         if (!(*field_ptr)) {
                              perror("strdup failed");
                              return -1;
                         }
                         LOG_DEBUG("Parsed option: %s = %s", rp_opts[key].templ, *field_ptr); // Key không đúng ở đây
                     }
                 }
             }
             return 0; // Đã xử lý, không cần FUSE làm gì thêm với arg này

        case KEY_OPT_PORT:
            // Số nguyên đã được fuse_opt_parse xử lý
            LOG_DEBUG("Parsed option: port = %d", conn->remote_port);
            return 0;

        default:
            // Để FUSE xử lý các tùy chọn khác (như -f, -d)
            return 1; // 1 = option not found here, let fuse handle it
    }
}


int main(int argc, char *argv[]) {
    int ret;

    // Định nghĩa các hàm FUSE callback
    struct fuse_operations rp_oper = {
        .init       = rp_init,      // Khởi tạo
        .destroy    = rp_destroy,   // Hủy
        .getattr    = rp_getattr,   // Lấy thuộc tính
        .readdir    = rp_readdir,   // Đọc thư mục
        .open       = rp_open,      // Mở file
        .read       = rp_read,      // Đọc file
        .release    = rp_release,   // Đóng file
        .access     = rp_access,    // Kiểm tra quyền (tùy chọn)

        // Các hàm hỗ trợ ghi, tạo, xóa file/thư mục
        .write      = rp_write,     // Ghi file
        .create     = rp_create,    // Tạo file mới
        .unlink     = rp_unlink,    // Xóa file
        .mkdir      = rp_mkdir,     // Tạo thư mục
        .rmdir      = rp_rmdir,     // Xóa thư mục
        .truncate   = rp_truncate,  // Cắt ngắn file
        .rename     = rp_rename,    // Đổi tên file/thư mục
    };

    // Phân tích đối số dòng lệnh sử dụng fuse_opt_parse
    // Nó sẽ xử lý cả tùy chọn FUSE chuẩn và tùy chọn tùy chỉnh của chúng ta
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    if (fuse_opt_parse(&args, &connection_info, rp_opts, rp_opt_proc) == -1) {
        fprintf(stderr, "Error parsing options.\n");
        fuse_opt_free_args(&args);
        return 1;
    }

    // Gán giá trị mặc định cho remote_proc_path nếu chưa được đặt, và cấp phát động
    if (!connection_info.remote_proc_path) {
        connection_info.remote_proc_path = strdup("/");
        if (!connection_info.remote_proc_path) {
             perror("strdup failed for default remote path");
             fuse_opt_free_args(&args);
             // Giải phóng các cái đã strdup khác nếu có
             free(connection_info.remote_host);
             free(connection_info.remote_user);
             free(connection_info.remote_pass);
             free(connection_info.ssh_key_path);
             return 1;
        }
   }


    // Kiểm tra các đối số bắt buộc
    if (!connection_info.remote_host || !connection_info.remote_user) {
        fprintf(stderr, "Error: Missing required options: host= and user=\n\n");
        show_usage(argv[0]);
        fuse_opt_free_args(&args);
        // Giải phóng bộ nhớ đã strdup nếu có
        free(connection_info.remote_host);
        free(connection_info.remote_user);
        free(connection_info.remote_pass);
        free(connection_info.ssh_key_path);
        free(connection_info.remote_proc_path); // <<< Luôn free vì đã strdup
        return 1;
    }
    if (!connection_info.remote_pass && !connection_info.ssh_key_path) {
        fprintf(stderr, "Error: Missing authentication option: pass= or key=\n\n");
        show_usage(argv[0]);
        fuse_opt_free_args(&args);
        free(connection_info.remote_host);
        free(connection_info.remote_user);
        free(connection_info.remote_pass);
        free(connection_info.ssh_key_path);
        free(connection_info.remote_proc_path); // <<< Luôn free
        return 1;
    }

    // In thông tin kết nối (trừ mật khẩu)
    LOG_INFO("Mounting remote filesystem:");
    LOG_INFO("  Remote Host: %s", connection_info.remote_host);
    LOG_INFO("  Remote Port: %d", connection_info.remote_port);
    LOG_INFO("  Remote User: %s", connection_info.remote_user);
    if (connection_info.ssh_key_path) {
        LOG_INFO("  Auth Method: Public Key (%s)", connection_info.ssh_key_path);
        if(connection_info.remote_pass) LOG_INFO("  Key Passphrase: [Provided]");
    } else {
        LOG_INFO("  Auth Method: Password [Provided]");
    }
    LOG_INFO("  Remote Path: %s", connection_info.remote_proc_path);
    
    // Lấy và lưu thông tin mount point
    // Đối số mount point là tham số không phải tùy chọn đầu tiên trong args.argv
    int mount_point_idx = 1;  // Thường là argv[1]
    while (mount_point_idx < args.argc) {
        // Bỏ qua các tùy chọn
        if (args.argv[mount_point_idx][0] == '-') {
            mount_point_idx++;
            // Bỏ qua cả đối số của tùy chọn nếu có
            if (mount_point_idx < args.argc && 
                (args.argv[mount_point_idx-1][1] == 'o' || 
                 strncmp(args.argv[mount_point_idx-1], "--opt", 5) == 0)) {
                mount_point_idx++;
            }
            continue;
        }
        break;  // Đã tìm thấy đối số không phải tùy chọn
    }
    
    if (mount_point_idx < args.argc) {
        char *mount_point = args.argv[mount_point_idx];
        char real_path[PATH_MAX];
        
        // Lấy đường dẫn tuyệt đối
        if (realpath(mount_point, real_path) != NULL) {
            LOG_INFO("  Mount Point: %s", real_path);
            
            // Lưu thông tin mount point
            if (save_mount_point(real_path, connection_info.remote_proc_path) != 0) {
                LOG_WARN("Failed to save mount point information");
            } else {
                LOG_INFO("Saved mount point info for tools");
            }
        } else {
            LOG_WARN("Could not resolve real path for mount point: %s", mount_point);
        }
    } else {
        LOG_WARN("Could not determine mount point");
    }

    // Gọi hàm chính của FUSE
    // Truyền con trỏ tới connection_info làm private_data
    // fuse_main sẽ không trả về trừ khi filesystem bị unmount hoặc có lỗi nghiêm trọng
    ret = fuse_main(args.argc, args.argv, &rp_oper, &connection_info);

    // Dọn dẹp sau khi fuse_main trả về
    fuse_opt_free_args(&args);

    // Giải phóng bộ nhớ đã cấp phát bởi strdup trong rp_opt_proc
    free(connection_info.remote_host);
    free(connection_info.remote_user);
    free(connection_info.remote_pass);
    free(connection_info.ssh_key_path);
    free(connection_info.remote_proc_path); // Luôn free vì đã strdup
    // Không free remote_proc_path nếu nó vẫn dùng giá trị mặc định "/proc"
    // Chỉ free nếu nó được gán từ strdup
    if (connection_info.remote_proc_path != NULL && strcmp(connection_info.remote_proc_path, "/proc") != 0) {
         // Hoặc cách an toàn hơn là luôn strdup giá trị mặc định vào conn_info ban đầu
         // và luôn free ở cuối.
         // free(connection_info.remote_proc_path);
    }


    if (ret != 0) {
        fprintf(stderr, "fuse_main returned an error: %d\n", ret);
    } else {
        LOG_INFO("Filesystem unmounted successfully.");
    }

    return ret;
}