#pragma once
// ds4linux GUI — IPC client (connects to daemon's Unix domain socket)

#include <QObject>
#include <QLocalSocket>
#include <QJsonObject>
#include <QString>
#include <QByteArray>

#include <functional>

namespace ds4linux::gui {

/// Async IPC client wrapping QLocalSocket for the daemon Unix domain socket.
class IpcClient : public QObject {
    Q_OBJECT

public:
    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    /// Connect to the daemon socket.
    void connectToDaemon(const QString& socketPath);

    /// Send a JSON message to the daemon.
    void send(const QString& jsonPayload);

    /// Convenience: send a typed message with optional payload object.
    void sendCommand(const QString& type, const QJsonObject& extra = QJsonObject{});

    [[nodiscard]] bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString& jsonPayload);
    void errorOccurred(const QString& errorString);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onError(QLocalSocket::LocalSocketError err);

private:
    QLocalSocket* socket_ = nullptr;
    QByteArray    recvBuf_;
};

} // namespace ds4linux::gui
