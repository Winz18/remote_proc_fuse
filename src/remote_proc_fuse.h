#ifndef REMOTE_PROC_FUSE_H
#define REMOTE_PROC_FUSE_H

#include "common.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

void* rp_init(struct fuse_conn_info *conn, struct fuse_config *cfg);
void rp_destroy(void *private_data);
int rp_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int rp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
               struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int rp_open(const char *path, struct fuse_file_info *fi);
int rp_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi);
int rp_release(const char *path, struct fuse_file_info *fi);
int rp_access(const char *path, int mask);
int rp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int rp_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int rp_unlink(const char *path);
int rp_mkdir(const char *path, mode_t mode);
int rp_rmdir(const char *path);
int rp_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int rp_rename(const char *from, const char *to, unsigned int flags);
int rp_fsync(const char *path, int isdatasync, struct fuse_file_info *fi);

#endif