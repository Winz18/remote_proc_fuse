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

static int rp_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
    remote_conn_info_t *conn = (remote_conn_info_t*)data;

    switch (key) {
        case KEY_HELP:
            show_usage(outargs->argv[0]);
             return fuse_opt_add_arg(outargs, "-ho") ? -1 : 1;

        case KEY_VERSION:
             fprintf(stderr, "RemoteFS - FUSE-based Remote Filesystem version 1.0\n");
             return fuse_opt_add_arg(outargs, "--version") ? -1 : 1;

        case KEY_OPT_HOST:
        case KEY_OPT_USER:
        case KEY_OPT_PASS:
        case KEY_OPT_KEY:
        case KEY_OPT_REMOTEPATH:
             {
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
                         LOG_DEBUG("Parsed option: %s = %s", rp_opts[key].templ, *field_ptr);
                     }
                 }
             }
             return 0;

        case KEY_OPT_PORT:
            LOG_DEBUG("Parsed option: port = %d", conn->remote_port);
            return 0;

        default:
            return 1;
    }
}

int main(int argc, char *argv[]) {
    int ret;

    struct fuse_operations rp_oper = {
        .init       = rp_init,
        .destroy    = rp_destroy,
        .getattr    = rp_getattr,
        .readdir    = rp_readdir,
        .open       = rp_open,
        .read       = rp_read,
        .release    = rp_release,
        .access     = rp_access,

        .write      = rp_write,
        .create     = rp_create,
        .unlink     = rp_unlink,
        .mkdir      = rp_mkdir,
        .rmdir      = rp_rmdir,
        .truncate   = rp_truncate,
        .rename     = rp_rename,
        .fsync      = rp_fsync,
    };

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    if (fuse_opt_parse(&args, &connection_info, rp_opts, rp_opt_proc) == -1) {
        fprintf(stderr, "Error parsing options.\n");
        fuse_opt_free_args(&args);
        return 1;
    }

    if (!connection_info.remote_proc_path) {
        connection_info.remote_proc_path = strdup("/");
        if (!connection_info.remote_proc_path) {
             perror("strdup failed for default remote path");
             fuse_opt_free_args(&args);
             return 1;
        }
    }

    if (!connection_info.remote_host || !connection_info.remote_user) {
        fprintf(stderr, "Error: Both host and user must be specified.\n");
        fprintf(stderr, "Use -h or --help for usage information.\n");
        fuse_opt_free_args(&args);
        return 1;
    }

    if (!connection_info.remote_pass && !connection_info.ssh_key_path) {
        fprintf(stderr, "Error: Either password or SSH key must be specified.\n");
        fprintf(stderr, "Use -h or --help for usage information.\n");
        fuse_opt_free_args(&args);
        return 1;
    }

    int mount_point_idx = 1;
    while (mount_point_idx < args.argc) {
        if (args.argv[mount_point_idx][0] == '-') {
            mount_point_idx++;
            if (mount_point_idx < args.argc && 
                (args.argv[mount_point_idx-1][1] == 'o' || 
                 strncmp(args.argv[mount_point_idx-1], "--opt", 5) == 0)) {
                mount_point_idx++;
            }
            continue;
        }
        break;
    }
    
    if (mount_point_idx < args.argc) {
        char *mount_point = args.argv[mount_point_idx];
        char real_path[PATH_MAX];
        
        if (realpath(mount_point, real_path) != NULL) {
            LOG_INFO("  Mount Point: %s", real_path);
            
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

    ret = fuse_main(args.argc, args.argv, &rp_oper, &connection_info);

    fuse_opt_free_args(&args);

    free(connection_info.remote_host);
    free(connection_info.remote_user);
    free(connection_info.remote_pass);
    free(connection_info.ssh_key_path);
    free(connection_info.remote_proc_path);

    if (ret != 0) {
        fprintf(stderr, "fuse_main returned an error: %d\n", ret);
    } else {
        LOG_INFO("Filesystem unmounted successfully.");
    }

    return ret;
}