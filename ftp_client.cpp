#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 4096

using namespace std;

string ftpServer = "127.0.0.1";
string username = "sinhvien";
string password = "1234";
string clamAVAgent = "127.0.0.10";

SOCKET controlSocket = INVALID_SOCKET;
bool promptMode = true;
enum TransferMode { ASCII, BINARY };
TransferMode transferMode = BINARY; 
bool passiveMode = true;
bool isConnected = false;

void logAction(const string& action, const string& filename, const string& status) {
    ofstream log("ftp_client_log.txt", ios::app);
    if (log.is_open()) {
        SYSTEMTIME time;
        GetLocalTime(&time);
        log << "[" << time.wYear << "-" << setw(2) << setfill('0') << time.wMonth << "-"
            << setw(2) << time.wDay << " " << setw(2) << time.wHour << ":"
            << setw(2) << time.wMinute << ":" << setw(2) << time.wSecond << "] ";
        log << setfill(' ') << left;
        log << setw(10) << action << setw(25) << filename << setw(10) << status << endl;
        log.close();
    }
}

// Send FTP command
void sendCommand(SOCKET sock, const string& cmd) {
    string fullCmd = cmd + "\r\n";
    send(sock, fullCmd.c_str(), fullCmd.size(), 0);
}

// Receive response from FTP server
string receiveResponse(SOCKET sock) {
    char buffer[1024];
    int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received > 0) {
        buffer[received] = '\0';
        return string(buffer);
    }
    return "";
}

// Connect to FTP Server
SOCKET connectToFTPServer(const string& host, int port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "[ERROR] Failed to create socket." << endl;
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "[ERROR] Cannot connect to server." << endl;
        closesocket(sock);
        return INVALID_SOCKET;
    }

    cout << "[INFO] Connected to " << host << ":" << port << endl;
    isConnected = true;
    return sock;
}

void disconnectFromServer() {
    if (isConnected && controlSocket != INVALID_SOCKET) {
        sendCommand(controlSocket, "QUIT");
        cout << receiveResponse(controlSocket); 
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
        isConnected = false;
        cout << "[INFO] Disconnected from server." << endl;
    } else {
        cout << "[WARN] No server to disconnect." << endl;
    }
}

bool sendFileToScan(const char* filename, const char* host, int port) {
    cout << "[DEBUG] Connecting to ClamAV at " << host << ":" << port << endl;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "[ERROR] WSAStartup failed." << endl;
        logAction("scan", filename, "FAILED");
        return false;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "[ERROR] Cannot create socket." << endl;
        WSACleanup();
        logAction("scan", filename, "FAILED");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "[ERROR] Cannot connect to ClamAV Agent at " << host << ":" << port << endl;
        cerr << "[WSA ERROR] Code: " << WSAGetLastError() << endl;
        closesocket(sock);
        WSACleanup();
        logAction("scan", filename, "FAILED");
        return false;
    }

    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Cannot open file." << endl;
        WSACleanup();
        logAction("scan", filename, "FAILED");
        return false;
    }

    char buffer[BUFFER_SIZE];
    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        int bytesRead = file.gcount();
        if (send(sock, buffer, bytesRead, 0) == SOCKET_ERROR) {
            cerr << "[ERROR] Sending file data failed." << endl;
            closesocket(sock);
            WSACleanup();
            logAction("scan", filename, "FAILED");
            return false;
        }
    }

    char result[32] = {};
    int recvBytes = recv(sock, result, sizeof(result) - 1, 0);
    if (recvBytes <= 0) {
        cerr << "[ERROR] Failed to receive scan result." << endl;
        closesocket(sock);
        WSACleanup();
        logAction("scan", filename, "FAILED");
        return false;
    }

    result[recvBytes] = '\0';  // Đảm bảo chuỗi kết thúc

    cout << "[Client] Scan Result: " << result << endl;
    logAction("scan", filename, result);

    closesocket(sock);
    WSACleanup();

    return string(result) == "OK";
}

void notifyClamAVExit(const char* host, int port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return;
    }

    const char* exitSignal = "EXIT";
    send(sock, exitSignal, strlen(exitSignal), 0);
    closesocket(sock);
    WSACleanup();
}

string getLocalIPAddress() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
        cerr << "[ERROR] gethostname failed." << endl;
        return "";
    }

    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(hostname, nullptr, &hints, &res) != 0 || res == nullptr) {
        cerr << "[ERROR] getaddrinfo failed." << endl;
        return "";
    }

    sockaddr_in* addr = (sockaddr_in*)res->ai_addr;
    string ip = inet_ntoa(addr->sin_addr);

    freeaddrinfo(res);
    return ip;
}

pair<string, int> parsePasvResponse(const string& response) {
    int h1, h2, h3, h4, p1, p2;
    size_t start = response.find('(');
    size_t end = response.find(')');
    if (start == string::npos || end == string::npos) return {"", -1};

    string pasvData = response.substr(start + 1, end - start - 1);
    replace(pasvData.begin(), pasvData.end(), ',', ' ');
    istringstream iss(pasvData);
    iss >> h1 >> h2 >> h3 >> h4 >> p1 >> p2;

    string ip = to_string(h1) + "." + to_string(h2) + "." + to_string(h3) + "." + to_string(h4);
    int port = p1 * 256 + p2;
    return {ip, port};
}

SOCKET openDataConnection() {
    if (passiveMode) {
        // PASV Mode
        sendCommand(controlSocket, "PASV");
        string pasvResponse = receiveResponse(controlSocket);
        auto [ip, port] = parsePasvResponse(pasvResponse);
        if (ip.empty() || port == -1) return INVALID_SOCKET;

        SOCKET dataSock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (connect(dataSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            cerr << "[ERROR] PASV connection failed." << endl;
            closesocket(dataSock);
            return INVALID_SOCKET;
        }

        return dataSock;
    } 
    else {
        // Active mode (PORT)
        SOCKET listenSock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;

        if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            cerr << "[ERROR] Bind failed." << endl;
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        if (listen(listenSock, 1) == SOCKET_ERROR) {
            cerr << "[ERROR] Listen failed." << endl;
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        sockaddr_in boundAddr{};
        int len = sizeof(boundAddr);
        getsockname(listenSock, (sockaddr*)&boundAddr, &len);
        unsigned short port = ntohs(boundAddr.sin_port);

        string ip = getLocalIPAddress();
        if (ip.empty()) {
            cerr << "[ERROR] Cannot get local IP." << endl;
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        replace(ip.begin(), ip.end(), '.', ',');
        int p1 = port / 256, p2 = port % 256;

        string portCmd = "PORT " + ip + "," + to_string(p1) + "," + to_string(p2);
        sendCommand(controlSocket, portCmd);
        string response = receiveResponse(controlSocket);
        if (response[0] != '2') {
            cerr << "[ERROR] PORT command failed." << endl;
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        SOCKET dataSock = accept(listenSock, nullptr, nullptr);
        closesocket(listenSock);

        if (dataSock == INVALID_SOCKET) {
            cerr << "[ERROR] Accept failed." << endl;
            return INVALID_SOCKET;
        }

        return dataSock;
    }
}

void listFiles() {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server." << endl;
        return;
    }

    SOCKET dataSocket = openDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        cerr << "[ERROR] Could not open data connection." << endl;
        return;
    }

    sendCommand(controlSocket, "LIST");
    string response = receiveResponse(controlSocket);
    if (response[0] != '1' && response[0] != '2') { 
        cerr << "[ERROR] Server rejected LIST command: " << response;
        closesocket(dataSocket);
        return;
    }

    cout << "\n[Directory Listing]\n";

    char buffer[BUFFER_SIZE];
    int bytesReceived;
    while ((bytesReceived = recv(dataSocket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        cout << buffer;
    }

    closesocket(dataSocket);
    cout << receiveResponse(controlSocket); 
}

void printWorkingDirectory() {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server." << endl;
        return;
    }

    sendCommand(controlSocket, "PWD");
    string response = receiveResponse(controlSocket);

    if (response[0] == '2') {
        size_t firstQuote = response.find('\"');
        size_t lastQuote = response.find_last_of('\"');
        if (firstQuote != string::npos && lastQuote != string::npos && lastQuote > firstQuote) {
            string path = response.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            cout << "[Remote PWD] " << path << endl;
        } else {
            cout << "[Remote PWD] " << response;
        }
    } else {
        cerr << "[ERROR] Failed to get current directory: " << response;
    }
}

void makeDirectory(const string& dirname) {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server.";
        return;
    }

    sendCommand(controlSocket, "MKD " + dirname);
    string response = receiveResponse(controlSocket);

    if (response[0] == '2') {
        cout << "[INFO] Folder created: " << dirname << endl;
    } else {
        cerr << "[ERROR] Failed to create folder: " << response;
    }
}

void removeDirectory(const string& dirname) {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server." << endl;
        return;
    }

    sendCommand(controlSocket, "RMD " + dirname);
    string response = receiveResponse(controlSocket);

    if (response[0] == '2') {
        cout << "[INFO] Folder removed: " << dirname << endl;
    } else {
        cerr << "[ERROR] Failed to remove folder: " << response;
    }
}

void changeDirectory(const string& path) {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server." << endl;
        return;
    }

    sendCommand(controlSocket, "CWD " + path);
    string response = receiveResponse(controlSocket);

    if (response[0] == '2') {
        cout << "[INFO] Changed remote directory to: " << path << endl;
    } else {
        cerr << "[ERROR] Failed to change directory: " << response;
    }
}

void deleteFile(const string& filename) {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server." << endl;
        return;
    }

    sendCommand(controlSocket, "DELE " + filename);
    string response = receiveResponse(controlSocket);

    if (response[0] == '2') {
        cout << "[INFO] File deleted: " << filename << endl;
    } else {
        cerr << "[ERROR] Failed to delete file: " << response;
    }
}

void renameFileOnServer(const string& oldName, const string& newName) {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server." << endl;
        return;
    }

    // RNFR
    sendCommand(controlSocket, "RNFR " + oldName);
    string response1 = receiveResponse(controlSocket);
    if (response1[0] != '3') {
        cerr << "[ERROR] RNFR failed: " << response1;
        return;
    }

    // RNTO
    sendCommand(controlSocket, "RNTO " + newName);
    string response2 = receiveResponse(controlSocket);
    if (response2[0] == '2') {
        cout << "[INFO] File renamed from " << oldName << " to " << newName << endl;
    } else {
        cerr << "[ERROR] RNTO failed: " << response2;
    }
}

bool uploadFile(const string& filename) {
     cout << "[DEBUG] Uploading file: " << filename << endl;
     cout << "[DEBUG] Calling sendFileToScan..." << endl;
    if (!sendFileToScan(filename.c_str(), clamAVAgent.c_str(), 9001)) {
        cerr << "[ERROR] File failed virus scan. Upload aborted." << endl;
        logAction("put", filename, "FAILED");
        return false;
    }

    SOCKET dataSocket = openDataConnection();
    if (dataSocket == INVALID_SOCKET) {
    cerr << "[ERROR] Could not open data connection.\n";
    logAction("put", filename, "FAILED");
    return false;
    }

    // Gửi STOR lệnh
    string baseFilename = filename.substr(filename.find_last_of("/\\") + 1);
    sendCommand(controlSocket, "STOR " + baseFilename);
    string response = receiveResponse(controlSocket);
    if (response[0] != '1') {
        cerr << "[ERROR] Server rejected STOR command." << endl;
        closesocket(dataSocket);
        logAction("put", filename, "FAILED");
        return false;
    }

    ios::openmode mode = ios::in;
    if (transferMode == BINARY) mode |= ios::binary;
    ifstream file(filename, mode);;
    if (!file) {
        cerr << "[ERROR] Cannot open file for upload." << endl;
        closesocket(dataSocket);
        logAction("put", filename, "FAILED");
        return false;
    }

    char buffer[BUFFER_SIZE];
    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        int bytesRead = file.gcount();
        send(dataSocket, buffer, bytesRead, 0);
    }

    file.close();
    closesocket(dataSocket);
    cout << receiveResponse(controlSocket); // 226 Transfer complete

    cout << "[INFO] File uploaded successfully." << endl;
    logAction("put", filename, "SUCCESS");
    return true;
}

bool downloadFile(const string& filename) {
    // Open data socket
    SOCKET dataSocket = openDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        cerr << "[ERROR] Could not open data connection." << endl;
        logAction("get", filename, "FAILED");
    return false;
    }

    // Gửi lệnh RETR
    sendCommand(controlSocket, "RETR " + filename);
    string response = receiveResponse(controlSocket);
    if (response[0] != '1' && response[0] != '2') { // 150 or 125 expected
        cerr << "[ERROR] Server rejected RETR command." << endl;
        closesocket(dataSocket);
        logAction("get", filename, "FAILED");
        return false;
    }

    // Nhận file
    ios::openmode mode = ios::out;
    if (transferMode == BINARY) mode |= ios::binary;
    ofstream outFile(filename, mode);
    if (!outFile) {
        cerr << "[ERROR] Cannot create local file." << endl;
        closesocket(dataSocket);
        logAction("get", filename, "FAILED");
        return false;
    }

    char buffer[BUFFER_SIZE];
    int bytesReceived;
    while ((bytesReceived = recv(dataSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        outFile.write(buffer, bytesReceived);
    }

    outFile.close();
    closesocket(dataSocket);
    cout << receiveResponse(controlSocket); // 226 Transfer complete

    cout << "[INFO] File downloaded successfully." << endl;
    logAction("get", filename, "SUCCESS");
    return true;
}

void mputFiles(const vector<string>& files) {
    for (const auto& filename : files) {
        if (promptMode) {
            cout << "mput " << filename << "? (y/n): ";
            string answer;
            getline(cin, answer);
            if (answer != "y" && answer != "Y") {
                cout << "[INFO] Skipped: " << filename << endl;
                logAction("mput", filename, "SKIPPED");
                continue;
            }
        }

        cout << "[INFO] Processing file: " << filename << endl;
        cout << "[DEBUG] Scanning file: " << filename << endl;
        if (!sendFileToScan(filename.c_str(), clamAVAgent.c_str(), 9001)) {
            cerr << "[WARN] Skipped (infected): " << filename << endl;
            logAction("mput", filename, "FAILED");
            continue;
        }

        SOCKET dataSocket = openDataConnection();
        if (dataSocket == INVALID_SOCKET) {
            cerr << "[ERROR] Failed to open data connection for file " << filename << endl;
            logAction("mput", filename, "FAILED");
            continue;
        }

        string baseFilename = filename.substr(filename.find_last_of("/\\") + 1);
        sendCommand(controlSocket, "STOR " + baseFilename);
        string response = receiveResponse(controlSocket);
        if (response[0] != '1') {
            cerr << "[ERROR] Server rejected STOR command for " << baseFilename << endl;
            closesocket(dataSocket);
            logAction("mput", filename, "FAILED");
            continue;
        }

        ios::openmode mode = ios::in;
        if (transferMode == BINARY) mode |= ios::binary;
        ifstream file(filename, mode);
        if (!file) {
            cerr << "[ERROR] Cannot open file: " << filename << endl;
            closesocket(dataSocket);
            logAction("mput", filename, "FAILED");
            continue;
        }

        char buffer[BUFFER_SIZE];
        while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
            int bytesRead = file.gcount();
            if (send(dataSocket, buffer, bytesRead, 0) == SOCKET_ERROR) {
                cerr << "[ERROR] Sending data failed for: " << filename << endl;
                logAction("mput", filename, "FAILED");
                break;
            }
        }

        file.close();
        closesocket(dataSocket);
        cout << receiveResponse(controlSocket); // e.g., 226 Transfer complete
        cout << "[INFO] Uploaded: " << baseFilename << endl;
        logAction("mput", filename, "SUCCESS");
    }
}

void mgetFiles(const vector<string>& files) {
    for (const auto& filename : files) {
        if (promptMode) {
            cout << "mget " << filename << "? (y/n): ";
            string answer;
            getline(cin, answer);
            if (answer != "y" && answer != "Y") {
                cout << "[INFO] Skipped: " << filename << endl;
                logAction("mget", filename, "SKIPPED");
                continue;
            }
        }

        cout << "[INFO] Downloading: " << filename << endl;

        SOCKET dataSocket = openDataConnection();
        if (dataSocket == INVALID_SOCKET) {
            cerr << "[ERROR] Could not open data connection for " << filename << endl;
            logAction("mget", filename, "FAILED");
            continue;
        }

        // Gửi lệnh RETR
        sendCommand(controlSocket, "RETR " + filename);
        string response = receiveResponse(controlSocket);
        if (response[0] != '1' && response[0] != '2') { // 150 or 125 expected
            cerr << "[ERROR] RETR rejected for " << filename << ": " << response;
            closesocket(dataSocket);
            logAction("mget", filename, "FAILED");
            continue;
        }

        // Tạo file để ghi
        ios::openmode mode = ios::out;
        if (transferMode == BINARY) mode |= ios::binary;
        ofstream outFile(filename, mode);
        if (!outFile) {
            cerr << "[ERROR] Cannot create local file: " << filename << endl;
            closesocket(dataSocket);
            logAction("mget", filename, "FAILED");
            continue;
        }

        // Nhận và ghi dữ liệu
        char buffer[BUFFER_SIZE];
        int bytesReceived;
        while ((bytesReceived = recv(dataSocket, buffer, BUFFER_SIZE, 0)) > 0) {
            outFile.write(buffer, bytesReceived);
        }

        outFile.close();
        closesocket(dataSocket);

        cout << receiveResponse(controlSocket); // e.g. 226 Transfer complete
        cout << "[INFO] Downloaded: " << filename << endl;
        logAction("mget", filename, "SUCCESS");
    }
}

void printStatus() {
    cout << "==== FTP Client Status ====" << endl;
    cout << "Connection   : " << (isConnected ? "Connected" : "Not connected") << endl;
    if (isConnected) {
        cout << "Server       : " << ftpServer << endl;
        cout << "Username     : " << username << endl;
    }
    cout << "Transfer Mode: " << (transferMode == ASCII ? "ASCII" : "Binary") << endl;
    cout << "Passive Mode : " << (passiveMode ? "On (PASV)" : "Off (PORT)") << endl;
    cout << "Prompt Mode  : " << (promptMode ? "On" : "Off") << endl;
    cout << "===========================\n";
}

void uploadFolder(const string& folderPath) {
    WIN32_FIND_DATAA findData;
    HANDLE hFind = INVALID_HANDLE_VALUE;

    string searchPath = folderPath + "\\*";
    hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        cerr << "[ERROR] Cannot open folder: " << folderPath << endl;
        return;
    }

    // Get folder name from Folderpath
    size_t lastSlash = folderPath.find_last_of("\\/");
    string folderName = (lastSlash != string::npos) ? folderPath.substr(lastSlash + 1) : folderPath;

    // Create new remote folder
    sendCommand(controlSocket, "MKD " + folderName);
    receiveResponse(controlSocket); // ignore errors if folder already exists

    // Change current directory to the new remote folder
    sendCommand(controlSocket, "CWD " + folderName);
    string cwdResp = receiveResponse(controlSocket);
    if (cwdResp[0] != '2') {
        cerr << "[ERROR] Failed to change directory to " << folderName << " on server." << endl;
        FindClose(hFind);
        return;
    }

    do {
        const string itemName = findData.cFileName;

        if (itemName == "." || itemName == "..")
            continue;

        string fullPath = folderPath + "\\" + itemName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            makeDirectory(itemName);
            changeDirectory(itemName);
            uploadFolder(fullPath);
            changeDirectory("..");
        }
        else {
            uploadFile(fullPath);
        }

    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

void downloadFolder(const string& remoteFolder, const string& localFolder) {
    CreateDirectoryA(localFolder.c_str(), NULL);
    changeDirectory(remoteFolder);

    SOCKET dataSocket = openDataConnection();
    sendCommand(controlSocket, "LIST");
    string response = receiveResponse(controlSocket);

    if (response.substr(0, 3) != "150" && response.substr(0, 3) != "125") {
        cerr << "[ERROR] Cannot list folder: " << remoteFolder << endl;
        closesocket(dataSocket);
        return;
    }

    vector<string> filesAndDirs;
    char buffer[BUFFER_SIZE];
    int bytesReceived;
    string listing;

    while ((bytesReceived = recv(dataSocket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytesReceived] = '\0';
        listing += buffer;
    }

    closesocket(dataSocket);
    receiveResponse(controlSocket); // 226 Transfer complete

    istringstream iss(listing);
    string line;
    while (getline(iss, line)) {
        if (line.empty()) continue;

        // Tìm vị trí cuối cùng chứa dung lượng hoặc <DIR>, tên bắt đầu sau đó
        size_t nameStart = line.find_last_of(" \t");
        while (nameStart != string::npos && (nameStart + 1 < line.length()) && isspace(line[nameStart])) {
            nameStart--;
        }
        nameStart = line.find_first_not_of(" \t", nameStart + 1);

        string name = line.substr(nameStart);
        string lower = line;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        bool isDirectory = lower.find("<dir>") != string::npos;

        if (isDirectory) {
        if (name != "." && name != "..") {
            changeDirectory(name);
            downloadFolder(name, localFolder + "\\" + name);
            sendCommand(controlSocket, "CDUP");
            receiveResponse(controlSocket);
            }
        }
        else {
            downloadFile(name);
            if (!MoveFileA(name.c_str(), (localFolder + "\\" + name).c_str())) {
                cerr << "[ERROR] Failed to move file: " << name << endl;
            }
        }    
    }
}

void showHelp() {
    cout << "======= FTP Client Commands =======" << endl;
    cout << "open             : Connect to FTP server" << endl;
    cout << "close            : Disconnect from FTP server" << endl;
    cout << "status           : Show current session status" << endl;
    cout << "ls               : List files in current directory" << endl;
    cout << "cd               : Change remote directory" << endl;
    cout << "pwd              : Show current remote directory" << endl;
    cout << "mkdir            : Create new directory on server" << endl;
    cout << "rmdir            : Remove directory from server" << endl; 
    cout << "delete           : Delete a file from server" << endl;
    cout << "rename           : Rename a file on server" << endl;
    cout << "get / recv       : Download file from server" << endl;
    cout << "put              : Upload file (scanned before upload)" << endl;
    cout << "mget             : Download multiple files (with prompt toggle)" << endl;
    cout << "mput             : Upload multiple files (with prompt toggle)" << endl;
    cout << "ascii            : Set transfer mode to ASCII (text)" << endl;
    cout << "binary           : Set transfer mode to BINARY (default)" << endl;
    cout << "prompt           : Toggle prompt before multi get/put" << endl;
    cout << "passive          : Toggle passive FTP mode" << endl;
    cout << "putdir           : Upload a local folder recursively" << endl;
    cout << "getdir           : Download a remote folder recursively" << endl;
    cout << "quit / bye       : Exit the FTP client" << endl;
    cout << "help / ?         : Show help text for commands" << endl;
    cout << "===================================" << endl;
}

int main() {
    cout << "Enter FTP server address: ";
    cin >> ftpServer;
    cout << "Enter username: ";
    cin >> username;
    cout << "Enter password: ";
    cin >> password;
    cout << "Enter clamAV agent address: ";
    cin >> clamAVAgent;
    cin.ignore();
    
    controlSocket = connectToFTPServer(ftpServer, 21);
    if (controlSocket == INVALID_SOCKET) return 1;

    cout << receiveResponse(controlSocket); // welcome message

    sendCommand(controlSocket, "USER " + username);
    cout << receiveResponse(controlSocket);

    sendCommand(controlSocket, "PASS " + password);
    cout << receiveResponse(controlSocket);
    

    // Handle commands
    string input = "";
    while (true) {
        cout << "ftp> ";
        getline(cin, input);
        istringstream iss(input);
        string cmd;
        iss >> cmd;
        if (cmd == "quit" || cmd == "bye") {
            disconnectFromServer();
            notifyClamAVExit(clamAVAgent.c_str(), 9001);
            break;
        }
        else if (cmd == "put") {
            string filename;
            iss >> filename;
            uploadFile(filename);
        }
        else if (cmd == "get" || cmd == "recv") {
            string filename;
            iss >> filename;
            downloadFile(filename);
        }
        else if (cmd == "mput") {
            vector<string> files;
            string filename;
            while (iss >> filename) files.push_back(filename);
            if (!files.empty()) {
                mputFiles(files);
            }
            else cout << "[ERROR] No files specified for mput." << endl;
        }
        else if (cmd == "mget") {
            vector<string> files;
            string filename;
            while (iss >> filename) files.push_back(filename);
            if (!files.empty()) {
                mgetFiles(files);
            }
            else cout << "[ERROR] No files specified for mget." << endl;
        }
        else if (cmd == "prompt") {
            promptMode = !promptMode;
            cout << "Confirmation for mput/mget " << (promptMode ? "ON" : "OFF") << endl;
        }
        else if (cmd == "ascii") {
            transferMode = ASCII;
            cout << "[INFO] Transfer mode set to ASCII (text)." << endl;
        } 
        else if (cmd == "binary") {
            transferMode = BINARY;
            cout << "[INFO] Transfer mode set to BINARY (raw)." << endl;
        }
        else if (cmd == "passive") {
            passiveMode = !passiveMode;
            cout << "Passive mode " << (passiveMode ? "ON" : "OFF") << endl;
        }
        else if (cmd == "open") {
            if (isConnected) {
            cout << "[WARN] Already connected. Please close first." << endl;
            continue;
            }

            cout << "Enter FTP server address: ";
            cin >> ws; getline(cin, ftpServer);

            controlSocket = connectToFTPServer(ftpServer, 21);
            if (controlSocket == INVALID_SOCKET) continue;

            cout << receiveResponse(controlSocket); // welcome msg

            cout << "Username: ";
            getline(cin, username);
            sendCommand(controlSocket, "USER " + username);
            cout << receiveResponse(controlSocket);

            cout << "Password: ";
            getline(cin, password);
            sendCommand(controlSocket, "PASS " + password);
            cout << receiveResponse(controlSocket);

            cout << "ClamAV agent address: ";
            getline(cin, clamAVAgent);
        }
        else if (cmd == "close") disconnectFromServer();
        else if (cmd == "status") printStatus();
        else if (cmd == "ls") listFiles();
        else if (cmd == "cd") {
            string path;
            iss >> path;
            changeDirectory(path);
        }
        else if (cmd == "pwd") printWorkingDirectory();
        else if (cmd == "mkdir") {
            string folder;
            iss >> folder;
            makeDirectory(folder);
        }
        else if (cmd == "rmdir") {
            string folder;
            iss >> folder;
            removeDirectory(folder);
        }       
        else if (cmd == "delete") {
            string filename;
            iss >> filename;
            deleteFile(filename);
        }
        else if (cmd == "rename") {
            string oldName, newName;
            iss >> oldName;
            if (oldName.empty()) {
                cerr << "[ERROR] Old filename is required." << endl;
                continue;
            }
            iss >> newName;
            if (newName.empty()) {
            cerr << "[ERROR] New filename is required." << endl;
            continue;
            }
            renameFileOnServer(oldName, newName);
        }
        else if (cmd == "putdir") {
            string folderPath;
            iss >> folderPath;
            if (folderPath.empty()) {
                cout << "[ERROR] Missing folder path.\n";
                continue;
            }
            uploadFolder(folderPath);
        }
        else if (cmd == "getdir") {
            string remoteFolder, localFolder;
            iss >> remoteFolder >> localFolder;
            if (remoteFolder.empty() || localFolder.empty()) {
                cout << "[ERROR] Usage: getdir <remote> <local>\n";
                continue;
            }
            downloadFolder(remoteFolder, localFolder);
        }
        else if (cmd == "help" || cmd == "?") showHelp();
        else cout << "[ERROR] Unknown command. Type 'help' to see available commands." << endl;
    }
    
    WSACleanup();
    return 0;
}