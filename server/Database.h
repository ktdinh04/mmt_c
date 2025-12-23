/**
 * @file Database.h
 * @brief SQLite database interface for user management
 */

#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <mutex>
#include <sqlite3.h>

class Database {
public:
    /**
     * @brief Get singleton instance
     * @return Reference to Database instance
     */
    static Database& getInstance();

    // Delete copy constructor and assignment operator
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /**
     * @brief Initialize database connection and create tables
     * @param dbPath Path to SQLite database file
     * @return true if successful
     */
    bool initialize(const std::string& dbPath = "chat_server.db");

    /**
     * @brief Close database connection
     */
    void close();

    /**
     * @brief Register a new user
     * @param username Username (unique)
     * @param password Password (will be hashed)
     * @param displayName Display name (optional, defaults to username)
     * @return true if registration successful, false if username exists
     */
    bool registerUser(const std::string& username, const std::string& password,
                      const std::string& displayName = "");

    /**
     * @brief Authenticate user login
     * @param username Username
     * @param password Password
     * @return true if credentials are valid
     */
    bool authenticateUser(const std::string& username, const std::string& password);

    /**
     * @brief Change user password
     * @param username Username
     * @param oldPassword Current password (for verification)
     * @param newPassword New password
     * @return true if password changed successfully
     */
    bool changePassword(const std::string& username, const std::string& oldPassword,
                        const std::string& newPassword);

    /**
     * @brief Check if username exists
     * @param username Username to check
     * @return true if username exists
     */
    bool userExists(const std::string& username);

    /**
     * @brief Get user's display name
     * @param username Username
     * @return Display name or empty string if not found
     */
    std::string getDisplayName(const std::string& username);

    /**
     * @brief Update user's display name
     * @param username Username
     * @param displayName New display name
     * @return true if updated successfully
     */
    bool updateDisplayName(const std::string& username, const std::string& displayName);

private:
    Database();
    ~Database();

    /**
     * @brief Create tables if not exist
     * @return true if successful
     */
    bool createTables();

    /**
     * @brief Simple hash function for password
     * @param password Plain text password
     * @return Hashed password
     */
    std::string hashPassword(const std::string& password);

    sqlite3* db_;
    std::mutex mutex_;
    bool initialized_;
};

#endif // DATABASE_H
