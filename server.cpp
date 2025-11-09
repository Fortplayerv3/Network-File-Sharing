#include <iostream>
#include <fstream>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <map>
#include <string>

#define PORT 8080
#define BUFFER_SIZE 1024
#define SHARED_DIR "shared/"
#define ENCRYPT_KEY 0xAA
using namespace std;

void xorEncryptDecrypt(char* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        buffer[i] ^= ENCRYPT_KEY;
    }
}

void sendFile(int clientSocket, const string& filename) {
    string filepath = string(SHARED_DIR) + filename;
    ifstream file(filepath, ios::binary);
    if (!file.is_open()) {
        string err = "ERROR: File not found\n";
        send(clientSocket, err.c_str(), err.size(), 0);
        return;
    }

    // Get file size
    file.seekg(0, ios::end);
    size_t filesize = file.tellg();
    file.seekg(0, ios::beg);

    // Send file size first
    send(clientSocket, &filesize, sizeof(filesize), 0);

    char buffer[BUFFER_SIZE];
    size_t bytesSent = 0;
    while (file.read(buffer, BUFFER_SIZE)) {
        xorEncryptDecrypt(buffer, BUFFER_SIZE);
        ssize_t sent = send(clientSocket, buffer, BUFFER_SIZE, 0);
        if (sent != BUFFER_SIZE) {
            cout << "Error: Failed to send data.\n";
            return;
        }
        bytesSent += BUFFER_SIZE;
    }
    if (file.gcount() > 0) {
        size_t remaining = file.gcount();
        xorEncryptDecrypt(buffer, remaining);
        ssize_t sent = send(clientSocket, buffer, remaining, 0);
        if (sent != remaining) {
            cout << "Error: Failed to send remaining data.\n";
            return;
        }
        bytesSent += remaining;
    }

    cout << "Sent file: " << filepath << " (" << bytesSent << " bytes)\n";
    file.close();
}


void receiveFile(int clientSocket, const string& filename) {
    string filepath = string(SHARED_DIR) + filename;

    size_t filesize = 0;
    ssize_t sizeRecv = recv(clientSocket, &filesize, sizeof(filesize), 0);
    if (sizeRecv != sizeof(filesize)) {
        cerr << "Failed to receive file size for " << filename << endl;
        return;
    }

    ofstream file(filepath, ios::binary);
    if (!file.is_open()) {
        cerr << "Cannot create file: " << filepath << endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytesReceived = 0;
    while (bytesReceived < filesize) {
        ssize_t chunk = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        if (chunk <= 0) {
            cerr << "Error: Failed to receive data for " << filename << endl;
            break;
        }
        xorEncryptDecrypt(buffer, chunk);
        file.write(buffer, chunk);
        bytesReceived += chunk;
    }

    file.close();
    cout << "Received file: " << filepath << " (" << bytesReceived << " bytes)\n";
}


void listFiles(int clientSocket) {
    DIR* dir = opendir(SHARED_DIR);
    if (!dir) {
        mkdir(SHARED_DIR, 0777);
        dir = opendir(SHARED_DIR);
    }

    struct dirent* entry;
    string files = "Files in shared folder:\n";
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG)
            files += string(entry->d_name) + "\n";
    }
    closedir(dir);
    send(clientSocket, files.c_str(), files.size(), 0);
}

int main() {
    // User database
    map<string, string> users;
    users["admin"] = "12345";
    users["user1"] = "pass1";
    users["user2"] = "pass2";

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{}, clientAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 5);

    cout << "Server listening on port " << PORT << "...\n";

    while (true) {
        socklen_t addr_size = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addr_size);
        if (clientSocket < 0) continue;

        cout << "Client connected.\n";

        // --- Authentication ---
        char userbuf[BUFFER_SIZE], passbuf[BUFFER_SIZE];

        ssize_t userBytes = recv(clientSocket, userbuf, BUFFER_SIZE - 1, 0);
        userbuf[userBytes] = '\0';
        send(clientSocket, "OK_USER", 7, 0);

        ssize_t passBytes = recv(clientSocket, passbuf, BUFFER_SIZE - 1, 0);
        passbuf[passBytes] = '\0';

        string userStr(userbuf), passStr(passbuf);
        userStr.erase(userStr.find_last_not_of("\r\n") + 1);
        passStr.erase(passStr.find_last_not_of("\r\n") + 1);

        if (users.find(userStr) != users.end() && users[userStr] == passStr) {
            send(clientSocket, "AUTH_SUCCESS", 12, 0);
            cout << "Client " << userStr << " authenticated successfully.\n";
        } else {
            send(clientSocket, "AUTH_FAIL", 9, 0);
            close(clientSocket);
            continue;
        }

        char command[BUFFER_SIZE];
        while (true) {
            memset(command, 0, BUFFER_SIZE);
            ssize_t bytes = recv(clientSocket, command, BUFFER_SIZE - 1, 0);
            if (bytes <= 0) break;

            string cmd(command);
            if (cmd.rfind("LIST", 0) == 0)
                listFiles(clientSocket);
            else if (cmd.rfind("GET ", 0) == 0)
                sendFile(clientSocket, cmd.substr(4));
            else if (cmd.rfind("PUT ", 0) == 0)
                receiveFile(clientSocket, cmd.substr(4));
            else if (cmd == "EXIT")
                break;
            else {
                string msg = "Unknown command\n";
                send(clientSocket, msg.c_str(), msg.size(), 0);
            }
        }

        close(clientSocket);
        cout << "Client disconnected.\n";
    }

    close(serverSocket);
    return 0;
}



