# Remote Proc FUSE Filesystem

Một hệ thống tập tin ảo đơn giản được viết bằng C, sử dụng FUSE (Filesystem in Userspace) và thư viện libssh2 để cho phép bạn "mount" thư mục `/proc` của một máy Linux từ xa vào hệ thống cục bộ thông qua kết nối SSH/SFTP. Sau khi mount, bạn có thể sử dụng các lệnh Linux thông thường như `ls`, `cat`, `cd`, `head`, `file`... để duyệt và xem thông tin từ `/proc` của máy từ xa ngay trên máy cục bộ của mình.

## Tính năng

* Mount thư mục `/proc` (hoặc đường dẫn cơ sở khác) từ máy Linux từ xa vào một điểm mount cục bộ.
* Truy cập **chỉ đọc** (read-only) vào các file và thư mục trong `/proc` từ xa.
* Sử dụng các lệnh shell tiêu chuẩn để tương tác (ví dụ: `ls /mnt/remote/1/status`, `cat /mnt/remote/meminfo`).
* Kết nối đến máy chủ SSH từ xa bằng giao thức SFTP.
* Hỗ trợ xác thực bằng mật khẩu hoặc khóa SSH (public key authentication).
* Cho phép tùy chỉnh cổng SSH và đường dẫn cơ sở trên máy remote.

## Yêu cầu Cài đặt

Để biên dịch và chạy dự án này, bạn cần cài đặt các gói sau trên máy cục bộ (máy sẽ chạy chương trình FUSE):

* **Trình biên dịch và Công cụ Build:**
    * `gcc`
    * `make`
    * `pkg-config`
* **Thư viện FUSE 3:**
    * `libfuse3-dev` (Debian/Ubuntu) hoặc `fuse3-devel` (Fedora/CentOS/RHEL)
    * `fuse3` (Gói runtime, thường được cài cùng `-dev`/`-devel`)
* **Thư viện Libssh2:**
    * `libssh2-1-dev` (Debian/Ubuntu) hoặc `libssh2-devel` (Fedora/CentOS/RHEL)

**Trên máy từ xa:**

* Cần có một máy chủ SSH đang chạy (`sshd`).

**Lệnh cài đặt ví dụ:**

* **Trên Debian/Ubuntu:**
    ```bash
    sudo apt update
    sudo apt install gcc make pkg-config libfuse3-dev libssh2-1-dev fuse3
    ```
* **Trên Fedora/CentOS/RHEL:**
    ```bash
    sudo dnf update
    sudo dnf install gcc make pkgconf-pkg-config fuse3-devel libssh2-devel fuse3
    ```

## Biên dịch Dự án

1.  Clone repository này (hoặc tải mã nguồn về):
    ```bash
    # Ví dụ nếu bạn đã đưa lên GitHub
    # git clone [https://github.com/your-username/remote-proc-fuse.git](https://github.com/your-username/remote-proc-fuse.git)
    # cd remote-proc-fuse
    ```
2.  Chạy lệnh `make` trong thư mục gốc của dự án:
    ```bash
    make
    ```
    Thao tác này sẽ biên dịch mã nguồn trong thư mục `src/` và tạo ra file thực thi tại `bin/remote_proc_fuse`.

## Sử dụng

1.  **Tạo một thư mục trống** để làm điểm mount trên máy cục bộ:
    ```bash
    mkdir ~/my_remote_proc
    ```
2.  **Chạy file thực thi** với các tùy chọn cần thiết:

    ```bash
    ./bin/remote_proc_fuse <mount_point> -o host=<hostname> -o user=<username> [các_tùy_chọn_khác]
    ```

    **Các tùy chọn `-o` bắt buộc:**

    * `host=<hostname>`: Địa chỉ IP hoặc hostname của máy Linux từ xa.
    * `user=<username>`: Tên người dùng để đăng nhập SSH.

    **Các tùy chọn `-o` tùy chọn:**

    * `port=<port>`: Cổng SSH trên máy từ xa (mặc định là 22).
    * `pass=<password>`: Mật khẩu để đăng nhập SSH. **(Cảnh báo: Sử dụng tùy chọn này không an toàn vì mật khẩu hiển thị trong lịch sử lệnh và danh sách tiến trình. Nên ưu tiên dùng khóa SSH).**
    * `key=<keyfile>`: Đường dẫn đến file khóa riêng (private key) SSH để xác thực (ví dụ: `~/.ssh/id_rsa`).
    * `pass=<passphrase>`: Nếu khóa SSH của bạn có mật khẩu (passphrase), hãy sử dụng tùy chọn này *cùng với* tùy chọn `key=...` để cung cấp passphrase.
    * `remoteprocpath=<path>`: Đường dẫn cơ sở trên hệ thống từ xa để mount (mặc định là `/proc`).

    **Cờ FUSE hữu ích:**

    * `-f`: Chạy ở chế độ foreground (không tách ra chạy nền). Hữu ích cho việc xem log trực tiếp. Nhấn `Ctrl+C` để dừng và unmount.
    * `-d`: Chạy ở chế độ debug FUSE (và bật log debug của ứng dụng này). Rất hữu ích khi kết hợp với `-f`.

3.  **Unmount Filesystem:** Khi bạn sử dụng xong, hãy unmount filesystem:
    ```bash
    fusermount3 -u <mount_point>
    ```
    Ví dụ:
    ```bash
    fusermount3 -u ~/my_remote_proc
    ```

## Ví dụ

* **Mount sử dụng mật khẩu (kém an toàn):**
    ```bash
    ./bin/remote_proc_fuse ~/my_remote_proc -o host=192.168.1.100 -o user=myuser -o pass=mypassword123
    ```

* **Mount sử dụng khóa SSH (khuyến nghị):**
    ```bash
    # Key không có passphrase
    ./bin/remote_proc_fuse ~/my_remote_proc -o host=server.example.com -o user=admin -o key=~/.ssh/id_rsa

    # Key có passphrase
    ./bin/remote_proc_fuse ~/my_remote_proc -o host=server.example.com -o user=admin -o key=/path/to/mykey -o pass=key_passphrase
    ```

* **Mount vào localhost để kiểm thử (cần sshd chạy trên localhost):**
    ```bash
    ./bin/remote_proc_fuse ~/my_remote_proc -f -d -o host=localhost -o user=$(whoami) -o key=~/.ssh/id_rsa
    ```

* **Sau khi mount thành công:**
    ```bash
    ls -l ~/my_remote_proc
    cat ~/my_remote_proc/meminfo
    cat ~/my_remote_proc/cpuinfo
    ls ~/my_remote_proc/1/ # Xem thư mục của tiến trình PID 1
    cat ~/my_remote_proc/1/cmdline
    cd ~/my_remote_proc/net
    cat tcp
    ```

## Giới hạn

* **Chỉ đọc (Read-Only):** Không hỗ trợ các thao tác ghi, tạo, xóa file/thư mục.
* **Xử lý lỗi cơ bản:** Việc xử lý lỗi mạng (mất kết nối đột ngột) có thể chưa hoàn thiện, có thể dẫn đến treo hoặc lỗi I/O.
* **Hiệu năng:** Tốc độ truy cập có thể chậm hơn so với truy cập trực tiếp qua SSH hoặc `sftp` do có thêm lớp FUSE.
* **Bảo mật:** Cẩn thận khi sử dụng tùy chọn `pass=` trên dòng lệnh. Luôn ưu tiên sử dụng xác thực bằng khóa SSH.
* **Tính tương thích:** Mặc dù mục tiêu là `/proc`, một số file hoặc thư mục đặc biệt trong `/proc` với hành vi không chuẩn có thể không hoạt động hoàn hảo.