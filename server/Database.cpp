/**
 * @file Database.cpp
 * @brief Implementation of SQLite database operations
 */

#include "Database.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <functional>

Database& Database::getInstance() {
    static Database instance;
    return instance;
}

Database::Database() : db_(nullptr), initialized_(false) {}

Database::~Database() {
    close();
}

bool Database::initialize(const std::string& dbPath) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] Error opening database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    if (!createTables()) {
        std::cerr << "[Database] Error creating tables" << std::endl;
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    initialized_ = true;
    std::cout << "[Database] Initialized successfully: " << dbPath << std::endl;
    return true;
}

void Database::close() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        initialized_ = false;
        std::cout << "[Database] Connection closed" << std::endl;
    }
}

bool Database::createTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            display_name TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE INDEX IF NOT EXISTS idx_username ON users(username);
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        std::cerr << "[Database] SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

std::string Database::hashPassword(const std::string& password) {
    // Simple hash using std::hash (for demo purposes)
    // In production, use bcrypt, argon2, or similar
    std::hash<std::string> hasher;
    size_t hash = hasher(password + "chat_salt_2024");

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

bool Database::registerUser(const std::string& username, const std::string& password,
                            const std::string& displayName) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !db_) {
        return false;
    }

    // Check if username already exists
    const char* checkSql = "SELECT COUNT(*) FROM users WHERE username = ?";
    sqlite3_stmt* checkStmt;

    if (sqlite3_prepare_v2(db_, checkSql, -1, &checkStmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(checkStmt, 1, username.c_str(), -1, SQLITE_STATIC);

    int count = 0;
    if (sqlite3_step(checkStmt) == SQLITE_ROW) {
        count = sqlite3_column_int(checkStmt, 0);
    }
    sqlite3_finalize(checkStmt);

    if (count > 0) {
        std::cout << "[Database] Username already exists: " << username << std::endl;
        return false;
    }

    // Insert new user
    const char* insertSql = "INSERT INTO users (username, password, display_name) VALUES (?, ?, ?)";
    sqlite3_stmt* insertStmt;

    if (sqlite3_prepare_v2(db_, insertSql, -1, &insertStmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string hashedPassword = hashPassword(password);
    std::string dispName = displayName.empty() ? username : displayName;

    sqlite3_bind_text(insertStmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(insertStmt, 2, hashedPassword.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(insertStmt, 3, dispName.c_str(), -1, SQLITE_STATIC);

    bool success = (sqlite3_step(insertStmt) == SQLITE_DONE);
    sqlite3_finalize(insertStmt);

    if (success) {
        std::cout << "[Database] User registered: " << username << std::endl;
    }

    return success;
}

bool Database::authenticateUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !db_) {
        return false;
    }

    const char* sql = "SELECT password FROM users WHERE username = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    bool authenticated = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* storedHash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string hashedInput = hashPassword(password);
        authenticated = (hashedInput == storedHash);
    }

    sqlite3_finalize(stmt);
    return authenticated;
}

bool Database::changePassword(const std::string& username, const std::string& oldPassword,
                              const std::string& newPassword) {
    // First authenticate with old password
    if (!authenticateUser(username, oldPassword)) {
        std::cout << "[Database] Password change failed - wrong old password: " << username << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "UPDATE users SET password = ?, updated_at = CURRENT_TIMESTAMP WHERE username = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string hashedPassword = hashPassword(newPassword);
    sqlite3_bind_text(stmt, 1, hashedPassword.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);

    if (success) {
        std::cout << "[Database] Password changed for: " << username << std::endl;
    }

    return success;
}

bool Database::userExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !db_) {
        return false;
    }

    const char* sql = "SELECT COUNT(*) FROM users WHERE username = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count > 0;
}

std::string Database::getDisplayName(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !db_) {
        return "";
    }

    const char* sql = "SELECT display_name FROM users WHERE username = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return "";
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    std::string displayName;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (name) {
            displayName = name;
        }
    }

    sqlite3_finalize(stmt);
    return displayName;
}

bool Database::updateDisplayName(const std::string& username, const std::string& displayName) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !db_) {
        return false;
    }

    const char* sql = "UPDATE users SET display_name = ?, updated_at = CURRENT_TIMESTAMP WHERE username = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, displayName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);

    if (success) {
        std::cout << "[Database] Display name updated for: " << username << std::endl;
    }

    return success;
}
