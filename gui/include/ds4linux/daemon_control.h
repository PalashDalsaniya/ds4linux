#pragma once
// ds4linux GUI — start / stop the daemon's systemd service
//
// Runs systemctl asynchronously.  Starting or stopping a system service as a
// normal user goes through polkit, so the desktop's authentication dialog
// asks for the password (remembered for a few minutes).

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace ds4linux::gui {

class DaemonControl : public QObject {
    Q_OBJECT
public:
    explicit DaemonControl(QObject* parent = nullptr);

    void start();
    void stop();
    void setAutostart(bool on);

    /// Look up whether the service starts at boot; `done(enabled, known)`.
    void queryAutostart(std::function<void(bool enabled, bool known)> done);

    [[nodiscard]] bool busy() const noexcept { return running_ > 0; }

signals:
    /// A start / stop / autostart change finished.  `error` is empty on
    /// success, otherwise systemctl's message (e.g. authentication cancelled).
    void finished(const QString& action, const QString& error);
    void busyChanged(bool busy);

private:
    void run(const QString& action, const QStringList& args);

    int running_ = 0;
};

} // namespace ds4linux::gui
