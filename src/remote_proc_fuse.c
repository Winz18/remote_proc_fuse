#include "remote_proc_fuse.h"
#include "ssh_sftp_client.h"
#include <stdio.h> // snprintf

// --- Helper Function ---
// Tạo đường dẫn đầy đủ trên máy remote từ đường dẫn FUSE
static char* build_remote_path(const char *fuse_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn) return NULL;

    // Kích thước buffer đủ lớn: /proc + fuse_path + null terminator
    size_t len = strlen(conn->remote_proc_path) + strlen(fuse_path) + 1;
    char *remote_path = malloc(len);
    if (!remote_path) {
        LOG_ERR("Failed to allocate memory for remote path");
        return NULL;
    }
    snprintf(remote_path, len, "%s%s", conn->remote_proc_path, fuse_path);
    return remote_path;
}

// --- FUSE Callbacks Implementation ---

void* rp_init(struct fuse_conn_info *conn_info, struct fuse_config *cfg) {
    LOG_INFO("Initializing Remote Proc Filesystem...");
    remote_conn_info_t *conn = get_conn_info(); // Lấy từ private_data đã set trong main
    if (!conn) {
        LOG_ERR("Connection info not found in private data!");
        return NULL; // Hoặc exit?
    }

    cfg->use_ino = 1; // Sử dụng inode numbers (giúp ls -i hoạt động)
    // cfg->nullpath_ok = 1; // Cho phép path là NULL (có thể cần cho một số op?)

    // cfg->kernel_cache = 0; // Tắt cache kernel nếu cần

    // Đặt timeout trong cấu trúc config thay vì conn_info (FUSE 3)
    cfg->attr_timeout = 1.0; // Cache thuộc tính trong 1 giây
    cfg->entry_timeout = 1.0; // Cache tên file/dir trong 1 giây
    cfg->use_ino = 1; // Sử dụng inode numbers (giúp ls -i hoạt động)

    if (sftp_connect_and_auth(conn) != 0) {
        LOG_ERR("Failed to connect to remote host during init.");
        // Không thể return lỗi từ init, FUSE sẽ tự unmount?
        // Hoặc phải exit? Tạm thời cứ để FUSE xử lý.
        return conn; // Vẫn trả về con trỏ dù lỗi, destroy sẽ dọn dẹp?
    }

    LOG_INFO("Remote Proc Filesystem Initialized Successfully.");
    return conn; // Trả về con trỏ data để FUSE lưu lại
}

void rp_destroy(void *private_data) {
    LOG_INFO("Destroying Remote Proc Filesystem...");
    remote_conn_info_t *conn = (remote_conn_info_t*)private_data;
    if (conn) {
        sftp_disconnect(conn);
        // Giải phóng các thành phần trong conn nếu đã malloc
        free(conn->remote_host);
        free(conn->remote_user);
        free(conn->remote_pass); // free nếu đã strdup
        free(conn->ssh_key_path); // free nếu đã strdup
        // Không free(conn) vì nó được cấp phát trên stack trong main
        // Nếu bạn cấp phát động conn trong main thì phải free ở đây
    }
    LOG_INFO("Remote Proc Filesystem Destroyed.");
}

int rp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi; // Không dùng fi trong getattr cơ bản
    LOG_DEBUG("getattr: %s", path);
    memset(stbuf, 0, sizeof(struct stat));

    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = sftp_stat_remote(remote_path, &attrs);
    free(remote_path);

    if (rc != 0) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err); // <<< SỬ DỤNG HELPER
        LOG_DEBUG("getattr: sftp_stat_remote failed for %s, rc=%d, sftp_err=%lu -> errno=%d", path, rc, sftp_err, err);
        // Trả về mã lỗi đã ánh xạ, hoặc EIO nếu helper trả về 0 (không nên xảy ra khi rc!=0)
        return -err ? -err : -EIO;
    }

    // --- Chuyển đổi SFTP attributes sang struct stat ---
    if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        stbuf->st_mode = attrs.permissions; // Giữ nguyên mode từ remote
         // Chỉ cho phép đọc cho tất cả (an toàn hơn)
         // stbuf->st_mode = (attrs.permissions & S_IFMT) | 0444;
         // if (S_ISDIR(attrs.permissions)) {
         //     stbuf->st_mode |= 0111; // Cho phép execute (truy cập) thư mục
         // }
    } else {
        // Mặc định nếu không có permission
        if (strcmp(path, "/") == 0) { // Thư mục gốc
             stbuf->st_mode = S_IFDIR | 0555; // r-xr-xr-x
        } else {
             stbuf->st_mode = S_IFREG | 0444; // r--r--r--
        }
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID) {
        stbuf->st_uid = attrs.uid;
        stbuf->st_gid = attrs.gid;
        // Nên đặt uid/gid của người dùng mount FUSE để tránh vấn đề quyền
        // stbuf->st_uid = getuid();
        // stbuf->st_gid = getgid();
    } else {
        // Đặt uid/gid của người dùng mount FUSE
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
    }

    if (S_ISDIR(stbuf->st_mode)) {
        stbuf->st_nlink = 2; // Thư mục thường có ít nhất 2 link (. và ..)
    } else {
        stbuf->st_nlink = 1;
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
        stbuf->st_size = attrs.filesize;
    
        // *** THAY ĐỔI LOGIC XỬ LÝ SIZE 0 ***
        // Chỉ báo cáo size giả > 0 nếu là file thường, size thực là 0 VÀ
        // đường dẫn cơ sở là "/proc" mặc định.
        remote_conn_info_t *conn = get_conn_info(); // Lấy thông tin kết nối
        if (S_ISREG(stbuf->st_mode) && stbuf->st_size == 0 &&
            conn && strcmp(conn->remote_proc_path, "/proc") == 0)
        {
            LOG_DEBUG("getattr: Reporting non-zero size (4096) for zero-sized regular file under default /proc path: %s", path);
            stbuf->st_size = 4096; // Giữ nguyên mẹo cho /proc
        }
        // *** KẾT THÚC THAY ĐỔI ***
    
    } else {
        // Nếu không có thông tin size từ remote
        stbuf->st_size = 0; // Mặc định là 0
        // Có thể thêm logic tương tự trên nếu cần, nhưng ít xảy ra hơn
    }
    
    // Cập nhật số block dựa trên size mới
    stbuf->st_blksize = 4096;
    stbuf->st_blocks = (stbuf->st_size + stbuf->st_blksize - 1) / stbuf->st_blksize;

     // /proc files thường thay đổi liên tục, timestamps không quá quan trọng
    if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        stbuf->st_atime = attrs.atime;
        stbuf->st_mtime = attrs.mtime;
        // st_ctime (change time) không có trong SFTP attrs, có thể để giống mtime
        stbuf->st_ctime = attrs.mtime;
    } else {
        // Đặt thời gian hiện tại nếu không có
        time_t now = time(NULL);
        stbuf->st_atime = now;
        stbuf->st_mtime = now;
        stbuf->st_ctime = now;
    }
    // st_blksize, st_blocks: Có thể để giá trị mặc định hoặc 0
    stbuf->st_blksize = 4096;
    stbuf->st_blocks = (stbuf->st_size + stbuf->st_blksize -1) / stbuf->st_blksize;


    LOG_DEBUG("getattr OK for %s (mode: %o, size: %ld)", path, stbuf->st_mode, stbuf->st_size);
    return 0; // Thành công
}


int rp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
               struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    (void) offset; // Offset thường không dùng trong readdir đơn giản
    (void) fi;
    (void) flags;
    LOG_DEBUG("readdir: %s", path);

    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;

    LIBSSH2_SFTP_HANDLE *handle = sftp_opendir_remote(remote_path);
    free(remote_path); // Không cần nữa

    if (!handle) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err); // <<< SỬ DỤNG HELPER

        LOG_ERR("readdir: sftp_opendir_remote failed for path '%s', sftp_err=%lu -> errno=%d",
                path, sftp_err, err);

        // Xử lý thêm: Nếu lỗi là không hỗ trợ (ENOSYS) hoặc lỗi chung (EIO),
        // có khả năng cao là do cố gắng mở file như thư mục. Trả về ENOTDIR.
        // Các lỗi như ENOENT, EACCES đã được helper xử lý đúng.
        if (err == ENOSYS || err == EIO) {
             // Để chắc chắn hơn, có thể gọi lại getattr để kiểm tra type, nhưng
             // tạm thời trả về ENOTDIR dựa trên ngữ cảnh opendir thất bại
             LOG_ERR("readdir: Assuming path '%s' is not a directory.", path);
             return -ENOTDIR; // Not a directory
        }

        // Trả về mã lỗi đã ánh xạ (hoặc EIO nếu không xác định được)
        return -err ? -err : -EIO;
    }

    // Thêm "." và ".." vào kết quả
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    char entry_buffer[512]; // Buffer đủ lớn cho tên file/dir trong /proc
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc;

    while (1) {
        rc = sftp_readdir_remote(handle, entry_buffer, sizeof(entry_buffer), &attrs);
        if (rc == 0) { // Hết entry
            break;
        } else if (rc < 0) { // Lỗi đọc dir
            LOG_ERR("readdir: Error reading remote directory %s", path);
            sftp_closedir_remote(handle);
            return -EIO;
        } else { // Tìm thấy entry
            // Bỏ qua "." và ".." từ remote (vì đã tự thêm)
            if (strcmp(entry_buffer, ".") == 0 || strcmp(entry_buffer, "..") == 0) {
                continue;
            }
            LOG_DEBUG("readdir: adding entry '%s'", entry_buffer);
            // Lấy struct stat (nếu cần và không quá chậm) để filler có thông tin tốt hơn
            // Hoặc chỉ cần truyền NULL cho stbuf và 0 cho flags của filler
            // struct stat st;
            // memset(&st, 0, sizeof(st));
            // if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
            //     st.st_mode = attrs.permissions;
            // }
            // filler(buf, entry_buffer, &st, 0, 0);
            filler(buf, entry_buffer, NULL, 0, 0); // Đơn giản hơn, kernel sẽ gọi getattr sau nếu cần
        }
    }

    sftp_closedir_remote(handle);
    LOG_DEBUG("readdir OK for %s", path);
    return 0; // Thành công
}

int rp_open(const char *path, struct fuse_file_info *fi) {
    LOG_DEBUG("open: %s (flags: 0x%x)", path, fi->flags);

    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;

    // Mở file trên remote với các flag thích hợp
    LIBSSH2_SFTP_HANDLE *handle;
    
    // Kiểm tra nếu là mở để ghi hoặc đọc+ghi
    if ((fi->flags & O_ACCMODE) == O_WRONLY || (fi->flags & O_ACCMODE) == O_RDWR) {
        unsigned long sftp_flags = 0;
        
        // Chuyển đổi flags từ POSIX sang SFTP
        if ((fi->flags & O_ACCMODE) == O_WRONLY)
            sftp_flags |= LIBSSH2_FXF_WRITE;
        else if ((fi->flags & O_ACCMODE) == O_RDWR)
            sftp_flags |= LIBSSH2_FXF_READ | LIBSSH2_FXF_WRITE;
            
        // Các flags khác
        if (fi->flags & O_APPEND)
            sftp_flags |= LIBSSH2_FXF_APPEND;
        
        // Không thêm O_CREAT/O_TRUNC ở đây vì FUSE sẽ gọi create() trước khi mở
        
        handle = sftp_open_remote(remote_path, sftp_flags, 0);
    } else {
        // Mở file chỉ đọc (giữ nguyên code cũ)
        handle = sftp_open_remote(remote_path, fi->flags, 0);
    }
    
    free(remote_path);

    if (!handle) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
   
        // Xử lý thêm trường hợp đặc biệt: nếu lỗi là EACCES nhưng cố mở thư mục -> EISDIR
        if (err == EACCES || err == EINVAL) {
            // Thử stat lại để chắc chắn đó là thư mục
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            char *r_path_stat = build_remote_path(path);
            if(r_path_stat && sftp_stat_remote(r_path_stat, &attrs) == 0 && S_ISDIR(attrs.permissions)) {
                free(r_path_stat);
                LOG_ERR("open: Attempted to open a directory: %s", path);
                return -EISDIR; // Là thư mục
            }
            free(r_path_stat);
        }
        LOG_ERR("open: sftp_open_remote failed for %s, sftp_err=%lu -> errno=%d", path, sftp_err, err);
        return -err ? -err : -EIO;
    }

    // Lưu handle vào fi->fh để dùng trong read/write/release
    fi->fh = (uint64_t)handle;
    LOG_DEBUG("open OK for %s, handle stored: %p", path, handle);

    return 0; // Thành công
}

int rp_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    LOG_DEBUG("create: %s (mode: %o)", path, mode);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
    // Tạo file mới trên remote
    LIBSSH2_SFTP_HANDLE *handle = sftp_create_remote(remote_path, mode);
    free(remote_path);
    
    if (!handle) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("create: sftp_create_remote failed for %s, sftp_err=%lu -> errno=%d", path, sftp_err, err);
        return -err ? -err : -EIO;
    }
    
    // Lưu handle vào fi->fh để dùng trong write/release
    fi->fh = (uint64_t)handle;
    LOG_DEBUG("create OK for %s, handle stored: %p", path, handle);
    
    return 0; // Thành công
}

int rp_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    LOG_DEBUG("read: %s (size: %zu, offset: %ld)", path, size, offset);

    LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
    if (!handle) {
        LOG_ERR("read: Invalid SFTP handle for %s", path);
        return -EBADF; // Bad file descriptor
    }

    // /proc files thường được đọc từ đầu, nhưng SFTP hỗ trợ seek
    // Nếu offset != 0, cần seek trước khi đọc
    if (offset > 0) {
        LOG_DEBUG("read: Seeking to offset %ld", offset);
        libssh2_sftp_seek64(handle, offset);
        // Nên kiểm tra lỗi seek ở đây nếu cần
    } else if (offset == 0) {
         // Nếu offset là 0, đảm bảo seek về đầu file (phòng trường hợp đọc nhiều lần)
         libssh2_sftp_seek64(handle, 0);
    }


    ssize_t bytes_read = sftp_read_remote(handle, buf, size);

    if (bytes_read < 0) {
        LOG_ERR("read: sftp_read_remote failed for %s (error code: %zd)", path, bytes_read);
        // Chuyển đổi lỗi EIO, EAGAIN... từ sftp_read_remote
        return (int)bytes_read; // Trả về mã lỗi âm đã nhận
    }

    LOG_DEBUG("read OK for %s: %zd bytes read", path, bytes_read);
    return (int)bytes_read; // Trả về số byte đã đọc
}

int rp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    LOG_DEBUG("write: %s (size: %zu, offset: %ld)", path, size, offset);
    
    LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
    if (!handle) {
        LOG_ERR("write: Invalid SFTP handle for %s", path);
        return -EBADF; // Bad file descriptor
    }
    
    // Nếu offset > 0, cần seek trước khi ghi
    if (offset > 0) {
        LOG_DEBUG("write: Seeking to offset %ld", offset);
        libssh2_sftp_seek64(handle, offset);
    }
    
    ssize_t bytes_written = sftp_write_remote(handle, buf, size);
    
    if (bytes_written < 0) {
        LOG_ERR("write: sftp_write_remote failed for %s (error code: %zd)", path, bytes_written);
        return (int)bytes_written; // Trả về mã lỗi âm đã nhận
    }
    
    LOG_DEBUG("write OK for %s: %zd bytes written", path, bytes_written);
    return (int)bytes_written; // Trả về số byte đã ghi
}

int rp_release(const char *path, struct fuse_file_info *fi) {
    LOG_DEBUG("release: %s", path);
    LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
    if (handle) {
        LOG_DEBUG("release: Closing SFTP handle %p", handle);
        sftp_close_remote(handle);
        fi->fh = 0; // Đặt lại handle về 0
    } else {
         LOG_DEBUG("release: No SFTP handle to close for %s", path);
    }
    return 0; // Release thường không trả về lỗi
}

int rp_access(const char *path, int mask) {
     LOG_DEBUG("access: %s (mask: %d)", path, mask);
     
     // Gọi getattr để kiểm tra sự tồn tại và quyền cơ bản
     struct stat stbuf;
     int res = rp_getattr(path, &stbuf, NULL);
     if (res != 0) {
         return res; // Trả về lỗi từ getattr (ENOENT, EACCES, EIO...)
     }

     // Kiểm tra quyền dựa trên mask (R_OK, W_OK, X_OK)
     mode_t mode = stbuf.st_mode;
     uid_t uid = getuid(); // Người dùng chạy FUSE
     gid_t gid = getgid();

     if (mask == F_OK) return 0; // Chỉ kiểm tra tồn tại

     // Cho phép kiểm tra quyền ghi - không từ chối mặc định
     if (mask & W_OK) {
         if (!((mode & S_IWUSR) && (stbuf.st_uid == uid)) &&
             !((mode & S_IWGRP) && (stbuf.st_gid == gid)) && 
             !((mode & S_IWOTH))) {
              LOG_DEBUG("access: Write permission denied for %s", path);
              return -EACCES;
         }
     }

     if (mask & R_OK) {
         if (!((mode & S_IRUSR) && (stbuf.st_uid == uid)) &&
             !((mode & S_IRGRP) && (stbuf.st_gid == gid)) && 
             !((mode & S_IROTH))) {
              LOG_DEBUG("access: Read permission denied for %s", path);
              return -EACCES;
         }
     }

     if (mask & X_OK) {
         if (!S_ISDIR(mode)) {
             // Kiểm tra quyền execute cho file thường
             if (!((mode & S_IXUSR) && (stbuf.st_uid == uid)) &&
                 !((mode & S_IXGRP) && (stbuf.st_gid == gid)) &&
                 !((mode & S_IXOTH))) {
                 LOG_DEBUG("access: Execute permission denied for %s", path);
                 return -EACCES;
             }
         } else { 
             // Kiểm tra quyền execute cho thư mục (có thể cd vào)
             if (!((mode & S_IXUSR) && (stbuf.st_uid == uid)) &&
                 !((mode & S_IXGRP) && (stbuf.st_gid == gid)) &&
                 !((mode & S_IXOTH))) {
                 LOG_DEBUG("access: Directory access permission denied for %s", path);
                 return -EACCES;
             }
         }
     }

     return 0; // Có quyền truy cập theo mask yêu cầu
}

int rp_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    LOG_DEBUG("truncate: %s (size: %ld)", path, size);
    
    // Trường hợp 1: Truncate với file handle (ftruncate)
    if (fi && fi->fh) {
        LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
        
        // Đối với truncate với kích thước 0, chỉ cần đóng và mở lại file với cờ TRUNC
        if (size == 0) {
            // Lưu lại đường dẫn remote
            char *remote_path = build_remote_path(path);
            if (!remote_path) return -ENOMEM;
            
            // Đóng handle hiện tại
            sftp_close_remote(handle);
            fi->fh = 0;
            
            // Mở lại với cờ TRUNC
            remote_conn_info_t *conn = get_conn_info();
            unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC;
            
            LIBSSH2_SFTP_HANDLE *new_handle = libssh2_sftp_open_ex(
                conn->sftp_session, 
                remote_path, 
                strlen(remote_path), 
                flags, 
                0644, 
                LIBSSH2_SFTP_OPENFILE
            );
            
            free(remote_path);
            
            if (!new_handle) {
                LOG_ERR("truncate: Failed to reopen file with TRUNC flag: %s", path);
                return -EIO;
            }
            
            // Lưu handle mới vào fi->fh
            fi->fh = (uint64_t)new_handle;
            return 0;
        }
        
        // Xử lý truncate với kích thước không phải 0
        // 1. Cần đóng handle hiện tại
        // 2. Mở file để đọc
        // 3. Đọc nội dung cần giữ lại
        // 4. Mở lại file với cờ TRUNC
        // 5. Ghi lại nội dung đã đọc
        
        char *remote_path = build_remote_path(path);
        if (!remote_path) return -ENOMEM;
        
        // Đóng handle hiện tại
        sftp_close_remote(handle);
        fi->fh = 0;
        
        // Thực hiện truncate thông qua đường dẫn
        int result = sftp_truncate_remote(remote_path, size);
        free(remote_path);
        
        if (result != 0) {
            LOG_ERR("truncate (with handle): sftp_truncate_remote failed: %d", result);
            return -EIO;
        }
        
        // Mở lại file 
        remote_path = build_remote_path(path);
        if (!remote_path) return -ENOMEM;
        
        remote_conn_info_t *conn = get_conn_info();
        unsigned long open_flags = LIBSSH2_FXF_READ | LIBSSH2_FXF_WRITE;
        
        LIBSSH2_SFTP_HANDLE *new_handle = libssh2_sftp_open_ex(
            conn->sftp_session, 
            remote_path, 
            strlen(remote_path), 
            open_flags, 
            0644, 
            LIBSSH2_SFTP_OPENFILE
        );
        
        free(remote_path);
        
        if (!new_handle) {
            LOG_ERR("truncate: Failed to reopen file after truncate: %s", path);
            return -EIO;
        }
        
        // Lưu handle mới vào fi->fh
        fi->fh = (uint64_t)new_handle;
        return 0;
    }
    
    // Trường hợp 2: Truncate thông qua đường dẫn (không có file handle)
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
    int rc = sftp_truncate_remote(remote_path, size);
    free(remote_path);
    
    if (rc != 0) {
        LOG_ERR("truncate: sftp_truncate_remote failed for %s: %d", path, rc);
        return -EIO;
    }
    
    LOG_DEBUG("truncate OK for %s (new size: %ld)", path, size);
    return 0;
}

int rp_unlink(const char *path) {
    LOG_DEBUG("unlink: %s", path);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
    // Xóa file từ xa
    int rc = sftp_unlink_remote(remote_path);
    free(remote_path);
    
    if (rc != 0) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("unlink: sftp_unlink_remote failed for %s, sftp_err=%lu -> errno=%d", path, sftp_err, err);
        return -err ? -err : -EIO;
    }
    
    LOG_DEBUG("unlink OK for %s", path);
    return 0; // Thành công
}

int rp_mkdir(const char *path, mode_t mode) {
    LOG_DEBUG("mkdir: %s (mode: %o)", path, mode);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
    // Tạo thư mục từ xa
    int rc = sftp_mkdir_remote(remote_path, mode);
    free(remote_path);
    
    if (rc != 0) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("mkdir: sftp_mkdir_remote failed for %s, sftp_err=%lu -> errno=%d", path, sftp_err, err);
        return -err ? -err : -EIO;
    }
    
    LOG_DEBUG("mkdir OK for %s", path);
    return 0; // Thành công
}

int rp_rmdir(const char *path) {
    LOG_DEBUG("rmdir: %s", path);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
    // Xóa thư mục từ xa
    int rc = sftp_rmdir_remote(remote_path);
    free(remote_path);
    
    if (rc != 0) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("rmdir: sftp_rmdir_remote failed for %s, sftp_err=%lu -> errno=%d", path, sftp_err, err);
        return -err ? -err : -EIO;
    }
    
    LOG_DEBUG("rmdir OK for %s", path);
    return 0; // Thành công
}

int rp_rename(const char *from, const char *to, unsigned int flags) {
    LOG_DEBUG("rename: %s -> %s (flags: %u)", from, to, flags);
    
    // Kiểm tra flags nâng cao (FUSE 3)
    if (flags) {
        return -EINVAL; // Không hỗ trợ flags đặc biệt như RENAME_NOREPLACE, RENAME_EXCHANGE
    }
    
    char *remote_from = build_remote_path(from);
    if (!remote_from) return -ENOMEM;
    
    char *remote_to = build_remote_path(to);
    if (!remote_to) {
        free(remote_from);
        return -ENOMEM;
    }
    
    // libssh2 không có hàm rename, nhưng SFTP v3+ hỗ trợ
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) {
        free(remote_from);
        free(remote_to);
        return -ENOTCONN;
    }
    
    int rc = libssh2_sftp_rename_ex(conn->sftp_session, 
                                    remote_from, strlen(remote_from),
                                    remote_to, strlen(remote_to),
                                    LIBSSH2_SFTP_RENAME_OVERWRITE);
    
    free(remote_from);
    free(remote_to);
    
    if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("rename: libssh2_sftp_rename_ex failed, rc=%d, sftp_err=%lu -> errno=%d", rc, sftp_err, err);
        return -err ? -err : -EIO;
    }
    
    LOG_DEBUG("rename OK: %s -> %s", from, to);
    return 0; // Thành công
}

// --- Các hàm không hỗ trợ khác ---
