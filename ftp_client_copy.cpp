#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <windows.h>
#include <wininet.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wininet.lib")

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
        cout << receiveResponse(controlSocket); // "221 Goodbye"
        closesocket(controlSocket);
        controlSocket = INVALID_SOCKET;
        isConnected = false;
        cout << "[INFO] Disconnected from server.\n";
    } else {
        cout << "[WARN] No server to disconnect.\n";
    }
}

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

string getLocalIPAddress() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) return "";

    addrinfo hints{}, *info;
    hints.ai_family = AF_INET;

    if (getaddrinfo(hostname, nullptr, &hints, &info) != 0) return "";

    sockaddr_in* addr = (sockaddr_in*)info->ai_addr;
    string ip = inet_ntoa(addr->sin_addr);

    freeaddrinfo(info);
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
            cerr << "[ERROR] PASV connection failed.\n";
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
            cerr << "[ERROR] Bind failed.\n";
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        if (listen(listenSock, 1) == SOCKET_ERROR) {
            cerr << "[ERROR] Listen failed.\n";
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        sockaddr_in boundAddr{};
        int len = sizeof(boundAddr);
        getsockname(listenSock, (sockaddr*)&boundAddr, &len);
        unsigned short port = ntohs(boundAddr.sin_port);

        string ip = getLocalIPAddress();
        if (ip.empty()) {
            cerr << "[ERROR] Cannot get local IP.\n";
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        replace(ip.begin(), ip.end(), '.', ',');
        int p1 = port / 256, p2 = port % 256;

        string portCmd = "PORT " + ip + "," + to_string(p1) + "," + to_string(p2);
        sendCommand(controlSocket, portCmd);
        string response = receiveResponse(controlSocket);
        if (response[0] != '2') {
            cerr << "[ERROR] PORT command failed.\n";
            closesocket(listenSock);
            return INVALID_SOCKET;
        }

        SOCKET dataSock = accept(listenSock, nullptr, nullptr);
        closesocket(listenSock);

        if (dataSock == INVALID_SOCKET) {
            cerr << "[ERROR] Accept failed.\n";
            return INVALID_SOCKET;
        }

        return dataSock;
    }
}

void listFiles() {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server.\n";
        return;
    }

    SOCKET dataSocket = openDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        cerr << "[ERROR] Could not open data connection.\n";
        return;
    }

    sendCommand(controlSocket, "LIST");
    string response = receiveResponse(controlSocket);
    if (response[0] != '1' && response[0] != '2') { // 150 Opening data connection or 125
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
    cout << receiveResponse(controlSocket); // 226 Transfer complete
}

void printWorkingDirectory() {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server.\n";
        return;
    }

    sendCommand(controlSocket, "PWD");
    string response = receiveResponse(controlSocket);

    if (response[0] == '2') {
        // Ví dụ response: 257 "/current/directory" is current directory
        size_t firstQuote = response.find('\"');
        size_t lastQuote = response.find_last_of('\"');
        if (firstQuote != string::npos && lastQuote != string::npos && lastQuote > firstQuote) {
            string path = response.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            cout << "[Remote PWD] " << path << endl;
        } else {
            cout << "[Remote PWD] " << response; // fallback
        }
    } else {
        cerr << "[ERROR] Failed to get current directory: " << response;
    }
}

void makeDirectory(const string& dirname) {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server.\n";
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
        cerr << "[ERROR] Not connected to any FTP server.\n";
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

bool uploadFile(const string& filename) {
    if (!sendFileToScan(filename.c_str(), clamAVAgent.c_str(), 3310)) {
        cerr << "[ERROR] File failed virus scan. Upload aborted." << endl;
        return false;
    }

    SOCKET dataSocket = openDataConnection();
    if (dataSocket == INVALID_SOCKET) {
    cerr << "[ERROR] Could not open data connection.\n";
    return false;
    }

    // Gửi STOR lệnh
    string baseFilename = filename.substr(filename.find_last_of("/\\") + 1);
    sendCommand(controlSocket, "STOR " + baseFilename);
    string response = receiveResponse(controlSocket);
    if (response[0] != '1') {
        cerr << "[ERROR] Server rejected STOR command." << endl;
        closesocket(dataSocket);
        return false;
    }

    ios::openmode mode = ios::in;
    if (transferMode == BINARY) mode |= ios::binary;
    ifstream file(filename, mode);;
    if (!file) {
        cerr << "[ERROR] Cannot open file for upload." << endl;
        closesocket(dataSocket);
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
    return true;
}

void changeDirectory(const string& path) {
    if (!isConnected) {
        cerr << "[ERROR] Not connected to any FTP server.\n";
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
        cerr << "[ERROR] Not connected to any FTP server.\n";
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
        cerr << "[ERROR] Not connected to any FTP server.\n";
        return;
    }

    // RNFR
    sendCommand(controlSocket, "RNFR " + oldName);
    string response1 = receiveResponse(controlSocket);
    if (response1[0] != '3') { // 350 File exists, ready for destination name
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

bool downloadFile(const string& filename) {
    // Open data socket
    SOCKET dataSocket = openDataConnection();
    if (dataSocket == INVALID_SOCKET) {
        cerr << "[ERROR] Could not open data connection.\n";
    return false;
    }

    // Gửi lệnh RETR
    sendCommand(controlSocket, "RETR " + filename);
    string response = receiveResponse(controlSocket);
    if (response[0] != '1' && response[0] != '2') { // 150 or 125 expected
        cerr << "[ERROR] Server rejected RETR command." << endl;
        closesocket(dataSocket);
        return false;
    }

    // Nhận file
    ios::openmode mode = ios::out;
    if (transferMode == BINARY) mode |= ios::binary;
    ofstream outFile(filename, mode);
    if (!outFile) {
        cerr << "[ERROR] Cannot create local file." << endl;
        closesocket(dataSocket);
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

    cout << "[INFO] File downloaded successfully.\n";
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
                continue;
            }
        }

        cout << "\n[INFO] Processing file: " << filename << endl;

        if (!sendFileToScan(filename.c_str(), clamAVAgent.c_str(), 3310)) {
            cerr << "[WARN] Skipped (infected): " << filename << endl;
            continue;
        }

        SOCKET dataSocket = openDataConnection();
        if (dataSocket == INVALID_SOCKET) {
            cerr << "[ERROR] Failed to open data connection for file " << filename << endl;
            continue;
        }

        string baseFilename = filename.substr(filename.find_last_of("/\\") + 1);
        sendCommand(controlSocket, "STOR " + baseFilename);
        string response = receiveResponse(controlSocket);
        if (response[0] != '1') {
            cerr << "[ERROR] Server rejected STOR command for " << baseFilename << endl;
            closesocket(dataSocket);
            continue;
        }

        ios::openmode mode = ios::in;
        if (transferMode == BINARY) mode |= ios::binary;
        ifstream file(filename, mode);
        if (!file) {
            cerr << "[ERROR] Cannot open file: " << filename << endl;
            closesocket(dataSocket);
            continue;
        }

        char buffer[BUFFER_SIZE];
        while (!file.eof()) {
            file.read(buffer, BUFFER_SIZE);
            int bytesRead = file.gcount();
            send(dataSocket, buffer, bytesRead, 0);
        }

        file.close();
        closesocket(dataSocket);
        cout << receiveResponse(controlSocket); // e.g., 226 Transfer complete
        cout << "[INFO] Uploaded: " << baseFilename << endl;
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
                continue;
            }
        }

        cout << "\n[INFO] Downloaded: " << filename << endl;

        SOCKET dataSocket = openDataConnection();
        if (dataSocket == INVALID_SOCKET) {
            cerr << "[ERROR] Could not open data connection for " << filename << endl;
            continue;
        }

        // Gửi lệnh RETR
        sendCommand(controlSocket, "RETR " + filename);
        string response = receiveResponse(controlSocket);
        if (response[0] != '1' && response[0] != '2') { // 150 or 125 expected
            cerr << "[ERROR] RETR rejected for " << filename << ": " << response;
            closesocket(dataSocket);
            continue;
        }

        // Tạo file để ghi
        ios::openmode mode = ios::out;
        if (transferMode == BINARY) mode |= ios::binary;
        ofstream outFile(filename, mode);
        if (!outFile) {
            cerr << "[ERROR] Cannot create local file: " << filename << endl;
            closesocket(dataSocket);
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
        cout << "[INFO] Downloaded: " << filename << "\n";
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
    cout << "help             : Show this help message" << endl;
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
        }
        else if (cmd == "close") disconnectFromServer();
        else if (cmd == "status") printStatus();
        else if (cmd == "ls") listFiles();
        else if (cmd == "cd") {
            string path;
            getline(cin >> ws, path);
            changeDirectory(path);
        }
        else if (cmd == "pwd") printWorkingDirectory();
        else if (cmd == "mkdir") {
            string folder;
            getline(cin >> ws, folder);
            makeDirectory(folder);
        }
        else if (cmd == "rmdir") {
            string folder;
            getline(cin >> ws, folder);
            removeDirectory(folder);
        }       
        else if (cmd == "delete") {
            string filename;
            getline(cin >> ws, filename);
            deleteFile(filename);
        }
        else if (cmd == "rename") {
            string oldName, newName;
            getline(cin >> ws, oldName);
            if (oldName.empty()) {
                cerr << "[ERROR] Old filename is required.\n";
                continue;
            }
            cout << "New filename: ";
            getline(cin >> ws, newName);
            if (newName.empty()) {
            cerr << "[ERROR] New filename is required.\n";
            continue;
            }
            renameFileOnServer(oldName, newName);
        }
        else if (cmd == "help" || cmd == "?") showHelp();
        else cout << "[ERROR] Unknown command. Type 'help' to see available commands." << endl;
    }
    
    WSACleanup();
    return 0;
}
