#ifndef MOUNT_CONFIG_H
#define MOUNT_CONFIG_H

// Hàm lấy đường dẫn đến thư mục cấu hình của user
char *get_config_dir();

// Hàm lưu thông tin mount point vào file cấu hình
int save_mount_point(const char *mount_point, const char *remote_path);

// Hàm lấy danh sách mount point từ file cấu hình
char **get_mount_points(int *count);

// Hàm lấy remote path tương ứng với mount point
char *get_remote_path_for_mount(const char *mount_point);

// Hàm xóa mount point khỏi file cấu hình
int remove_mount_point(const char *mount_point);

#endif // MOUNT_CONFIG_H