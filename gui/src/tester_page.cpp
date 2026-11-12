// ds4linux GUI — TesterPage implementation

#include "ds4linux/tester_page.h"
#include "ds4linux/controller_test.h"
#include "ds4linux/gui_util.h"
#include "ds4linux/ipc_client.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>

namespace ds4linux::gui {

TesterPage::TesterPage(IpcClient* ipc, QWidget* parent)
    : QWidget(parent), ipc_(ipc)
{
    auto* layout = new QVBoxLayout(this);

    // ── Header: controller chooser, its status, view mode ────────────────────
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel("Controller:", this));
    device_ = new QComboBox(this);
    device_->setMinimumWidth(240);
    top->addWidget(device_);
    info_ = new QLabel(this);
    info_->setTextFormat(Qt::RichText);
    top->addSpacing(12);
    top->addWidget(info_, 1);
    auto* inputBtn  = new QRadioButton("Raw input", this);
    auto* outputBtn = new QRadioButton("What games see", this);
    inputBtn->setToolTip("The controller drawing shows what the physical controller sends");
    outputBtn->setToolTip("The controller drawing shows what games receive, after the "
                          "profile's deadzones and button remapping");
    outputBtn->setChecked(true);
    view_ = new QButtonGroup(this);
    view_->addButton(inputBtn, 0);
    view_->addButton(outputBtn, 1);
    top->addWidget(inputBtn);
    top->addWidget(outputBtn);
    layout->addLayout(top);

    // ── Body: drawing + sticks / triggers on the left, touchpad + motion right
    body_ = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);

    auto* leftCol = new QVBoxLayout;
    pad_ = new ControllerView(body_);
    leftCol->addWidget(pad_, 1);
    auto* sticks = new QHBoxLayout;
    left_     = new StickPanel("Left stick", "lx", "ly", "l3", body_);
    right_    = new StickPanel("Right stick", "rx", "ry", "r3", body_);
    triggers_ = new TriggerPanel(body_);
    sticks->addWidget(left_, 1);
    sticks->addWidget(right_, 1);
    sticks->addWidget(triggers_, 1);
    leftCol->addLayout(sticks);
    bodyLayout->addLayout(leftCol, 3);

    auto* rightCol = new QVBoxLayout;
    touch_  = new TouchpadPanel(body_);
    motion_ = new MotionPanel(body_);
    rightCol->addWidget(touch_, 2);
    rightCol->addWidget(motion_, 3);
    bodyLayout->addLayout(rightCol, 2);

    layout->addWidget(body_, 1);

    auto* legend = new QLabel(
        "Sticks: <span style='color:#8c909a'>○ raw input</span> &nbsp; "
        "<span style='color:#2696ff'>● what games see</span> &nbsp; "
        "<span style='color:#ffbe3c'>- - deadzones</span>", this);
    legend->setTextFormat(Qt::RichText);
    layout->addWidget(legend);

    hint_ = new QLabel("No controllers connected.", this);
    hint_->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint_);

    timer_ = new QTimer(this);
    timer_->setInterval(16);  // ~60 Hz
    connect(timer_, &QTimer::timeout, this, &TesterPage::poll);
    connect(view_, &QButtonGroup::idClicked, this, [this]() { poll(); });
    connect(device_, &QComboBox::currentIndexChanged, this, &TesterPage::selectionChanged);
    // A request lost with the connection never answers; don't wait for it
    connect(ipc_, &IpcClient::disconnected, this, [this]() { waiting_ = false; });

    setDevices({});
}

QJsonObject TesterPage::currentDevice() const {
    const QString path = device_->currentData().toString();
    for (const auto& d : devices_) {
        if (d.toObject().value("path").toString() == path) return d.toObject();
    }
    return {};
}

void TesterPage::setDevices(const QJsonArray& devices) {
    const QString oldProfile = currentDevice().value("profile").toString();
    devices_ = devices;

    // Called on every controller refresh; only rebuild the chooser (which
    // would close it if open) when its entries would change.
    QStringList labels, paths;
    for (int i = 0; i < devices.size(); ++i) {
        const auto d = devices[i].toObject();
        labels << QString("%1. %2 (%3)").arg(i + 1).arg(d.value("model").toString(),
                                                        connectionLabel(d));
        paths << d.value("path").toString();
    }
    QStringList current, currentPaths;
    for (int i = 0; i < device_->count(); ++i) {
        current << device_->itemText(i);
        currentPaths << device_->itemData(i).toString();
    }
    if (labels != current || paths != currentPaths) {
        const QString selected = device_->currentData().toString();
        {
            QSignalBlocker block(device_);
            device_->clear();
            for (int i = 0; i < labels.size(); ++i) device_->addItem(labels[i], paths[i]);
            const int idx = device_->findData(selected);
            if (idx >= 0) device_->setCurrentIndex(idx);
        }
        selectionChanged();
    } else {
        updateInfo();
        // Pick up edits to the profile (deadzones, lightbar) while visible
        if (isVisible() || currentDevice().value("profile").toString() != oldProfile) loadProfile();
    }

    const bool any = !devices.isEmpty();
    hint_->setVisible(!any);
    body_->setEnabled(any);
    if (!any) clearView();
}

void TesterPage::selectionChanged() {
    clearView();
    updateInfo();
    loadProfile();
    poll();
}

void TesterPage::updateInfo() {
    const auto d = currentDevice();
    if (d.isEmpty()) {
        info_->clear();
        return;
    }
    pad_->setModel(d.value("model").toString());
    const auto bat = d.value("battery").toObject();
    const int pct = bat.value("percent").toInt(-1);
    const QString battery = pct < 0 ? QString("battery n/a")
                                    : QString("🔋 %1% %2").arg(pct).arg(bat.value("status").toString());
    info_->setText(QString("<span style='color:#8c909a'>%1 &nbsp;·&nbsp; %2 &nbsp;·&nbsp; "
                           "profile <b style='color:#dee0e6'>%3</b> &nbsp;·&nbsp; emulating %4</span>")
                       .arg(connectionLabel(d), battery,
                            d.value("profile").toString().toHtmlEscaped(),
                            d.value("output_mode").toString() == "x360" ? "Xbox 360" : "DualShock 4"));
}

void TesterPage::loadProfile() {
    const QString name = currentDevice().value("profile").toString();
    if (name.isEmpty() || !ipc_->isConnected()) return;
    ipc_->request("LoadProfile", {{"name", name}}, [this, name](const QJsonObject& r) {
        if (r.value("type").toString() != "Profile") return;
        if (currentDevice().value("profile").toString() != name) return;  // stale
        const Profile p = profileFromJson(r.value("profile").toObject());
        left_->setZone(p.left_stick_dz);
        right_->setZone(p.right_stick_dz);
        triggers_->setZones(p.left_trigger_dz, p.right_trigger_dz);
        pad_->setLightbar(QColor(p.lightbar_color.r, p.lightbar_color.g, p.lightbar_color.b));
    });
}

void TesterPage::clearView() {
    pad_->setState({});
    left_->clear();
    right_->clear();
    triggers_->clear();
    touch_->clear();
    motion_->clear();
}

void TesterPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    loadProfile();
    timer_->start();
}

void TesterPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    timer_->stop();
}

void TesterPage::poll() {
    const QString path = device_->currentData().toString();
    if (path.isEmpty() || waiting_ || !ipc_->isConnected()) return;
    waiting_ = true;
    ipc_->request("GetInputState", {{"device", path}}, [this, path](const QJsonObject& r) {
        waiting_ = false;
        if (r.value("type").toString() != "InputState") return;
        if (device_->currentData().toString() != path) return;  // switched meanwhile
        const auto in  = r.value("input").toObject();
        const auto out = r.value("output").toObject();
        pad_->setState(view_->checkedId() == 1 ? out : in);
        left_->setFrame(in, out);
        right_->setFrame(in, out);
        triggers_->setFrame(in, out);
        touch_->setFrame(in);
        motion_->setFrame(in);
    });
}

} // namespace ds4linux::gui
