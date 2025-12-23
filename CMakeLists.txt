cmake_minimum_required(VERSION 3.14)
project(TCPChat VERSION 1.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(BUILD_SERVER "Build the chat server" ON)
option(BUILD_CLIENT "Build the chat client (requires Qt)" ON)

# Platform-specific settings
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread)
endif()

# Common library (Protocol)
add_library(common STATIC
    common/Protocol.cpp
    common/Protocol.h
)
target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)

# Server
if(BUILD_SERVER)
    message(STATUS "Building Chat Server...")

    # Find SQLite3
    find_package(SQLite3 QUIET)
    if(NOT SQLite3_FOUND)
        # Try pkg-config
        find_package(PkgConfig)
        if(PkgConfig_FOUND)
            pkg_check_modules(SQLITE3 sqlite3)
        endif()
    endif()

    if(SQLite3_FOUND OR SQLITE3_FOUND)
        add_executable(chat_server
            server/server_main.cpp
            server/Server.cpp
            server/Server.h
            server/ClientSession.cpp
            server/ClientSession.h
            server/Database.cpp
            server/Database.h
        )

        target_link_libraries(chat_server
            common
            ${PLATFORM_LIBS}
        )

        if(SQLite3_FOUND)
            target_include_directories(chat_server PRIVATE ${SQLite3_INCLUDE_DIRS})
            target_link_libraries(chat_server ${SQLite3_LIBRARIES})
        else()
            target_include_directories(chat_server PRIVATE ${SQLITE3_INCLUDE_DIRS})
            target_link_libraries(chat_server ${SQLITE3_LIBRARIES})
        endif()

        # Also try to link sqlite3 directly if the above doesn't work
        target_link_libraries(chat_server sqlite3)

        message(STATUS "Chat Server will be built")
    else()
        message(WARNING "SQLite3 not found. Server will not be built.")
        message(WARNING "Install SQLite3: sudo apt install libsqlite3-dev (Ubuntu/Debian)")
    endif()
endif()

# Client
if(BUILD_CLIENT)
    message(STATUS "Building Chat Client...")

    # Find Qt (prefer Qt6, fall back to Qt5)
    find_package(Qt6 COMPONENTS Widgets Network QUIET)
    if(NOT Qt6_FOUND)
        find_package(Qt5 COMPONENTS Widgets Network QUIET)
        if(Qt5_FOUND)
            set(QT_VERSION 5)
            set(QT_LIBRARIES Qt5::Widgets Qt5::Network)
        endif()
    else()
        set(QT_VERSION 6)
        set(QT_LIBRARIES Qt6::Widgets Qt6::Network)
    endif()

    if(Qt6_FOUND OR Qt5_FOUND)
        # Enable Qt MOC, UIC, RCC
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTOUIC ON)
        set(CMAKE_AUTORCC ON)

        add_executable(chat_client
            client/client_main.cpp
            client/MainWindow.cpp
            client/MainWindow.h
            client/AuthDialog.cpp
            client/AuthDialog.h
            client/ChatClient.cpp
            client/ChatClient.h
        )

        target_link_libraries(chat_client
            common
            ${QT_LIBRARIES}
            ${PLATFORM_LIBS}
        )

        message(STATUS "Chat Client will be built with Qt${QT_VERSION}")
    else()
        message(WARNING "Qt5 or Qt6 not found. Client will not be built.")
        message(WARNING "Install Qt: sudo apt install qt5-default qtbase5-dev (Ubuntu/Debian)")
        message(WARNING "         or: sudo apt install qt6-base-dev (Ubuntu 22.04+)")
    endif()
endif()

# Installation
install(TARGETS chat_server chat_client
    RUNTIME DESTINATION bin
    OPTIONAL
)

# Print summary
message(STATUS "")
message(STATUS "=== Build Configuration ===")
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
