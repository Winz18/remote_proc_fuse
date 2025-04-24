# RemoteFS - FUSE-based Remote Filesystem

RemoteFS is a versatile filesystem developed in C that allows you to mount any directory from a remote Linux server onto your local system via SSH/SFTP. It uses FUSE (Filesystem in Userspace) and the libssh2 library to create a seamless bridge between local and remote filesystems.

![RemoteFS Logo](https://example.com/remotefs-logo.png)

## Overview

RemoteFS lets you work with remote data as if it were on your local machine. Once mounted, you can:

- Browse remote directories using local commands like `ls`, `cd`, `find`
- Read remote files with `cat`, `less`, `head`, `tail`
- Edit remote files with your favorite editor (VS Code, Vim, Nano, etc.)
- Copy, move, rename, and delete remote files
- Automatically synchronize changes with the remote server
- **Copy and move files between local and remote systems** using the `remote-cp` and `remote-mv` utilities

## Features

- **Seamless remote access**: Mount any directory from a remote Linux server
- **Wide compatibility**: Works with standard shell commands and GUI applications
- **Full read/write support**: Not just viewing, but editing remote content
- **IDE support**: Compatible with VS Code, Sublime Text, and other IDEs
- **Strong security**: SSH authentication with support for public key and password
- **Optimized performance**: Designed to minimize network latency
- **Helper utilities**: `remote-cp` and `remote-mv` for easy file transfer

## System Requirements

### On the Local Machine (Client)

- **Operating System**: Linux (tested on Ubuntu, Debian, Fedora, Kali)
- **Compiler**: gcc, make, pkg-config
- **Libraries**:
  * FUSE 3 (`libfuse3-dev` on Debian/Ubuntu)
  * libssh2 (`libssh2-1-dev` on Debian/Ubuntu)

### On the Remote Server

- **Operating System**: Any Linux system with SSH server
- **Service**: OpenSSH (or equivalent)

## Installation

### Install Dependencies

#### Debian/Ubuntu/Kali:
```bash
sudo apt update
sudo apt install gcc make pkg-config libfuse3-dev libssh2-1-dev fuse3
```

#### Fedora/RHEL/CentOS:
```bash
sudo dnf update
sudo dnf install gcc make pkgconf-pkg-config fuse3-devel libssh2-devel fuse3
```

#### Arch Linux:
```bash
sudo pacman -Syu
sudo pacman -S gcc make pkg-config fuse3 libssh2
```

### Build RemoteFS

1. Clone the repository (or download the source):
   ```bash
   git clone https://github.com/yourusername/remotefs.git
   cd remotefs
   ```

2. Build the source code:
   ```bash
   make
   ```

3. (Optional) Install to the system:
   ```bash
   sudo make install
   ```

## Usage Guide

### Mounting a Remote Directory

1. Create a local mount point:
   ```bash
   mkdir ~/remote_mount
   ```

2. Mount the remote directory:
   ```bash
   remotefs ~/remote_mount -o host=server.example.com -o user=username -o key=~/.ssh/id_rsa -o remotepath=/path/to/remote/directory
   ```
   Or if not installed system-wide:
   ```bash
   ./bin/remotefs ~/remote_mount -o host=server.example.com -o user=username -o key=~/.ssh/id_rsa -o remotepath=/path/to/remote/directory
   ```

### Mount Options

| Option         | Description                                      | Default      |
|---------------|--------------------------------------------------|--------------|
| host=HOSTNAME | Remote server IP or hostname                     | (required)   |
| user=USERNAME | SSH username                                     | (required)   |
| port=PORT     | SSH port                                         | 22           |
| key=KEYFILE   | Path to SSH private key                          | None         |
| pass=PASSWORD | SSH password or key passphrase                   | None         |
| remotepath=PATH| Remote directory path to mount                   | /            |
| readonly      | Mount as read-only                               | No           |
| allow_other   | Allow other users to access the mount point      | No           |

### Useful FUSE Flags

- `-f`: Run in foreground (see logs directly)
- `-d`: Debug mode with more information
- `-o allow_other`: Allow other users to access the mount point
- `-o default_permissions`: Apply standard permission checks

### Helper Utilities

RemoteFS provides additional tools for working with the remote filesystem:

#### remote-cp: Copy files between local and remote

```bash
remote-cp [options] <source> <destination>
```

Examples:
```bash
# Copy a local file to remote
remote-cp localfile.txt /mnt/remote/path/

# Copy a remote file to local
remote-cp /mnt/remote/file.txt ./

# Copy and rename a file
remote-cp file1.txt /mnt/remote/file2.txt
```

Options:
- `-v, --verbose`: Show detailed information
- `-r, --recursive`: Copy directories recursively (not yet implemented)
- `-h, --help`: Show help

#### remote-mv: Move files between local and remote

```bash
remote-mv [options] <source> <destination>
```

Examples:
```bash
# Move a local file to remote
remote-mv localfile.txt /mnt/remote/path/

# Move a remote file to local
remote-mv /mnt/remote/file.txt ./

# Move and rename a file
remote-mv file1.txt /mnt/remote/file2.txt
```

Options:
- `-v, --verbose`: Show detailed information
- `-h, --help`: Show help

### Unmounting

```bash
fusermount3 -u ~/remote_mount
```

## Real-World Examples

### Access your remote home directory
```bash
remotefs ~/remote_home -o host=myserver.com -o user=john -o key=~/.ssh/id_rsa -o remotepath=/home/john
```

### Edit a remote config file with VS Code
```bash
# After mounting
vim ~/remote_mount/etc/config.conf
```

### Copy data from local to remote server
```bash
cp ~/documents/report.pdf ~/remote_mount/documents/
```

### Search files on the remote filesystem
```bash
find ~/remote_mount -name "*.log" -type f -mtime -7
```

### View remote system logs
```bash
tail -f ~/remote_mount/var/log/syslog
```

## Troubleshooting

### Common Issues

1. **"Transport endpoint is not connected" error**
   - Check your network connection
   - Ensure the SSH server is running
   - Try unmounting and remounting

2. **Cannot write files**
   - Check permissions on the remote server
   - Make sure you did not mount with the `readonly` option
   - Check disk quota and available space

3. **Slow performance**
   - Reduce network latency if possible
   - Use compression: add `-o compression=yes`
   - Increase cache size: add `-o cache_timeout=600`

## Current Limitations

- Performance may be affected by network speed and latency
- No support for special atomic operations
- Complex file truncation (truncate) may not work well with some editors
- Connection may be lost if the SSH session is interrupted

## Security

- **DO NOT** use the `pass=` option on the command line in production, as passwords will appear in command history and process lists
- Always use SSH key authentication when possible
- Consider mounting as read-only if you do not need to modify files
- Restrict access to the mount point (e.g., `chmod 700`)

## Contributing

Contributions are welcome! To improve RemoteFS:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add some amazing feature'`)
4. Push to your branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## Acknowledgments

- The FUSE project for a great framework
- Libssh2 for a reliable SSH/SFTP API
- The open source community for valuable contributions and feedback