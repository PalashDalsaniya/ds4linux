// ds4linux GUI — controller test widgets

#include "ds4linux/controller_test.h"
#include "ds4linux/constants.h"

#include <QJsonArray>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>
#include <vector>

namespace ds4linux::gui {

namespace {

// ── Palette ──────────────────────────────────────────────────────────────────

const QColor kPanel(30, 32, 38);
const QColor kPanelEdge(52, 55, 63);
const QColor kBodyTop(62, 65, 74), kBodyBottom(40, 42, 49);
const QColor kOutline(96, 100, 112);
const QColor kIdle(74, 78, 89), kIdleDark(52, 55, 63);
const QColor kRecess(24, 25, 30);
const QColor kAccent(38, 150, 255);
const QColor kText(222, 224, 230), kDim(140, 144, 154);
const QColor kAxis[3] = {QColor(255, 99, 99), QColor(92, 214, 120), QColor(80, 160, 255)};

// PlayStation face button colours
const QColor kTriangle(64, 214, 160), kCircle(255, 96, 96), kCross(120, 170, 255),
             kSquare(245, 110, 220);

bool pressed(const QJsonObject& state, const char* name) {
    return state.value("buttons").toObject().value(name).toBool();
}

/// Stick byte (0-255, centre 128) → -1..1
double axis(const QJsonObject& state, const char* key) {
    return std::clamp((state.value(key).toInt(128) - 128) / 127.0, -1.0, 1.0);
}

/// Soft glow behind a pressed control.
void glow(QPainter& p, QPointF c, double r, QColor color) {
    QRadialGradient g(c, r);
    color.setAlpha(150);
    g.setColorAt(0.0, color);
    color.setAlpha(0);
    g.setColorAt(1.0, color);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, r, r);
}

/// Rounded panel background with a title.  Returns the content area.
QRectF panelFrame(QPainter& p, const QRect& bounds, const QString& title) {
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(bounds).adjusted(1, 1, -1, -1);
    p.setPen(QPen(kPanelEdge, 1));
    p.setBrush(kPanel);
    p.drawRoundedRect(r, 10, 10);
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(kText);
    p.drawText(r.adjusted(12, 8, -12, 0), Qt::AlignLeft | Qt::AlignTop, title);
    p.setFont(QFont());
    return r.adjusted(10, 30, -10, -10);
}

void smallText(QPainter& p, const QRectF& r, int flags, const QString& s, QColor c = kDim) {
    QFont f = p.font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1));
    p.setFont(f);
    p.setPen(c);
    p.drawText(r, flags, s);
    p.setFont(QFont());
}

template <typename T>
void push(std::deque<T>& d, const T& v, std::size_t max) {
    d.push_back(v);
    while (d.size() > max) d.pop_front();
}

// ── Controller body shape ────────────────────────────────────────────────────
// Designed in a 1000 x 600 space; the left half is mirrored for the right.

struct Seg { QPointF c1, c2, p; };

QPointF mirror(QPointF pt) { return {1000 - pt.x(), pt.y()}; }

QPainterPath mirrored(QPointF start, const std::vector<Seg>& left) {
    QPainterPath path(start);
    std::vector<QPointF> ends{start};
    for (const auto& s : left) {
        path.cubicTo(s.c1, s.c2, s.p);
        ends.push_back(s.p);
    }
    for (int i = static_cast<int>(left.size()) - 1; i >= 0; --i) {
        path.cubicTo(mirror(left[i].c2), mirror(left[i].c1), mirror(ends[i]));
    }
    path.closeSubpath();
    return path;
}

QPainterPath bodyShape() {
    return mirrored({500, 96}, {
        {{440, 96}, {380, 94}, {330, 94}},      // top edge
        {{262, 92}, {208, 98}, {172, 128}},     // shoulder
        {{132, 162}, {106, 250}, {96, 330}},    // outer side
        {{84, 430}, {96, 522}, {150, 550}},     // down the grip
        {{196, 572}, {242, 548}, {264, 506}},   // grip end
        {{292, 456}, {322, 404}, {372, 394}},   // inner side of the grip
        {{420, 386}, {460, 386}, {500, 386}},   // belly
    });
}

/// A D-pad arm pointing at the centre, rotated by `deg` (0 = up).
QPainterPath dpadArm(QPointF c, double deg) {
    QPainterPath arm;
    arm.moveTo(-19, -62);
    arm.lineTo(19, -62);
    arm.lineTo(19, -24);
    arm.lineTo(0, -6);
    arm.lineTo(-19, -24);
    arm.closeSubpath();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(deg);
    return t.map(arm);
}

void faceSymbol(QPainter& p, QPointF c, const char* name, QColor color) {
    p.setPen(QPen(color, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const double s = 10;
    if (std::string_view(name) == "triangle") {
        QPolygonF tri{{c.x(), c.y() - s - 1}, {c.x() + s + 1, c.y() + s * 0.75},
                      {c.x() - s - 1, c.y() + s * 0.75}};
        p.drawPolygon(tri);
    } else if (std::string_view(name) == "circle") {
        p.drawEllipse(c, s, s);
    } else if (std::string_view(name) == "cross") {
        p.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
        p.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
    } else {
        p.drawRect(QRectF(c.x() - s * 0.9, c.y() - s * 0.9, s * 1.8, s * 1.8));
    }
}

} // namespace

// ── TestProfile ──────────────────────────────────────────────────────────────

TestProfile TestProfile::fromJson(const QJsonObject& profile) {
    auto zone = [&](const char* key) {
        const auto z = profile.value(key).toObject();
        return Zone{z.value("type").toInt(0), z.value("inner").toDouble(0),
                    z.value("outer").toDouble(1)};
    };
    TestProfile t;
    t.left_stick  = zone("left_stick_dz");
    t.right_stick = zone("right_stick_dz");
    t.l2          = zone("left_trigger_dz");
    t.r2          = zone("right_trigger_dz");
    const auto c  = profile.value("lightbar_color").toObject();
    t.lightbar    = QColor(c.value("r").toInt(0), c.value("g").toInt(0), c.value("b").toInt(255));
    return t;
}

// ── ControllerView ───────────────────────────────────────────────────────────

ControllerView::ControllerView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 252);
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Expanding);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void ControllerView::setState(const QJsonObject& state) {
    state_ = state;
    update();
}

void ControllerView::setModel(const QString& model) {
    const bool ds = !model.startsWith("DualShock");
    if (ds != dualsense_) {
        dualsense_ = ds;
        update();
    }
}

void ControllerView::setLightbar(const QColor& color) {
    if (color != lightbar_) {
        lightbar_ = color;
        update();
    }
}

void ControllerView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double scale = std::min(width() / 1000.0, height() / 600.0);
    p.translate((width() - 1000 * scale) / 2, (height() - 600 * scale) / 2);
    p.scale(scale, scale);

    const bool live = isEnabled() && !state_.isEmpty();
    auto on = [&](const char* name) { return live && pressed(state_, name); };
    QFont label;
    label.setPixelSize(15);
    label.setBold(true);

    // ── Triggers (furthest back) and bumpers ─────────────────────────────────
    auto trigger = [&](bool left) {
        // The bumper covers the trigger's lower end; fill the visible part
        const QRectF r = left ? QRectF(196, 0, 128, 92) : QRectF(676, 0, 128, 92);
        const QRectF visible = r.adjusted(0, 0, 0, -20);
        const int v = live ? state_.value(left ? "l2" : "r2").toInt() : 0;
        QPainterPath shape;
        shape.addRoundedRect(r, 22, 22);
        p.setPen(QPen(v > 0 ? kAccent.lighter(130) : kOutline, 2));
        p.setBrush(kIdleDark);
        p.drawPath(shape);
        if (v > 0) {
            p.save();
            p.setClipPath(shape);
            QRectF fill = r;
            fill.setTop(visible.bottom() - visible.height() * v / 255.0);
            QLinearGradient g(fill.topLeft(), fill.bottomLeft());
            g.setColorAt(0, kAccent.lighter(130));
            g.setColorAt(1, kAccent);
            p.fillRect(fill, g);
            p.restore();
        }
        p.setFont(label);
        p.setPen(kText);
        p.drawText(visible, Qt::AlignCenter, QString("%1  %2").arg(left ? "L2" : "R2").arg(v));
    };
    trigger(true);
    trigger(false);

    auto bumper = [&](bool left) {
        const QRectF r = left ? QRectF(182, 72, 150, 34) : QRectF(668, 72, 150, 34);
        const bool down = on(left ? "l1" : "r1");
        p.setPen(QPen(down ? kAccent.lighter(140) : kOutline, 2));
        p.setBrush(down ? kAccent : kIdle);
        p.drawRoundedRect(r, 15, 15);
        p.setFont(label);
        p.setPen(kText);
        p.drawText(r.adjusted(0, -8, 0, 0), Qt::AlignCenter, left ? "L1" : "R1");
    };
    bumper(true);
    bumper(false);

    // ── Body ─────────────────────────────────────────────────────────────────
    const QPainterPath body = bodyShape();
    QLinearGradient bodyFill(0, 90, 0, 560);
    bodyFill.setColorAt(0, kBodyTop);
    bodyFill.setColorAt(1, kBodyBottom);
    p.setPen(QPen(kOutline, 2.5));
    p.setBrush(bodyFill);
    p.drawPath(body);

    // ── Touchpad and lightbar ────────────────────────────────────────────────
    const QRectF pad(362, 104, 276, 150);
    const bool padClick = on("touchpad");
    if (padClick) glow(p, pad.center(), 170, kAccent);
    QLinearGradient padFill(pad.topLeft(), pad.bottomLeft());
    padFill.setColorAt(0, QColor(50, 53, 61));
    padFill.setColorAt(1, QColor(40, 42, 49));
    p.setPen(QPen(padClick ? kAccent : kOutline, padClick ? 3 : 2));
    p.setBrush(padFill);
    p.drawRoundedRect(pad, 20, 20);

    QColor bar = lightbar_;
    if (!live) bar = bar.darker(250);
    p.setPen(Qt::NoPen);
    if (dualsense_) {
        // Light strips along both sides of the touchpad
        for (double x : {348.0, 646.0}) {
            glow(p, QPointF(x + 3, 180), 34, bar);
            p.setBrush(bar);
            p.drawRoundedRect(QRectF(x, 118, 6, 122), 3, 3);
        }
    } else {
        glow(p, QPointF(500, 92), 60, bar);
        p.setBrush(bar);
        p.drawRoundedRect(QRectF(392, 88, 216, 8), 4, 4);
    }

    // Finger positions on the touchpad
    const auto touches = state_.value("touches").toArray();
    for (int i = 0; live && i < touches.size(); ++i) {
        const auto t = touches[i].toObject();
        if (!t.value("active").toBool()) continue;
        const QPointF pt(pad.left() + 10 + (pad.width() - 20) * t.value("x").toInt() / double(kDS4TouchpadMaxX),
                         pad.top() + 10 + (pad.height() - 20) * t.value("y").toInt() / double(kDS4TouchpadMaxY));
        const QColor c = i == 0 ? QColor(255, 176, 50) : QColor(60, 220, 150);
        glow(p, pt, 26, c);
        p.setBrush(c);
        p.drawEllipse(pt, 9, 9);
    }

    // ── Share / Create and Options ───────────────────────────────────────────
    auto smallButton = [&](QPointF c, bool down, const QString& text) {
        if (down) glow(p, c, 34, kAccent);
        p.setPen(QPen(down ? kAccent.lighter(140) : kOutline, 1.5));
        p.setBrush(down ? kAccent : kIdle);
        p.drawRoundedRect(QRectF(c.x() - 9, c.y() - 17, 18, 34), 9, 9);
        QFont f;
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(kDim);
        p.drawText(QRectF(c.x() - 50, c.y() - 42, 100, 20), Qt::AlignCenter, text);
    };
    smallButton({322, 140}, on("share"), dualsense_ ? "CREATE" : "SHARE");
    smallButton({678, 140}, on("options"), "OPTIONS");

    // ── D-pad ────────────────────────────────────────────────────────────────
    const QPointF dc(232, 222);
    p.setPen(Qt::NoPen);
    p.setBrush(kRecess);
    p.drawEllipse(dc, 78, 78);
    const struct { const char* name; double deg; } arms[] = {
        {"dpad_up", 0}, {"dpad_right", 90}, {"dpad_down", 180}, {"dpad_left", 270}};
    for (const auto& a : arms) {
        const bool down = on(a.name);
        const QPainterPath arm = dpadArm(dc, a.deg);
        if (down) glow(p, arm.boundingRect().center(), 46, kAccent);
        p.setPen(QPen(down ? kAccent.lighter(140) : kOutline, 1.5));
        p.setBrush(down ? kAccent : kIdle);
        p.drawPath(arm);
    }

    // ── Face buttons ─────────────────────────────────────────────────────────
    const QPointF fc(768, 222);
    p.setPen(Qt::NoPen);
    p.setBrush(kRecess);
    p.drawEllipse(fc, 92, 92);
    const struct { const char* name; QPointF off; QColor color; } faces[] = {
        {"triangle", {0, -52}, kTriangle}, {"circle", {52, 0}, kCircle},
        {"cross",    {0, 52},  kCross},    {"square", {-52, 0}, kSquare}};
    for (const auto& f : faces) {
        const QPointF c = fc + f.off;
        const bool down = on(f.name);
        if (down) glow(p, c, 48, f.color);
        p.setPen(QPen(down ? f.color.lighter(130) : kOutline, 1.5));
        p.setBrush(down ? f.color : kIdle);
        p.drawEllipse(c, 27, 27);
        faceSymbol(p, c, f.name, down ? QColor(20, 20, 24) : f.color);
    }

    // ── Speaker grille, PS button ────────────────────────────────────────────
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(28, 29, 34));
    for (int row = 0; row < 3; ++row) {
        for (int col = -4 + row % 2; col <= 4; col += 2) {
            p.drawEllipse(QPointF(500 + col * 9, 276 + row * 8), 2.2, 2.2);
        }
    }
    const QPointF ps(500, 336);
    const bool psDown = on("ps");
    if (psDown) glow(p, ps, 44, kAccent);
    p.setPen(QPen(psDown ? kAccent.lighter(140) : kOutline, 1.5));
    p.setBrush(psDown ? kAccent : kIdle);
    p.drawEllipse(ps, 20, 20);
    QFont psFont;
    psFont.setPixelSize(14);
    psFont.setBold(true);
    p.setFont(psFont);
    p.setPen(psDown ? QColor(20, 20, 24) : kText);
    p.drawText(QRectF(ps.x() - 20, ps.y() - 20, 40, 40), Qt::AlignCenter, "PS");

    // ── Sticks ───────────────────────────────────────────────────────────────
    auto stick = [&](QPointF c, const char* xk, const char* yk, const char* click) {
        const double x = live ? axis(state_, xk) : 0, y = live ? axis(state_, yk) : 0;
        const bool down = on(click);
        // Well
        QRadialGradient well(c, 64);
        well.setColorAt(0.0, QColor(18, 19, 23));
        well.setColorAt(1.0, QColor(34, 36, 42));
        p.setPen(QPen(kOutline, 2));
        p.setBrush(well);
        p.drawEllipse(c, 62, 62);
        // Cap, displaced by the deflection
        const QPointF cap = c + QPointF(x, y) * 22;
        if (down) glow(p, cap, 70, kAccent);
        QRadialGradient capFill(cap - QPointF(10, 12), 52);
        capFill.setColorAt(0.0, down ? kAccent.lighter(130) : QColor(96, 100, 112));
        capFill.setColorAt(1.0, down ? kAccent.darker(130) : QColor(58, 61, 70));
        p.setPen(QPen(down ? kAccent.lighter(150) : QColor(110, 114, 126), 2));
        p.setBrush(capFill);
        p.drawEllipse(cap, 42, 42);
        // Concave top and grip ring
        p.setPen(QPen(QColor(0, 0, 0, 70), 4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(cap, 30, 30);
        // Direction marker
        if (std::hypot(x, y) > 0.05) {
            p.setPen(Qt::NoPen);
            p.setBrush(kAccent.lighter(down ? 170 : 120));
            p.drawEllipse(cap + QPointF(x, y) * 26, 5, 5);
        }
    };
    stick({366, 342}, "lx", "ly", "l3");
    stick({634, 342}, "rx", "ry", "r3");
}

// ── StickPanel ───────────────────────────────────────────────────────────────

StickPanel::StickPanel(const QString& title, const char* xKey, const char* yKey,
                       const char* clickKey, QWidget* parent)
    : QWidget(parent), title_(title), xKey_(xKey), yKey_(yKey), clickKey_(clickKey) {}

void StickPanel::setFrame(const QJsonObject& in, const QJsonObject& out) {
    in_    = {axis(in, xKey_), axis(in, yKey_)};
    out_   = {axis(out, xKey_), axis(out, yKey_)};
    click_ = pressed(in, clickKey_);
    valid_ = true;
    push(trail_, out_, 40);
    update();
}

void StickPanel::setZone(const TestProfile::Zone& zone) {
    zone_ = zone;
    update();
}

void StickPanel::clear() {
    valid_ = false;
    trail_.clear();
    update();
}

void StickPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRectF area = panelFrame(p, rect(), title_);
    const QRectF textArea(area.left(), area.bottom() - 34, area.width(), 34);
    const double r = std::min(area.width(), area.height() - 40) / 2 - 2;
    const QPointF c(area.center().x(), area.top() + r + 2);

    // Field
    p.setPen(QPen(click_ && valid_ ? kAccent : kOutline, click_ && valid_ ? 2.5 : 1.5));
    p.setBrush(kRecess);
    p.drawEllipse(c, r, r);
    p.setPen(QPen(QColor(60, 63, 72), 1));
    p.drawLine(c - QPointF(r, 0), c + QPointF(r, 0));
    p.drawLine(c - QPointF(0, r), c + QPointF(0, r));

    // Deadzone rings (dashed): inner, and outer when below 100 %
    if (zone_.type != 0) {
        QPen dz(QColor(255, 190, 60, 170), 1.2, Qt::DashLine);
        p.setPen(dz);
        p.setBrush(QColor(255, 190, 60, 28));
        auto ring = [&](double f, bool fill) {
            if (!fill) p.setBrush(Qt::NoBrush);
            if (zone_.type == 2) p.drawRect(QRectF(c.x() - f * r, c.y() - f * r, 2 * f * r, 2 * f * r));
            else p.drawEllipse(c, f * r, f * r);
        };
        if (zone_.inner > 0) ring(zone_.inner, true);
        if (zone_.outer < 1) ring(zone_.outer, false);
    }

    if (valid_) {
        // Output trail, fading
        p.setBrush(Qt::NoBrush);
        for (std::size_t i = 1; i < trail_.size(); ++i) {
            QColor tc = kAccent;
            tc.setAlpha(static_cast<int>(200.0 * i / trail_.size()));
            p.setPen(QPen(tc, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c + trail_[i - 1] * r, c + trail_[i] * r);
        }
        // Raw input: hollow ring; output: filled dot
        p.setPen(QPen(kDim, 2));
        p.drawEllipse(c + in_ * r, 7, 7);
        glow(p, c + out_ * r, 16, kAccent);
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent.lighter(120));
        p.drawEllipse(c + out_ * r, 5.5, 5.5);

        smallText(p, textArea, Qt::AlignLeft | Qt::AlignTop,
                  QString("in   %1, %2").arg(in_.x(), 5, 'f', 2).arg(in_.y(), 5, 'f', 2));
        smallText(p, textArea, Qt::AlignLeft | Qt::AlignBottom,
                  QString("out  %1, %2").arg(out_.x(), 5, 'f', 2).arg(out_.y(), 5, 'f', 2), kText);
        smallText(p, textArea, Qt::AlignRight | Qt::AlignBottom,
                  QString("%1%").arg(std::min(100.0, std::hypot(out_.x(), out_.y()) * 100), 0, 'f', 0), kText);
    }
}

// ── TriggerPanel ─────────────────────────────────────────────────────────────

TriggerPanel::TriggerPanel(QWidget* parent) : QWidget(parent) {}

void TriggerPanel::setFrame(const QJsonObject& in, const QJsonObject& out) {
    in_     = {in.value("l2").toInt(), in.value("r2").toInt()};
    out_    = {out.value("l2").toInt(), out.value("r2").toInt()};
    button_ = {pressed(out, "l2"), pressed(out, "r2")};
    update();
}

void TriggerPanel::setZones(const TestProfile::Zone& l2, const TestProfile::Zone& r2) {
    zone_ = {l2, r2};
    update();
}

void TriggerPanel::clear() {
    in_ = out_ = {};
    button_ = {};
    update();
}

void TriggerPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRectF area = panelFrame(p, rect(), "Triggers");
    const double barW = std::min(38.0, area.width() / 4);
    const double top = area.top() + 4, bottom = area.bottom() - 40;

    for (int i = 0; i < 2; ++i) {
        const double cx = area.left() + area.width() * (i == 0 ? 0.3 : 0.7);
        const QRectF bar(cx - barW / 2, top, barW, bottom - top);
        p.setPen(QPen(button_[i] ? kAccent : kOutline, button_[i] ? 2 : 1.2));
        p.setBrush(kRecess);
        p.drawRoundedRect(bar, 8, 8);

        auto level = [&](int v) {
            QRectF f = bar.adjusted(3, 3, -3, -3);
            f.setTop(f.bottom() - f.height() * v / 255.0);
            return f;
        };
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(140, 144, 154, 70));      // raw pull
        p.drawRoundedRect(level(in_[i]), 5, 5);
        QLinearGradient g(bar.topLeft(), bar.bottomLeft());
        g.setColorAt(0, kAccent.lighter(130));
        g.setColorAt(1, kAccent);
        p.setBrush(g);                               // what games get
        p.drawRoundedRect(level(out_[i]), 5, 5);

        // Deadzone marks
        if (zone_[i].type != 0) {
            p.setPen(QPen(QColor(255, 190, 60, 200), 1.5, Qt::DashLine));
            const QRectF inner = bar.adjusted(3, 3, -3, -3);
            for (double f : {zone_[i].inner, zone_[i].outer}) {
                if (f <= 0 || f >= 1) continue;
                const double y = inner.bottom() - inner.height() * f;
                p.drawLine(QPointF(bar.left() - 4, y), QPointF(bar.right() + 4, y));
            }
        }

        QFont f;
        f.setBold(true);
        p.setFont(f);
        p.setPen(kText);
        p.drawText(QRectF(cx - 40, bottom + 2, 80, 18), Qt::AlignCenter,
                   QString("%1 %2").arg(i == 0 ? "L2" : "R2").arg(out_[i]));
        smallText(p, QRectF(cx - 40, bottom + 20, 80, 16), Qt::AlignCenter,
                  QString("raw %1").arg(in_[i]));
    }
}

// ── TouchpadPanel ────────────────────────────────────────────────────────────

TouchpadPanel::TouchpadPanel(QWidget* parent) : QWidget(parent) {}

void TouchpadPanel::setFrame(const QJsonObject& state) {
    click_ = pressed(state, "touchpad");
    const auto touches = state.value("touches").toArray();
    for (int i = 0; i < 2; ++i) {
        auto& f = fingers_[i];
        const auto t = i < touches.size() ? touches[i].toObject() : QJsonObject{};
        const bool active = t.value("active").toBool();
        if (!active) {
            f.active = false;
            if (!f.trail.empty()) f.trail.pop_front();  // let the trail fade out
            continue;
        }
        const int id = t.value("id").toInt();
        if (!f.active || id != f.id) f.trail.clear();
        f.active = true;
        f.id     = id;
        f.pos    = {t.value("x").toInt() / double(kDS4TouchpadMaxX),
                    t.value("y").toInt() / double(kDS4TouchpadMaxY)};
        push(f.trail, f.pos, 45);
    }
    update();
}

void TouchpadPanel::clear() {
    fingers_ = {};
    click_ = false;
    update();
}

void TouchpadPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRectF area = panelFrame(p, rect(), "Touchpad");
    // Keep the touchpad's aspect ratio
    const double aspect = double(kDS4TouchpadMaxX) / kDS4TouchpadMaxY;
    const double avail = area.height() - 22;
    double w = area.width(), h = w / aspect;
    if (h > avail) { h = avail; w = h * aspect; }
    const QRectF pad(area.center().x() - w / 2, area.top(), w, h);

    p.setPen(QPen(click_ ? kAccent : kOutline, click_ ? 2.5 : 1.5));
    p.setBrush(kRecess);
    p.drawRoundedRect(pad, 12, 12);
    p.setPen(QPen(QColor(60, 63, 72), 1, Qt::DotLine));
    p.drawLine(QPointF(pad.center().x(), pad.top() + 6), QPointF(pad.center().x(), pad.bottom() - 6));

    const QColor colors[2] = {QColor(255, 176, 50), QColor(60, 220, 150)};
    QStringList info;
    for (int i = 0; i < 2; ++i) {
        const auto& f = fingers_[i];
        auto at = [&](QPointF n) {
            return QPointF(pad.left() + n.x() * pad.width(), pad.top() + n.y() * pad.height());
        };
        for (std::size_t k = 1; k < f.trail.size(); ++k) {
            QColor tc = colors[i];
            tc.setAlpha(static_cast<int>(180.0 * k / f.trail.size()));
            p.setPen(QPen(tc, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(at(f.trail[k - 1]), at(f.trail[k]));
        }
        if (!f.active) continue;
        const QPointF pt = at(f.pos);
        glow(p, pt, 22, colors[i]);
        p.setPen(Qt::NoPen);
        p.setBrush(colors[i]);
        p.drawEllipse(pt, 10, 10);
        QFont fnt;
        fnt.setPixelSize(11);
        fnt.setBold(true);
        p.setFont(fnt);
        p.setPen(QColor(20, 20, 24));
        p.drawText(QRectF(pt.x() - 10, pt.y() - 10, 20, 20), Qt::AlignCenter, QString::number(i + 1));
        info << QString("%1: %2, %3").arg(i + 1)
                    .arg(std::lround(f.pos.x() * kDS4TouchpadMaxX))
                    .arg(std::lround(f.pos.y() * kDS4TouchpadMaxY));
    }
    if (click_) info << "click";
    smallText(p, QRectF(area.left(), pad.bottom() + 4, area.width(), 18), Qt::AlignCenter,
              info.isEmpty() ? QString("touch the touchpad") : info.join("    "),
              info.isEmpty() ? kDim : kText);
}

// ── MotionPanel ──────────────────────────────────────────────────────────────

MotionPanel::MotionPanel(QWidget* parent) : QWidget(parent) {}

void MotionPanel::setFrame(const QJsonObject& state) {
    const auto g = state.value("gyro").toArray(), a = state.value("accel").toArray();
    if (g.size() < 3 || a.size() < 3) return;
    Vec3 gv{}, av{};
    for (int i = 0; i < 3; ++i) {
        gv[i] = g[i].toDouble() / kDS4GyroPerDegS;
        av[i] = a[i].toDouble() / kDS4AccelPerG;
    }
    push(gyro_, gv, 150);
    push(accel_, av, 150);
    update();
}

void MotionPanel::clear() {
    gyro_.clear();
    accel_.clear();
    update();
}

void MotionPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRectF area = panelFrame(p, rect(), "Motion");

    // ── Tilt indicator (pitch / roll from gravity) ───────────────────────────
    const double r = std::min(area.height() / 2 - 12, area.width() * 0.2);
    const QPointF c(area.left() + r + 2, area.top() + r + 4);
    double roll = 0, pitch = 0;
    if (!accel_.empty()) {
        const auto& a = accel_.back();
        roll  = std::atan2(a[0], a[1]);
        pitch = std::atan2(a[2], std::hypot(a[0], a[1]));
    }
    QPainterPath disc;
    disc.addEllipse(c, r, r);
    p.save();
    p.setClipPath(disc);
    p.fillRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), QColor(36, 64, 104));  // sky
    p.translate(c);
    p.rotate(-roll * 180 / std::numbers::pi);
    const double offset = std::clamp(pitch / (std::numbers::pi / 2), -1.0, 1.0) * r;
    p.fillRect(QRectF(-2 * r, offset, 4 * r, 4 * r), QColor(78, 58, 40));           // ground
    p.setPen(QPen(QColor(230, 230, 235, 180), 1.5));
    p.drawLine(QPointF(-2 * r, offset), QPointF(2 * r, offset));
    p.restore();
    p.setPen(QPen(kOutline, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
    // Fixed controller marker
    p.setPen(QPen(QColor(255, 210, 80), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c + QPointF(-r * 0.55, 0), c + QPointF(-r * 0.18, 0));
    p.drawLine(c + QPointF(r * 0.18, 0), c + QPointF(r * 0.55, 0));
    p.drawPoint(c);
    smallText(p, QRectF(c.x() - r, c.y() + r + 2, 2 * r, 16), Qt::AlignCenter,
              accel_.empty() ? QString("no motion data")
                             : QString("roll %1°  pitch %2°")
                                   .arg(std::lround(roll * 180 / std::numbers::pi))
                                   .arg(std::lround(pitch * 180 / std::numbers::pi)));

    // ── Graphs ───────────────────────────────────────────────────────────────
    const double gx = c.x() + r + 14;
    const QRectF graphs(gx, area.top(), area.right() - gx, area.height());
    auto graph = [&](QRectF g, const std::deque<Vec3>& data, double range,
                     const QString& title, const QString& unit) {
        p.setPen(QPen(kPanelEdge, 1));
        p.setBrush(kRecess);
        p.drawRoundedRect(g, 6, 6);
        p.setPen(QPen(QColor(60, 63, 72), 1, Qt::DotLine));
        p.drawLine(QPointF(g.left(), g.center().y()), QPointF(g.right(), g.center().y()));
        p.save();
        p.setClipRect(g.adjusted(1, 1, -1, -1));
        for (int k = 0; k < 3 && data.size() > 1; ++k) {
            QPainterPath line;
            for (std::size_t i = 0; i < data.size(); ++i) {
                const double x = g.left() + g.width() * i / 149.0;
                const double v = std::clamp(data[i][k] / range, -1.0, 1.0);
                const QPointF pt(x, g.center().y() - v * (g.height() / 2 - 3));
                if (i == 0) line.moveTo(pt); else line.lineTo(pt);
            }
            p.setPen(QPen(kAxis[k], 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawPath(line);
        }
        p.restore();
        smallText(p, g.adjusted(6, 2, -6, 0), Qt::AlignLeft | Qt::AlignTop,
                  QString("%1 (%2)").arg(title, unit), kText);
        // Current x / y / z values in their line colours
        const char* names[3] = {"x", "y", "z"};
        const double colW = (g.width() - 12) / 3;
        for (int k = 0; k < 3; ++k) {
            const QString v = data.empty() ? QString("-")
                : QString::number(data.back()[k], 'f', range > 10 ? 0 : 2);
            smallText(p, QRectF(g.left() + 6 + k * colW, g.bottom() - 18, colW, 16),
                      Qt::AlignLeft | Qt::AlignVCenter, QString("%1 %2").arg(names[k], v), kAxis[k]);
        }
    };
    const double gh = (graphs.height() - 8) / 2;
    graph(QRectF(graphs.left(), graphs.top(), graphs.width(), gh), gyro_, 400, "Gyro", "°/s");
    graph(QRectF(graphs.left(), graphs.top() + gh + 8, graphs.width(), gh), accel_, 2, "Accel", "g");
}

} // namespace ds4linux::gui
