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

remote_conn_info_t *ssh_cli_conn = NULL;

void show_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [options] <source> <destination>\n\n", progname);
    fprintf(stderr, "Copy files between local filesystem and RemoteFS mounted directories.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --verbose        Enable verbose output\n");
    fprintf(stderr, "  -r, --recursive      Copy directories recursively (not yet implemented)\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s localfile.txt /path/to/mounted/remotefs/      # Copy local file to remote\n", progname);
    fprintf(stderr, "  %s /path/to/mounted/remotefs/file.txt ./         # Copy remote file to local\n", progname);
    fprintf(stderr, "  %s file1.txt /path/to/mounted/remotefs/file2.txt # Copy and rename\n", progname);
}

const char* is_remote_path(const char *path, char *mount_point_buf, size_t buf_size) {
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path) == NULL) {
        strncpy(abs_path, path, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = '\0';
    }
    
    int count = 0;
    char **mount_points = get_mount_points(&count);
    if (!mount_points || count == 0) {
        return NULL;
    }
    
    for (int i = 0; i < count; i++) {
        size_t len = strlen(mount_points[i]);
        if (strncmp(abs_path, mount_points[i], len) == 0 && 
            (abs_path[len] == '/' || abs_path[len] == '\0')) {
            if (mount_point_buf && buf_size > 0) {
                strncpy(mount_point_buf, mount_points[i], buf_size - 1);
                mount_point_buf[buf_size - 1] = '\0';
            }
            
            for (int j = 0; j < count; j++) {
                free(mount_points[j]);
            }
            free(mount_points);
            
            return mount_point_buf;
        }
    }
    
    for (int i = 0; i < count; i++) {
        free(mount_points[i]);
    }
    free(mount_points);
    
    return NULL;
}

int get_remote_path(const char *path, char *remote_path, size_t size) {
    char mount_point[PATH_MAX];
    
    if (!is_remote_path(path, mount_point, sizeof(mount_point))) {
        return -1;
    }
    
    char *base_remote_path = get_remote_path_for_mount(mount_point);
    if (!base_remote_path) {
        fprintf(stderr, "Error: Cannot determine remote path for mount point: %s\n", mount_point);
        return -1;
    }
    
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path) == NULL) {
        strncpy(abs_path, path, sizeof(abs_path) - 1);
        abs_path[sizeof(abs_path) - 1] = '\0';
    }
    
    const char *rel_path = abs_path + strlen(mount_point);
    
    if (rel_path[0] != '/' && strlen(rel_path) > 0) {
        snprintf(remote_path, size, "%s/%s", base_remote_path, rel_path);
    } else {
        snprintf(remote_path, size, "%s%s", base_remote_path, rel_path);
    }
    
    if (strlen(base_remote_path) > 0 && base_remote_path[strlen(base_remote_path) - 1] == '/' && 
        rel_path[0] == '/') {
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
    int recursive = 0;
    
    static struct option long_options[] = {
        {"help",     no_argument, 0, 'h'},
        {"verbose",  no_argument, 0, 'v'},
        {"recursive", no_argument, 0, 'r'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    
    while ((opt = getopt_long(argc, argv, "hvr", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                show_usage(argv[0]);
                return 0;
            case 'v':
                verbose = 1;
                break;
            case 'r':
                recursive = 1;
                fprintf(stderr, "Warning: Recursive copy not yet implemented\n");
                break;
            default:
                show_usage(argv[0]);
                return 1;
        }
    }

    if (argc - optind != 2) {
        fprintf(stderr, "Error: Source and destination arguments are required.\n");
        show_usage(argv[0]);
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

    if (relevant_mount_point) {
        ssh_cli_conn = calloc(1, sizeof(remote_conn_info_t));
        if (!ssh_cli_conn) {
            perror("Failed to allocate memory for connection info");
            return 1;
        }

        ssh_cli_conn->remote_proc_path = get_remote_path_for_mount(relevant_mount_point);
        if (!ssh_cli_conn->remote_proc_path) {
            fprintf(stderr, "Error: Cannot determine remote base path for mount point: %s\n", relevant_mount_point);
            free(ssh_cli_conn);
            ssh_cli_conn = NULL;
            return 1;
        }

        fprintf(stderr, "Warning: Connection details (host, user, port) are not loaded from config. SFTP connection will likely fail.\n");

        if (sftp_connect_and_auth(ssh_cli_conn) != 0) {
            fprintf(stderr, "Error: Failed to connect and authenticate SFTP session for %s\n", relevant_mount_point);
            free(ssh_cli_conn->remote_proc_path);
            free(ssh_cli_conn);
            ssh_cli_conn = NULL;
            return 1;
        }
        if (verbose) {
             printf("SFTP connection established for mount point %s\n", relevant_mount_point);
        }
    }

    int result = 1;
    if (source_is_remote && dest_is_remote) {
        fprintf(stderr, "Error: Cannot copy directly between two remote locations\n");
    } else if (!source_is_remote && !dest_is_remote) {
        fprintf(stderr, "Note: Both paths are local, using system cp\n");
        
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "/bin/cp %s %s", source, destination);
        result = system(cmd);
    } else if (source_is_remote && !dest_is_remote) {
        char remote_path[PATH_MAX];
        if (get_remote_path(source, remote_path, sizeof(remote_path)) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path\n");
            result = 1;
        } else {
            struct stat st;
            char new_dest_buf[PATH_MAX];
            char *actual_destination = destination;

            if (stat(destination, &st) == 0 && S_ISDIR(st.st_mode)) {
                char *source_basename_dup = strdup(source);
                if (!source_basename_dup) { perror("strdup"); result = 1; goto cleanup; }
                char *source_basename = basename(source_basename_dup);
                snprintf(new_dest_buf, sizeof(new_dest_buf), "%s/%s", 
                        destination, source_basename);
                free(source_basename_dup);
                actual_destination = new_dest_buf;
            }
            
            if (verbose) {
                printf("Copying from remote %s to local %s\n", remote_path, actual_destination);
            }
            
            result = sftp_copy_remote_to_local(remote_path, actual_destination);
        }
    } else if (!source_is_remote && dest_is_remote) {
        char remote_path[PATH_MAX];
        if (get_remote_path(destination, remote_path, sizeof(remote_path)) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path\n");
            result = 1;
        } else {
            struct stat st;
            char new_dest_buf[PATH_MAX];
            char *actual_destination = remote_path;

            if (stat(source, &st) != 0) {
                fprintf(stderr, "Error: Cannot stat source file: %s\n", source);
                result = 1;
                goto cleanup;
            }

            LIBSSH2_SFTP_ATTRIBUTES attrs;
            int is_dir = 0;
            if (sftp_stat_remote(remote_path, &attrs) == 0) {
                if (LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
                    is_dir = 1;
                }
            }

            if (is_dir) {
                char *source_basename_dup = strdup(source);
                if (!source_basename_dup) { perror("strdup"); result = 1; goto cleanup; }
                char *source_basename = basename(source_basename_dup);
                int remote_len = strlen(remote_path);
                if (remote_len > 0 && remote_path[remote_len - 1] == '/') {
                    snprintf(new_dest_buf, sizeof(new_dest_buf), "%s%s", 
                            remote_path, source_basename);
                } else {
                    snprintf(new_dest_buf, sizeof(new_dest_buf), "%s/%s", 
                            remote_path, source_basename);
                }
                free(source_basename_dup);
                actual_destination = new_dest_buf;
            }
            
            if (verbose) {
                printf("Copying from local %s to remote %s\n", source, actual_destination);
            }
            
            result = sftp_copy_local_to_remote(source, actual_destination);
        }
    } else {
         fprintf(stderr, "Error: Invalid combination of source/destination types.\n");
         result = 1;
    }

cleanup:
    if (ssh_cli_conn) {
        sftp_disconnect(ssh_cli_conn);
        free(ssh_cli_conn->remote_proc_path);
        free(ssh_cli_conn);
        ssh_cli_conn = NULL;
        if (verbose) {
            printf("SFTP connection closed.\n");
        }
    }

    return result;
}