#include "common.h"
#include "ssh_sftp_client.h"
#include "mount_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <getopt.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h> // Thêm include cho unlink
#include <libssh2.h> // <-- Thêm include cho libssh2_init/exit

// Biến toàn cục này nên được định nghĩa duy nhất trong ssh_sftp_client.c
// extern remote_conn_info_t *ssh_cli_conn; // Khai báo extern nếu cần

void show_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [options] <source> <destination>\n\n", progname);
    fprintf(stderr, "Move files between local filesystem and RemoteFS mounted directories.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --verbose        Enable verbose output\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s localfile.txt /path/to/mounted/remotefs/      # Move local file to remote\n", progname);
    fprintf(stderr, "  %s /path/to/mounted/remotefs/file.txt ./         # Move remote file to local\n", progname);
    fprintf(stderr, "  %s file1.txt /path/to/mounted/remotefs/file2.txt # Move and rename\n", progname);
}

const char* is_remote_path(const char *path, char *mount_point_buf, size_t buf_size) {
    // Empty path check
    if (!path || strlen(path) == 0) {
        return NULL;
    }

    // Try to resolve the real absolute path
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path) == NULL) {
        // If realpath fails, just use the provided path directly
        strncpy(abs_path, path, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = '\0';
    }

    // Get all mount points
    int count = 0;
    char **mount_points = get_mount_points(&count);
    if (!mount_points || count == 0) {
        // No mount points found
        return NULL;
    }

    // Check if the path starts with any of our mount points
    for (int i = 0; i < count; i++) {
        size_t len = strlen(mount_points[i]);
        if (strncmp(abs_path, mount_points[i], len) == 0 &&
            (abs_path[len] == '/' || abs_path[len] == '\0')) {
            // Match found - copy to output buffer if provided
            if (mount_point_buf && buf_size > 0) {
                strncpy(mount_point_buf, mount_points[i], buf_size - 1);
                mount_point_buf[buf_size - 1] = '\0';
            }

            // Free all mount points
            for (int j = 0; j < count; j++) {
                free(mount_points[j]);
            }
            free(mount_points);

            return mount_point_buf;
        }
    }

    // No match found - free all mount points
    for (int i = 0; i < count; i++) {
        free(mount_points[i]);
    }
    free(mount_points);

    return NULL;
}

int get_remote_path(const char *path, char *remote_path, size_t size, int verbose) {
    if (!path || !remote_path || size == 0) {
        return -1;
    }

    char mount_point[PATH_MAX];

    // Determine if the path is on a remote filesystem
    if (!is_remote_path(path, mount_point, sizeof(mount_point))) {
        if (verbose) {
            fprintf(stderr, "Path is not on a remote filesystem: %s\n", path);
        }
        return -1;
    }

    // Get the remote base path for this mount point
    char *base_remote_path = get_remote_path_for_mount(mount_point);
    if (!base_remote_path) {
        fprintf(stderr, "Error: Cannot determine remote path for mount point: %s\n", mount_point);
        return -1;
    }

    // Get the absolute path of the local path
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path) == NULL) {
        // If realpath fails, use the provided path
        strncpy(abs_path, path, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = '\0';
    }

    // Calculate the relative part (path within the mount)
    const char *rel_path = abs_path + strlen(mount_point);

    // Construct the full remote path
    if (rel_path[0] != '/' && strlen(rel_path) > 0) {
        // Add slash if needed
        snprintf(remote_path, size, "%s/%s", base_remote_path, rel_path);
    } else {
        // Already has a slash or is empty
        snprintf(remote_path, size, "%s%s", base_remote_path, rel_path);
    }

    // Cleanup any double slashes that might occur
    if (strlen(base_remote_path) > 0 && base_remote_path[strlen(base_remote_path) - 1] == '/' &&
        rel_path[0] == '/') {
        char *double_slash = strstr(remote_path, "//");
        if (double_slash) {
            memmove(double_slash, double_slash + 1, strlen(double_slash));
        }
    }

    if (verbose) {
        printf("Local path: %s\n", path);
        printf("Mount point: %s\n", mount_point);
        printf("Remote base path: %s\n", base_remote_path);
        printf("Relative path: %s\n", rel_path);
        printf("Full remote path: %s\n", remote_path);
    }

    free(base_remote_path);
    return 0;
}

int setup_connection(const char *mount_point, int verbose) {
    if (verbose) {
        printf("Setting up connection for mount point: %s\n", mount_point);
    }

    // Allocate connection info structure
    ssh_cli_conn = calloc(1, sizeof(remote_conn_info_t));
    if (!ssh_cli_conn) {
        perror("Failed to allocate memory for connection info");
        return -1;
    }

    // Initialize to default values
    ssh_cli_conn->sock = -1;
    ssh_cli_conn->remote_port = 22;

    // Try to load full connection information
    int load_result = load_connection_info_for_mount(mount_point, ssh_cli_conn);

    if (load_result != 0) {
        fprintf(stderr, "Error: Cannot load connection configuration for mount point: %s\n", mount_point);
        fprintf(stderr, "Ensure the filesystem was mounted with host, user, key/pass options.\n");
        if (ssh_cli_conn->remote_proc_path) free(ssh_cli_conn->remote_proc_path);
        free(ssh_cli_conn);
        ssh_cli_conn = NULL;
        return -1;
    }

    // Validate the loaded connection information
    if (!ssh_cli_conn->remote_host || !ssh_cli_conn->remote_user) {
        fprintf(stderr, "Error: Missing required connection information (host or user).\n");
        free(ssh_cli_conn->remote_host);
        free(ssh_cli_conn->remote_user);
        free(ssh_cli_conn->remote_pass);
        free(ssh_cli_conn->ssh_key_path);
        free(ssh_cli_conn->remote_proc_path);
        free(ssh_cli_conn);
        ssh_cli_conn = NULL;
        return -1;
    }

    if (!ssh_cli_conn->remote_pass && !ssh_cli_conn->ssh_key_path) {
        fprintf(stderr, "Error: No authentication method available (password or key).\n");
        free(ssh_cli_conn->remote_host);
        free(ssh_cli_conn->remote_user);
        free(ssh_cli_conn->remote_pass);
        free(ssh_cli_conn->ssh_key_path);
        free(ssh_cli_conn->remote_proc_path);
        free(ssh_cli_conn);
        ssh_cli_conn = NULL;
        return -1;
    }

    if (verbose) {
        printf("Connecting to %s@%s:%d\n",
               ssh_cli_conn->remote_user,
               ssh_cli_conn->remote_host,
               ssh_cli_conn->remote_port);
        printf("Authentication method: %s\n",
               ssh_cli_conn->ssh_key_path ? "SSH key" : "Password");
        printf("Remote path: %s\n",
               ssh_cli_conn->remote_proc_path ? ssh_cli_conn->remote_proc_path : "/");
    }

    // Attempt to connect and authenticate
    if (sftp_connect_and_auth(ssh_cli_conn) != 0) {
        fprintf(stderr, "Error: Failed to connect and authenticate SFTP session\n");
        free(ssh_cli_conn->remote_host);
        free(ssh_cli_conn->remote_user);
        free(ssh_cli_conn->remote_pass);
        free(ssh_cli_conn->ssh_key_path);
        free(ssh_cli_conn->remote_proc_path);
        free(ssh_cli_conn);
        ssh_cli_conn = NULL;
        return -1;
    }

    if (verbose) {
        printf("SFTP connection established successfully\n");
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // ---> KHỞI TẠO LIBSSH2 <---
    if (libssh2_init(0) != 0) {
        fprintf(stderr, "Error initializing libssh2\n");
        return 1;
    }

    int verbose = 0;
    int result = 1; // Mặc định là lỗi

    static struct option long_options[] = {
        {"help",     no_argument, 0, 'h'},
        {"verbose",  no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "hv", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                show_usage(argv[0]);
                libssh2_exit(); // <-- Dọn dẹp trước khi thoát
                return 0;
            case 'v':
                verbose = 1;
                break;
            default:
                show_usage(argv[0]);
                libssh2_exit(); // <-- Dọn dẹp trước khi thoát
                return 1;
        }
    }

    if (argc - optind != 2) {
        fprintf(stderr, "Error: Source and destination arguments are required.\n");
        show_usage(argv[0]);
        libssh2_exit(); // <-- Dọn dẹp trước khi thoát
        return 1;
    }

    const char *source = argv[optind];
    const char *destination = argv[optind + 1];

    char source_mount_point[PATH_MAX] = {0};
    char dest_mount_point[PATH_MAX] = {0};
    int source_is_remote = (is_remote_path(source, source_mount_point, sizeof(source_mount_point)) != NULL);
    int dest_is_remote = (is_remote_path(destination, dest_mount_point, sizeof(dest_mount_point)) != NULL);

    if (verbose) {
        printf("Source is %s\n", source_is_remote ? "remote" : "local");
        printf("Destination is %s\n", dest_is_remote ? "remote" : "local");
        if (source_is_remote) printf("Source mount point: %s\n", source_mount_point);
        if (dest_is_remote) printf("Destination mount point: %s\n", dest_mount_point);
    }

    const char *relevant_mount_point = NULL;
    if (source_is_remote) {
        relevant_mount_point = source_mount_point;
    } else if (dest_is_remote) {
        relevant_mount_point = dest_mount_point;
    }

    // Chỉ thiết lập kết nối nếu cần (một trong hai là remote)
    if (relevant_mount_point) {
        if (setup_connection(relevant_mount_point, verbose) != 0) {
            libssh2_exit(); // <-- Dọn dẹp trước khi thoát
            return 1;
        }
    } else if (!source_is_remote && !dest_is_remote) {
        // Trường hợp cả hai đều local, không cần kết nối SSH
    } else {
        // Trường hợp không xác định được mount point liên quan (lỗi logic?)
        fprintf(stderr, "Error: Could not determine relevant mount point for operation.\n");
        libssh2_exit();
        return 1;
    }


    if (source_is_remote && dest_is_remote) {
        // Thử thực hiện rename trên remote nếu cả hai cùng mount point
        if (strcmp(source_mount_point, dest_mount_point) == 0) {
             char remote_source_path[PATH_MAX];
             char remote_dest_path[PATH_MAX];
             if (get_remote_path(source, remote_source_path, sizeof(remote_source_path), verbose) != 0 ||
                 get_remote_path(destination, remote_dest_path, sizeof(remote_dest_path), verbose) != 0)
             {
                 fprintf(stderr, "Error: Cannot determine remote paths for rename operation.\n");
                 result = 1;
             } else {
                 if (verbose) {
                     printf("Attempting remote rename from %s to %s\n", remote_source_path, remote_dest_path);
                 }
                 // Gọi hàm rename mới trong ssh_sftp_client.c (cần được thêm vào)
                 result = sftp_rename_remote(remote_source_path, remote_dest_path);
                 if (result != 0) {
                     fprintf(stderr, "Remote rename failed: %s\n", strerror(-result));
                     result = 1; // Đánh dấu lỗi
                 } else {
                      result = 0; // Thành công
                 }
             }
        } else {
            fprintf(stderr, "Error: Cannot move directly between two different remote locations/mount points.\n");
            result = 1;
        }
    } else if (!source_is_remote && !dest_is_remote) {
        fprintf(stderr, "Note: Both paths are local, using system mv\n");

        char cmd[PATH_MAX * 2 + 20]; // Tăng kích thước buffer
        // Đảm bảo source và destination được bao trong dấu nháy kép
        int ret = snprintf(cmd, sizeof(cmd), "/bin/mv \"%s\" \"%s\"", source, destination);
         if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
             fprintf(stderr, "Error: Command string too long.\n");
             result = 1;
         } else {
             result = system(cmd);
             if (result != 0) {
                  fprintf(stderr, "System mv command failed with exit code %d\n", result);
                  result = 1;
             } else {
                  result = 0; // Thành công
             }
         }
    } else if (source_is_remote && !dest_is_remote) {
        // --- Move từ Remote về Local ---
        char remote_path[PATH_MAX];
        if (get_remote_path(source, remote_path, sizeof(remote_path), verbose) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path for source %s\n", source);
            result = 1;
        } else {
            const char *actual_destination = destination;
            struct stat dest_st; // Đổi tên biến
            char new_dest_buf[PATH_MAX];

            // Kiểm tra xem đích local có phải là thư mục không
            if (stat(destination, &dest_st) == 0 && S_ISDIR(dest_st.st_mode)) {
                // Nếu là thư mục, tạo đường dẫn đích cuối cùng bên trong nó
                char *source_basename_dup = strdup(source); // Cần basename của source remote
                if (!source_basename_dup) { perror("strdup"); result = 1; goto cleanup; }
                 // Lấy basename từ remote_path thay vì source local
                 char *remote_path_dup = strdup(remote_path);
                 if (!remote_path_dup) { perror("strdup"); free(source_basename_dup); result = 1; goto cleanup; }
                 char *source_basename = basename(remote_path_dup);

                int ret = snprintf(new_dest_buf, sizeof(new_dest_buf), "%s/%s",
                        destination, source_basename);
                 if (ret < 0 || (size_t)ret >= sizeof(new_dest_buf)) {
                     fprintf(stderr, "Error: Destination path string too long.\n");
                     free(source_basename_dup);
                     free(remote_path_dup);
                     result = 1;
                     goto cleanup;
                 }
                free(source_basename_dup);
                free(remote_path_dup);
                actual_destination = new_dest_buf;
            }
            // Logic kiểm tra file đích khác tương tự cp.c

            if (verbose) {
                printf("Moving from remote %s to local %s\n", remote_path, actual_destination);
            }

            result = sftp_move_remote_to_local(remote_path, actual_destination);
            if (result != 0) {
                fprintf(stderr, "Error moving file: %s\n", strerror(-result));
                result = 1; // Đánh dấu lỗi
            } else {
                 result = 0; // Thành công
            }
        }
    } else if (!source_is_remote && dest_is_remote) {
        // --- Move từ Local về Remote ---
        char remote_path[PATH_MAX];
        if (get_remote_path(destination, remote_path, sizeof(remote_path), verbose) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path for destination %s\n", destination);
            result = 1;
        } else {
             // Kiểm tra nguồn local trước
             struct stat source_st;
             if (stat(source, &source_st) != 0) {
                 fprintf(stderr, "Error: Cannot stat local source %s: %s\n", source, strerror(errno));
                 result = 1;
                 goto cleanup;
             }
             // Không cho phép di chuyển thư mục (chưa hỗ trợ)
             if (S_ISDIR(source_st.st_mode)) {
                 fprintf(stderr, "Error: Moving directories from local to remote is not currently supported by remote-mv.\n");
                 fprintf(stderr, "Use 'remote-cp -r' instead.\n");
                 result = 1;
                 goto cleanup;
             }


            LIBSSH2_SFTP_ATTRIBUTES attrs;
            int is_remote_dir = 0;
            int stat_rc = sftp_stat_remote(remote_path, &attrs);
             if (stat_rc == 0) {
                 is_remote_dir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
             } else {
                 remote_conn_info_t *conn = get_conn_info();
                 unsigned long sftp_err = (conn && conn->sftp_session) ? libssh2_sftp_last_error(conn->sftp_session) : 0;
                 if (sftp_error_to_errno(sftp_err) != ENOENT) {
                     fprintf(stderr, "Error: Cannot stat remote destination %s (sftp_err: %lu)\n", remote_path, sftp_err);
                     result = 1;
                     goto cleanup;
                 }
                 // Nếu không tồn tại, is_remote_dir = 0
             }


            char final_remote_path_buf[PATH_MAX]; // Đổi tên buffer
            const char *final_remote_path = remote_path; // Mặc định

            if (is_remote_dir) {
                // Đích là thư mục remote, tạo đường dẫn cuối cùng bên trong nó
                char *source_basename_dup = strdup(source);
                if (!source_basename_dup) { perror("strdup"); result = 1; goto cleanup; }
                char *source_basename = basename(source_basename_dup);
                int remote_len = strlen(remote_path);

                if (remote_len > 0 && remote_path[remote_len - 1] == '/') {
                     int ret = snprintf(final_remote_path_buf, sizeof(final_remote_path_buf), "%s%s", remote_path, source_basename);
                      if (ret < 0 || (size_t)ret >= sizeof(final_remote_path_buf)) {
                          fprintf(stderr, "Error: Final remote path string too long.\n");
                          free(source_basename_dup);
                          result = 1;
                          goto cleanup;
                      }
                } else {
                     int ret = snprintf(final_remote_path_buf, sizeof(final_remote_path_buf), "%s/%s", remote_path, source_basename);
                      if (ret < 0 || (size_t)ret >= sizeof(final_remote_path_buf)) {
                          fprintf(stderr, "Error: Final remote path string too long.\n");
                          free(source_basename_dup);
                          result = 1;
                          goto cleanup;
                      }
                }
                free(source_basename_dup);
                final_remote_path = final_remote_path_buf; // Sử dụng buffer
            }
            // Nếu is_remote_dir = 0, final_remote_path giữ nguyên

            if (verbose) {
                printf("Moving from local %s to remote %s\n", source, final_remote_path);
            }

            // Đảm bảo đích remote không phải là thư mục nếu nguồn là file
            if (is_remote_dir && !S_ISDIR(source_st.st_mode)) {
                 // Trường hợp này đã được xử lý bằng cách tạo final_remote_path bên trong thư mục
            } else if (is_remote_dir && S_ISDIR(source_st.st_mode)) {
                 // Đã chặn ở trên
            } else if (!is_remote_dir && S_ISDIR(source_st.st_mode)) {
                 // Đã chặn ở trên
            } else if (!is_remote_dir && !S_ISDIR(source_st.st_mode) && stat_rc == 0) {
                 // Cả hai đều là file, ghi đè
            }


            result = sftp_move_local_to_remote(source, final_remote_path);
            if (result != 0) {
                fprintf(stderr, "Error moving file: %s\n", strerror(-result));
                result = 1; // Đánh dấu lỗi
            } else {
                 result = 0; // Thành công
            }
        }
    } else {
         fprintf(stderr, "Error: Invalid combination of source/destination types.\n");
         result = 1;
    }

cleanup:
    // Đóng kết nối SSH nếu đã mở
    if (ssh_cli_conn) {
        sftp_disconnect(ssh_cli_conn);

        // Free all connection resources
        free(ssh_cli_conn->remote_host);
        free(ssh_cli_conn->remote_user);
        free(ssh_cli_conn->remote_pass);
        free(ssh_cli_conn->ssh_key_path);
        free(ssh_cli_conn->remote_proc_path);

        free(ssh_cli_conn);
        ssh_cli_conn = NULL;
        if (verbose) {
            printf("SFTP connection closed.\n");
        }
    }

    // ---> DỌN DẸP LIBSSH2 <---
    libssh2_exit();
    return result; // Trả về 0 nếu thành công, 1 nếu lỗi
}
