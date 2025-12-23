/**
 * @file ChatClient.cpp
 * @brief Implementation of Chat client networking
 */

#include "ChatClient.h"
#include "../thirdparty/json.hpp"
#include <QDebug>

using json = nlohmann::json;

ChatClient::ChatClient(QObject* parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , pingTimer_(new QTimer(this))
    , state_(ConnectionState::Disconnected)
    , pendingOp_(PendingOp::None) {

    // Connect socket signals
    connect(socket_, &QTcpSocket::connected, this, &ChatClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &ChatClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead, this, &ChatClient::onReadyRead);
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &ChatClient::onSocketError);

    // Setup ping timer (heartbeat every 30 seconds)
    pingTimer_->setInterval(30000);
    connect(pingTimer_, &QTimer::timeout, this, &ChatClient::onPingTimeout);
}

ChatClient::~ChatClient() {
    disconnectFromServer();
}

void ChatClient::connectToServer(const QString& host, quint16 port) {
    if (state_ != ConnectionState::Disconnected) {
        disconnectFromServer();
    }

    setState(ConnectionState::Connecting);
    socket_->connectToHost(host, port);
}

void ChatClient::disconnectFromServer() {
    pingTimer_->stop();
    buffer_.clear();
    username_.clear();
    displayName_.clear();
    pendingOp_ = PendingOp::None;

    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            socket_->waitForDisconnected(1000);
        }
    }

    setState(ConnectionState::Disconnected);
}

bool ChatClient::isConnected() const {
    return socket_->state() == QAbstractSocket::ConnectedState;
}

void ChatClient::login(const QString& username, const QString& password) {
    if (!isConnected()) {
        emit loginFailed("Not connected to server");
        return;
    }

    json j;
    j["username"] = username.toStdString();
    j["password"] = password.toStdString();

    Protocol::Message msg(Protocol::MessageType::LOGIN);
    msg.content = j.dump();

    pendingOp_ = PendingOp::Login;
    sendMessage(msg);
}

void ChatClient::logout() {
    if (!isAuthenticated()) {
        return;
    }

    Protocol::Message msg(Protocol::MessageType::LOGOUT);
    sendMessage(msg);
}

void ChatClient::registerUser(const QString& username, const QString& password) {
    if (!isConnected()) {
        emit registerFailed("Not connected to server");
        return;
    }

    json j;
    j["username"] = username.toStdString();
    j["password"] = password.toStdString();

    Protocol::Message msg(Protocol::MessageType::REGISTER);
    msg.content = j.dump();

    pendingOp_ = PendingOp::Register;
    sendMessage(msg);
}

void ChatClient::changePassword(const QString& oldPassword, const QString& newPassword) {
    if (!isAuthenticated()) {
        emit passwordChangeFailed("Not logged in");
        return;
    }

    json j;
    j["oldPassword"] = oldPassword.toStdString();
    j["newPassword"] = newPassword.toStdString();

    Protocol::Message msg(Protocol::MessageType::CHANGE_PASSWORD);
    msg.content = j.dump();

    pendingOp_ = PendingOp::ChangePassword;
    sendMessage(msg);
}

void ChatClient::sendGlobalMessage(const QString& content) {
    if (!isAuthenticated()) {
        return;
    }

    Protocol::Message msg = Protocol::createGlobalMessage(
        username_.toStdString(), content.toStdString());
    sendMessage(msg);
}

void ChatClient::sendPrivateMessage(const QString& receiver, const QString& content) {
    if (!isAuthenticated()) {
        return;
    }

    Protocol::Message msg = Protocol::createPrivateMessage(
        username_.toStdString(), receiver.toStdString(), content.toStdString());
    sendMessage(msg);
}

void ChatClient::sendMessage(const Protocol::Message& msg) {
    if (!isConnected()) {
        return;
    }

    std::vector<uint8_t> data = Protocol::serialize(msg);
    socket_->write(reinterpret_cast<const char*>(data.data()), data.size());
    socket_->flush();
}

void ChatClient::onConnected() {
    setState(ConnectionState::Connected);
    pingTimer_->start();
    emit connected();
}

void ChatClient::onDisconnected() {
    pingTimer_->stop();
    buffer_.clear();

    ConnectionState oldState = state_;
    setState(ConnectionState::Disconnected);

    if (oldState == ConnectionState::Authenticated) {
        username_.clear();
        displayName_.clear();
    }

    emit disconnected();
}

void ChatClient::onReadyRead() {
    QByteArray data = socket_->readAll();
    buffer_.append(reinterpret_cast<const uint8_t*>(data.constData()), data.size());

    while (buffer_.hasCompleteMessage()) {
        Protocol::Message msg = buffer_.extractMessage();
        processMessage(msg);
    }
}

void ChatClient::onSocketError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error)
    QString errorStr = socket_->errorString();
    qDebug() << "Socket error:" << errorStr;

    emit connectionError(errorStr);

    if (state_ == ConnectionState::Connecting) {
        setState(ConnectionState::Disconnected);
    }
}

void ChatClient::onPingTimeout() {
    if (isConnected()) {
        Protocol::Message msg(Protocol::MessageType::PING);
        sendMessage(msg);
    }
}

void ChatClient::processMessage(const Protocol::Message& msg) {
    switch (msg.type) {
        case Protocol::MessageType::OK:
            handleOkResponse(msg);
            break;

        case Protocol::MessageType::ERROR:
            handleErrorResponse(msg);
            break;

        case Protocol::MessageType::MSG_GLOBAL:
            emit globalMessageReceived(
                QString::fromStdString(msg.sender),
                QString::fromStdString(msg.content),
                QString::fromStdString(msg.timestamp));
            break;

        case Protocol::MessageType::MSG_PRIVATE:
            emit privateMessageReceived(
                QString::fromStdString(msg.sender),
                QString::fromStdString(msg.receiver),
                QString::fromStdString(msg.content),
                QString::fromStdString(msg.timestamp));
            break;

        case Protocol::MessageType::ONLINE_LIST: {
            QStringList users;
            try {
                json j = json::parse(msg.extra);
                for (const auto& user : j) {
                    users.append(QString::fromStdString(user.get<std::string>()));
                }
            } catch (...) {}
            emit onlineListReceived(users);
            break;
        }

        case Protocol::MessageType::USER_STATUS:
            if (msg.content == "online") {
                emit userOnline(QString::fromStdString(msg.sender));
            } else {
                emit userOffline(QString::fromStdString(msg.sender));
            }
            break;

        case Protocol::MessageType::PONG:
            // Heartbeat response received
            break;

        default:
            qDebug() << "Unknown message type:" << static_cast<int>(msg.type);
            break;
    }
}

void ChatClient::handleOkResponse(const Protocol::Message& msg) {
    PendingOp op = pendingOp_;
    pendingOp_ = PendingOp::None;

    switch (op) {
        case PendingOp::Login: {
            // Parse user info from extra
            try {
                json j = json::parse(msg.extra);
                username_ = QString::fromStdString(j["username"].get<std::string>());
                displayName_ = QString::fromStdString(j["displayName"].get<std::string>());
            } catch (...) {
                username_ = "Unknown";
                displayName_ = "Unknown";
            }
            setState(ConnectionState::Authenticated);
            emit loginSuccess(username_, displayName_);
            break;
        }

        case PendingOp::Register:
            emit registerSuccess();
            break;

        case PendingOp::ChangePassword:
            emit passwordChangeSuccess();
            break;

        default:
            // Could be logout response
            if (msg.content.find("Logged out") != std::string::npos) {
                username_.clear();
                displayName_.clear();
                setState(ConnectionState::Connected);
                emit logoutSuccess();
            }
            break;
    }
}

void ChatClient::handleErrorResponse(const Protocol::Message& msg) {
    QString error = QString::fromStdString(msg.content);

    PendingOp op = pendingOp_;
    pendingOp_ = PendingOp::None;

    switch (op) {
        case PendingOp::Login:
            emit loginFailed(error);
            break;

        case PendingOp::Register:
            emit registerFailed(error);
            break;

        case PendingOp::ChangePassword:
            emit passwordChangeFailed(error);
            break;

        default:
            emit errorReceived(error);
            break;
    }
}

void ChatClient::setState(ConnectionState state) {
    if (state_ != state) {
        state_ = state;
        emit connectionStateChanged(state);
    }
}
