/**
 * @file Server.h
 * @brief TCP Chat Server class
 */

#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include "../common/Protocol.h"

class ClientSession;

class Server {
public:
    /**
     * @brief Construct a new Server
     * @param port Server port
     * @param maxClients Maximum number of clients (default 100)
     */
    Server(int port, int maxClients = 100);

    /**
     * @brief Destroy the Server
     */
    ~Server();

    // Disable copy
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /**
     * @brief Start the server
     * @return true if started successfully
     */
    bool start();

    /**
     * @brief Stop the server
     */
    void stop();

    /**
     * @brief Check if server is running
     * @return true if running
     */
    bool isRunning() const { return running_; }

    /**
     * @brief Get server port
     * @return Port number
     */
    int getPort() const { return port_; }

    /**
     * @brief Get number of connected clients
     * @return Client count
     */
    size_t getClientCount() const;

    /**
     * @brief Get list of online usernames
     * @return Vector of usernames
     */
    std::vector<std::string> getOnlineUsers() const;

    /**
     * @brief Check if a username is online
     * @param username Username to check
     * @return true if online
     */
    bool isUserOnline(const std::string& username) const;

    /**
     * @brief Broadcast message to all authenticated clients
     * @param msg Message to broadcast
     * @param excludeSocket Socket to exclude (optional, -1 for none)
     */
    void broadcast(const Protocol::Message& msg, int excludeSocket = -1);

    /**
     * @brief Send message to a specific user
     * @param username Target username
     * @param msg Message to send
     * @return true if user found and message sent
     */
    bool sendToUser(const std::string& username, const Protocol::Message& msg);

    /**
     * @brief Register a client session after login
     * @param username Username
     * @param session Client session pointer
     */
    void registerUser(const std::string& username, ClientSession* session);

    /**
     * @brief Unregister a user (on logout/disconnect)
     * @param username Username
     */
    void unregisterUser(const std::string& username);

    /**
     * @brief Log server event
     * @param event Event description
     */
    void log(const std::string& event);

private:
    /**
     * @brief Main accept loop
     */
    void acceptLoop();

    /**
     * @brief Handle a client connection
     * @param socketFd Client socket
     * @param address Client address
     */
    void handleClient(int socketFd, const std::string& address);

    /**
     * @brief Remove a client session
     * @param socketFd Socket fd to remove
     */
    void removeClient(int socketFd);

    /**
     * @brief Broadcast online list to all clients
     */
    void broadcastOnlineList();

    int port_;
    int maxClients_;
    int serverSocket_;
    std::atomic<bool> running_;

    // Active client sessions (socket -> session)
    std::map<int, std::unique_ptr<ClientSession>> clients_;
    mutable std::mutex clientsMutex_;

    // Username to socket mapping for quick lookup
    std::map<std::string, int> userToSocket_;
    mutable std::mutex userMapMutex_;

    // Accept thread
    std::thread acceptThread_;

    // Client handler threads
    std::vector<std::thread> clientThreads_;
    std::mutex threadsMutex_;
};

#endif // SERVER_H
