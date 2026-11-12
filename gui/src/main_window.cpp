// ds4linux GUI — MainWindow implementation

#include "ds4linux/main_window.h"
#include "ds4linux/controllers_page.h"
#include "ds4linux/daemon_control.h"
#include "ds4linux/ipc_client.h"
#include "ds4linux/profile_editor.h"
#include "ds4linux/tester_page.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace ds4linux::gui {

MainWindow::MainWindow(QString socketPath, QWidget* parent)
    : QMainWindow(parent), socketPath_(std::move(socketPath))
{
    setWindowTitle("DS4Linux");
    resize(980, 700);

    ipc_ = new IpcClient(this);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    daemon_ = new DaemonControl(this);

    // ── Not running: start button ────────────────────────────────────────────
    offline_ = new QWidget(this);
    auto* offLayout = new QVBoxLayout(offline_);
    offLayout->addStretch();
    offlineText_ = new QLabel(
        "<h2>ds4linux is not running</h2>"
        "<p>Your controllers work as normal (raw) controllers.<br>"
        "Start ds4linux to use profiles, remapping and DS4 / Xbox 360 emulation.</p>",
        offline_);
    offlineText_->setAlignment(Qt::AlignCenter);
    offLayout->addWidget(offlineText_);
    startBtn_ = new QPushButton("Start ds4linux", offline_);
    startBtn_->setMinimumWidth(200);
    offLayout->addWidget(startBtn_, 0, Qt::AlignCenter);
    auto* startOnOpen = new QCheckBox("Start ds4linux when this app opens", offline_);
    startOnOpen->setChecked(QSettings().value("startDaemonOnOpen", false).toBool());
    offLayout->addWidget(startOnOpen, 0, Qt::AlignCenter);
    auto* hint = new QLabel(
        "Starting or stopping asks for your password once.\n"
        "Start ds4linux before Steam, or restart Steam afterwards.", offline_);
    hint->setAlignment(Qt::AlignCenter);
    hint->setEnabled(false);  // dimmed
    offLayout->addWidget(hint);
    offLayout->addStretch();
    layout->addWidget(offline_);

    connect(startBtn_, &QPushButton::clicked, daemon_, &DaemonControl::start);
    connect(startOnOpen, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue("startDaemonOnOpen", on);
    });

    tabs_        = new QTabWidget(this);
    controllers_ = new ControllersPage(ipc_, this);
    profiles_    = new ProfileEditor(ipc_, this);
    tester_      = new TesterPage(ipc_, this);
    tabs_->addTab(controllers_, "Controllers");
    tabs_->addTab(profiles_, "Profiles");
    tabs_->addTab(tester_, "Controller test");
    layout->addWidget(tabs_);
    setCentralWidget(central);

    statusBar()->showMessage("Connecting to daemon…");

    // ── Running: stop button and start-at-boot ───────────────────────────────
    bootBox_ = new QCheckBox("Start at boot", this);
    bootBox_->setToolTip("Start ds4linux automatically when the computer starts");
    stopBtn_ = new QPushButton("Stop ds4linux", this);
    stopBtn_->setToolTip("Stop emulation and give the raw controllers back");
    statusBar()->addPermanentWidget(bootBox_);
    statusBar()->addPermanentWidget(stopBtn_);
    connect(stopBtn_, &QPushButton::clicked, daemon_, &DaemonControl::stop);
    connect(bootBox_, &QCheckBox::clicked, daemon_, &DaemonControl::setAutostart);
    connect(daemon_, &DaemonControl::finished, this, &MainWindow::onServiceFinished);
    connect(daemon_, &DaemonControl::busyChanged, this, [this](bool busy) {
        startBtn_->setEnabled(!busy);
        stopBtn_->setEnabled(!busy);
        bootBox_->setEnabled(!busy);
    });
    refreshAutostart();

    connect(profiles_, &ProfileEditor::profilesChanged, controllers_, &ControllersPage::setProfiles);
    connect(controllers_, &ControllersPage::devicesChanged, tester_, &TesterPage::setDevices);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
        if (ipc_->isConnected()) controllers_->refresh();
    });

    // Controller list / battery refresh
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(2000);
    connect(pollTimer_, &QTimer::timeout, controllers_, &ControllersPage::refresh);

    retryTimer_ = new QTimer(this);
    retryTimer_->setInterval(2000);
    retryTimer_->setSingleShot(true);
    connect(retryTimer_, &QTimer::timeout, this, &MainWindow::connectToDaemon);

    connect(ipc_, &IpcClient::connected, this, &MainWindow::onConnected);
    connect(ipc_, &IpcClient::disconnected, this, &MainWindow::onDisconnected);
    connect(ipc_, &IpcClient::errorOccurred, this, [this](const QString&) {
        if (ipc_->isConnected()) return;
        onDisconnected();
        // Not running when the app opened: start it if the user asked for that
        if (!triedAutoStart_) {
            triedAutoStart_ = true;
            if (QSettings().value("startDaemonOnOpen", false).toBool()) daemon_->start();
        }
    });

    onDisconnected();
    connectToDaemon();
}

MainWindow::~MainWindow() = default;

void MainWindow::connectToDaemon() {
    ipc_->connectToDaemon(socketPath_);
}

void MainWindow::onConnected() {
    triedAutoStart_ = true;
    retryTimer_->stop();
    stopBtn_->show();
    offline_->hide();
    tabs_->show();
    ipc_->request("Ping", {}, [this](const QJsonObject& r) {
        statusBar()->showMessage(QString("Connected to ds4linux daemon %1 · GUI %2")
                                     .arg(r.value("version").toString(),
                                          QCoreApplication::applicationVersion()));
    });
    profiles_->refreshProfileList();
    controllers_->refresh();
    pollTimer_->start();
}

void MainWindow::onDisconnected() {
    pollTimer_->stop();
    tabs_->hide();
    stopBtn_->hide();
    offline_->show();
    if (!daemon_->busy()) statusBar()->showMessage("ds4linux is not running");
    if (!retryTimer_->isActive()) retryTimer_->start();
}

void MainWindow::onServiceFinished(const QString& action, const QString& error) {
    if (!error.isEmpty()) {
        statusBar()->showMessage(QString("Could not %1 ds4linux: %2").arg(action, error));
        if (action == "enable" || action == "disable") refreshAutostart();
        return;
    }
    if (action == "start") {
        statusBar()->showMessage("ds4linux started");
        connectToDaemon();
    } else if (action == "stop") {
        statusBar()->showMessage("ds4linux stopped; your controllers work as normal again");
    } else {
        statusBar()->showMessage(action == "enable" ? "ds4linux will start at boot"
                                                    : "ds4linux will no longer start at boot");
    }
}

void MainWindow::refreshAutostart() {
    daemon_->queryAutostart([this](bool enabled, bool known) {
        bootBox_->setChecked(enabled);
        bootBox_->setVisible(known);  // hidden if the service isn't installed
    });
}

} // namespace ds4linux::gui
