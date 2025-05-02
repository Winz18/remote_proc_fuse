#ifndef MOUNT_CONFIG_H
#define MOUNT_CONFIG_H

char *get_config_dir();
int save_mount_point(const char *mount_point, const char *remote_path);
char **get_mount_points(int *count);
char *get_remote_path_for_mount(const char *mount_point);
int load_connection_info_for_mount(const char *mount_point, remote_conn_info_t *conn_info);
int remove_mount_point(const char *mount_point);

// Declaration for saving full connection info
int save_connection_info(const char *mount_point, const remote_conn_info_t *conn_info);

#endif // MOUNT_CONFIG_H