/**
 * @file ClientSession.cpp
 * @brief Implementation of ClientSession
 */

#include "ClientSession.h"
#include "Server.h"
#include "Database.h"
#include "../thirdparty/json.hpp"
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <unistd.h>
#endif

using json = nlohmann::json;

ClientSession::ClientSession(int socketFd, Server* server)
    : socketFd_(socketFd)
    , server_(server)
    , authenticated_(false)
    , active_(true) {
}

ClientSession::~ClientSession() {
    if (socketFd_ >= 0) {
#ifdef _WIN32
        closesocket(socketFd_);
#else
        close(socketFd_);
#endif
    }
}

std::string ClientSession::getUsername() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return username_;
}

std::string ClientSession::getDisplayName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return displayName_;
}

void ClientSession::setAuthenticated(const std::string& username, const std::string& displayName) {
    std::lock_guard<std::mutex> lock(mutex_);
    username_ = username;
    displayName_ = displayName;
    authenticated_ = true;
}

void ClientSession::clearAuthentication() {
    std::lock_guard<std::mutex> lock(mutex_);
    username_.clear();
    displayName_.clear();
    authenticated_ = false;
}

bool ClientSession::sendMessage(const Protocol::Message& msg) {
    std::lock_guard<std::mutex> lock(sendMutex_);

    if (!active_ || socketFd_ < 0) {
        return false;
    }

    std::vector<uint8_t> data = Protocol::serialize(msg);

    size_t totalSent = 0;
    while (totalSent < data.size()) {
        ssize_t sent = send(socketFd_, reinterpret_cast<const char*>(data.data() + totalSent),
                           data.size() - totalSent, 0);
        if (sent <= 0) {
            return false;
        }
        totalSent += sent;
    }

    return true;
}

void ClientSession::processData(const uint8_t* data, size_t length) {
    buffer_.append(data, length);

    // Process all complete messages in buffer
    while (buffer_.hasCompleteMessage()) {
        Protocol::Message msg = buffer_.extractMessage();
        handleMessage(msg);
    }
}

void ClientSession::handleMessage(const Protocol::Message& msg) {
    server_->log("[" + address_ + "] Received: " + Protocol::messageTypeToString(msg.type));

    switch (msg.type) {
        case Protocol::MessageType::REGISTER:
            handleRegister(msg);
            break;

        case Protocol::MessageType::LOGIN:
            handleLogin(msg);
            break;

        case Protocol::MessageType::LOGOUT:
            handleLogout(msg);
            break;

        case Protocol::MessageType::CHANGE_PASSWORD:
            handleChangePassword(msg);
            break;

        case Protocol::MessageType::MSG_GLOBAL:
            handleGlobalMessage(msg);
            break;

        case Protocol::MessageType::MSG_PRIVATE:
            handlePrivateMessage(msg);
            break;

        case Protocol::MessageType::PING:
            sendMessage(Protocol::Message(Protocol::MessageType::PONG));
            break;

        default:
            sendMessage(Protocol::createErrorResponse("Unknown command"));
            break;
    }
}

void ClientSession::handleRegister(const Protocol::Message& msg) {
    // Parse credentials from content (JSON format: {"username": "...", "password": "..."})
    try {
        json j = json::parse(msg.content);
        std::string username = j["username"].get<std::string>();
        std::string password = j["password"].get<std::string>();

        // Validate input
        if (username.empty() || password.empty()) {
            sendMessage(Protocol::createErrorResponse("Username and password are required"));
            return;
        }

        if (username.length() < 3 || username.length() > 20) {
            sendMessage(Protocol::createErrorResponse("Username must be 3-20 characters"));
            return;
        }

        if (password.length() < 4) {
            sendMessage(Protocol::createErrorResponse("Password must be at least 4 characters"));
            return;
        }

        // Register user
        if (Database::getInstance().registerUser(username, password)) {
            server_->log("User registered: " + username);
            sendMessage(Protocol::createOkResponse("Registration successful"));
        } else {
            sendMessage(Protocol::createErrorResponse("Username already exists"));
        }
    } catch (const std::exception& e) {
        sendMessage(Protocol::createErrorResponse("Invalid request format"));
    }
}

void ClientSession::handleLogin(const Protocol::Message& msg) {
    if (authenticated_) {
        sendMessage(Protocol::createErrorResponse("Already logged in"));
        return;
    }

    try {
        json j = json::parse(msg.content);
        std::string username = j["username"].get<std::string>();
        std::string password = j["password"].get<std::string>();

        // Check if already logged in elsewhere
        if (server_->isUserOnline(username)) {
            sendMessage(Protocol::createErrorResponse("User already logged in from another location"));
            return;
        }

        // Authenticate
        if (Database::getInstance().authenticateUser(username, password)) {
            std::string displayName = Database::getInstance().getDisplayName(username);
            setAuthenticated(username, displayName);
            server_->registerUser(username, this);

            server_->log("User logged in: " + username + " from " + address_);

            // Send success response with user info
            json response;
            response["username"] = username;
            response["displayName"] = displayName;
            sendMessage(Protocol::createOkResponse("Login successful", response.dump()));

            // Broadcast user online status to all clients
            server_->broadcast(Protocol::createUserStatusMessage(username, Protocol::UserStatus::ONLINE));

            // Send current online list to this client
            sendMessage(Protocol::createOnlineListMessage(server_->getOnlineUsers()));
        } else {
            sendMessage(Protocol::createErrorResponse("Invalid username or password"));
        }
    } catch (const std::exception& e) {
        sendMessage(Protocol::createErrorResponse("Invalid request format"));
    }
}

void ClientSession::handleLogout(const Protocol::Message& msg) {
    if (!authenticated_) {
        sendMessage(Protocol::createErrorResponse("Not logged in"));
        return;
    }

    std::string username = getUsername();
    server_->log("User logged out: " + username);

    // Broadcast user offline status
    server_->broadcast(Protocol::createUserStatusMessage(username, Protocol::UserStatus::OFFLINE), socketFd_);

    // Unregister and clear auth
    server_->unregisterUser(username);
    clearAuthentication();

    sendMessage(Protocol::createOkResponse("Logged out successfully"));
}

void ClientSession::handleChangePassword(const Protocol::Message& msg) {
    if (!authenticated_) {
        sendMessage(Protocol::createErrorResponse("Must be logged in to change password"));
        return;
    }

    try {
        json j = json::parse(msg.content);
        std::string oldPassword = j["oldPassword"].get<std::string>();
        std::string newPassword = j["newPassword"].get<std::string>();

        if (newPassword.length() < 4) {
            sendMessage(Protocol::createErrorResponse("New password must be at least 4 characters"));
            return;
        }

        std::string username = getUsername();
        if (Database::getInstance().changePassword(username, oldPassword, newPassword)) {
            server_->log("Password changed for: " + username);
            sendMessage(Protocol::createOkResponse("Password changed successfully"));
        } else {
            sendMessage(Protocol::createErrorResponse("Incorrect old password"));
        }
    } catch (const std::exception& e) {
        sendMessage(Protocol::createErrorResponse("Invalid request format"));
    }
}

void ClientSession::handleGlobalMessage(const Protocol::Message& msg) {
    if (!authenticated_) {
        sendMessage(Protocol::createErrorResponse("Must be logged in to send messages"));
        return;
    }

    std::string username = getUsername();
    std::string content = msg.content;

    if (content.empty()) {
        return;  // Ignore empty messages
    }

    server_->log("Global message from " + username + ": " + content.substr(0, 50) +
                 (content.length() > 50 ? "..." : ""));

    // Create and broadcast global message
    Protocol::Message globalMsg = Protocol::createGlobalMessage(username, content);
    server_->broadcast(globalMsg);
}

void ClientSession::handlePrivateMessage(const Protocol::Message& msg) {
    if (!authenticated_) {
        sendMessage(Protocol::createErrorResponse("Must be logged in to send messages"));
        return;
    }

    std::string sender = getUsername();
    std::string receiver = msg.receiver;
    std::string content = msg.content;

    if (receiver.empty()) {
        sendMessage(Protocol::createErrorResponse("Receiver not specified"));
        return;
    }

    if (content.empty()) {
        return;  // Ignore empty messages
    }

    if (receiver == sender) {
        sendMessage(Protocol::createErrorResponse("Cannot send message to yourself"));
        return;
    }

    server_->log("Private message from " + sender + " to " + receiver + ": " +
                 content.substr(0, 50) + (content.length() > 50 ? "..." : ""));

    // Create private message
    Protocol::Message privateMsg = Protocol::createPrivateMessage(sender, receiver, content);

    // Send to receiver
    if (!server_->sendToUser(receiver, privateMsg)) {
        sendMessage(Protocol::createErrorResponse("User not online: " + receiver));
        return;
    }

    // Also send copy to sender (for display)
    sendMessage(privateMsg);
}
