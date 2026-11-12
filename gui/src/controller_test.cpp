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

const QColor kPanel(20, 22, 28);
const QColor kPanelEdge(52, 55, 63);
const QColor kBodyTop(62, 65, 74), kBodyBottom(40, 42, 49);
const QColor kOutline(96, 100, 112);
const QColor kIdle(74, 78, 89), kIdleDark(52, 55, 63);
const QColor kRecess(13, 14, 18);
const QColor kAccent(26, 159, 255);
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

// ── Controller line art ──────────────────────────────────────────────────────
// Drawn like Steam's controller test: light outlines on a dark background,
// pressed controls fill with the accent colour.  Designed in a 1000 x 830
// space; the body's top edge is at y = kTop, triggers and title above it.
// The left half is mirrored for the right.

constexpr double kTop = 160;
const QColor kLine(214, 219, 228);          // outlines
const QColor kLineDim(120, 126, 140);       // secondary lines
const QColor kArtBg(15, 17, 22);

QPointF at(double x, double y) { return {x, y + kTop}; }

struct Seg { QPointF c1, c2, p; };

QPointF mirror(QPointF pt) { return {1000 - pt.x(), pt.y()}; }

/// Close a left-half outline (from top centre to bottom centre) with its mirror image.
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

/// An open left-half line and its mirror image.
QPainterPath symmetricLine(QPointF start, const std::vector<Seg>& segs) {
    QPainterPath path(start);
    for (const auto& s : segs) path.cubicTo(s.c1, s.c2, s.p);
    QPainterPath right(mirror(start));
    for (const auto& s : segs) right.cubicTo(mirror(s.c1), mirror(s.c2), mirror(s.p));
    path.addPath(right);
    return path;
}

QPainterPath bodyShape() {
    return mirrored(at(500, 0), {
        {at(430, 0),   at(360, -2),  at(302, -2)},    // top edge
        {at(252, -6),  at(200, -10), at(160, 12)},    // shoulder
        {at(118, 36),  at(90, 110),  at(76, 190)},    // outer side
        {at(60, 290),  at(40, 420),  at(46, 520)},    // grip, outside
        {at(52, 612),  at(94, 662),  at(150, 650)},   // grip end
        {at(206, 638), at(230, 580), at(254, 520)},   // grip, inside
        {at(278, 462), at(302, 426), at(346, 414)},   // towards the middle
        {at(402, 402), at(452, 398), at(500, 398)},   // bottom edge
    });
}

/// The edge of the centre panel: from the touchpad down into the grips.
QPainterPath panelLines() {
    return symmetricLine(at(302, 214), {
        {at(270, 250), at(246, 300), at(250, 380)},
        {at(252, 440), at(254, 480), at(254, 520)},   // meets the grip outline
    });
}

QPainterPath touchpadShape() {
    QPainterPath t(at(306, 4));
    t.lineTo(at(694, 4));
    t.cubicTo(at(700, 80), at(698, 150), at(690, 200));
    t.cubicTo(at(620, 232), at(380, 232), at(310, 200));
    t.cubicTo(at(302, 150), at(300, 80), at(306, 4));
    t.closeSubpath();
    return t;
}

QPainterPath bumperShape(bool left) {
    QPainterPath b(at(298, -12));
    b.cubicTo(at(252, -16), at(204, -20), at(166, 2));
    b.cubicTo(at(150, -6), at(148, -20), at(160, -26));
    b.cubicTo(at(200, -44), at(254, -42), at(300, -36));
    b.cubicTo(at(310, -30), at(308, -16), at(298, -12));
    b.closeSubpath();
    if (left) return b;
    QTransform flip(-1, 0, 0, 1, 1000, 0);
    return flip.map(b);
}

QPainterPath triggerShape(bool left) {
    QPainterPath t;
    const QRectF r = left ? QRectF(186, kTop - 118, 104, 62) : QRectF(710, kTop - 118, 104, 62);
    t.addRoundedRect(r, 24, 24);
    return t;
}

/// A D-pad key: a rounded diamond at `offset` from the centre.
QPainterPath dpadKey(QPointF centre, QPointF offset) {
    QPainterPath k;
    k.addRoundedRect(QRectF(-21, -21, 42, 42), 9, 9);
    QTransform t;
    t.translate(centre.x() + offset.x(), centre.y() + offset.y());
    t.rotate(45);
    return t.map(k);
}

void faceSymbol(QPainter& p, QPointF c, std::string_view name, QColor color) {
    p.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const double s = 11;
    if (name == "triangle") {
        p.drawPolygon(QPolygonF{{c.x(), c.y() - s - 1}, {c.x() + s + 1, c.y() + s * 0.75},
                                {c.x() - s - 1, c.y() + s * 0.75}});
    } else if (name == "circle") {
        p.drawEllipse(c, s, s);
    } else if (name == "cross") {
        p.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
        p.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
    } else {
        p.drawRect(QRectF(c.x() - s * 0.9, c.y() - s * 0.9, s * 1.8, s * 1.8));
    }
}

} // namespace

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

    // Dark card behind the drawing
    p.setPen(QPen(kPanelEdge, 1));
    p.setBrush(kArtBg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

    const double scale = std::min(width() / 1000.0, height() / 830.0);
    p.translate((width() - 1000 * scale) / 2, (height() - 830 * scale) / 2);
    p.scale(scale, scale);

    const bool live = isEnabled() && !state_.isEmpty();
    auto on = [&](const char* name) { return live && pressed(state_, name); };
    const double lw = std::max(2.2, 1.6 / scale);   // at least ~1.6 px on screen
    const QPen line(kLine, lw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QPen thin(kLineDim, lw * 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QColor pressFill = kAccent;

    // Outline a shape; pressed shapes glow and fill with the accent colour
    auto shape = [&](const QPainterPath& path, bool down) {
        if (down) glow(p, path.boundingRect().center(),
                       0.7 * std::max(path.boundingRect().width(), path.boundingRect().height()), kAccent);
        p.setPen(down ? QPen(kAccent.lighter(150), lw) : line);
        p.setBrush(down ? QBrush(pressFill) : Qt::NoBrush);
        p.drawPath(path);
    };

    QFont titleFont;
    titleFont.setPixelSize(30);
    p.setFont(titleFont);
    p.setPen(kText);
    p.drawText(QRectF(200, 0, 600, 40), Qt::AlignCenter,
               dualsense_ ? "DualSense Wireless Controller" : "DUALSHOCK 4 Wireless Controller");

    // ── Triggers: fill as they are pulled, value inside ──────────────────────
    QFont valueFont;
    valueFont.setPixelSize(22);
    valueFont.setBold(true);
    for (bool left : {true, false}) {
        const QPainterPath t = triggerShape(left);
        const int v = live ? state_.value(left ? "l2" : "r2").toInt() : 0;
        if (v > 0) {
            const QRectF r = t.boundingRect();
            QRectF fill = r;
            fill.setTop(r.bottom() - r.height() * v / 255.0);
            p.save();
            p.setClipPath(t);
            QColor c = kAccent;
            c.setAlpha(110 + v * 145 / 255);
            p.fillRect(fill, c);
            p.restore();
        }
        p.setPen(v > 0 ? QPen(kAccent.lighter(150), lw) : line);
        p.setBrush(Qt::NoBrush);
        p.drawPath(t);
        p.setFont(valueFont);
        p.setPen(kText);
        p.drawText(t.boundingRect(), Qt::AlignCenter,
                   QString("%1  %2").arg(left ? "L2" : "R2").arg(v));
    }

    // ── Bumpers, body, centre panel ──────────────────────────────────────────
    shape(bumperShape(true), on("l1"));
    shape(bumperShape(false), on("r1"));

    p.setPen(line);
    p.setBrush(QColor(22, 24, 30));
    p.drawPath(bodyShape());
    p.setPen(thin);
    p.setBrush(Qt::NoBrush);
    p.drawPath(panelLines());

    // ── Touchpad, lightbar, touches ──────────────────────────────────────────
    const QPainterPath pad = touchpadShape();
    shape(pad, on("touchpad"));
    if (on("touchpad")) {   // keep the touches readable on the filled pad
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(22, 24, 30, 150));
        p.drawPath(pad);
    }

    QColor bar = lightbar_;
    if (!live) bar = bar.darker(260);
    QPen barPen(bar, 7, Qt::SolidLine, Qt::RoundCap);
    if (dualsense_) {
        // Light strips along both sides of the touchpad
        for (bool left : {true, false}) {
            QPainterPath strip(at(left ? 290 : 710, 30));
            strip.cubicTo(at(left ? 286 : 714, 90), at(left ? 288 : 712, 150), at(left ? 296 : 704, 196));
            QColor halo = bar;
            halo.setAlpha(70);
            p.setPen(QPen(halo, 20, Qt::SolidLine, Qt::RoundCap));
            p.drawPath(strip);
            p.setPen(barPen);
            p.drawPath(strip);
        }
    } else {
        QColor halo = bar;
        halo.setAlpha(70);
        p.setPen(QPen(halo, 20, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(at(370, -14), at(630, -14));
        p.setPen(barPen);
        p.drawLine(at(370, -14), at(630, -14));
    }

    const QRectF padBox = pad.boundingRect().adjusted(16, 14, -16, -26);
    const auto touches = state_.value("touches").toArray();
    for (int i = 0; live && i < touches.size(); ++i) {
        const auto t = touches[i].toObject();
        if (!t.value("active").toBool()) continue;
        const QPointF pt(padBox.left() + padBox.width() * t.value("x").toInt() / double(kDS4TouchpadMaxX),
                         padBox.top() + padBox.height() * t.value("y").toInt() / double(kDS4TouchpadMaxY));
        const QColor c = i == 0 ? QColor(255, 176, 50) : QColor(60, 220, 150);
        glow(p, pt, 30, c);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(pt, 11, 11);
    }

    // ── Create / Share and Options ───────────────────────────────────────────
    for (bool left : {true, false}) {
        QPainterPath b;
        b.addRoundedRect(QRectF(left ? 258 : 726, kTop + 30, 16, 46), 8, 8);
        shape(b, on(left ? "share" : "options"));
    }

    // ── D-pad: four diamond keys with arrows ─────────────────────────────────
    const QPointF dc = at(184, 150);
    const struct { const char* name; QPointF dir; } keys[] = {
        {"dpad_up", {0, -1}}, {"dpad_right", {1, 0}}, {"dpad_down", {0, 1}}, {"dpad_left", {-1, 0}}};
    for (const auto& k : keys) {
        const bool down = on(k.name);
        shape(dpadKey(dc, k.dir * 44), down);
        // Arrow pointing outwards
        const QPointF tip = dc + k.dir * 56, base = dc + k.dir * 40;
        const QPointF side(-k.dir.y() * 8, k.dir.x() * 8);
        p.setPen(Qt::NoPen);
        p.setBrush(down ? QColor(15, 17, 22) : kLine);
        p.drawPolygon(QPolygonF{tip, base + side, base - side});
    }

    // ── Face buttons ─────────────────────────────────────────────────────────
    const QPointF fc = at(816, 150);
    const struct { const char* name; QPointF off; QColor color; } faces[] = {
        {"triangle", {0, -58}, kTriangle}, {"circle", {58, 0}, kCircle},
        {"cross",    {0, 58},  kCross},    {"square", {-58, 0}, kSquare}};
    for (const auto& f : faces) {
        const QPointF c = fc + f.off;
        const bool down = on(f.name);
        if (down) glow(p, c, 58, f.color);
        p.setPen(down ? QPen(f.color.lighter(130), lw) : line);
        p.setBrush(down ? QBrush(f.color) : Qt::NoBrush);
        p.drawEllipse(c, 30, 30);
        faceSymbol(p, c, f.name, down ? QColor(15, 17, 22) : kLine);
    }

    // ── Speaker, PS and mute buttons ─────────────────────────────────────────
    p.setPen(QPen(kLineDim, lw * 0.7));
    p.setBrush(Qt::NoBrush);
    for (int row = 0; row < 2; ++row) {
        const int n = row == 0 ? 5 : 4;
        for (int i = 0; i < n; ++i) {
            p.drawEllipse(at(500 + (i - (n - 1) / 2.0) * 17, 248 + row * 15), 5, 5);
        }
    }
    QPainterPath ps;
    ps.addEllipse(at(500, 314), 26, 26);
    shape(ps, on("ps"));
    QFont psFont;
    psFont.setPixelSize(20);
    psFont.setBold(true);
    p.setFont(psFont);
    p.setPen(on("ps") ? QColor(15, 17, 22) : kLine);
    p.drawText(QRectF(474, kTop + 288, 52, 52), Qt::AlignCenter, "PS");
    if (dualsense_) {
        QPainterPath mute;
        mute.addRoundedRect(QRectF(478, kTop + 360, 44, 12), 6, 6);
        shape(mute, false);
    }

    // ── Sticks: base ring, cap that moves, lights on L3 / R3 ─────────────────
    auto stick = [&](QPointF c, const char* xk, const char* yk, const char* click) {
        const double x = live ? axis(state_, xk) : 0, y = live ? axis(state_, yk) : 0;
        const bool down = on(click);
        p.setPen(thin);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 60, 60);
        const QPointF cap = c + QPointF(x, y) * 20;
        if (down) glow(p, cap, 80, kAccent);
        p.setPen(down ? QPen(kAccent.lighter(150), lw) : line);
        p.setBrush(down ? QBrush(kAccent) : QBrush(kArtBg));
        p.drawEllipse(cap, 44, 44);
        p.setPen(down ? QPen(QColor(15, 17, 22), lw) : line);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(cap, 31, 31);
        if (std::hypot(x, y) > 0.05) {
            p.setPen(Qt::NoPen);
            p.setBrush(down ? QColor(15, 17, 22) : kAccent);
            p.drawEllipse(cap + QPointF(x, y) * 30, 6, 6);
        }
    };
    stick(at(334, 314), "lx", "ly", "l3");
    stick(at(666, 314), "rx", "ry", "r3");
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

void StickPanel::setZone(const DeadzoneParams& zone) {
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
    if (zone_.type != DeadzoneType::None) {
        QPen dz(QColor(255, 190, 60, 170), 1.2, Qt::DashLine);
        p.setPen(dz);
        p.setBrush(QColor(255, 190, 60, 28));
        auto ring = [&](double f, bool fill) {
            if (!fill) p.setBrush(Qt::NoBrush);
            if (zone_.type == DeadzoneType::Square) p.drawRect(QRectF(c.x() - f * r, c.y() - f * r, 2 * f * r, 2 * f * r));
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

void TriggerPanel::setZones(const DeadzoneParams& l2, const DeadzoneParams& r2) {
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
        if (zone_[i].type != DeadzoneType::None) {
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
