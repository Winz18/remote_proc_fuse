#ifndef REMOTE_PROC_FUSE_H
#define REMOTE_PROC_FUSE_H

#include "common.h"
#include <sys/stat.h> // struct stat
#include <string.h>   // strcmp, strdup
#include <stdlib.h>   // free
#include <errno.h>    // Error codes like ENOENT, EACCES

// Khởi tạo filesystem (kết nối SSH/SFTP)
void* rp_init(struct fuse_conn_info *conn, struct fuse_config *cfg);

// Hủy filesystem (ngắt kết nối SSH/SFTP)
void rp_destroy(void *private_data);

// Lấy thuộc tính file/thư mục
int rp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);

// Đọc nội dung thư mục
int rp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
               struct fuse_file_info *fi, enum fuse_readdir_flags flags);

// Tra cứu entry trong thư mục (tương tự getattr nhưng cho lookup)
// FUSE 3 không còn dùng lookup nhiều, getattr là chính. Nhưng vẫn nên có.
// int rp_lookup(const char* path, struct stat* stbuf); // FUSE 2 style
// FUSE 3 style (nếu cần, nhưng getattr thường đủ)
// int rp_lookup(const char *parent, const char *name, struct fuse_entry_param *e);

// Mở file
int rp_open(const char *path, struct fuse_file_info *fi);

// Đọc file
int rp_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi);

// Đóng/Release file handle
int rp_release(const char *path, struct fuse_file_info *fi);

// Các thao tác khác (không hỗ trợ)
int rp_access(const char *path, int mask);

// Các thao tác ghi, tạo, xóa file/thư mục
int rp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int rp_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int rp_unlink(const char *path);
int rp_mkdir(const char *path, mode_t mode);
int rp_rmdir(const char *path);
int rp_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int rp_rename(const char *from, const char *to, unsigned int flags);

#endif // REMOTE_PROC_FUSE_H