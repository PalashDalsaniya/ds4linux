// ds4linux GUI — IPC client implementation

#include "ds4linux/ipc_client.h"
#include "ds4linux/constants.h"

#include <QJsonDocument>
#include <QtEndian>

#include <cstring>

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
    const quint32 len = qToBigEndian(static_cast<quint32>(payload.size()));
    QByteArray frame(reinterpret_cast<const char*>(&len), sizeof(len));
    frame.append(payload);
    socket_->write(frame);
}

void IpcClient::onReadyRead() {
    recvBuf_.append(socket_->readAll());

    // Decode length-prefixed (big-endian) frames
    while (recvBuf_.size() >= static_cast<qsizetype>(sizeof(quint32))) {
        quint32 netLen = 0;
        std::memcpy(&netLen, recvBuf_.constData(), sizeof(netLen));
        const quint32 payloadLen = qFromBigEndian(netLen);

        if (payloadLen > kIpcMaxMessageBytes) {
            emit errorOccurred("IPC frame too large");
            socket_->abort();
            return;
        }

        const auto total = static_cast<qsizetype>(sizeof(quint32) + payloadLen);
        if (recvBuf_.size() < total) break; // incomplete

        const auto doc = QJsonDocument::fromJson(
            recvBuf_.mid(sizeof(quint32), static_cast<qsizetype>(payloadLen)));
        recvBuf_.remove(0, total);

        const QJsonObject obj = doc.object();
        const int id = obj.value("id").toInt(0);
        if (auto it = pending_.find(id); it != pending_.end()) {
            Reply cb = std::move(it.value());
            pending_.erase(it);
            cb(obj);
        }
    }
}

} // namespace ds4linux::gui
