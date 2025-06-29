IDE: Visual Studio Code
A> Set-up clamAV:
1/ Cấu hình Path cho clamAV:
- Vào taskbar, search và chọn "Edit the system environment variables" -> Advanced -> Environment variables -> Chọn Path (trong User variables for user), sau đó Edit + New và dán địa chỉ folder của clamAV sau khi giải nén. Làm tương tự cho phần Path của System variables.

2/ Cấu hình database cho clamAV
- Tạo folder database trong folder clamAV.
- Nhập "cmd" tại thanh địa chỉ của folder clamAV và nhập các lệnh sau: 
copy .\conf_examples\freshclam.conf.sample .\freshclam.conf
copy .\conf_examples\clamd.conf.sample .\clamd.conf
write.exe .\freshclam.conf (hoặc notepad .\freshclam.conf nếu báo lỗi). Sau đó xóa dòng "Example" và ký tự "#" tại dòng "#DatabaseDirectory "C:\Program Files\ClamAV\database". Đổi đường dẫn "C:\Program Files\ClamAV\database: thành đường dẫn của folder database.
write.exe .\clamd.conf (hoặc notepad .\clamd.conf nếu báo lỗi). Sau đó xóa dòng "Example".
- Tiếp tục nhập "freshclam" trên terminad để khởi tạo database cho clamAV.

B> Compile file:
- ftp_client.cpp: g++ ftp_client.cpp -o ftp_client.exe -lws2_32
- clamav_agent.cpp: g++ clamav_agent.cpp -o clamav_agent.exe -lws2_32

B> Cách chạy chương trình:
1/ Máy 1: Chạy Filezilla Server.
2/ Máy 2: Chạy file clamav_agent.exe (./clamav_agent.exe)
3/ Máy 3: Chạy file ftp_client.exe (./ftp_client.exe). Terminal sẽ hiện:
Enter FTP server address: -> Nhập địa chỉ Filezilla Server
Username: -> Nhập username đã cài trên Filezilla Server
Password: -> Nhập password của user trên Filezilla Server
ClamAV agent address: -> Nhập địa chỉ của máy chạy clamav_agent.exe (Chạy lệnh ipconfig trên command prompt của máy và lấy địa chỉ IPv4)
Nếu hiện "230 login successful" thì đã kết nối Filezilla Server thành công, có thể chạy các lệnh được hỗ trợ.
