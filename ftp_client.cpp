#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 4096

using namespace std;

bool sendFile(const char* filename, const char* host, int port) {
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
        cerr << "Cannot open file.\n";
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
    cout << "[Client] Scan Result: " << result << "\n";

    closesocket(sock);
    WSACleanup();

    return string(result) == "OK";
}

int main() {
    string filename;
    cout << "Enter filename to scan before FTP upload: ";
    cin >> filename;

    if (sendFile(filename.c_str(), "127.0.0.1", 9001)) {
        cout << "[Client] File clean. Proceeding to FTP upload...\n";
        // TODO: Add FTP upload here
    } else {
        cout << "[Client] File is infected! Upload aborted.\n";
    }

    return 0;
}