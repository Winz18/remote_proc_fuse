#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <pwd.h>

// Hàm lấy đường dẫn đến thư mục cấu hình của user
char *get_config_dir() {
    const char *home_dir;
    
    // Thử lấy từ biến môi trường HOME
    home_dir = getenv("HOME");
    
    // Nếu không có, thử lấy từ password database
    if (!home_dir) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            home_dir = pw->pw_dir;
        }
    }
    
    // Nếu vẫn không tìm được thì trả về NULL
    if (!home_dir) {
        return NULL;
    }
    
    // Tạo đường dẫn đến thư mục cấu hình
    char *config_dir = malloc(strlen(home_dir) + 20); // 20 đủ cho "/.config/remotefs"
    if (!config_dir) {
        return NULL;
    }
    
    sprintf(config_dir, "%s/.config", home_dir);
    
    // Kiểm tra và tạo thư mục .config nếu chưa tồn tại
    struct stat st;
    if (stat(config_dir, &st) == -1) {
        if (mkdir(config_dir, 0700) == -1) {
            free(config_dir);
            return NULL;
        }
    }
    
    // Thêm thư mục remotefs
    sprintf(config_dir, "%s/.config/remotefs", home_dir);
    if (stat(config_dir, &st) == -1) {
        if (mkdir(config_dir, 0700) == -1) {
            free(config_dir);
            return NULL;
        }
    }
    
    return config_dir;
}

// Hàm lưu thông tin mount point vào file cấu hình
int save_mount_point(const char *mount_point, const char *remote_path) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        LOG_ERR("Failed to get config directory");
        return -1;
    }
    
    // Tạo đường dẫn đến file cấu hình
    char *config_file = malloc(strlen(config_dir) + 20); // 20 đủ cho "/mounts.conf"
    if (!config_file) {
        free(config_dir);
        return -1;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    // Mở file để ghi
    FILE *fp = fopen(config_file, "a");
    if (!fp) {
        LOG_ERR("Failed to open config file for writing: %s", config_file);
        free(config_dir);
        free(config_file);
        return -1;
    }
    
    // Ghi thông tin mount point và remote path
    fprintf(fp, "%s:%s\n", mount_point, remote_path);
    
    // Đóng file
    fclose(fp);
    
    LOG_INFO("Saved mount point: %s -> %s", mount_point, remote_path);
    
    free(config_dir);
    free(config_file);
    return 0;
}

// Hàm lấy danh sách mount point từ file cấu hình
char **get_mount_points(int *count) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        *count = 0;
        return NULL;
    }
    
    // Tạo đường dẫn đến file cấu hình
    char *config_file = malloc(strlen(config_dir) + 20);
    if (!config_file) {
        free(config_dir);
        *count = 0;
        return NULL;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    // Kiểm tra nếu file không tồn tại
    if (access(config_file, F_OK) == -1) {
        free(config_dir);
        free(config_file);
        *count = 0;
        return NULL;
    }
    
    // Mở file để đọc
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        LOG_ERR("Failed to open config file for reading: %s", config_file);
        free(config_dir);
        free(config_file);
        *count = 0;
        return NULL;
    }
    
    // Đếm số dòng trong file
    char line[1024];
    int line_count = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_count++;
    }
    
    // Quay lại đầu file
    rewind(fp);
    
    // Cấp phát mảng lưu các mount point
    char **mount_points = malloc(sizeof(char *) * (line_count + 1)); // +1 cho NULL terminator
    if (!mount_points) {
        fclose(fp);
        free(config_dir);
        free(config_file);
        *count = 0;
        return NULL;
    }
    
    // Đọc các mount point từ file
    int idx = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Loại bỏ ký tự newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        // Bỏ qua dòng trống
        if (strlen(line) == 0) continue;
        
        // Tách mount point và remote path
        char *sep = strchr(line, ':');
        if (!sep) continue;
        
        *sep = '\0'; // Chia dòng thành 2 phần
        
        // Lưu mount point
        mount_points[idx] = strdup(line);
        if (!mount_points[idx]) {
            // Lỗi cấp phát bộ nhớ, giải phóng tất cả
            for (int i = 0; i < idx; i++) {
                free(mount_points[i]);
            }
            free(mount_points);
            fclose(fp);
            free(config_dir);
            free(config_file);
            *count = 0;
            return NULL;
        }
        
        idx++;
    }
    
    // Thêm NULL terminator
    mount_points[idx] = NULL;
    *count = idx;
    
    // Đóng file và giải phóng bộ nhớ
    fclose(fp);
    free(config_dir);
    free(config_file);
    
    return mount_points;
}

// Hàm lấy remote path tương ứng với mount point
char *get_remote_path_for_mount(const char *mount_point) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return NULL;
    }
    
    // Tạo đường dẫn đến file cấu hình
    char *config_file = malloc(strlen(config_dir) + 20);
    if (!config_file) {
        free(config_dir);
        return NULL;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    // Kiểm tra nếu file không tồn tại
    if (access(config_file, F_OK) == -1) {
        free(config_dir);
        free(config_file);
        return NULL;
    }
    
    // Mở file để đọc
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        LOG_ERR("Failed to open config file for reading: %s", config_file);
        free(config_dir);
        free(config_file);
        return NULL;
    }
    
    // Tìm mount point trong file
    char line[1024];
    char *remote_path = NULL;
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Loại bỏ ký tự newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        // Tách mount point và remote path
        char *sep = strchr(line, ':');
        if (!sep) continue;
        
        *sep = '\0'; // Chia dòng thành 2 phần
        
        // Kiểm tra nếu mount point khớp
        if (strcmp(line, mount_point) == 0) {
            remote_path = strdup(sep + 1);
            break;
        }
    }
    
    // Đóng file và giải phóng bộ nhớ
    fclose(fp);
    free(config_dir);
    free(config_file);
    
    return remote_path;
}

// Hàm xóa mount point khỏi file cấu hình
int remove_mount_point(const char *mount_point) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return -1;
    }
    
    // Tạo đường dẫn đến file cấu hình
    char *config_file = malloc(strlen(config_dir) + 20);
    if (!config_file) {
        free(config_dir);
        return -1;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    // Kiểm tra nếu file không tồn tại
    if (access(config_file, F_OK) == -1) {
        free(config_dir);
        free(config_file);
        return 0; // Không có lỗi nếu file không tồn tại
    }
    
    // Mở file để đọc
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        LOG_ERR("Failed to open config file for reading: %s", config_file);
        free(config_dir);
        free(config_file);
        return -1;
    }
    
    // Tạo file tạm để ghi
    char *temp_file = malloc(strlen(config_file) + 5);
    if (!temp_file) {
        fclose(fp);
        free(config_dir);
        free(config_file);
        return -1;
    }
    sprintf(temp_file, "%s.tmp", config_file);
    
    FILE *fp_tmp = fopen(temp_file, "w");
    if (!fp_tmp) {
        LOG_ERR("Failed to open temp file for writing: %s", temp_file);
        fclose(fp);
        free(config_dir);
        free(config_file);
        free(temp_file);
        return -1;
    }
    
    // Sao chép các dòng không chứa mount point
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char line_copy[1024];
        strcpy(line_copy, line);
        
        // Loại bỏ ký tự newline cho việc so sánh
        char *nl = strchr(line_copy, '\n');
        if (nl) *nl = '\0';
        
        // Tách mount point và remote path
        char *sep = strchr(line_copy, ':');
        if (!sep) {
            // Dòng không đúng định dạng, vẫn sao chép
            fputs(line, fp_tmp);
            continue;
        }
        
        *sep = '\0'; // Chia dòng thành 2 phần
        
        // Nếu không phải mount point cần xóa, sao chép
        if (strcmp(line_copy, mount_point) != 0) {
            fputs(line, fp_tmp);
        }
    }
    
    // Đóng các file
    fclose(fp);
    fclose(fp_tmp);
    
    // Thay thế file cũ bằng file mới
    if (rename(temp_file, config_file) != 0) {
        LOG_ERR("Failed to rename temp file");
        free(config_dir);
        free(config_file);
        free(temp_file);
        return -1;
    }
    
    LOG_INFO("Removed mount point: %s", mount_point);
    
    free(config_dir);
    free(config_file);
    free(temp_file);
    return 0;
}