#define _XOPEN_SOURCE 500 // Required for nftw
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
#include <dirent.h>
#include <ftw.h> // Include for nftw

remote_conn_info_t *ssh_cli_conn = NULL;

void show_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [options] <source> <destination>\n\n", progname);
    fprintf(stderr, "Copy files between local filesystem and RemoteFS mounted directories.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --verbose        Enable verbose output\n");
    fprintf(stderr, "  -r, --recursive      Copy directories recursively\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s localfile.txt /path/to/mounted/remotefs/      # Copy local file to remote\n", progname);
    fprintf(stderr, "  %s /path/to/mounted/remotefs/file.txt ./         # Copy remote file to local\n", progname);
    fprintf(stderr, "  %s -r local_dir/ /path/to/mounted/remotefs/      # Copy directory recursively\n", progname);
    fprintf(stderr, "  %s file1.txt /path/to/mounted/remotefs/file2.txt # Copy and rename\n", progname);
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

// A data structure to hold information for recursive copies
typedef struct {
    int verbose;
    const char* source_base;      // Base source path (for calculating relative paths)
    const char* dest_base;        // Base destination path
    int source_is_remote;
    int dest_is_remote;
} copy_context_t;

// Global pointer for copy context
static copy_context_t *g_copy_ctx = NULL;

// Copy a single file from local to remote
int copy_local_to_remote_file(const char* local_path, const char* remote_path, int verbose) {
    if (verbose) {
        printf("Copying local file %s to remote %s\n", local_path, remote_path);
    }
    return sftp_copy_local_to_remote(local_path, remote_path);
}

// Copy a single file from remote to local
int copy_remote_to_local_file(const char* remote_path, const char* local_path, int verbose) {
    if (verbose) {
        printf("Copying remote file %s to local %s\n", remote_path, local_path);
    }
    return sftp_copy_remote_to_local(remote_path, local_path);
}

// Function to create remote directory
int create_remote_dir(const char* remote_path, mode_t mode, int verbose) {
    if (verbose) {
        printf("Creating remote directory: %s\n", remote_path);
    }
    
    // Check if directory already exists
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    if (sftp_stat_remote(remote_path, &attrs) == 0) {
        if (LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
            if (verbose) {
                printf("Remote directory already exists: %s\n", remote_path);
            }
            return 0; // Directory already exists
        }
        return -EEXIST; // Exists but is not a directory
    }
    
    return sftp_mkdir_remote(remote_path, mode);
}

// Callback function for ftw() in local to remote recursive copy
int copy_local_to_remote_callback(const char *path, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    // Use global context instead of ftwbuf->extra
    copy_context_t *ctx = g_copy_ctx;
    
    // Calculate the path relative to source base
    const char *rel_path = path + strlen(ctx->source_base);
    if (*rel_path == '/') rel_path++; // Skip leading slash
    
    // Build destination path
    char dest_path[PATH_MAX];
    if (strlen(rel_path) == 0) {
        // Root directory, already handled
        return 0;
    }
    
    // Combine destination base with relative path
    snprintf(dest_path, sizeof(dest_path), "%s/%s", ctx->dest_base, rel_path);
    
    if (typeflag == FTW_D) {
        // Directory
        if (ctx->verbose) {
            printf("Creating remote directory: %s\n", dest_path);
        }
        
        // Create remote directory with same permissions
        int result = create_remote_dir(dest_path, sb->st_mode & 0777, ctx->verbose);
        if (result != 0 && result != -EEXIST) {
            fprintf(stderr, "Error creating remote directory %s: %s\n", 
                    dest_path, strerror(errno));
            return -1;
        }
    } else if (typeflag == FTW_F) {
        // Regular file
        int result = copy_local_to_remote_file(path, dest_path, ctx->verbose);
        if (result != 0) {
            fprintf(stderr, "Error copying %s to %s: %s\n", 
                    path, dest_path, strerror(-result));
            return -1;
        }
    }
    
    return 0;
}

// Copy a directory recursively from local to remote
int copy_local_to_remote_recursive(const char* local_dir, const char* remote_path, int verbose) {
    struct stat st;
    if (stat(local_dir, &st) != 0) {
        fprintf(stderr, "Error: Cannot stat local directory: %s\n", local_dir);
        return -errno;
    }
    
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: %s is not a directory\n", local_dir);
        return -ENOTDIR;
    }
    
    // Create the top-level directory on the remote side
    int result = create_remote_dir(remote_path, st.st_mode & 0777, verbose);
    if (result != 0 && result != -EEXIST) {
        fprintf(stderr, "Error creating remote directory %s: %s\n", 
                remote_path, strerror(errno));
        return result;
    }
    
    // Setup context for nftw
    copy_context_t ctx = {
        .verbose = verbose,
        .source_base = local_dir,
        .dest_base = remote_path,
        .source_is_remote = 0,
        .dest_is_remote = 1
    };
    
    // Normalize source path to ensure it ends with /
    char normalized_source[PATH_MAX];
    strncpy(normalized_source, local_dir, PATH_MAX - 1);
    normalized_source[PATH_MAX - 1] = '\0';
    
    size_t len = strlen(normalized_source);
    if (len > 0 && normalized_source[len - 1] != '/') {
        if (len < PATH_MAX - 1) {
            normalized_source[len] = '/';
            normalized_source[len + 1] = '\0';
        }
    }
    
    ctx.source_base = normalized_source;
    
    // Walk through the directory tree and copy files using global context
    g_copy_ctx = &ctx;
    result = nftw(local_dir, copy_local_to_remote_callback, 20, FTW_PHYS | FTW_DEPTH);
    g_copy_ctx = NULL;
    
    return result;
}

// Copy a directory recursively from remote to local
int copy_remote_to_local_recursive(const char* remote_dir, const char* local_path, int verbose) {
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    
    // Check if remote path exists and is a directory
    if (sftp_stat_remote(remote_dir, &attrs) != 0) {
        fprintf(stderr, "Error: Cannot stat remote directory: %s\n", remote_dir);
        return -ENOENT;
    }
    
    if (!LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
        fprintf(stderr, "Error: %s is not a directory\n", remote_dir);
        return -ENOTDIR;
    }
    
    // Create the top-level directory on the local side
    struct stat st;
    if (stat(local_path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Error: Local path %s exists but is not a directory\n", local_path);
            return -ENOTDIR;
        }
    } else {
        if (mkdir(local_path, 0755) != 0) {
            fprintf(stderr, "Error creating local directory %s: %s\n", 
                    local_path, strerror(errno));
            return -errno;
        }
        if (verbose) {
            printf("Created local directory: %s\n", local_path);
        }
    }
    
    // Open the remote directory
    LIBSSH2_SFTP_HANDLE *dir_handle = sftp_opendir_remote(remote_dir);
    if (!dir_handle) {
        fprintf(stderr, "Error opening remote directory: %s\n", remote_dir);
        return -EIO;
    }
    
    int result = 0;
    char filename[512];
    LIBSSH2_SFTP_ATTRIBUTES file_attrs;
    
    // Read directory entries
    while (1) {
        memset(filename, 0, sizeof(filename));
        int rc = sftp_readdir_remote(dir_handle, filename, sizeof(filename), &file_attrs);
        
        if (rc <= 0)
            break;
        
        // Skip . and ..
        if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0)
            continue;
        
        // Construct full remote and local paths
        char remote_path[PATH_MAX];
        char local_file_path[PATH_MAX];
        
        snprintf(remote_path, sizeof(remote_path), "%s/%s", remote_dir, filename);
        snprintf(local_file_path, sizeof(local_file_path), "%s/%s", local_path, filename);
        
        if (LIBSSH2_SFTP_S_ISDIR(file_attrs.permissions)) {
            // Recursively copy subdirectory
            result = copy_remote_to_local_recursive(remote_path, local_file_path, verbose);
            if (result != 0)
                break;
        } else {
            // Copy regular file
            result = copy_remote_to_local_file(remote_path, local_file_path, verbose);
            if (result != 0) {
                fprintf(stderr, "Error copying %s to %s: %s\n", 
                        remote_path, local_file_path, strerror(-result));
                break;
            }
        }
    }
    
    sftp_closedir_remote(dir_handle);
    return result;
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
        if (setup_connection(relevant_mount_point, verbose) != 0) {
            return 1;
        }
    }

    int result = 1;
    if (source_is_remote && dest_is_remote) {
        fprintf(stderr, "Error: Cannot copy directly between two remote locations\n");
    } else if (!source_is_remote && !dest_is_remote) {
        fprintf(stderr, "Note: Both paths are local, using system cp\n");
        
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "/bin/cp %s %s %s", 
                 recursive ? "-r" : "", source, destination);
        result = system(cmd);
    } else if (source_is_remote && !dest_is_remote) {
        char remote_path[PATH_MAX];
        if (get_remote_path(source, remote_path, sizeof(remote_path), verbose) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path for %s\n", source);
            result = 1;
        } else {
            struct stat st;
            char new_dest_buf[PATH_MAX];
            const char *actual_destination = destination;

            if (stat(destination, &st) == 0 && S_ISDIR(st.st_mode)) {
                char *source_basename_dup = strdup(source);
                if (!source_basename_dup) { perror("strdup"); result = 1; goto cleanup; }
                char *source_basename = basename(source_basename_dup);
                snprintf(new_dest_buf, sizeof(new_dest_buf), "%s/%s", 
                        destination, source_basename);
                free(source_basename_dup);
                actual_destination = new_dest_buf;
            }
            
            // Check if source is a directory (recursive copy needed)
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            int is_dir = 0;
            if (sftp_stat_remote(remote_path, &attrs) == 0) {
                is_dir = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
            }
            
            if (is_dir) {
                if (!recursive) {
                    fprintf(stderr, "Error: Source is a directory. Use -r option for recursive copy.\n");
                    result = 1;
                    goto cleanup;
                }
                
                if (verbose) {
                    printf("Copying recursively from remote directory %s to local %s\n", 
                           remote_path, actual_destination);
                }
                
                result = copy_remote_to_local_recursive(remote_path, actual_destination, verbose);
            } else {
                if (verbose) {
                    printf("Copying from remote file %s to local %s\n", remote_path, actual_destination);
                }
                
                result = sftp_copy_remote_to_local(remote_path, actual_destination);
            }
        }
    } else if (!source_is_remote && dest_is_remote) {
        char remote_path[PATH_MAX];
        if (get_remote_path(destination, remote_path, sizeof(remote_path), verbose) != 0) {
            fprintf(stderr, "Error: Cannot determine remote path for %s\n", destination);
            result = 1;
        } else {
            struct stat source_st;
            char new_dest_buf[PATH_MAX];
            const char *actual_destination = remote_path;

            if (stat(source, &source_st) != 0) {
                fprintf(stderr, "Error: Cannot stat source: %s\n", source);
                result = 1;
                goto cleanup;
            }

            // Check if the remote path exists and is a directory
            LIBSSH2_SFTP_ATTRIBUTES attrs;
            int is_remote_dir = 0;
            if (sftp_stat_remote(remote_path, &attrs) == 0) {
                is_remote_dir = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
            }

            if (is_remote_dir) {
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
            
            // Check if source is a directory (recursive copy needed)
            if (S_ISDIR(source_st.st_mode)) {
                if (!recursive) {
                    fprintf(stderr, "Error: Source is a directory. Use -r option for recursive copy.\n");
                    result = 1;
                    goto cleanup;
                }
                
                if (verbose) {
                    printf("Copying recursively from local directory %s to remote %s\n", 
                           source, actual_destination);
                }
                
                result = copy_local_to_remote_recursive(source, actual_destination, verbose);
            } else {
                if (verbose) {
                    printf("Copying from local file %s to remote %s\n", source, actual_destination);
                }
                
                result = sftp_copy_local_to_remote(source, actual_destination);
            }
        }
    } else {
         fprintf(stderr, "Error: Invalid combination of source/destination types.\n");
         result = 1;
    }

cleanup:
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

    return result;
}