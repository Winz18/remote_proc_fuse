#include "remote_proc_fuse.h"
#include "ssh_sftp_client.h"
#include <libssh2_sftp.h>
#include <stdio.h>
#include <errno.h>
#include "common.h"

static char* build_remote_path(const char *fuse_path) {
    remote_conn_info_t *conn = get_conn_info();
    if (!conn) return NULL;

    size_t len = strlen(conn->remote_proc_path) + strlen(fuse_path) + 1;
    char *remote_path = malloc(len);
    if (!remote_path) {
        LOG_ERR("Failed to allocate memory for remote path");
        return NULL;
    }
    snprintf(remote_path, len, "%s%s", conn->remote_proc_path, fuse_path);
    return remote_path;
}

void* rp_init(struct fuse_conn_info *conn_info, struct fuse_config *cfg) {
    LOG_INFO("Initializing Remote Proc Filesystem...");
    remote_conn_info_t *conn = get_conn_info();
    if (!conn) {
        LOG_ERR("Connection info not found in private data!");
        return NULL;
    }

    // Enable FUSE kernel caching for attributes and directory entries.
    // These values specify how long (in seconds) the kernel should cache
    // this information before re-requesting it from our filesystem.
    // Adjust these values based on how frequently remote files/directories change.
    cfg->attr_timeout = 5.0;  // Cache attributes for 5 seconds
    cfg->entry_timeout = 5.0; // Cache directory entries (filenames) for 5 seconds
    cfg->negative_timeout = 1.0; // Cache negative lookups (file not found) for 1 second

    // Use inode numbers provided by the filesystem (requires SFTP server support for consistent IDs, which might not always be the case)
    // If experiencing issues with file identity, consider disabling this (cfg->use_ino = 0)
    cfg->use_ino = 1;

    // Optional: Enable kernel writeback caching for potentially better write performance.
    // Note: This introduces a small risk of data loss on crash if data hasn't been flushed.
    // To enable, add "-o writeback_cache" to the mount command.
    // cfg->writeback_cache = 1; // Uncomment if enabling via mount option is desired by default

    // Optional: Enable asynchronous reads for potentially better read performance.
    // To enable, add "-o async_read" to the mount command.
    // conn_info->async_read = 1; // Enable async reads

    if (sftp_connect_and_auth(conn) != 0) {
        LOG_ERR("Failed to connect to remote host during init.");
        // Return conn here allows destroy to be called for cleanup
        return conn;
    }

    LOG_INFO("Remote Proc Filesystem Initialized Successfully (Caching enabled: attr=%.1fs, entry=%.1fs).", cfg->attr_timeout, cfg->entry_timeout);
    return conn;
}

void rp_destroy(void *private_data) {
    LOG_INFO("Destroying Remote Proc Filesystem...");
    remote_conn_info_t *conn = (remote_conn_info_t*)private_data;
    if (conn) {
        sftp_disconnect(conn);
        free(conn->remote_host);
        free(conn->remote_user);
        free(conn->remote_pass);
        free(conn->ssh_key_path);
    }
    LOG_INFO("Remote Proc Filesystem Destroyed.");
}

int rp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void) fi;
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
        int err = sftp_error_to_errno(sftp_err);
        LOG_DEBUG("getattr: sftp_stat_remote failed for %s, rc=%d, sftp_err=%lu -> errno=%d", path, rc, sftp_err, err);
        return -err ? -err : -EIO;
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        stbuf->st_mode = attrs.permissions;
    } else {
        if (strcmp(path, "/") == 0) {
             stbuf->st_mode = S_IFDIR | 0555;
        } else {
             stbuf->st_mode = S_IFREG | 0444;
        }
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID) {
        stbuf->st_uid = attrs.uid;
        stbuf->st_gid = attrs.gid;
    } else {
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
    }

    if (S_ISDIR(stbuf->st_mode)) {
        stbuf->st_nlink = 2;
    } else {
        stbuf->st_nlink = 1;
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
        stbuf->st_size = attrs.filesize;
    
        remote_conn_info_t *conn = get_conn_info();
        if (S_ISREG(stbuf->st_mode) && stbuf->st_size == 0 &&
            conn && strcmp(conn->remote_proc_path, "/proc") == 0)
        {
            LOG_DEBUG("getattr: Reporting non-zero size (4096) for zero-sized regular file under default /proc path: %s", path);
            stbuf->st_size = 4096;
        }
    
    } else {
        stbuf->st_size = 0;
    }
    
    stbuf->st_blksize = 4096;
    stbuf->st_blocks = (stbuf->st_size + stbuf->st_blksize - 1) / stbuf->st_blksize;

    if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        stbuf->st_atime = attrs.atime;
        stbuf->st_mtime = attrs.mtime;
        stbuf->st_ctime = attrs.mtime;
    } else {
        time_t now = time(NULL);
        stbuf->st_atime = now;
        stbuf->st_mtime = now;
        stbuf->st_ctime = now;
    }
    stbuf->st_blksize = 4096;
    stbuf->st_blocks = (stbuf->st_size + stbuf->st_blksize -1) / stbuf->st_blksize;

    LOG_DEBUG("getattr OK for %s (mode: %o, size: %ld)", path, stbuf->st_mode, stbuf->st_size);
    return 0;
}

int rp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
               struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
    (void) offset;
    (void) fi;
    (void) flags;
    LOG_DEBUG("readdir: %s", path);

    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;

    LIBSSH2_SFTP_HANDLE *handle = sftp_opendir_remote(remote_path);
    free(remote_path);

    if (!handle) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);

        LOG_ERR("readdir: sftp_opendir_remote failed for path '%s', sftp_err=%lu -> errno=%d",
                path, sftp_err, err);

        if (err == ENOSYS || err == EIO) {
             LOG_ERR("readdir: Assuming path '%s' is not a directory.", path);
             return -ENOTDIR;
        }

        return -err ? -err : -EIO;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    char entry_buffer[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc;

    while (1) {
        rc = sftp_readdir_remote(handle, entry_buffer, sizeof(entry_buffer), &attrs);
        if (rc == 0) {
            break;
        } else if (rc < 0) {
            LOG_ERR("readdir: Error reading remote directory %s", path);
            sftp_closedir_remote(handle);
            return -EIO;
        } else {
            if (strcmp(entry_buffer, ".") == 0 || strcmp(entry_buffer, "..") == 0) {
                continue;
            }
            LOG_DEBUG("readdir: adding entry '%s'", entry_buffer);
            filler(buf, entry_buffer, NULL, 0, 0);
        }
    }

    sftp_closedir_remote(handle);
    LOG_DEBUG("readdir OK for %s", path);
    return 0;
}

int rp_open(const char *path, struct fuse_file_info *fi) {
    LOG_DEBUG("open: %s (POSIX flags: 0x%x)", path, fi->flags);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
    unsigned long sftp_flags = 0;
    int access_mode = fi->flags & O_ACCMODE;

    if (access_mode == O_RDONLY) {
        sftp_flags |= LIBSSH2_FXF_READ;
    } else if (access_mode == O_WRONLY) {
        sftp_flags |= LIBSSH2_FXF_WRITE;
    } else if (access_mode == O_RDWR) {
        sftp_flags |= LIBSSH2_FXF_READ | LIBSSH2_FXF_WRITE;
    }

    if (fi->flags & O_APPEND) {
        sftp_flags |= LIBSSH2_FXF_APPEND;
    }
    if (fi->flags & O_TRUNC) {
        if (access_mode == O_WRONLY || access_mode == O_RDWR) {
             sftp_flags |= LIBSSH2_FXF_TRUNC;
             LOG_DEBUG("  O_TRUNC detected, adding LIBSSH2_FXF_TRUNC");
        } else {
             LOG_WARN("  O_TRUNC ignored because file is opened read-only");
        }
    }

    long open_mode = 0;

    LIBSSH2_SFTP_HANDLE *handle = sftp_open_remote(remote_path, sftp_flags, open_mode);

    free(remote_path);

    if (!handle) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);

        if (err == EACCES || err == EINVAL || err == EIO || err == ENOSYS) {
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            char *r_path_stat = build_remote_path(path);
            if(r_path_stat && sftp_stat_remote(r_path_stat, &attrs) == 0 && S_ISDIR(attrs.permissions)) {
                free(r_path_stat);
                LOG_ERR("open: Attempted to open a directory with flags 0x%x: %s", fi->flags, path);
                return -EISDIR;
            }
            free(r_path_stat);
        }
        LOG_ERR("open: sftp_open_remote failed for %s with SFTP flags 0x%lx, sftp_err=%lu -> errno=%d", path, sftp_flags, sftp_err, err);
        return -err ? -err : -EIO;
    }

    fi->fh = (uint64_t)handle;
    LOG_DEBUG("open OK for %s, handle stored: %p", path, handle);
    
    return 0;
}

int rp_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    LOG_DEBUG("create: %s (mode: %o)", path, mode);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
    LIBSSH2_SFTP_HANDLE *handle = sftp_create_remote(remote_path, mode);
    free(remote_path);
    
    if (!handle) {
        remote_conn_info_t *conn = get_conn_info();
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("create: sftp_create_remote failed for %s, sftp_err=%lu -> errno=%d", path, sftp_err, err);
        return -err ? -err : -EIO;
    }
    
    fi->fh = (uint64_t)handle;
    LOG_DEBUG("create OK for %s, handle stored: %p", path, handle);
    
    return 0;
}

int rp_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    LOG_DEBUG("read: %s (size: %zu, offset: %ld)", path, size, offset);

    LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
    if (!handle) {
        LOG_ERR("read: Invalid SFTP handle for %s", path);
        return -EBADF;
    }

    if (offset > 0) {
        LOG_DEBUG("read: Seeking to offset %ld", offset);
        libssh2_sftp_seek64(handle, offset);
    } else if (offset == 0) {
         libssh2_sftp_seek64(handle, 0);
    }

    ssize_t bytes_read = sftp_read_remote(handle, buf, size);

    if (bytes_read < 0) {
        LOG_ERR("read: sftp_read_remote failed for %s (error code: %zd)", path, bytes_read);
        return (int)bytes_read;
    }

    LOG_DEBUG("read OK for %s: %zd bytes read", path, bytes_read);
    return (int)bytes_read;
}

int rp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    LOG_DEBUG("write: %s (size: %zu, offset: %ld)", path, size, offset);
    
    LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
    if (!handle) {
        LOG_ERR("write: Invalid SFTP handle for %s", path);
        return -EBADF;
    }
    
    if (offset > 0) {
        LOG_DEBUG("write: Seeking to offset %ld", offset);
        libssh2_sftp_seek64(handle, offset);
    }
    
    ssize_t bytes_written = sftp_write_remote(handle, buf, size);
    
    if (bytes_written < 0) {
        LOG_ERR("write: sftp_write_remote failed for %s (error code: %zd)", path, bytes_written);
        return (int)bytes_written;
    }
    
    LOG_DEBUG("write OK for %s: %zd bytes written", path, bytes_written);
    return (int)bytes_written;
}

int rp_release(const char *path, struct fuse_file_info *fi) {
    LOG_DEBUG("release: %s", path ? path : "N/A");
    LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
    int ret = 0;

    if (handle) {
        LOG_DEBUG("release: Closing SFTP handle %p", handle);
        int close_rc = sftp_close_remote(handle);
        if (close_rc != 0) {
            LOG_ERR("release: sftp_close_remote reported failure for %s with libssh2_rc=%d. Reporting EIO to FUSE.", path ? path : "N/A", close_rc);
            ret = -EIO;
        }
        fi->fh = 0;
    } else {
         LOG_DEBUG("release: No SFTP handle to close for %s", path ? path : "N/A");
    }
    return ret;
}

int rp_access(const char *path, int mask) {
     LOG_DEBUG("access: %s (mask: %d)", path, mask);
     
     struct stat stbuf;
     int res = rp_getattr(path, &stbuf, NULL);
     if (res != 0) {
         return res;
     }

     mode_t mode = stbuf.st_mode;
     uid_t uid = getuid();
     gid_t gid = getgid();

     if (mask == F_OK) return 0;

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
             if (!((mode & S_IXUSR) && (stbuf.st_uid == uid)) &&
                 !((mode & S_IXGRP) && (stbuf.st_gid == gid)) &&
                 !((mode & S_IXOTH))) {
                 LOG_DEBUG("access: Execute permission denied for %s", path);
                 return -EACCES;
             }
         } else {
             if (!((mode & S_IXUSR) && (stbuf.st_uid == uid)) &&
                 !((mode & S_IXGRP) && (stbuf.st_gid == gid)) &&
                 !((mode & S_IXOTH))) {
                 LOG_DEBUG("access: Directory access permission denied for %s", path);
                 return -EACCES;
             }
         }
     }

     return 0;
}

int rp_mkdir(const char *path, mode_t mode) {
    LOG_DEBUG("mkdir: %s (mode: %o)", path, mode);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
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
    return 0;
}

int rp_rmdir(const char *path) {
    LOG_DEBUG("rmdir: %s", path);
    
    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;
    
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
    return 0;
}

int rp_rename(const char *from, const char *to, unsigned int flags) {
    LOG_DEBUG("rename: %s -> %s (flags: %u)", from, to, flags);

    if (flags) {
        LOG_ERR("rename: Unsupported rename flags received: %u", flags);
        return -EINVAL;
    }

    char *remote_from = build_remote_path(from);
    if (!remote_from) return -ENOMEM;

    char *remote_to = build_remote_path(to);
    if (!remote_to) {
        free(remote_from);
        return -ENOMEM;
    }

    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) {
        free(remote_from);
        free(remote_to);
        return -ENOTCONN;
    }

    long rename_flags = LIBSSH2_SFTP_RENAME_OVERWRITE |
                        LIBSSH2_SFTP_RENAME_ATOMIC |
                        LIBSSH2_SFTP_RENAME_NATIVE;

    int rc = libssh2_sftp_rename_ex(conn->sftp_session,
                                   remote_from, strlen(remote_from),
                                   remote_to, strlen(remote_to),
                                   rename_flags);

    free(remote_from);
    free(remote_to);

    if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("rename: libssh2_sftp_rename_ex failed for %s -> %s, sftp_err=%lu -> errno=%d",
                from, to, sftp_err, err);
        return -err ? -err : -EIO;
    }

    LOG_DEBUG("rename OK: %s -> %s", from, to);
    return 0;
}

int rp_fsync(const char *path, int isdatasync, struct fuse_file_info *fi) {
    (void) path;
    LOG_DEBUG("fsync: %s (isdatasync: %d)", path ? path : "N/A", isdatasync);

    LIBSSH2_SFTP_HANDLE *handle = (LIBSSH2_SFTP_HANDLE *)fi->fh;
    if (!handle) {
        LOG_ERR("fsync: Invalid SFTP handle");
        return -EIO;
    }

    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -ENOTCONN;

    int rc = libssh2_sftp_fsync(handle);

    if (rc == LIBSSH2_ERROR_EAGAIN) {
         LOG_WARN("fsync: EAGAIN received, operation might take time.");
         return 0;
    } else if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        if (err == ENOSYS || rc == LIBSSH2_ERROR_SFTP_PROTOCOL || sftp_err == LIBSSH2_FX_OP_UNSUPPORTED) {
             LOG_WARN("fsync: Operation not supported by server or handle for %s", path);
             return -ENOSYS;
        }
        LOG_ERR("fsync: libssh2_sftp_fsync failed, rc=%d, sftp_err=%lu -> errno=%d", rc, sftp_err, err);
        return -err ? -err : -EIO;
    }

    LOG_DEBUG("fsync OK for %s", path);
    return 0;
}

int rp_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    LOG_DEBUG("truncate: %s (size: %ld)", path, size);
    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) return -ENOTCONN;

    char *remote_path = build_remote_path(path);
    if (!remote_path) return -ENOMEM;

    LIBSSH2_SFTP_ATTRIBUTES new_attrs;
    memset(&new_attrs, 0, sizeof(new_attrs));

    new_attrs.flags = LIBSSH2_SFTP_ATTR_SIZE;
    new_attrs.filesize = (libssh2_uint64_t)size;

    LOG_DEBUG("SFTP setstat (truncate): %s to size %llu", remote_path, new_attrs.filesize);

    int rc = libssh2_sftp_setstat(conn->sftp_session, remote_path, &new_attrs);

    free(remote_path);

    if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("truncate: libssh2_sftp_setstat failed for %s, rc=%d, sftp_err=%lu -> errno=%d",
                path, rc, sftp_err, err);

        if (err == EACCES || err == EROFS) {
            return -err;
        }
        return -EIO;
    }

    LOG_DEBUG("truncate OK for %s (new size: %ld)", path, size);
    return 0;
}

int rp_unlink(const char *path) {
    LOG_DEBUG("unlink: %s", path);

    char *remote_path = build_remote_path(path);
    if (!remote_path) {
        return -ENOMEM;
    }

    remote_conn_info_t *conn = get_conn_info();
    if (!conn || !conn->sftp_session) {
        free(remote_path);
        return -ENOTCONN;
    }

    int rc = sftp_unlink_remote(remote_path);

    free(remote_path);

    if (rc != 0) {
        unsigned long sftp_err = libssh2_sftp_last_error(conn->sftp_session);
        int err = sftp_error_to_errno(sftp_err);
        LOG_ERR("unlink: sftp_unlink_remote failed for %s, rc=%d, sftp_err=%lu -> errno=%d",
                path, rc, sftp_err, err);

        if (err == ENOSYS || err == EIO) {
        }
        return -err ? -err : -EIO;
    }

    LOG_DEBUG("unlink OK for %s", path);
    return 0;
}
