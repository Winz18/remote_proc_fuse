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
#include <errno.h> // For errno

// Global pointer for CLI tools
remote_conn_info_t *ssh_cli_conn = NULL;

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

// Kiểm tra xem một đường dẫn có thuộc một mount point của remotefs hay không
// Trả về mount point nếu tìm thấy, NULL nếu không tìm thấy
const char* is_remote_path(const char *path, char *mount_point_buf, size_t buf_size) {
    // Chuẩn hóa đường dẫn thành đường dẫn tuyệt đối
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path) == NULL) {
        // Không thể chuyển đổi thành đường dẫn tuyệt đối, có thể file không tồn tại
        strncpy(abs_path, path, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = '\0';
    }
    
    // Lấy danh sách các mount point từ file cấu hình
    int count = 0;
    char **mount_points = get_mount_points(&count);
    if (!mount_points || count == 0) {
        return NULL;
    }
    
    // Kiểm tra xem path có bắt đầu bằng một trong các mount point không
    for (int i = 0; i < count; i++) {
        size_t len = strlen(mount_points[i]);
        if (strncmp(abs_path, mount_points[i], len) == 0 && 
            (abs_path[len] == '/' || abs_path[len] == '\0')) {
            // Path nằm trong mount point
            if (mount_point_buf && buf_size > 0) {
                strncpy(mount_point_buf, mount_points[i], buf_size - 1);
                mount_point_buf[buf_size - 1] = '\0';
            }
            
            // Giải phóng mảng mount points
            for (int j = 0; j < count; j++) {
                free(mount_points[j]);
            }
            free(mount_points);
            
            return mount_point_buf;
        }
    }
    
    // Giải phóng mảng mount points
    for (int i = 0; i < count; i++) {
        free(mount_points[i]);
    }
    free(mount_points);
    
    // Không thuộc mount point nào
    return NULL;
}

// Lấy đường dẫn trên remote từ đường dẫn cục bộ
int get_remote_path(const char *path, char *remote_path, size_t size) {
    char mount_point[PATH_MAX];
    
    // Kiểm tra xem path có thuộc mount point nào không
    if (!is_remote_path(path, mount_point, sizeof(mount_point))) {
        return -1;
    }
    
    // Lấy remote path tương ứng với mount point
    char *base_remote_path = get_remote_path_for_mount(mount_point);
    if (!base_remote_path) {
        fprintf(stderr, "Error: Cannot determine remote path for mount point: %s\n", mount_point);
        return -1;
    }
    
    // Tính đường dẫn tương đối từ mount point
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path) == NULL) {
        strncpy(abs_path, path, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = '\0';
    }
    
    // Bỏ qua phần mount point
    const char *rel_path = abs_path + strlen(mount_point);
    
    // Nếu rel_path không bắt đầu bằng '/', thêm vào
    if (rel_path[0] != '/' && strlen(rel_path) > 0) {
        snprintf(remote_path, size, "%s/%s", base_remote_path, rel_path);
    } else {
        snprintf(remote_path, size, "%s%s", base_remote_path, rel_path);
    }
    
    // Xử lý trường hợp base_remote_path kết thúc bằng '/'
    if (strlen(base_remote_path) > 0 && base_remote_path[strlen(base_remote_path) - 1] == '/' && 
        rel_path[0] == '/') {
        // Có hai dấu '/' liên tiếp, cần loại bỏ một
        char *double_slash = strstr(remote_path, "//");
        if (double_slash) {
            memmove(double_slash, double_slash + 1, strlen(double_slash));
        }
    }
    
    free(base_remote_path);
    return 0;
}

int main(int argc, char *argv[]) {
    int verbose = 0;
    
    // Xử lý các tùy chọn
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
                return 0;
            case 'v':
                verbose = 1;
                break;
            default:
                show_usage(argv[0]);
                return 1;
        }
    }
    
    // Kiểm tra đối số còn lại
    if (optind + 2 != argc) {
        fprintf(stderr, "Error: Expected source and destination arguments\n");
        show_usage(argv[0]);
        return 1;
    }
    
    char *source = argv[optind];
    char *destination = argv[optind + 1];
    
    if (verbose) {
        printf("Source: %s\n", source);
        printf("Destination: %s\n", destination);
    }
    
    // Kiểm tra xem source và destination có phải là đường dẫn từ xa không
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
    
    // Determine which mount point configuration to use
    const char *relevant_mount_point = NULL;
    if (source_is_remote) {
        relevant_mount_point = source_mount_point;
    } else if (dest_is_remote) {
        relevant_mount_point = dest_mount_point;
    }

    // Initialize connection if needed
    if (relevant_mount_point) {
        ssh_cli_conn = calloc(1, sizeof(remote_conn_info_t));
        if (!ssh_cli_conn) {
            perror("Failed to allocate memory for connection info");
            return 1;
        }

        // TODO: Implement loading connection details from config based on relevant_mount_point
        //       Similar to the TODO in cp.c
        //       Need: load_connection_info_for_mount(relevant_mount_point, ssh_cli_conn);
        ssh_cli_conn->remote_proc_path = get_remote_path_for_mount(relevant_mount_point); // Corrected member name
         if (!ssh_cli_conn->remote_proc_path) { // Corrected member name
            fprintf(stderr, "Error: Cannot determine remote base path for mount point: %s\n", relevant_mount_point);
            free(ssh_cli_conn);
            ssh_cli_conn = NULL;
            return 1;
        }
        fprintf(stderr, "Warning: Connection details (host, user, port) are not loaded from config. SFTP connection will likely fail.\n");
        // TODO: Implement config loading for connection details.

        if (sftp_connect_and_auth(ssh_cli_conn) != 0) {
            fprintf(stderr, "Error: Failed to connect and authenticate SFTP session for %s\n", relevant_mount_point);
            free(ssh_cli_conn->remote_proc_path); // Corrected member name
            free(ssh_cli_conn);
            ssh_cli_conn = NULL;
            return 1;
        }
        if (verbose) {
             printf("SFTP connection established for mount point %s\n", relevant_mount_point);
        }
    }

    // Xử lý các trường hợp
    int result = 1; // Default to error
    if (source_is_remote && dest_is_remote) {
        // Cả nguồn và đích đều là remote - không hỗ trợ trực tiếp
        fprintf(stderr, "Error: Cannot move directly between two remote locations\n");
        result = 1;
    } else if (!source_is_remote && !dest_is_remote) {
        // Cả nguồn và đích đều là local - sử dụng mv thông thường
        fprintf(stderr, "Note: Both paths are local, using system mv\n");
        
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "/bin/mv %s %s", source, destination);
        result = system(cmd);
    } else if (source_is_remote && !dest_is_remote) {
        // Di chuyển từ remote về local
        char remote_path[PATH_MAX];
        if (get_remote_path(source, remote_path, sizeof(remote_path)) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path\n");
            result = 1;
        } else {
            // Xử lý trường hợp destination là thư mục
            struct stat st;
            char new_dest_buf[PATH_MAX]; // Use a separate buffer
            char *actual_destination = destination; // Pointer to the final destination path
            if (stat(destination, &st) == 0 && S_ISDIR(st.st_mode)) {
                char *source_basename_dup = strdup(source);
                if (!source_basename_dup) { perror("strdup"); result = 1; goto cleanup; }
                char *source_basename = basename(source_basename_dup);
                snprintf(new_dest_buf, sizeof(new_dest_buf), "%s/%s", destination, source_basename);
                free(source_basename_dup);
                actual_destination = new_dest_buf; // Use the new path
            }
            
            if (verbose) {
                printf("Moving from remote %s to local %s\n", remote_path, actual_destination);
            }
            
            // Kết nối tới SFTP và di chuyển
            result = sftp_move_remote_to_local(remote_path, actual_destination);
        }
    } else if (!source_is_remote && dest_is_remote) {
        // Di chuyển từ local lên remote
        char remote_path[PATH_MAX];
        if (get_remote_path(destination, remote_path, sizeof(remote_path)) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path\n");
            result = 1;
        } else {
            // Xử lý trường hợp destination là thư mục remote
            // We need to check if the *remote* destination is a directory.
            // This requires an SFTP stat call.
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            int is_dir = 0;
            if (sftp_stat_remote(remote_path, &attrs) == 0) {
                if (LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
                    is_dir = 1;
                }
            } // Ignore error, maybe it doesn't exist yet which is fine for mv

            char final_remote_path[PATH_MAX];
            strncpy(final_remote_path, remote_path, sizeof(final_remote_path) -1);
            final_remote_path[sizeof(final_remote_path) -1] = '\0';

            if (is_dir) {
                // Append source filename to remote directory path
                char *source_basename_dup = strdup(source);
                 if (!source_basename_dup) { perror("strdup"); result = 1; goto cleanup; }
                char *source_basename = basename(source_basename_dup);
                // Ensure no double slash if remote_path ends with /
                int remote_len = strlen(final_remote_path);
                if (remote_len > 0 && final_remote_path[remote_len - 1] == '/') {
                     snprintf(final_remote_path, sizeof(final_remote_path), "%s%s", remote_path, source_basename);
                } else {
                     snprintf(final_remote_path, sizeof(final_remote_path), "%s/%s", remote_path, source_basename);
                }
                free(source_basename_dup);
            }
            
            if (verbose) {
                printf("Moving from local %s to remote %s\n", source, final_remote_path);
            }
            
            // Kết nối tới SFTP và di chuyển
            result = sftp_move_local_to_remote(source, final_remote_path);
        }
    } else {
         fprintf(stderr, "Error: Invalid combination of source/destination types.\n");
         result = 1;
    }

cleanup: // Label for cleanup before exit
    // Disconnect and clean up if connection was made
    if (ssh_cli_conn) {
        sftp_disconnect(ssh_cli_conn);
        free(ssh_cli_conn->remote_proc_path); // Corrected member name
        free(ssh_cli_conn);
        ssh_cli_conn = NULL;
        if (verbose) {
            printf("SFTP connection closed.\n");
        }
    }

    return result;
}