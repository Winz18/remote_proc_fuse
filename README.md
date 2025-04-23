# RemoteFS - FUSE-based Remote Filesystem

RemoteFS là một hệ thống tập tin đa năng được phát triển bằng C, cho phép bạn "mount" bất kỳ thư mục nào từ máy chủ Linux từ xa vào hệ thống cục bộ thông qua kết nối SSH/SFTP. Ứng dụng này sử dụng FUSE (Filesystem in Userspace) và thư viện libssh2 để tạo ra một cầu nối liền mạch giữa hệ thống cục bộ và từ xa.

![RemoteFS Logo](https://example.com/remotefs-logo.png)

## Tổng quan

RemoteFS cho phép bạn làm việc với dữ liệu từ xa như thể chúng đang ở trên máy tính cục bộ của bạn. Sau khi mount, bạn có thể:

- Duyệt thư mục từ xa với các lệnh cục bộ như `ls`, `cd`, `find`
- Đọc nội dung file từ xa với `cat`, `less`, `head`, `tail`
- Chỉnh sửa file từ xa bằng trình soạn thảo yêu thích như VS Code, Vim, Nano
- Sao chép, di chuyển, đổi tên và xóa file từ xa
- Tự động đồng bộ hóa các thay đổi với máy chủ từ xa
- **Sao chép và di chuyển file giữa hệ thống cục bộ và từ xa** với lệnh `remote-cp` và `remote-mv`

## Tính năng chính

- **Truy cập từ xa liền mạch**: Mount bất kỳ thư mục nào từ máy chủ Linux từ xa
- **Tương thích đa dạng**: Hoạt động với các lệnh shell tiêu chuẩn và ứng dụng đồ họa
- **Hỗ trợ đầy đủ đọc/ghi**: Không chỉ xem mà còn có thể chỉnh sửa nội dung từ xa
- **Hỗ trợ IDE**: Tương thích với VS Code, Sublime Text, và các IDE khác
- **Bảo mật cao**: Xác thực qua SSH với hỗ trợ khóa công khai và mật khẩu
- **Hiệu năng tối ưu**: Được thiết kế để giảm thiểu độ trễ mạng
- **Tiện ích hỗ trợ**: Các lệnh `remote-cp` và `remote-mv` để dễ dàng sao chép và di chuyển file

## Yêu cầu hệ thống

### Trên máy cục bộ (client)

- **Hệ điều hành**: Linux (đã thử nghiệm trên Ubuntu, Debian, Fedora, Kali)
- **Trình biên dịch**: gcc, make, pkg-config
- **Thư viện**:
  * FUSE 3 (`libfuse3-dev` trên Debian/Ubuntu)
  * libssh2 (`libssh2-1-dev` trên Debian/Ubuntu)

### Trên máy chủ từ xa (server)

- **Hệ điều hành**: Bất kỳ hệ thống Linux nào có SSH server
- **Dịch vụ**: OpenSSH (hoặc tương đương)

## Cài đặt

### Cài đặt các gói phụ thuộc

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

### Biên dịch RemoteFS

1. Clone repository (hoặc tải mã nguồn về):
   ```bash
   git clone https://github.com/yourusername/remotefs.git
   cd remotefs
   ```

2. Biên dịch mã nguồn:
   ```bash
   make
   ```

3. (Tùy chọn) Cài đặt vào hệ thống:
   ```bash
   sudo make install
   ```

## Hướng dẫn sử dụng

### Cách mount một thư mục từ xa

1. Tạo một thư mục mount point trên máy cục bộ:
   ```bash
   mkdir ~/remote_mount
   ```

2. Mount thư mục từ xa:
   ```bash
   remotefs ~/remote_mount -o host=server.example.com -o user=username -o key=~/.ssh/id_rsa -o remotepath=/path/to/remote/directory
   ```

   hoặc nếu bạn chưa cài đặt:
   ```bash
   ./bin/remotefs ~/remote_mount -o host=server.example.com -o user=username -o key=~/.ssh/id_rsa -o remotepath=/path/to/remote/directory
   ```

### Các tùy chọn mount

| Tùy chọn | Mô tả | Mặc định |
|----------|-------|---------|
| host=HOSTNAME | Địa chỉ IP hoặc hostname của máy chủ từ xa | (bắt buộc) |
| user=USERNAME | Tên người dùng SSH | (bắt buộc) |
| port=PORT | Cổng SSH | 22 |
| key=KEYFILE | Đường dẫn đến file khóa SSH | Không có |
| pass=PASSWORD | Mật khẩu SSH hoặc passphrase của khóa | Không có |
| remotepath=PATH | Đường dẫn thư mục trên máy chủ từ xa | / |
| readonly | Mount ở chế độ chỉ đọc | Không |
| allow_other | Cho phép các người dùng khác truy cập | Không |

### Các cờ FUSE hữu ích

- `-f`: Chạy ở chế độ foreground (xem log trực tiếp)
- `-d`: Chế độ debug với nhiều thông tin hơn
- `-o allow_other`: Cho phép các người dùng khác truy cập vào mount point
- `-o default_permissions`: Áp dụng kiểm tra quyền truy cập chuẩn

### Các công cụ hỗ trợ

RemoteFS cung cấp các công cụ bổ sung để làm việc với hệ thống file từ xa:

#### remote-cp: Sao chép file giữa hệ thống cục bộ và từ xa

```bash
remote-cp [options] <source> <destination>
```

Ví dụ:
```bash
# Sao chép file từ máy cục bộ lên máy từ xa
remote-cp localfile.txt /mnt/remote/path/

# Sao chép file từ máy từ xa về máy cục bộ
remote-cp /mnt/remote/file.txt ./

# Sao chép và đổi tên file
remote-cp file1.txt /mnt/remote/file2.txt
```

Tùy chọn:
- `-v, --verbose`: Hiển thị thông tin chi tiết
- `-r, --recursive`: Sao chép thư mục và nội dung bên trong (chưa hỗ trợ đầy đủ)
- `-h, --help`: Hiển thị trợ giúp

#### remote-mv: Di chuyển file giữa hệ thống cục bộ và từ xa

```bash
remote-mv [options] <source> <destination>
```

Ví dụ:
```bash
# Di chuyển file từ máy cục bộ lên máy từ xa
remote-mv localfile.txt /mnt/remote/path/

# Di chuyển file từ máy từ xa về máy cục bộ
remote-mv /mnt/remote/file.txt ./

# Di chuyển và đổi tên file
remote-mv file1.txt /mnt/remote/file2.txt
```

Tùy chọn:
- `-v, --verbose`: Hiển thị thông tin chi tiết
- `-h, --help`: Hiển thị trợ giúp

### Cách unmount

```bash
fusermount3 -u ~/remote_mount
```

## Ví dụ thực tế

### Truy cập thư mục home từ xa
```bash
remotefs ~/remote_home -o host=myserver.com -o user=john -o key=~/.ssh/id_rsa -o remotepath=/home/john
```

### Chỉnh sửa file cấu hình từ xa với VS Code
```bash
# Sau khi mount
code ~/remote_mount/etc/config.conf
```

### Sao chép dữ liệu từ máy cục bộ lên máy chủ từ xa
```bash
cp ~/documents/report.pdf ~/remote_mount/documents/
```

### Tìm kiếm trên hệ thống file từ xa
```bash
find ~/remote_mount -name "*.log" -type f -mtime -7
```

### Xem log hệ thống từ xa
```bash
tail -f ~/remote_mount/var/log/syslog
```

## Khắc phục sự cố

### Các vấn đề thường gặp

1. **Lỗi "Transport endpoint is not connected"**
   - Kiểm tra kết nối mạng
   - Xác nhận máy chủ SSH đang hoạt động
   - Thử unmount và mount lại

2. **Không thể ghi file**
   - Kiểm tra quyền truy cập trên máy chủ từ xa
   - Đảm bảo không mount với tùy chọn readonly
   - Kiểm tra quota và dung lượng ổ đĩa

3. **Hiệu suất chậm**
   - Giảm độ trễ mạng nếu có thể
   - Sử dụng compression: thêm `-o compression=yes`
   - Tăng kích thước cache: thêm `-o cache_timeout=600`

## Giới hạn hiện tại

- Hiệu suất có thể bị ảnh hưởng bởi tốc độ mạng và độ trễ
- Không hỗ trợ các thao tác nguyên tử đặc biệt
- Trường hợp cắt ngắn file (truncate) phức tạp có thể không hoạt động tốt với một số trình soạn thảo
- Có thể mất kết nối nếu phiên SSH bị ngắt đột ngột

## Bảo mật

- **KHÔNG** sử dụng tùy chọn `pass=` trên dòng lệnh trong môi trường sản xuất vì mật khẩu sẽ hiển thị trong lịch sử lệnh và ps
- Luôn sử dụng xác thực khóa SSH khi có thể
- Cân nhắc sử dụng mount ở chế độ chỉ đọc nếu không cần chỉnh sửa
- Giới hạn quyền truy cập vào mount point (chmod 700)

## Đóng góp

Đóng góp luôn được hoan nghênh! Nếu bạn muốn cải thiện RemoteFS:

1. Fork repository
2. Tạo nhánh tính năng (`git checkout -b feature/amazing-feature`)
3. Commit thay đổi (`git commit -m 'Add some amazing feature'`)
4. Push lên nhánh (`git push origin feature/amazing-feature`)
5. Mở Pull Request

## Giấy phép

Dự án này được phân phối dưới giấy phép MIT. Xem file `LICENSE` để biết thêm chi tiết.

## Tác giả

- **Tên của bạn** - [GitHub](https://github.com/yourusername)

## Lời cảm ơn

- Dự án FUSE vì đã tạo ra một framework tuyệt vời
- Libssh2 vì đã cung cấp API SSH/SFTP đáng tin cậy
- Cộng đồng nguồn mở vì những đóng góp và phản hồi quý báu
