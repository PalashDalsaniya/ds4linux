// ds4linux GUI — TesterPage / ControllerView implementation

#include "ds4linux/tester_page.h"
#include "ds4linux/ipc_client.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QRadioButton>
#include <QTimer>
#include <QVBoxLayout>

namespace ds4linux::gui {

// ── ControllerView ───────────────────────────────────────────────────────────

ControllerView::ControllerView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(520, 320);
}

void ControllerView::setState(const QJsonObject& state) {
    state_ = state;
    update();
}

void ControllerView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Lay out in a 640x380 design space, scaled to fit
    const double scale = std::min(width() / 640.0, height() / 380.0);
    p.translate((width() - 640 * scale) / 2, (height() - 380 * scale) / 2);
    p.scale(scale, scale);

    const QColor body(58, 60, 66), outline(110, 112, 120), idle(85, 88, 96);
    const QColor active(0, 150, 255), text(220, 220, 225);
    const auto b = state_.value("buttons").toObject();
    auto on = [&](const char* name) { return b.value(name).toBool(); };

    // Body
    QPainterPath shape;
    shape.addRoundedRect(QRectF(60, 70, 520, 230), 90, 90);
    p.setPen(QPen(outline, 2));
    p.setBrush(body);
    p.drawPath(shape);

    // Triggers (L2/R2 fill bars) and bumpers
    auto trigger = [&](QRectF r, int value, const char* label) {
        p.setPen(QPen(outline, 1.5));
        p.setBrush(idle);
        p.drawRoundedRect(r, 6, 6);
        QRectF fill = r.adjusted(2, 2, -2, -2);
        fill.setTop(fill.bottom() - fill.height() * value / 255.0);
        p.setPen(Qt::NoPen);
        p.setBrush(active);
        p.drawRoundedRect(fill, 4, 4);
        p.setPen(text);
        p.drawText(r.adjusted(0, -18, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                   QString("%1 %2").arg(label).arg(value));
    };
    trigger(QRectF(110, 20, 70, 36), state_.value("l2").toInt(), "L2");
    trigger(QRectF(460, 20, 70, 36), state_.value("r2").toInt(), "R2");

    auto pill = [&](QRectF r, bool pressed, const QString& label) {
        p.setPen(QPen(outline, 1.5));
        p.setBrush(pressed ? active : idle);
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        p.setPen(text);
        p.drawText(r, Qt::AlignCenter, label);
    };
    pill(QRectF(105, 58, 80, 16), on("l1"), "L1");
    pill(QRectF(455, 58, 80, 16), on("r1"), "R1");

    // Touchpad with touch points
    const QRectF pad(220, 80, 200, 92);
    p.setPen(QPen(on("touchpad") ? active : outline, 2));
    p.setBrush(QColor(44, 46, 52));
    p.drawRoundedRect(pad, 10, 10);
    const auto touches = state_.value("touches").toArray();
    for (int i = 0; i < touches.size(); ++i) {
        const auto t = touches[i].toObject();
        if (!t.value("active").toBool()) continue;
        const QPointF pt(pad.left() + pad.width() * t.value("x").toInt() / 1919.0,
                         pad.top() + pad.height() * t.value("y").toInt() / 941.0);
        p.setPen(Qt::NoPen);
        p.setBrush(i == 0 ? QColor(255, 170, 0) : QColor(0, 220, 140));
        p.drawEllipse(pt, 7, 7);
    }

    // Share / Options / PS
    pill(QRectF(188, 92, 26, 14), on("share"), "");
    pill(QRectF(426, 92, 26, 14), on("options"), "");
    p.setPen(QPen(outline, 1.5));
    p.setBrush(on("ps") ? active : idle);
    p.drawEllipse(QPointF(320, 250), 14, 14);
    p.setPen(text);
    p.drawText(QRectF(300, 236, 40, 28), Qt::AlignCenter, "PS");

    // D-pad
    const auto dpad = state_.value("dpad").toObject();
    const int dx = dpad.value("x").toInt(), dy = dpad.value("y").toInt();
    auto arm = [&](QRectF r, bool pressed) {
        p.setPen(QPen(outline, 1.5));
        p.setBrush(pressed ? active : idle);
        p.drawRoundedRect(r, 4, 4);
    };
    arm(QRectF(128, 110, 24, 30), dy < 0);
    arm(QRectF(128, 164, 24, 30), dy > 0);
    arm(QRectF(98, 140, 30, 24), dx < 0);
    arm(QRectF(152, 140, 30, 24), dx > 0);

    // Face buttons
    struct Face { QPointF c; const char* name; const char* glyph; QColor color; };
    const Face faces[] = {
        {{500, 115}, "triangle", "△", QColor(64, 226, 160)},
        {{532, 150}, "circle",   "○", QColor(255, 102, 102)},
        {{500, 185}, "cross",    "✕", QColor(124, 178, 232)},
        {{468, 150}, "square",   "□", QColor(255, 105, 248)},
    };
    QFont glyphFont = p.font();
    glyphFont.setPointSizeF(13);
    for (const auto& f : faces) {
        p.setPen(QPen(outline, 1.5));
        p.setBrush(on(f.name) ? f.color : idle);
        p.drawEllipse(f.c, 16, 16);
        p.setFont(glyphFont);
        p.setPen(on(f.name) ? Qt::black : f.color);
        p.drawText(QRectF(f.c.x() - 16, f.c.y() - 16, 32, 32), Qt::AlignCenter, f.glyph);
    }
    p.setFont(QFont());

    // Sticks: ring, position dot, centre cross; ring lights on L3/R3
    auto stickView = [&](QPointF c, int x, int y, bool click, const char* label) {
        const double r = 38;
        p.setPen(QPen(click ? active : outline, click ? 3 : 1.5));
        p.setBrush(QColor(44, 46, 52));
        p.drawEllipse(c, r, r);
        p.setPen(QPen(idle, 1));
        p.drawLine(c + QPointF(-r, 0), c + QPointF(r, 0));
        p.drawLine(c + QPointF(0, -r), c + QPointF(0, r));
        const QPointF dot(c.x() + (x - 128) / 128.0 * r, c.y() + (y - 128) / 128.0 * r);
        p.setPen(Qt::NoPen);
        p.setBrush(active);
        p.drawEllipse(dot, 8, 8);
        p.setPen(text);
        p.drawText(QRectF(c.x() - 60, c.y() + r + 4, 120, 18), Qt::AlignCenter,
                   QString("%1  %2, %3").arg(label).arg(x - 128).arg(y - 128));
    };
    stickView(QPointF(230, 225), state_.value("lx").toInt(128), state_.value("ly").toInt(128),
              on("l3"), "L");
    stickView(QPointF(410, 225), state_.value("rx").toInt(128), state_.value("ry").toInt(128),
              on("r3"), "R");
}

// ── TesterPage ───────────────────────────────────────────────────────────────

TesterPage::TesterPage(IpcClient* ipc, QWidget* parent)
    : QWidget(parent), ipc_(ipc)
{
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel("Controller:", this));
    device_ = new QComboBox(this);
    device_->setMinimumWidth(260);
    top->addWidget(device_);
    top->addSpacing(20);
    auto* inputBtn  = new QRadioButton("Input", this);
    auto* outputBtn = new QRadioButton("Output (after profile)", this);
    inputBtn->setToolTip("What the physical controller sends");
    outputBtn->setToolTip("What games receive, after deadzones and remapping");
    inputBtn->setChecked(true);
    view_ = new QButtonGroup(this);
    view_->addButton(inputBtn, 0);
    view_->addButton(outputBtn, 1);
    top->addWidget(inputBtn);
    top->addWidget(outputBtn);
    top->addStretch();
    layout->addLayout(top);

    pad_ = new ControllerView(this);
    layout->addWidget(pad_, 1);

    motion_ = new QLabel(this);
    motion_->setFont(QFont("monospace"));
    layout->addWidget(motion_);

    hint_ = new QLabel("No controllers connected.", this);
    hint_->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint_);

    timer_ = new QTimer(this);
    timer_->setInterval(33);  // ~30 Hz
    connect(timer_, &QTimer::timeout, this, &TesterPage::poll);
    connect(view_, &QButtonGroup::idClicked, this, [this]() { poll(); });
    // A request lost with the connection never answers; don't wait for it
    connect(ipc_, &IpcClient::disconnected, this, [this]() { waiting_ = false; });

    setDevices({});
}

void TesterPage::setDevices(const QJsonArray& devices) {
    const QString selected = device_->currentData().toString();
    QSignalBlocker block(device_);
    device_->clear();
    for (int i = 0; i < devices.size(); ++i) {
        const auto d = devices[i].toObject();
        device_->addItem(QString("%1. %2 (%3)").arg(i + 1)
                             .arg(d.value("model").toString(),
                                  d.value("connection").toString() == "bluetooth" ? "Bluetooth" : "USB"),
                         d.value("path").toString());
    }
    const int idx = device_->findData(selected);
    if (idx >= 0) device_->setCurrentIndex(idx);
    const bool any = !devices.isEmpty();
    hint_->setVisible(!any);
    pad_->setEnabled(any);
    if (!any) {
        pad_->setState({});
        motion_->clear();
    }
}

void TesterPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
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
    ipc_->request("GetInputState", {{"device", path}}, [this](const QJsonObject& r) {
        waiting_ = false;
        if (r.value("type").toString() != "InputState") return;
        const auto in = r.value(view_->checkedId() == 1 ? "output" : "input").toObject();
        pad_->setState(in);
        const auto g = in.value("gyro").toArray(), a = in.value("accel").toArray();
        motion_->setText(QString("gyro  %1 %2 %3     accel  %4 %5 %6")
                             .arg(g[0].toInt(), 6).arg(g[1].toInt(), 6).arg(g[2].toInt(), 6)
                             .arg(a[0].toInt(), 6).arg(a[1].toInt(), 6).arg(a[2].toInt(), 6));
    });
}

} // namespace ds4linux::gui
