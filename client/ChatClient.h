/**
 * @file ChatClient.h
 * @brief Chat client networking class with Qt signals/slots
 */

#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QString>
#include <QStringList>
#include "../common/Protocol.h"

class ChatClient : public QObject {
    Q_OBJECT

public:
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Authenticated
    };
    Q_ENUM(ConnectionState)

    explicit ChatClient(QObject* parent = nullptr);
    ~ChatClient();

    // Connection
    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;
    ConnectionState connectionState() const { return state_; }

    // Authentication
    void login(const QString& username, const QString& password);
    void logout();
    void registerUser(const QString& username, const QString& password);
    void changePassword(const QString& oldPassword, const QString& newPassword);

    bool isAuthenticated() const { return state_ == ConnectionState::Authenticated; }
    QString currentUsername() const { return username_; }
    QString currentDisplayName() const { return displayName_; }

    // Chat
    void sendGlobalMessage(const QString& content);
    void sendPrivateMessage(const QString& receiver, const QString& content);

signals:
    // Connection signals
    void connected();
    void disconnected();
    void connectionError(const QString& error);
    void connectionStateChanged(ConnectionState state);

    // Authentication signals
    void loginSuccess(const QString& username, const QString& displayName);
    void loginFailed(const QString& error);
    void logoutSuccess();
    void registerSuccess();
    void registerFailed(const QString& error);
    void passwordChangeSuccess();
    void passwordChangeFailed(const QString& error);

    // Chat signals
    void globalMessageReceived(const QString& sender, const QString& content, const QString& timestamp);
    void privateMessageReceived(const QString& sender, const QString& receiver,
                                const QString& content, const QString& timestamp);

    // User status signals
    void onlineListReceived(const QStringList& users);
    void userOnline(const QString& username);
    void userOffline(const QString& username);

    // Error signals
    void errorReceived(const QString& error);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onPingTimeout();

private:
    void sendMessage(const Protocol::Message& msg);
    void processMessage(const Protocol::Message& msg);
    void handleOkResponse(const Protocol::Message& msg);
    void handleErrorResponse(const Protocol::Message& msg);
    void setState(ConnectionState state);

    QTcpSocket* socket_;
    QTimer* pingTimer_;
    Protocol::MessageBuffer buffer_;
    ConnectionState state_;
    QString username_;
    QString displayName_;

    // Track pending operations
    enum class PendingOp {
        None,
        Login,
        Register,
        ChangePassword
    };
    PendingOp pendingOp_;
};

#endif // CHATCLIENT_H
