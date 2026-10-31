// ds4linux GUI — ControllersPage implementation

#include "ds4linux/controllers_page.h"
#include "ds4linux/ipc_client.h"

#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ds4linux::gui {

namespace {
enum Column { ColIndex, ColModel, ColConnection, ColBattery, ColOutput, ColProfile, ColCount };

void showError(QWidget* parent, const QJsonObject& resp) {
    if (resp.value("type").toString() == "Error") {
        QMessageBox::warning(parent, "DS4Linux", resp.value("message").toString());
    }
}
} // namespace

ControllersPage::ControllersPage(IpcClient* ipc, QWidget* parent)
    : QWidget(parent), ipc_(ipc)
{
    auto* layout = new QVBoxLayout(this);

    table_ = new QTableWidget(0, ColCount, this);
    table_->setHorizontalHeaderLabels({"#", "Controller", "Connection", "Battery", "Output", "Profile"});
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setSectionResizeMode(ColModel, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColIndex, QHeaderView::ResizeToContents);
    table_->setColumnWidth(ColBattery, 140);
    table_->setColumnWidth(ColProfile, 170);
    layout->addWidget(table_);

    empty_ = new QLabel("No controllers connected.\n"
                        "Connect a DualShock 4 or DualSense and press Rescan.", this);
    empty_->setAlignment(Qt::AlignCenter);
    layout->addWidget(empty_);

    auto* buttons = new QHBoxLayout;
    auto* rescanBtn = new QPushButton("Rescan", this);
    rumbleBtn_ = new QPushButton("Test rumble", this);
    colorBtn_  = new QPushButton("Try lightbar colour…", this);
    rumbleBtn_->setToolTip("Rumble the selected controller for half a second");
    colorBtn_->setToolTip("Set the selected controller's lightbar until the next profile change.\n"
                          "Use the Profiles page to change it permanently.");
    buttons->addWidget(rescanBtn);
    buttons->addStretch();
    buttons->addWidget(rumbleBtn_);
    buttons->addWidget(colorBtn_);
    layout->addLayout(buttons);

    connect(rescanBtn, &QPushButton::clicked, this, [this]() {
        ipc_->request("RescanDevices", {}, [this](const QJsonObject&) { refresh(); });
    });
    connect(rumbleBtn_, &QPushButton::clicked, this, [this]() {
        const auto path = selectedPath();
        if (path.isEmpty()) return;
        ipc_->request("SetRumble", {{"device", path}, {"heavy", 200}, {"light", 200},
                                    {"duration_ms", 500}},
                      [this](const QJsonObject& r) { showError(this, r); });
    });
    connect(colorBtn_, &QPushButton::clicked, this, [this]() {
        const auto path = selectedPath();
        if (path.isEmpty()) return;
        const QColor c = QColorDialog::getColor(Qt::blue, this, "Lightbar colour");
        if (!c.isValid()) return;
        ipc_->request("SetLightbarColor", {{"device", path}, {"r", c.red()},
                                           {"g", c.green()}, {"b", c.blue()}},
                      [this](const QJsonObject& r) { showError(this, r); });
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        const bool any = !selectedPath().isEmpty();
        rumbleBtn_->setEnabled(any);
        colorBtn_->setEnabled(any);
    });

    populate({});
}

void ControllersPage::refresh() {
    ipc_->request("ListDevices", {}, [this](const QJsonObject& r) {
        if (r.value("type").toString() == "DeviceList") populate(r.value("devices").toArray());
    });
}

void ControllersPage::setProfiles(const QStringList& profiles) {
    profiles_ = profiles;
    populate(devices_);
}

QString ControllersPage::selectedPath() const {
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return {};
    const auto* item = table_->item(rows.first().row(), ColModel);
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

void ControllersPage::populate(const QJsonArray& devices) {
    // Rebuilding the rows would close an open profile drop-down, so when only
    // battery levels changed, update those in place.
    QString key = profiles_.join('|');
    for (const auto& v : devices) {
        const auto d = v.toObject();
        key += '\n' + d.value("path").toString() + d.value("profile").toString() +
               d.value("output_mode").toString() + d.value("connection").toString();
    }
    const bool sameLayout = key == layoutKey_ && table_->rowCount() == devices.size();
    layoutKey_ = key;
    devices_ = devices;
    emit devicesChanged(devices);
    if (sameLayout) {
        updateBatteries(devices);
        return;
    }

    const QString selected = selectedPath();

    table_->setRowCount(static_cast<int>(devices.size()));
    table_->setVisible(!devices.isEmpty());
    empty_->setVisible(devices.isEmpty());

    for (int row = 0; row < devices.size(); ++row) {
        const auto d = devices[row].toObject();
        const auto path = d.value("path").toString();

        auto* idx = new QTableWidgetItem(QString::number(row + 1));
        idx->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, ColIndex, idx);

        auto* model = new QTableWidgetItem(d.value("model").toString());
        model->setData(Qt::UserRole, path);
        model->setToolTip(QString("%1\n%2\nVirtual MAC %3")
                              .arg(d.value("name").toString(), path, d.value("mac").toString()));
        table_->setItem(row, ColModel, model);

        table_->setItem(row, ColConnection, new QTableWidgetItem(
            d.value("connection").toString() == "bluetooth" ? "Bluetooth" : "USB"));

        auto* bar = new QProgressBar;
        bar->setRange(0, 100);
        table_->setCellWidget(row, ColBattery, bar);

        table_->setItem(row, ColOutput, new QTableWidgetItem(
            d.value("output_mode").toString() == "x360" ? "Xbox 360" : "DualShock 4"));

        auto* combo = new QComboBox;
        combo->addItems(profiles_);
        const auto current = d.value("profile").toString();
        if (combo->findText(current) < 0) combo->addItem(current);
        combo->setCurrentText(current);
        connect(combo, &QComboBox::activated, this, [this, combo, path]() {
            ipc_->request("SetActiveProfile", {{"device", path}, {"name", combo->currentText()}},
                          [this](const QJsonObject& r) { showError(this, r); refresh(); });
        });
        table_->setCellWidget(row, ColProfile, combo);

        if (path == selected) table_->selectRow(row);
    }
    updateBatteries(devices);
    const bool any = !selectedPath().isEmpty();
    rumbleBtn_->setEnabled(any);
    colorBtn_->setEnabled(any);
}

void ControllersPage::updateBatteries(const QJsonArray& devices) {
    for (int row = 0; row < devices.size() && row < table_->rowCount(); ++row) {
        auto* bar = qobject_cast<QProgressBar*>(table_->cellWidget(row, ColBattery));
        if (!bar) continue;
        const auto battery = devices[row].toObject().value("battery").toObject();
        const int pct = battery.value("percent").toInt(-1);
        if (pct >= 0) {
            const auto status = battery.value("status").toString();
            bar->setValue(pct);
            bar->setFormat(status == "charging" || status == "full"
                               ? QString("%1% (%2)").arg(pct).arg(status) : QString("%1%").arg(pct));
        } else {
            bar->setValue(0);
            bar->setFormat("n/a");
        }
    }
}

} // namespace ds4linux::gui
