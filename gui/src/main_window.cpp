// ds4linux GUI — MainWindow implementation

#include "ds4linux/main_window.h"
#include "ds4linux/constants.h"
#include "ds4linux/device_widget.h"
#include "ds4linux/ipc_client.h"
#include "ds4linux/led_picker.h"
#include "ds4linux/profile_editor.h"

#include <QApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace ds4linux::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString("DS4Linux v%1").arg(QString::fromStdString(std::string(kVersion))));
    resize(900, 600);

    setupUi();
    connectToDaemon();
}

MainWindow::~MainWindow() = default;

// ── UI construction ──────────────────────────────────────────────────────────

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* root    = new QHBoxLayout(central);

    // ── Left: devices panel ──────────────────────────────────────────────────
    auto* devGroup = new QGroupBox("Connected Controllers", this);
    devicesLayout_ = new QVBoxLayout(devGroup);

    auto* rescanBtn = new QPushButton("Rescan Controllers", this);
    connect(rescanBtn, &QPushButton::clicked, this, [this]() {
        if (ipc_->isConnected()) {
            ipc_->sendCommand("RescanDevices");
            // Refresh device list shortly after rescan completes
            QTimer::singleShot(500, this, &MainWindow::pollDevices);
        }
    });
    devicesLayout_->addWidget(rescanBtn);
    devicesLayout_->addStretch();

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(devGroup);
    scrollArea->setMinimumWidth(350);

    // ── Right: profile editor + LED picker ───────────────────────────────────
    ipc_ = new IpcClient(this);

    auto* rightPanel = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPanel);

    profileEditor_ = new ProfileEditor(ipc_, this);
    ledPicker_     = new LedPicker(this);

    rightLayout->addWidget(profileEditor_);
    rightLayout->addWidget(ledPicker_);
    rightLayout->addStretch();

    // ── Splitter ─────────────────────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(scrollArea);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    root->addWidget(splitter);
    setCentralWidget(central);

    // ── Status bar ───────────────────────────────────────────────────────────
    statusBar_ = new QStatusBar(this);
    setStatusBar(statusBar_);
    statusBar_->showMessage("Connecting to daemon…");

    // ── Poll timer (battery / device list refresh) ───────────────────────────
    pollTimer_ = new QTimer(this);
    connect(pollTimer_, &QTimer::timeout, this, &MainWindow::pollDevices);

    // ── IPC signals ──────────────────────────────────────────────────────────
    connect(ipc_, &IpcClient::connected,       this, &MainWindow::onDaemonConnected);
    connect(ipc_, &IpcClient::disconnected,    this, &MainWindow::onDaemonDisconnected);
    connect(ipc_, &IpcClient::messageReceived, this, &MainWindow::onDaemonMessage);
    connect(ipc_, &IpcClient::errorOccurred,   this, [this](const QString& e) {
        statusBar_->showMessage("IPC error: " + e);
    });

    // ── LED picker → daemon ──────────────────────────────────────────────────
    connect(ledPicker_, &LedPicker::colorChanged, this, [this](const QColor& c) {
        QJsonObject extra;
        extra["r"] = c.red();
        extra["g"] = c.green();
        extra["b"] = c.blue();
        ipc_->sendCommand("SetLightbarColor", extra);
    });
}

// ── Daemon connection ────────────────────────────────────────────────────────

void MainWindow::connectToDaemon() {
    ipc_->connectToDaemon(QString::fromStdString(std::string(kSocketPath)));
}

void MainWindow::onDaemonConnected() {
    statusBar_->showMessage("Connected to daemon");
    pollTimer_->start(3000); // refresh every 3 s
    pollDevices();
    profileEditor_->refreshProfileList();
}

void MainWindow::onDaemonDisconnected() {
    statusBar_->showMessage("Disconnected from daemon — retrying…");
    pollTimer_->stop();

    // Retry after 2 seconds
    QTimer::singleShot(2000, this, &MainWindow::connectToDaemon);
}

// ── IPC message handler ──────────────────────────────────────────────────────

void MainWindow::onDaemonMessage(const QString& json) {
    auto doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return;
    auto obj  = doc.object();
    auto type = obj.value("type").toString();

    if (type == "DeviceList") {
        // Clear existing device widgets but keep the rescan button (index 0)
        // and the stretch (last item)
        while (devicesLayout_->count() > 2) { // keep rescan btn + stretch
            auto* item = devicesLayout_->takeAt(1); // remove after rescan btn
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }

        auto devs = obj.value("devices").toArray();
        for (const auto& d : devs) {
            auto dobj = d.toObject();
            auto* w = new DeviceWidget(this);
            w->setDeviceName(dobj.value("name").toString());
            w->setDevicePath(dobj.value("path").toString());
            w->setBatteryPercent(dobj.value("battery").toInt(-1));
            w->setProfileName(dobj.value("profile").toString());
            // Insert before the stretch (last item)
            devicesLayout_->insertWidget(devicesLayout_->count() - 1, w);
        }

        if (devs.isEmpty()) {
            auto* lbl = new QLabel("No controllers connected.\n"
                                   "Make sure the daemon runs as root (sudo).",
                                   this);
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setWordWrap(true);
            devicesLayout_->insertWidget(1, lbl); // after rescan btn
        }

        statusBar_->showMessage(
            QString("%1 controller(s) connected").arg(devs.size()), 5000);

    } else if (type == "ProfileList") {
        // Populate the profile combo in the editor
        auto profiles = obj.value("profiles").toArray();
        auto* combo = profileEditor_->findChild<QComboBox*>();
        if (combo) {
            combo->clear();
            for (const auto& p : profiles) {
                combo->addItem(p.toString());
            }
        }

    } else if (type == "Pong") {
        statusBar_->showMessage(
            QString("Daemon v%1 online").arg(obj.value("version").toString()),
            5000
        );
    }
}

void MainWindow::pollDevices() {
    if (ipc_->isConnected()) {
        ipc_->sendCommand("ListDevices");
    }
}

} // namespace ds4linux::gui
