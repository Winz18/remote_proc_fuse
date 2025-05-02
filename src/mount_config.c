#include "common.h"
#include "mount_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

char *get_config_dir() {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw)
            home = pw->pw_dir;
    }
    
    if (!home) {
        return NULL;
    }
    
    char *config_dir = malloc(strlen(home) + 20);
    if (!config_dir) {
        return NULL;
    }
    sprintf(config_dir, "%s/.config/remotefs", home);
    
    struct stat st;
    if (stat(config_dir, &st) == -1) {
        if (mkdir(config_dir, 0700) == -1) {
            free(config_dir);
            return NULL;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        free(config_dir);
        return NULL;
    }
    
    return config_dir;
}

int save_mount_point(const char *mount_point, const char *remote_path) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return -1;
    }
    
    char *config_file = malloc(strlen(config_dir) + 20);
    if (!config_file) {
        free(config_dir);
        return -1;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    FILE *fp;
    char line[PATH_MAX * 2];
    int found = 0;
    
    fp = fopen(config_file, "r");
    FILE *temp_fp = NULL;
    char *temp_file = NULL;
    
    if (fp) {
        temp_file = malloc(strlen(config_file) + 10);
        if (!temp_file) {
            free(config_dir);
            free(config_file);
            fclose(fp);
            return -1;
        }
        sprintf(temp_file, "%s.tmp", config_file);
        
        temp_fp = fopen(temp_file, "w");
        if (!temp_fp) {
            free(config_dir);
            free(config_file);
            free(temp_file);
            fclose(fp);
            return -1;
        }
        
        while (fgets(line, sizeof(line), fp)) {
            char *sep = strchr(line, ':');
            if (sep) {
                *sep = '\0';
                if (strcmp(line, mount_point) == 0) {
                    fprintf(temp_fp, "%s:%s\n", mount_point, remote_path);
                    found = 1;
                } else {
                    *sep = ':';
                    fputs(line, temp_fp);
                }
            } else {
                fputs(line, temp_fp);
            }
        }
        
        fclose(fp);
    } else {
        temp_fp = fopen(config_file, "w");
        if (!temp_fp) {
            free(config_dir);
            free(config_file);
            return -1;
        }
    }
    
    if (!found) {
        fprintf(temp_fp, "%s:%s\n", mount_point, remote_path);
    }
    
    fclose(temp_fp);
    
    if (temp_file) {
        if (rename(temp_file, config_file) == -1) {
            unlink(temp_file);
            free(temp_file);
            free(config_dir);
            free(config_file);
            return -1;
        }
        free(temp_file);
    }
    
    free(config_dir);
    free(config_file);
    return 0;
}

// New function to save full connection info for a mount point
int save_connection_info(const char *mount_point, const remote_conn_info_t *conn_info) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return -1;
    }
    
    char *config_file = malloc(strlen(config_dir) + 30);
    if (!config_file) {
        free(config_dir);
        return -1;
    }
    sprintf(config_file, "%s/connections.conf", config_dir);
    
    FILE *fp;
    char line[PATH_MAX * 4];  // Larger buffer for connection details
    int found = 0;
    
    fp = fopen(config_file, "r");
    FILE *temp_fp = NULL;
    char *temp_file = NULL;
    
    if (fp) {
        temp_file = malloc(strlen(config_file) + 10);
        if (!temp_file) {
            free(config_dir);
            free(config_file);
            fclose(fp);
            return -1;
        }
        sprintf(temp_file, "%s.tmp", config_file);
        
        temp_fp = fopen(temp_file, "w");
        if (!temp_fp) {
            free(config_dir);
            free(config_file);
            free(temp_file);
            fclose(fp);
            return -1;
        }
        
        while (fgets(line, sizeof(line), fp)) {
            char *sep = strchr(line, ':');
            if (sep) {
                *sep = '\0';
                if (strcmp(line, mount_point) == 0) {
                    // Format: mount_point:host:user:port:remotepath:key_path|password
                    // Using pipe (|) separator between key_path and password to avoid colon parsing issues
                    fprintf(temp_fp, "%s:%s:%s:%d:%s:%s|%s\n", 
                            mount_point,
                            conn_info->remote_host ? conn_info->remote_host : "",
                            conn_info->remote_user ? conn_info->remote_user : "",
                            conn_info->remote_port,
                            conn_info->remote_proc_path ? conn_info->remote_proc_path : "",
                            conn_info->ssh_key_path ? conn_info->ssh_key_path : "",
                            conn_info->remote_pass ? conn_info->remote_pass : "");
                    found = 1;
                } else {
                    *sep = ':';
                    fputs(line, temp_fp);
                }
            } else {
                fputs(line, temp_fp);
            }
        }
        
        fclose(fp);
    } else {
        temp_fp = fopen(config_file, "w");
        if (!temp_fp) {
            free(config_dir);
            free(config_file);
            return -1;
        }
    }
    
    if (!found) {
        // Format: mount_point:host:user:port:remotepath:key_path|password
        fprintf(temp_fp, "%s:%s:%s:%d:%s:%s|%s\n",
                mount_point,
                conn_info->remote_host ? conn_info->remote_host : "",
                conn_info->remote_user ? conn_info->remote_user : "",
                conn_info->remote_port,
                conn_info->remote_proc_path ? conn_info->remote_proc_path : "",
                conn_info->ssh_key_path ? conn_info->ssh_key_path : "",
                conn_info->remote_pass ? conn_info->remote_pass : "");
    }
    
    fclose(temp_fp);
    
    if (temp_file) {
        if (rename(temp_file, config_file) == -1) {
            unlink(temp_file);
            free(temp_file);
            free(config_dir);
            free(config_file);
            return -1;
        }
        free(temp_file);
    }
    
    free(config_dir);
    free(config_file);
    return 0;
}

char **get_mount_points(int *count) {
    *count = 0;
    
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return NULL;
    }
    
    char *config_file = malloc(strlen(config_dir) + 20);
    if (!config_file) {
        free(config_dir);
        return NULL;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        free(config_dir);
        free(config_file);
        return NULL;
    }
    
    char line[PATH_MAX * 2];
    int num_lines = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        char *sep = strchr(line, ':');
        if (sep) num_lines++;
    }
    
    if (num_lines == 0) {
        fclose(fp);
        free(config_dir);
        free(config_file);
        return NULL;
    }
    
    char **mount_points = malloc(sizeof(char*) * (num_lines + 1));
    if (!mount_points) {
        fclose(fp);
        free(config_dir);
        free(config_file);
        return NULL;
    }
    
    rewind(fp);
    
    int idx = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        if (strlen(line) == 0) continue;
        
        char *sep = strchr(line, ':');
        if (!sep) continue;
        
        *sep = '\0';
        
        mount_points[idx] = strdup(line);
        if (!mount_points[idx]) {
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
    
    mount_points[idx] = NULL;
    *count = idx;
    
    fclose(fp);
    free(config_dir);
    free(config_file);
    
    return mount_points;
}

char *get_remote_path_for_mount(const char *mount_point) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return NULL;
    }
    
    char *config_file = malloc(strlen(config_dir) + 20);
    if (!config_file) {
        free(config_dir);
        return NULL;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    if (access(config_file, F_OK) == -1) {
        free(config_dir);
        free(config_file);
        return NULL;
    }
    
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        free(config_dir);
        free(config_file);
        return NULL;
    }
    
    char line[PATH_MAX * 2];
    char *remote_path = NULL;
    
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        char *sep = strchr(line, ':');
        if (!sep) continue;
        
        *sep = '\0';
        sep++;
        
        if (strcmp(line, mount_point) == 0) {
            remote_path = strdup(sep);
            break;
        }
    }
    
    fclose(fp);
    free(config_dir);
    free(config_file);
    
    return remote_path;
}

int load_connection_info_for_mount(const char *mount_point, remote_conn_info_t *conn_info) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return -1;
    }
    
    char *config_file = malloc(strlen(config_dir) + 30);
    if (!config_file) {
        free(config_dir);
        return -1;
    }
    sprintf(config_file, "%s/connections.conf", config_dir);
    
    if (access(config_file, F_OK) == -1) {
        // Fall back to getting just the remote path from mounts.conf
        free(config_file);
        
        char *remote_path = get_remote_path_for_mount(mount_point);
        if (remote_path) {
            conn_info->remote_proc_path = remote_path;
            free(config_dir);
            return 1; // Partial success - only remote_proc_path is available
        }
        
        free(config_dir);
        return -1;
    }
    
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        free(config_dir);
        free(config_file);
        return -1;
    }
    
    char line[PATH_MAX * 4];  // Larger buffer for connection details
    int found = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        
        // Find mount point at start of line
        char *mount_start = line;
        char *first_colon = strchr(mount_start, ':');
        if (!first_colon) continue;
        
        *first_colon = '\0';
        if (strcmp(mount_start, mount_point) != 0) {
            *first_colon = ':'; // Restore to avoid modifying the line for next iterations
            continue;
        }
        
        // We found our mount point, now parse each field
        char *current_pos = first_colon + 1;
        
        // 1. Host
        char *next_colon = strchr(current_pos, ':');
        if (!next_colon) break;
        *next_colon = '\0';
        if (strlen(current_pos) > 0) {
            conn_info->remote_host = strdup(current_pos);
        }
        current_pos = next_colon + 1;
        
        // 2. User
        next_colon = strchr(current_pos, ':');
        if (!next_colon) break;
        *next_colon = '\0';
        if (strlen(current_pos) > 0) {
            conn_info->remote_user = strdup(current_pos);
        }
        current_pos = next_colon + 1;
        
        // 3. Port
        next_colon = strchr(current_pos, ':');
        if (!next_colon) break;
        *next_colon = '\0';
        if (strlen(current_pos) > 0) {
            conn_info->remote_port = atoi(current_pos);
        } else {
            conn_info->remote_port = 22; // Default SSH port
        }
        current_pos = next_colon + 1;
        
        // 4. Remote path
        next_colon = strchr(current_pos, ':');
        if (!next_colon) break;
        *next_colon = '\0';
        if (strlen(current_pos) > 0) {
            conn_info->remote_proc_path = strdup(current_pos);
        } else {
            conn_info->remote_proc_path = strdup("/");
        }
        current_pos = next_colon + 1;
        
        // 5. SSH key path and password (divided by pipe)
        char *pipe_char = strchr(current_pos, '|');
        if (pipe_char) {
            *pipe_char = '\0';
            if (strlen(current_pos) > 0) {
                conn_info->ssh_key_path = strdup(current_pos);
            }
            
            // Get password after the pipe
            char *password = pipe_char + 1;
            if (strlen(password) > 0) {
                conn_info->remote_pass = strdup(password);
            }
        } else {
            // Old format or no password case
            if (strlen(current_pos) > 0) {
                conn_info->ssh_key_path = strdup(current_pos);
            }
        }
        
        found = 1;
        break;
    }
    
    fclose(fp);
    free(config_dir);
    free(config_file);
    
    if (!found) {
        // Fall back to getting just the remote path from mounts.conf
        char *remote_path = get_remote_path_for_mount(mount_point);
        if (remote_path) {
            conn_info->remote_proc_path = remote_path;
            return 1; // Partial success - only remote_proc_path is available
        }
        return -1;
    }
    
    return 0;
}

int remove_mount_point(const char *mount_point) {
    char *config_dir = get_config_dir();
    if (!config_dir) {
        return -1;
    }
    
    char *config_file = malloc(strlen(config_dir) + 20);
    if (!config_file) {
        free(config_dir);
        return -1;
    }
    sprintf(config_file, "%s/mounts.conf", config_dir);
    
    if (access(config_file, F_OK) == -1) {
        free(config_dir);
        free(config_file);
        return 0;
    }
    
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        free(config_dir);
        free(config_file);
        return -1;
    }
    
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
        fclose(fp);
        free(config_dir);
        free(config_file);
        free(temp_file);
        return -1;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char line_copy[1024];
        strcpy(line_copy, line);
        
        char *nl = strchr(line_copy, '\n');
        if (nl) *nl = '\0';
        
        char *sep = strchr(line_copy, ':');
        if (!sep) {
            fputs(line, fp_tmp);
            continue;
        }
        
        *sep = '\0';
        
        if (strcmp(line_copy, mount_point) != 0) {
            fputs(line, fp_tmp);
        }
    }
    
    fclose(fp);
    fclose(fp_tmp);
    
    if (rename(temp_file, config_file) != 0) {
        free(config_dir);
        free(config_file);
        free(temp_file);
        return -1;
    }
    
    free(config_dir);
    free(config_file);
    free(temp_file);
    return 0;
}