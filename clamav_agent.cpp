#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#pragma comment(lib, "ws2_32.lib")

#define PORT 9001
#define BUFFER_SIZE 4096

using namespace std;

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 1);

    cout << "[ClamAVAgent] Waiting for client...\n";
    SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
    cout << "[ClamAVAgent] Connected.\n";

    ofstream out("received_file.tmp", std::ios::binary);
    char buffer[BUFFER_SIZE];
    int bytesReceived;

    while ((bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        out.write(buffer, bytesReceived);
        if (bytesReceived < BUFFER_SIZE) break; // End of file
    }
    out.close();

    cout << "[ClamAVAgent] File received. Scanning...\n";
    int result = system("clamscan received_file.tmp > scan_result.txt");

    ifstream scan("scan_result.txt");
    string line;
    string status = "INFECTED";
    while (getline(scan, line)) {
        if (line.find("OK") != std::string::npos) {
            status = "OK";
            break;
        }
    }

    send(clientSocket, status.c_str(), status.size(), 0);
    cout << "[ClamAVAgent] Scan result: " << status << "\n";

    closesocket(clientSocket);
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}