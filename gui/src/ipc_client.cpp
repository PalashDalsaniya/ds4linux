// ds4linux GUI — IPC client implementation

#include "ds4linux/ipc_client.h"
#include "ds4linux/ipc_protocol.h"

#include <QJsonDocument>

#include <exception>

namespace ds4linux::gui {

IpcClient::IpcClient(QObject* parent)
    : QObject(parent)
    , socket_(new QLocalSocket(this))
{
    connect(socket_, &QLocalSocket::connected, this, &IpcClient::connected);
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        pending_.clear();
        recvBuf_.clear();
        emit disconnected();
    });
    connect(socket_, &QLocalSocket::readyRead, this, &IpcClient::onReadyRead);
    connect(socket_, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        emit errorOccurred(socket_->errorString());
    });
}

IpcClient::~IpcClient() = default;

void IpcClient::connectToDaemon(const QString& socketPath) {
    if (socket_->state() != QLocalSocket::UnconnectedState) socket_->abort();
    socket_->connectToServer(socketPath);
}

bool IpcClient::isConnected() const {
    return socket_->state() == QLocalSocket::ConnectedState;
}

void IpcClient::request(const QString& type, const QJsonObject& extra, Reply onReply) {
    if (!isConnected()) return;

    QJsonObject msg = extra;
    msg["type"] = type;
    const int id = nextId_++;
    msg["id"] = id;
    if (onReply) pending_.insert(id, std::move(onReply));

    const QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    const auto frame = ipc::encode_message(payload.toStdString());
    socket_->write(reinterpret_cast<const char*>(frame.data()),
                   static_cast<qint64>(frame.size()));
}

void IpcClient::onReadyRead() {
    const QByteArray data = socket_->readAll();
    recvBuf_.insert(recvBuf_.end(), data.begin(), data.end());

    try {
        while (auto payload = ipc::decode_message(recvBuf_)) {
            const QJsonObject obj = QJsonDocument::fromJson(
                QByteArray::fromStdString(*payload)).object();
            const int id = obj.value("id").toInt(0);
            if (auto it = pending_.find(id); it != pending_.end()) {
                Reply cb = std::move(it.value());
                pending_.erase(it);
                cb(obj);
            }
        }
    } catch (const std::exception& ex) {  // oversized / corrupt frame
        emit errorOccurred(QString::fromUtf8(ex.what()));
        socket_->abort();
    }
}

} // namespace ds4linux::gui
