#include <iostream>
#include <fstream>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

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

void downloadFile(int socket, const string& filename) {
    string filepath = filename;  // Save to current directory
    size_t filesize = 0;

    ssize_t sizeRecv = recv(socket, &filesize, sizeof(filesize), 0);
    if (sizeRecv != sizeof(filesize)) {
        cout << "Error: Did not receive valid file size.\n";
        return;
    }

    ofstream file(filepath, ios::binary);
    if (!file.is_open()) {
        cout << "Cannot open " << filepath << " for writing.\n";
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytesReceived = 0;
    while (bytesReceived < filesize) {
        ssize_t chunk = recv(socket, buffer, BUFFER_SIZE, 0);
        if (chunk <= 0) {
            cout << "Error: Failed to receive data.\n";
            break;
        }
        xorEncryptDecrypt(buffer, chunk);
        file.write(buffer, chunk);
        bytesReceived += chunk;
    }

    file.close();
    cout << "Downloaded " << filename << " (" << bytesReceived << " bytes)\n";
}


void uploadFile(int socket, const string& filename) {
    string filepath = filename;  // Read from current directory
    ifstream file(filepath, ios::binary);
    if (!file.is_open()) {
        cout << "Error: File not found.\n";
        return;
    }

    // Calculate file size
    file.seekg(0, ios::end);
    size_t filesize = file.tellg();
    file.seekg(0, ios::beg);

    // Send file size
    send(socket, &filesize, sizeof(filesize), 0);

    // Send file data
    char buffer[BUFFER_SIZE];
    size_t totalSent = 0;
    while (file.read(buffer, BUFFER_SIZE)) {
        xorEncryptDecrypt(buffer, BUFFER_SIZE);
        ssize_t sent = send(socket, buffer, BUFFER_SIZE, 0);
        if (sent != BUFFER_SIZE) {
            cout << "Error: Failed to send data.\n";
            file.close();
            return;
        }
        totalSent += BUFFER_SIZE;
    }
    if (file.gcount() > 0) {
        size_t remaining = file.gcount();
        xorEncryptDecrypt(buffer, remaining);
        ssize_t sent = send(socket, buffer, remaining, 0);
        if (sent != remaining) {
            cout << "Error: Failed to send remaining data.\n";
            file.close();
            return;
        }
        totalSent += remaining;
    }

    file.close();
    cout << "Uploaded " << filename << " (" << totalSent << " bytes)\n";
}


int main() {
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    cout << "Connected to server.\n";

    // Authentication
    string username, password;
    char response[BUFFER_SIZE];
    cout << "Username: "; getline(cin, username);
    send(clientSocket, username.c_str(), username.size(), 0);
    recv(clientSocket, response, BUFFER_SIZE, 0);
    cout << "Password: "; getline(cin, password);
    send(clientSocket, password.c_str(), password.size(), 0);
    memset(response, 0, BUFFER_SIZE);
    recv(clientSocket, response, BUFFER_SIZE, 0);
    if (string(response).find("AUTH_SUCCESS") == string::npos) {
        cout << "Authentication failed.\n";
        close(clientSocket);
        return 0;
    }
    cout << "Authenticated successfully.\n";

    string command;
    char buffer[BUFFER_SIZE];

    while (true) {
        cout << "\nCommands:\n"
             << "1. LIST - List files on server\n"
             << "2. GET <filename> - Download file\n"
             << "3. PUT <filename> - Upload file\n"
             << "4. EXIT - Disconnect\n"
             << "> ";
        getline(cin, command);

        send(clientSocket, command.c_str(), command.size(), 0);
        if (command == "EXIT")
            break;

        if (command.rfind("GET ", 0) == 0)
            downloadFile(clientSocket, command.substr(4));
        else if (command.rfind("PUT ", 0) == 0)
            uploadFile(clientSocket, command.substr(4));
        else {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t bytes = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                cout << buffer;
            }
        }
    }

    close(clientSocket);
    return 0;
}



