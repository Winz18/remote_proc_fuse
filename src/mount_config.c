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