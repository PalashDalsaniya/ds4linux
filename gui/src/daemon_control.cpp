// ds4linux GUI — DaemonControl implementation

#include "ds4linux/daemon_control.h"
#include "ds4linux/constants.h"
#include "ds4linux/gui_util.h"

#include <QProcess>

namespace ds4linux::gui {

static QString unitName() { return toQString(kServiceName); }

DaemonControl::DaemonControl(QObject* parent) : QObject(parent) {}

void DaemonControl::start() { run("start", {"start", unitName()}); }
void DaemonControl::stop()  { run("stop",  {"stop",  unitName()}); }

void DaemonControl::setAutostart(bool on) {
    run(on ? "enable" : "disable", {on ? "enable" : "disable", unitName()});
}

void DaemonControl::run(const QString& action, const QStringList& args) {
    auto* p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    if (running_++ == 0) emit busyChanged(true);

    auto done = [this, p, action](const QString& error) {
        p->deleteLater();
        emit finished(action, error);
        if (--running_ == 0) emit busyChanged(false);
    };
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus st) {
        if (st == QProcess::NormalExit && code == 0) {
            done({});
        } else {
            const QString out = QString::fromLocal8Bit(p->readAll()).trimmed();
            done(out.isEmpty() ? QString("systemctl failed (exit code %1)").arg(code) : out);
        }
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) done("Could not run systemctl: " + p->errorString());
    });
    p->start("systemctl", args);
}

void DaemonControl::queryAutostart(std::function<void(bool, bool)> done) {
    auto* p = new QProcess(this);
    connect(p, &QProcess::finished, this, [p, done](int, QProcess::ExitStatus) {
        const QString state = QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed();
        p->deleteLater();
        // enabled / disabled / static / not-found ...
        done(state == "enabled", state == "enabled" || state == "disabled");
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        done(false, false);
    });
    p->start("systemctl", {"is-enabled", unitName()});
}

} // namespace ds4linux::gui
