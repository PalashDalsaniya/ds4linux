#pragma once
// ds4linux GUI — IPC client (connects to the daemon's Unix domain socket)

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>

#include <functional>

namespace ds4linux::gui {

/// Async IPC client wrapping QLocalSocket for the daemon Unix domain socket.
/// Requests carry an "id" so each response reaches its own callback.
class IpcClient : public QObject {
    Q_OBJECT
public:
    using Reply = std::function<void(const QJsonObject& response)>;

    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    /// Connect to the daemon socket.
    void connectToDaemon(const QString& socketPath);

    /// Send a request {"type": type, ...extra}.  `onReply` receives the
    /// response (which may be {"type":"Error","message":...}).
    void request(const QString& type, const QJsonObject& extra = {}, Reply onReply = {});

    [[nodiscard]] bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& errorString);

private slots:
    void onReadyRead();

private:
    QLocalSocket*      socket_ = nullptr;
    QByteArray         recvBuf_;
    int                nextId_ = 1;
    QHash<int, Reply>  pending_;
};

} // namespace ds4linux::gui
