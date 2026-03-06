// ds4linux GUI — IPC client implementation

#include "ds4linux/ipc_client.h"
#include "ds4linux/constants.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDataStream>
#include <QtEndian>

namespace ds4linux::gui {

IpcClient::IpcClient(QObject* parent)
    : QObject(parent)
    , socket_(new QLocalSocket(this))
{
    connect(socket_, &QLocalSocket::connected,    this, &IpcClient::onConnected);
    connect(socket_, &QLocalSocket::disconnected, this, &IpcClient::onDisconnected);
    connect(socket_, &QLocalSocket::readyRead,    this, &IpcClient::onReadyRead);
    connect(socket_, &QLocalSocket::errorOccurred,this, &IpcClient::onError);
}

IpcClient::~IpcClient() = default;

void IpcClient::connectToDaemon(const QString& socketPath) {
    socket_->connectToServer(socketPath);
}

bool IpcClient::isConnected() const {
    return socket_->state() == QLocalSocket::ConnectedState;
}

void IpcClient::send(const QString& jsonPayload) {
    if (!isConnected()) return;

    QByteArray payload = jsonPayload.toUtf8();
    quint32 len = qToBigEndian(static_cast<quint32>(payload.size()));

    QByteArray frame;
    frame.append(reinterpret_cast<const char*>(&len), sizeof(len));
    frame.append(payload);

    socket_->write(frame);
    socket_->flush();
}

void IpcClient::sendCommand(const QString& type, const QJsonObject& extra) {
    QJsonObject msg = extra;
    msg["type"] = type;
    send(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

// ── Slots ────────────────────────────────────────────────────────────────────

void IpcClient::onConnected() {
    emit connected();
}

void IpcClient::onDisconnected() {
    emit disconnected();
}

void IpcClient::onError(QLocalSocket::LocalSocketError /*err*/) {
    emit errorOccurred(socket_->errorString());
}

void IpcClient::onReadyRead() {
    recvBuf_.append(socket_->readAll());

    // Decode length-prefixed frames
    while (recvBuf_.size() >= static_cast<qsizetype>(sizeof(quint32))) {
        quint32 netLen = 0;
        std::memcpy(&netLen, recvBuf_.constData(), sizeof(netLen));
        quint32 payloadLen = qFromBigEndian(netLen);

        if (payloadLen > kIpcMaxMessageBytes) {
            recvBuf_.clear();
            emit errorOccurred("IPC frame too large");
            return;
        }

        auto totalFrame = static_cast<qsizetype>(sizeof(quint32) + payloadLen);
        if (recvBuf_.size() < totalFrame) break; // incomplete

        QString payload = QString::fromUtf8(
            recvBuf_.constData() + sizeof(quint32),
            static_cast<qsizetype>(payloadLen)
        );

        recvBuf_.remove(0, static_cast<int>(totalFrame));
        emit messageReceived(payload);
    }
}

} // namespace ds4linux::gui
