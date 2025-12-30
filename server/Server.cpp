/**
 * @file Server.cpp
 * @brief Implementation of TCP Chat Server
 */

#include "Server.h"
#include "ClientSession.h"
#include "Database.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

#define BUFFER_SIZE 4096

Server::Server(int port, int maxClients)
    : port_(port)
    , maxClients_(maxClients)
    , serverSocket_(-1)
    , running_(false) {
}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (running_) {
        log("Server already running");
        return false;
    }

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log("WSAStartup failed");
        return false;
    }
#endif

    // Create socket
    serverSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ < 0) {
        log("Failed to create socket");
        return false;
    }

    // Set socket options
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    // Bind
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port_);

    if (bind(serverSocket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        log("Bind failed on port " + std::to_string(port_));
#ifdef _WIN32
        closesocket(serverSocket_);
#else
        close(serverSocket_);
#endif
        return false;
    }

    // Listen
    if (listen(serverSocket_, maxClients_) < 0) {
        log("Listen failed");
#ifdef _WIN32
        closesocket(serverSocket_);
#else
        close(serverSocket_);
#endif
        return false;
    }

    // Initialize database
    if (!Database::getInstance().initialize()) {
        log("Database initialization failed");
        return false;
    }

    running_ = true;
    log("Server started on port " + std::to_string(port_));

    // Start accept thread
    acceptThread_ = std::thread(&Server::acceptLoop, this);

    return true;
}

void Server::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    log("Stopping server...");

    // Close server socket to interrupt accept()
    if (serverSocket_ >= 0) {
#ifdef _WIN32
        closesocket(serverSocket_);
#else
        shutdown(serverSocket_, SHUT_RDWR);
        close(serverSocket_);
#endif
        serverSocket_ = -1;
    }

    // Wait for accept thread
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (auto& [fd, session] : clients_) {
            session->setInactive();
        }
        clients_.clear();
    }

    // Wait for all client threads
    {
        std::lock_guard<std::mutex> lock(threadsMutex_);
        for (auto& t : clientThreads_) {
            if (t.joinable()) {
                t.join();
            }
        }
        clientThreads_.clear();
    }

    // Close database
    Database::getInstance().close();

#ifdef _WIN32
    WSACleanup();
#endif

    log("Server stopped");
}

void Server::acceptLoop() {
    while (running_) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        int clientSocket = accept(serverSocket_, (struct sockaddr*)&clientAddr, &clientLen);

        if (clientSocket < 0) {
            if (running_) {
                log("Accept error");
            }
            continue;
        }

        // Get client address string
        char addrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, INET_ADDRSTRLEN);
        std::string clientAddress = std::string(addrStr) + ":" + std::to_string(ntohs(clientAddr.sin_port));

        log("New connection from " + clientAddress);

        // Check max clients
        if (getClientCount() >= static_cast<size_t>(maxClients_)) {
            log("Max clients reached, rejecting connection");
#ifdef _WIN32
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
            continue;
        }

        // Create client session
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_[clientSocket] = std::make_unique<ClientSession>(clientSocket, this);
            clients_[clientSocket]->setAddress(clientAddress);
        }

        // Start client handler thread
        {
            std::lock_guard<std::mutex> lock(threadsMutex_);
            clientThreads_.emplace_back(&Server::handleClient, this, clientSocket, clientAddress);
        }
    }
}

void Server::handleClient(int socketFd, const std::string& address) {
    uint8_t buffer[BUFFER_SIZE];
    ClientSession* session = nullptr;

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (clients_.find(socketFd) != clients_.end()) {
            session = clients_[socketFd].get();
        }
    }

    if (!session) {
        return;
    }

    while (running_ && session->isActive()) {
        ssize_t bytesRead = recv(socketFd, reinterpret_cast<char*>(buffer), BUFFER_SIZE, 0);

        if (bytesRead <= 0) {
            // Connection closed or error
            break;
        }

        // Process received data
        session->processData(buffer, bytesRead);
    }

    // Client disconnected
    std::string username = session->getUsername();
    log("Client disconnected: " + address + (username.empty() ? "" : " (" + username + ")"));

    // Broadcast offline status if user was logged in
    if (!username.empty()) {
        broadcast(Protocol::createUserStatusMessage(username, Protocol::UserStatus::OFFLINE), socketFd);
        unregisterUser(username);
    }

    // Remove client
    removeClient(socketFd);
}

void Server::removeClient(int socketFd) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(socketFd);
}

size_t Server::getClientCount() const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_.size();
}

std::vector<std::string> Server::getOnlineUsers() const {
    std::lock_guard<std::mutex> lock(userMapMutex_);
    std::vector<std::string> users;
    users.reserve(userToSocket_.size());
    for (const auto& [username, socket] : userToSocket_) {
        users.push_back(username);
    }
    return users;
}

bool Server::isUserOnline(const std::string& username) const {
    std::lock_guard<std::mutex> lock(userMapMutex_);
    return userToSocket_.find(username) != userToSocket_.end();
}

void Server::broadcast(const Protocol::Message& msg, int excludeSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex_);

    for (auto& [fd, session] : clients_) {
        if (fd != excludeSocket && session->isAuthenticated()) {
            session->sendMessage(msg);
        }
    }
}

bool Server::sendToUser(const std::string& username, const Protocol::Message& msg) {
    int socketFd = -1;

    {
        std::lock_guard<std::mutex> lock(userMapMutex_);
        auto it = userToSocket_.find(username);
        if (it != userToSocket_.end()) {
            socketFd = it->second;
        }
    }

    if (socketFd < 0) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(socketFd);
        if (it != clients_.end()) {
            return it->second->sendMessage(msg);
        }
    }

    return false;
}

void Server::registerUser(const std::string& username, ClientSession* session) {
    std::lock_guard<std::mutex> lock(userMapMutex_);
    userToSocket_[username] = session->getSocketFd();
}

void Server::unregisterUser(const std::string& username) {
    std::lock_guard<std::mutex> lock(userMapMutex_);
    userToSocket_.erase(username);
}

void Server::kickUser(const std::string& username) {
    int socketFd = -1;

    {
        std::lock_guard<std::mutex> lock(userMapMutex_);
        auto it = userToSocket_.find(username);
        if (it != userToSocket_.end()) {
            socketFd = it->second;
            userToSocket_.erase(it);
        }
    }

    if (socketFd >= 0) {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(socketFd);
        if (it != clients_.end()) {
            it->second->setInactive();
            it->second->clearAuthentication();
        }
    }
}

void Server::broadcastOnlineList() {
    Protocol::Message msg = Protocol::createOnlineListMessage(getOnlineUsers());
    broadcast(msg);
}

void Server::log(const std::string& event) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_now = std::localtime(&time_t_now);

    std::ostringstream oss;
    oss << "[" << std::setfill('0')
        << std::setw(2) << tm_now->tm_hour << ":"
        << std::setw(2) << tm_now->tm_min << ":"
        << std::setw(2) << tm_now->tm_sec << "] "
        << event;

    std::cout << oss.str() << std::endl;
}
