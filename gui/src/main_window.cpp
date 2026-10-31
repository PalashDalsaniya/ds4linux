// ds4linux GUI — MainWindow implementation

#include "ds4linux/main_window.h"
#include "ds4linux/controllers_page.h"
#include "ds4linux/ipc_client.h"
#include "ds4linux/profile_editor.h"
#include "ds4linux/tester_page.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLabel>
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

    offline_ = new QLabel(
        QString("Not connected to the ds4linux daemon.\n\n"
                "Start it with:  sudo ds4linux-daemon\n"
                "or:  sudo systemctl start ds4linux\n\n"
                "Waiting for %1 …").arg(socketPath_), this);
    offline_->setAlignment(Qt::AlignCenter);
    layout->addWidget(offline_);

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
        if (!ipc_->isConnected()) onDisconnected();
    });

    onDisconnected();
    connectToDaemon();
}

MainWindow::~MainWindow() = default;

void MainWindow::connectToDaemon() {
    ipc_->connectToDaemon(socketPath_);
}

void MainWindow::onConnected() {
    retryTimer_->stop();
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
    offline_->show();
    statusBar()->showMessage("Not connected to the daemon — retrying…");
    if (!retryTimer_->isActive()) retryTimer_->start();
}

} // namespace ds4linux::gui
