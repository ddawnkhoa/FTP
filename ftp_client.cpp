#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <windows.h>
#include <wininet.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")

#define BUFFER_SIZE 4096

using namespace std;

HINTERNET hInternet = NULL;
HINTERNET hConnect = NULL;

string ftpServer = "127.0.0.1";
string username = "sinhvien";
string password = "1234";

bool promptMode = true; // Toggle confirmation for mget / mput operations if true
DWORD transferMode = FTP_TRANSFER_TYPE_BINARY; // file transfer mode (text/binary)
bool passiveMode = true;

bool sendFileToScan(const char* filename, const char* host, int port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Connection failed.\n";
        return false;
    }

    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Cannot open file." << endl;
        return false;
    }

    char buffer[BUFFER_SIZE];
    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        int bytesRead = file.gcount();
        send(sock, buffer, bytesRead, 0);
    }

    char result[32] = {};
    recv(sock, result, sizeof(result), 0);
    cout << "[Client] Scan Result: " << result << endl;

    closesocket(sock);
    WSACleanup();

    return string(result) == "OK";
}

bool uploadToFTP(HINTERNET hConnect, const char* filename, const char* remoteDir = "/") {
    string remotePath = string(remoteDir) + "/" + filename;

    BOOL result = FtpPutFileA(hConnect, filename, remotePath.c_str(), transferMode, 0);
    if (!result) {
        cerr << "[FTP] Upload failed.\n";
    } else {
        cout << "[FTP] Upload successful.\n";
    }
    return result;
}

void downloadFile(HINTERNET hConnect, const string& remoteFile, const string& localFile) {
    if (FtpGetFileA(
            hConnect,
            remoteFile.c_str(),
            localFile.c_str(),
            FALSE, // override local file if existed already
            FILE_ATTRIBUTE_NORMAL,
            transferMode,
            0)) 
    {
        cout << "[FTP] Downloaded file: " << remoteFile << endl;
    } else {
        cerr << "[FTP] Failed to download file: " << remoteFile << endl;
    }
}

void multiUpload(HINTERNET hConnect, const char* ftpServer, const string& remoteDir = "/") {
    cout << "Enter files to upload (space-separated): ";
    string line;
    cin.ignore(); 
    getline(cin, line);

    istringstream iss(line);
    string filename;
    while (iss >> filename) {
        if (promptMode) {
        cout << "Upload " << filename << "? (y/n): ";
        string ans;
        cin >> ans;
        if (ans != "y" && ans != "Y") continue;
        }

        cout << ">> Scanning: " << filename << "\n";
        if (!sendFileToScan(filename.c_str(), ftpServer, 9001)) {
            cerr << "[Client] Skipped (infected): " << filename << endl;
            continue;
        }
        uploadToFTP(hConnect, filename.c_str(), remoteDir.c_str());
    }
}

void multiDownload(HINTERNET hConnect) {
    cout << "Enter remote files to download (space-separated): ";
    string line;
    cin.ignore();
    getline(cin, line);

    istringstream iss(line);
    string remoteFile;
    while (iss >> remoteFile) {
        if (promptMode) {
        cout << "Download " << remoteFile << "? (y/n): ";
        string ans;
        cin >> ans;
        if (ans != "y" && ans != "Y") continue;
        }
    

        if (FtpGetFileA(hConnect,
                        remoteFile.c_str(),
                        remoteFile.c_str(),
                        FALSE, // override local file if existed already
                        FILE_ATTRIBUTE_NORMAL,
                        transferMode,
                        0)) 
        {
            cout << "[FTP] Downloaded: " << remoteFile << endl;
        } else {
            cerr << "[FTP] Failed to download: " << remoteFile << endl;
        }
    }
}

void listFTPDirectory(HINTERNET hConnect) {
    WIN32_FIND_DATAA findData;
    HINTERNET hFind = FtpFindFirstFileA(hConnect, "*", &findData, INTERNET_FLAG_RELOAD, 0);

    if (!hFind) {
        cerr << "[FTP] cannot list directory.";
        return;
    }

    cout << "[FTP] Directory listing: " << endl;
    do { 
        cout << " - " << findData.cFileName;
        if (findData.dwFileAttributes && FILE_ATTRIBUTE_DIRECTORY) {
            cout << " [DIRECTORY]";
        }
        cout << endl;
    }
    while (InternetFindNextFileA(hFind, &findData));
    InternetCloseHandle(hFind);
}

void changeDirectory(HINTERNET hConnect, const string& dir) {
    if (!FtpSetCurrentDirectoryA(hConnect, dir.c_str() )) {
        cerr << "[FTP] Cannot change directory" << endl;
    }
    else cout << "[FTP] Current directory change to: " << dir << endl;
}

void showCurrentDirectory(HINTERNET hConnect) {
    char currentDir[MAX_PATH];
    DWORD size = sizeof(currentDir);

    if (FtpGetCurrentDirectoryA(hConnect, currentDir, &size)) {
        cout << "[FTP] Current directory: " << currentDir << endl;
    } else {
        cerr << "[FTP] Failed to get current directory." << endl;
    }
}

void makeDirectory(HINTERNET hConnect, const string& dir) {
    if (FtpCreateDirectoryA(hConnect, dir.c_str())) {
        cout << "[FTP] Created directory: " << dir << endl;
    }
    else cout << "[FTP] Failed to create diretory: " << dir << endl;
}

void removeDirectory(HINTERNET hConnect, const string& dir) {
    if (FtpRemoveDirectoryA(hConnect, dir.c_str())) {
        cout << "[FTP] Removed directory: " << dir << endl;
    }
    else cout << "[FTP] Failed to remove directory: " << dir << endl;
}

void deleteFIle(HINTERNET hConnect, const string& filename) {
    if (FtpDeleteFileA(hConnect, filename.c_str())) {
        cout << "[FTP] Deleted file: " << filename << endl;
    }
    else cout << "[FTP] Failed to delete file: " << filename << endl; 
}

void renameFile(HINTERNET hConnect, const string& filename, const string& newname) {
    if (FtpRenameFileA(hConnect, filename.c_str(), newname.c_str())) {
        cout << "[FTP] Renamed file " << filename << " to " << newname << endl;
    }
    else cout << "[FTP] Failed to rename file " << filename << " to " << newname << endl;
}

void showStatus(HINTERNET hConnect, const char* ftpServer) {
    cout << "====== FTP Session Status ======" << endl;
    cout << "Server:        " << ftpServer << endl;
    cout << "Connected:     " << (hConnect ? "YES" : "NO") << endl;;

    if (hConnect) {
        char currentDir[MAX_PATH];
        DWORD size = sizeof(currentDir);
        if (FtpGetCurrentDirectoryA(hConnect, currentDir, &size)) {
            cout << "Remote Dir:    " << currentDir << endl;
        } else {
            cout << "Remote Dir:    [Unknown]" << endl;
        }
    }

    cout << "Transfer Mode: " << (transferMode == FTP_TRANSFER_TYPE_BINARY ? "BINARY" : "ASCII") << endl;
    cout << "Prompt:        " << (promptMode ? "ON" : "OFF") << endl;
    cout << "Passive:       " << (passiveMode ? "ON" : "OFF") << endl;
    cout << "================================\n";
}

bool openConnection() {
    if (hInternet) InternetCloseHandle(hInternet);
    hInternet = InternetOpenA("FTPClient", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        cerr << "[FTP] InternetOpen failed." << endl;
        return false;
    }

    DWORD flags = passiveMode ? INTERNET_FLAG_PASSIVE : 0;
    hConnect = InternetConnectA(hInternet, ftpServer.c_str(), INTERNET_DEFAULT_FTP_PORT,username.c_str(), password.c_str(), INTERNET_SERVICE_FTP,flags,0);

    if (!hConnect) {
        cerr << "[FTP] InternetConnect failed." << endl;
        InternetCloseHandle(hInternet);
        hInternet = NULL;
        return false;
    }

    cout << "[FTP] Connected to " << ftpServer << endl;
    return true;
}

void closeConnection() {
    if (hConnect) {
        InternetCloseHandle(hConnect);
        hConnect = NULL;
    }
    if (hInternet) {
        InternetCloseHandle(hInternet);
        hInternet = NULL;
    }
    cout << "[FTP] Connection closed.\n";
}

void showHelp() {
    cout << "======= FTP Client Commands =======\n";
    cout << "open             : Connect to FTP server\n";
    cout << "close            : Disconnect from FTP server\n";
    cout << "status           : Show current session status\n";
    cout << "ls               : List files in current directory\n";
    cout << "cd               : Change remote directory\n";
    cout << "pwd              : Show current remote directory\n";
    cout << "mkdir            : Create new directory on server\n";
    cout << "rmdir            : Remove directory from server\n";
    cout << "delete           : Delete a file from server\n";
    cout << "rename           : Rename a file on server\n";
    cout << "get / recv       : Download file from server\n";
    cout << "put              : Upload file (scanned before upload)\n";
    cout << "mget             : Download multiple files (with prompt toggle)\n";
    cout << "mput             : Upload multiple files (with prompt toggle)\n";
    cout << "ascii            : Set transfer mode to ASCII (text)\n";
    cout << "binary           : Set transfer mode to BINARY (default)\n";
    cout << "prompt           : Toggle prompt before multi get/put\n";
    cout << "passive          : Toggle passive FTP mode\n";
    cout << "help             : Show this help message\n";
    cout << "quit / bye       : Exit the FTP client\n";
    cout << "help / ?         : Show help text for commands";
    cout << "===================================\n";
}

int main() {
    // Connect to Internet
    hInternet = InternetOpenA("FTPClient", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        cerr << "InternetOpen failed." << endl;
        return 1;
    }
    else cout << "InternetOpen successful." << endl;

    DWORD dwFlags = passiveMode ? INTERNET_FLAG_PASSIVE : 0;

    hConnect = InternetConnectA(hInternet, ftpServer.c_str(), INTERNET_DEFAULT_FTP_PORT, username.c_str(), password.c_str(), INTERNET_SERVICE_FTP, dwFlags, 0);
    if (!hConnect) {
        cerr << "InternetConnect failed." << endl;
        InternetCloseHandle(hInternet);
        return 1;
    }
    else cout << "InternetConnect successful." << endl;

    // Handle commands
    string cmd = "";
    while (true) {
        cout << "ftp> ";
        cin >> cmd;
        if (cmd == "quit" || cmd == "bye") break;
        else if (cmd == "ls") listFTPDirectory(hConnect);
        else if (cmd == "cd") {
            string dir;
            cout << "Enter directory name: ";
            cin >> dir;
            changeDirectory(hConnect, dir);
        }
        else if (cmd == "pwd") showCurrentDirectory(hConnect);
        else if (cmd == "mkdir") {
            string dir;
            cout << "Enter directory to make: ";
            cin >> dir;
            makeDirectory(hConnect, dir);
        }
        else if (cmd == "rmdir") {
            string dir;
            cout << "Enter directory to remove: ";
            cin >> dir;
            removeDirectory(hConnect, dir);
        }
        else if (cmd == "delete") {
            string filename;
            cout << "Enter file to delete: ";
            cin >> filename;
            deleteFIle(hConnect, filename);
        }
        else if (cmd == "rename") {
            string filename, newname;
            cout << "Enter file to name: ";
            cin >> filename;
            cout << "Enter new name: ";
            cin >> newname;
            renameFile(hConnect, filename, newname);
        }
        else if (cmd == "get" || cmd == "recv") {
            string remote;
            cout << "Enter file to download from FTP server: ";
            cin >> remote;
            downloadFile(hConnect, remote, remote); 
        }
        else if (cmd == "put") {
            string filename;
            cout << "Enter file to put: ";
            cin >> filename;
            if (sendFileToScan(filename.c_str(), ftpServer.c_str(), 9001)) {
                uploadToFTP(hConnect, filename.c_str());
            }
            else cout << "[FTP] File is infected! Upload aborted. " << endl;
        }
        else if (cmd == "mput") multiUpload(hConnect, ftpServer.c_str());
        else if (cmd == "mget") multiDownload(hConnect);
        else if (cmd == "prompt") {
        promptMode = !promptMode;
        cout << "[FTP] Prompt mode is now " << (promptMode ? "ON" : "OFF") << endl;
        }
        else if (cmd == "ascii") {
       transferMode = FTP_TRANSFER_TYPE_ASCII;
       cout << "[FTP] Transfer mode set to ASCII (text mode)." << endl;
       }
       else if (cmd == "binary") {
       transferMode = FTP_TRANSFER_TYPE_BINARY;
       cout << "[FTP] Transfer mode set to BINARY (default)." << endl;
       }
       else if (cmd == "status") showStatus(hConnect, ftpServer.c_str());
       else if (cmd == "passive") {
       passiveMode = !passiveMode;
       cout << "[FTP] Passive mode is now " << (passiveMode ? "ON" : "OFF") << endl;

       // Reconnect with new mode
       InternetCloseHandle(hConnect);

       DWORD flags = passiveMode ? INTERNET_FLAG_PASSIVE : 0;

       hConnect = InternetConnectA(hInternet, ftpServer.c_str(), INTERNET_DEFAULT_FTP_PORT, username.c_str(), password.c_str(),INTERNET_SERVICE_FTP,flags,0);

       if (!hConnect) {
          cerr << "[FTP] Failed to reconnect in new mode." << endl;
          } 
          else {
          cout << "[FTP] Reconnected with " << (passiveMode ? "Passive" : "Active") << " mode." << endl;
          }
       }
       else if (cmd == "open") {
        if (hConnect) {
            cout << "[FTP] Already connected. " << endl;
        }
        else openConnection();
       }
       else if (cmd == "close") {
        if (!hConnect) {
            cout << "[FTP] Already unconnected" << endl;
        }
        else closeConnection();
       }
       else if (cmd == "help" || cmd == "?") showHelp();
    }

    closeConnection();
    return 0;
}