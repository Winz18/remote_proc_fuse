**remotefs - Hệ thống file FUSE cho SSH/SFTP**

**remotefs** là một client hệ thống file dựa trên FUSE (Filesystem in Userspace) được viết bằng ngôn ngữ C, cho phép bạn mount (gắn kết) các thư mục từ một máy chủ Linux từ xa vào hệ thống cục bộ của bạn thông qua kết nối SSH/SFTP an toàn. Dự án này cũng bao gồm các tiện ích hỗ trợ (`remote-cp`, `remote-mv`) để sao chép và di chuyển file/thư mục giữa máy cục bộ và máy từ xa một cách thuận tiện.

**Tổng quan**

**remotefs** giúp bạn làm việc với dữ liệu từ xa như thể chúng đang nằm trên máy tính của bạn. Sau khi mount một thư mục từ xa, bạn có thể:

* Duyệt các thư mục từ xa bằng các lệnh cục bộ như `ls`, `cd`, `find`, hoặc các trình quản lý file đồ họa.
* Đọc nội dung file từ xa với `cat`, `less`, `head`, `tail`,...
* **Chỉnh sửa file từ xa** bằng các trình soạn thảo yêu thích của bạn (VS Code, Vim, Mousepad, Nano, Geany,...).
* Sao chép, di chuyển, đổi tên, xóa file và thư mục từ xa bằng các công cụ chuẩn hoặc các tiện ích đi kèm.
* Tương tác với hệ thống file từ xa một cách liền mạch thông qua các ứng dụng cục bộ.
* Dễ dàng chuyển dữ liệu giữa máy cục bộ và các điểm mount từ xa bằng `remote-cp` và `remote-mv`.

**Tính năng chính**

* **Mount thư mục từ xa:** Gắn kết bất kỳ thư mục nào từ server SSH/SFTP.
* **Xác thực linh hoạt:** Hỗ trợ xác thực bằng mật khẩu hoặc khóa SSH (private key).
* **Hỗ trợ Đọc/Ghi:** Cho phép đọc, ghi, chỉnh sửa và quản lý file/thư mục từ xa.
* **Tương thích ứng dụng:** Hoạt động tốt với các trình soạn thảo mã nguồn, IDE, và các công cụ dòng lệnh chuẩn.
* **Thao tác file/thư mục cơ bản:** Hỗ trợ `getattr`, `readdir`, `open`, `read`, `write`, `release`, `create`, `unlink`, `rename`, `truncate`, `mkdir`, `rmdir`, `fsync`, `access`.
* **Tiện ích hỗ trợ:**
    * `remote-cp`: Sao chép file và thư mục (hỗ trợ đệ quy `-r`) giữa hệ thống cục bộ và các điểm mount `remotefs`.
    * `remote-mv`: Di chuyển file và thư mục giữa cục bộ và remote, hoặc đổi tên/di chuyển giữa các vị trí trên cùng một điểm mount remote.
* **Lưu cấu hình:** Tự động lưu thông tin kết nối khi mount để các tiện ích `remote-cp`, `remote-mv` có thể sử dụng lại mà không cần nhập lại thông tin host/user/pass/key.
* **Caching:** Sử dụng cơ chế caching cơ bản của FUSE để cải thiện hiệu năng truy cập.

**Yêu cầu hệ thống**

* **Máy cục bộ (Client)**
    * **Hệ điều hành:** Linux (đã thử nghiệm trên Kali, nên hoạt động trên Ubuntu, Debian, Fedora, Arch, v.v.)
    * **Trình biên dịch & Công cụ:** `gcc`, `make`, `pkg-config`
    * **Thư viện:**
        * `libfuse3-dev` (hoặc tên gói tương đương trên bản phân phối của bạn)
        * `libssh2-1-dev` (hoặc tên gói tương đương)
        * `fuse3` (cần thiết khi chạy)
* **Máy chủ từ xa (Server)**
    * **Hệ điều hành:** Bất kỳ hệ thống Linux/Unix nào có chạy SSH server.
    * **Dịch vụ:** OpenSSH server (hoặc tương đương) với SFTP subsystem được bật (đây là cấu hình mặc định phổ biến).

**Cài đặt**

1.  **Cài đặt các gói phụ thuộc**

    * **Trên Debian/Ubuntu/Kali:**
        ```bash
        sudo apt update
        sudo apt install gcc make pkg-config libfuse3-dev libssh2-1-dev fuse3
        ```
    * **Trên Fedora/RHEL/CentOS:**
        ```bash
        sudo dnf update
        sudo dnf install gcc make pkgconf-pkg-config fuse3-devel libssh2-devel fuse3
        ```
    * **Trên Arch Linux:**
        ```bash
        sudo pacman -Syu
        sudo pacman -S gcc make pkg-config fuse3 libssh2
        ```

2.  **Biên dịch remotefs**

    * Clone repository (hoặc tải mã nguồn về):
        ```bash
        # Thay bằng URL repository của bạn nếu có
        git clone https://github.com/Winz18/remotefs.git
        cd remotefs
        ```
    * Biên dịch mã nguồn:
        ```bash
        make clean && make
        ```
        Các file thực thi sẽ nằm trong thư mục `bin/`.

3.  **Cài đặt (Tùy chọn)**

    Nếu bạn muốn cài đặt vào hệ thống để có thể gọi lệnh `remotefs`, `remote-cp`, `remote-mv` từ bất kỳ đâu:
    ```bash
    sudo make install
    ```
    Lệnh này thường sẽ cài các file thực thi vào `/usr/local/bin`.

**Hướng dẫn sử dụng**

1.  **Mount thư mục từ xa**

    * **Tạo điểm mount cục bộ:** (Là một thư mục trống trên máy của bạn)
        ```bash
        mkdir ~/my_remote_server
        ```
    * **Chạy lệnh `remotefs`:**
        Sử dụng đường dẫn đến file thực thi trong thư mục `bin/` nếu chưa cài đặt, hoặc gọi trực tiếp nếu đã cài.

        **Cú pháp:**
        ```
        remotefs [FUSE flags] <mountpoint> -o host=<hostname> -o user=<username> [-o port=<port>] [-o pass=<password_or_passphrase> | -o key=<path_to_private_key>] [-o remotepath=<remote_directory>] [other_fuse_options]
        ```

        **Các tùy chọn `-o` quan trọng:**
        * `host=<hostname>`: (Bắt buộc) Địa chỉ IP hoặc tên miền của server SSH.
        * `user=<username>`: (Bắt buộc) Tên người dùng SSH để đăng nhập.
        * `port=<port>`: Cổng SSH trên server (mặc định: 22).
        * `pass=<password>`: Mật khẩu SSH hoặc passphrase cho key SSH (Lưu ý: **Không an toàn** khi dùng trực tiếp trên dòng lệnh).
        * `key=<path_to_key>`: Đường dẫn đến file private key SSH (ví dụ: `~/.ssh/id_rsa`). Nên sử dụng thay cho `pass`.
        * `remotepath=<path>`: Thư mục trên server từ xa mà bạn muốn mount (mặc định: `/` - thư mục gốc, thường bạn sẽ muốn chỉ định cụ thể hơn như `/home/username`).

        **Ví dụ:**

        * Mount thư mục `/home/user_remote` trên server `192.168.1.100` bằng key SSH:
            ```bash
            ./bin/remotefs ~/my_remote_server -o host=192.168.1.100 -o user=user_remote -o key=~/.ssh/id_rsa -o remotepath=/home/user_remote
            ```
        * Mount thư mục `/var/www` trên server `yourdomain.com` bằng mật khẩu (chỉ nên dùng cho mục đích kiểm thử nhanh):
            ```bash
            ./bin/remotefs ~/my_remote_server -o host=yourdomain.com -o user=admin -o pass=your_password -o remotepath=/var/www
            ```

        **Các cờ FUSE hữu ích (đặt trước điểm mount):**
        * `-f`: Chạy ở chế độ foreground (hiển thị log trực tiếp trên terminal, nhấn Ctrl+C để unmount).
        * `-d`: Chạy ở chế độ debug (kết hợp với `-f` để xem log chi tiết của FUSE và `remotefs`).
        * `-o allow_other`: Cho phép các người dùng khác trên máy cục bộ truy cập vào điểm mount (cần cấu hình trong `/etc/fuse.conf`).
        * `-o default_permissions`: Để FUSE kiểm tra quyền truy cập dựa trên mode của file (thường không cần thiết vì `remotefs` đã có hàm `access`).

        **Lưu ý:** Khi `remotefs` mount thành công, nó sẽ lưu thông tin kết nối (host, user, port, key/pass, remotepath) vào file cấu hình (thường là `/root/.config/remotefs/` nếu chạy bằng root, hoặc `~/.config/remotefs/` nếu chạy bằng user thường) để các lệnh `remote-cp` và `remote-mv` sử dụng.

2.  **Unmount (Gỡ gắn kết)**

    Khi không cần truy cập nữa, hãy unmount bằng lệnh:
    ```bash
    fusermount3 -u ~/my_remote_server
    ```
    *(Thay `~/my_remote_server` bằng điểm mount thực tế của bạn)*

3.  **Sử dụng tiện ích `remote-cp` và `remote-mv`**

    Các công cụ này giúp bạn dễ dàng chuyển dữ liệu giữa máy cục bộ và các điểm mount `remotefs` đã được cấu hình.

    * **`remote-cp` (Sao chép):**
        ```bash
        remote-cp [options] <source> <destination>
        ```
        * **Options:**
            * `-r`, `--recursive`: Sao chép thư mục đệ quy.
            * `-v`, `--verbose`: Hiển thị thông tin chi tiết.
            * `-h`, `--help`: Hiển thị trợ giúp.
        * **Ví dụ:**
            ```bash
            # Local -> Remote (File)
            ./bin/remote-cp ./local_file.txt ~/my_remote_server/

            # Remote -> Local (File)
            ./bin/remote-cp ~/my_remote_server/remote_file.log ./backup/

            # Local -> Remote (Thư mục)
            ./bin/remote-cp -r ./local_folder ~/my_remote_server/

            # Remote -> Local (Thư mục)
            ./bin/remote-cp -r ~/my_remote_server/remote_project ./
            ```

    * **`remote-mv` (Di chuyển / Đổi tên):**
        ```bash
        remote-mv [options] <source> <destination>
        ```
        * **Options:**
            * `-v`, `--verbose`: Hiển thị thông tin chi tiết.
            * `-h`, `--help`: Hiển thị trợ giúp.
        * **Ví dụ:**
            ```bash
            # Local -> Remote (File)
            ./bin/remote-mv ./upload_me.zip ~/my_remote_server/

            # Remote -> Local (File)
            ./bin/remote-mv ~/my_remote_server/important.dat ./local_archive/

            # Remote -> Remote (Đổi tên/Di chuyển trên cùng server)
            ./bin/remote-mv ~/my_remote_server/old_name.txt ~/my_remote_server/new_name.txt
            ./bin/remote-mv ~/my_remote_server/file.txt ~/my_remote_server/subfolder/
            ```
        * **Lưu ý:** `remote-mv` không thể di chuyển thư mục từ Local -> Remote (do hạn chế hiện tại, hãy dùng `remote-cp -r` rồi xóa nguồn). Di chuyển thư mục Remote -> Local có thể chưa được hỗ trợ đầy đủ.

4.  **Sử dụng với các ứng dụng khác (VS Code, Vim, ...)**

    Sau khi mount thành công, chỉ cần mở file hoặc thư mục bên trong điểm mount (`~/my_remote_server`) bằng ứng dụng bạn muốn. Ví dụ:
    ```bash
    code ~/my_remote_server/my_project_folder
    vim ~/my_remote_server/config.conf
    mousepad ~/my_remote_server/notes.txt
    ```
    Việc đọc, ghi, lưu file sẽ được `remotefs` xử lý ngầm qua SFTP.

**Cách hoạt động**

1.  **FUSE:** Kernel Linux sử dụng FUSE để chặn các lời gọi hệ thống (syscalls) liên quan đến file/thư mục trên điểm mount.
2.  **remotefs:** Tiến trình `remotefs` nhận các yêu cầu từ FUSE (ví dụ: đọc, ghi, mở, ...).
3.  **libssh2/SFTP:** `remotefs` sử dụng thư viện `libssh2` để dịch các yêu cầu FUSE thành các lệnh của giao thức SFTP và gửi chúng đến server SSH từ xa. Kết quả từ server được gửi trả lại cho FUSE và ứng dụng gốc.
4.  **Helper Utilities:** `remote-cp` và `remote-mv` đọc file cấu hình đã lưu (`~/.config/remotefs/connections.conf`, `mounts.conf`) để lấy thông tin kết nối và thực hiện truyền dữ liệu trực tiếp qua SFTP bằng `libssh2`.

**Vấn đề bảo mật**

* **Không nên** sử dụng tùy chọn `-o pass=...` trực tiếp trên dòng lệnh trong môi trường thực tế vì mật khẩu có thể bị lộ qua lịch sử lệnh hoặc danh sách tiến trình.
* **Ưu tiên sử dụng xác thực bằng khóa SSH (`-o key=...`).**
* Cẩn thận khi sử dụng cờ `-o allow_other` vì nó cho phép người dùng khác truy cập điểm mount.

**Đóng góp**

Chào mừng các đóng góp! Nếu bạn muốn cải thiện `remotefs`:

1.  Fork repository.
2.  Tạo một nhánh mới cho tính năng/sửa lỗi của bạn (`git checkout -b feature/my-new-feature`).
3.  Thực hiện thay đổi và commit (`git commit -am 'Add some feature'`).
4.  Push lên nhánh của bạn (`git push origin feature/my-new-feature`).
5.  Mở một Pull Request.

**Lời cảm ơn**

* Dự án [FUSE (Filesystem in Userspace)](https://github.com/libfuse/libfuse) đã cung cấp một framework tuyệt vời.
* Thư viện [libssh2](https://www.libssh2.org/) đã cung cấp API mạnh mẽ cho SSH/SFTP.
