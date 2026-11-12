#pragma once
// ds4linux GUI — widgets of the controller test page
//
// Each widget takes one frame of controller state from GetInputState: the
// raw input and the output after the profile (deadzones, remaps), as the
// JSON objects the daemon sends (sticks 0-255, triggers 0-255, buttons,
// D-pad, gyro / accel in DS4 units, touch points in DS4 touchpad units).

#include <QColor>
#include <QJsonObject>
#include <QPointF>
#include <QWidget>

#include <array>
#include <deque>

namespace ds4linux::gui {

/// Profile settings the test page draws (deadzone rings, lightbar colour).
struct TestProfile {
    struct Zone {
        int    type  = 0;    // 0 none, 1 circular, 2 square (triggers: 0 = off)
        double inner = 0.0;
        double outer = 1.0;
    };
    Zone   left_stick, right_stick, l2, r2;
    QColor lightbar{0, 0, 255};

    static TestProfile fromJson(const QJsonObject& profile);
};

/// A drawing of the controller: buttons light up, sticks move, triggers fill.
class ControllerView : public QWidget {
public:
    explicit ControllerView(QWidget* parent = nullptr);
    void setState(const QJsonObject& state);
    void setModel(const QString& model);         // "DualSense", "DualShock 4 (v2)", ...
    void setLightbar(const QColor& color);
    [[nodiscard]] QSize sizeHint() const override { return {660, 400}; }
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int w) const override { return w * 6 / 10; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QJsonObject state_;
    bool        dualsense_ = true;
    QColor      lightbar_{0, 0, 255};
};

/// One stick: input and output position, deadzone rings and a short trail.
class StickPanel : public QWidget {
public:
    StickPanel(const QString& title, const char* xKey, const char* yKey,
               const char* clickKey, QWidget* parent = nullptr);
    void setFrame(const QJsonObject& in, const QJsonObject& out);
    void setZone(const TestProfile::Zone& zone);
    void clear();
    [[nodiscard]] QSize minimumSizeHint() const override { return {170, 200}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString            title_;
    const char*        xKey_;
    const char*        yKey_;
    const char*        clickKey_;
    QPointF            in_, out_;
    bool               click_ = false;
    bool               valid_ = false;
    TestProfile::Zone  zone_;
    std::deque<QPointF> trail_;
};

/// L2 / R2: raw pull, output and deadzone marks.
class TriggerPanel : public QWidget {
public:
    explicit TriggerPanel(QWidget* parent = nullptr);
    void setFrame(const QJsonObject& in, const QJsonObject& out);
    void setZones(const TestProfile::Zone& l2, const TestProfile::Zone& r2);
    void clear();
    [[nodiscard]] QSize minimumSizeHint() const override { return {150, 200}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::array<int, 2> in_{}, out_{};
    std::array<bool, 2> button_{};
    std::array<TestProfile::Zone, 2> zone_{};
};

/// The touchpad: finger positions with their tracking IDs and trails.
class TouchpadPanel : public QWidget {
public:
    explicit TouchpadPanel(QWidget* parent = nullptr);
    void setFrame(const QJsonObject& state);
    void clear();
    [[nodiscard]] QSize minimumSizeHint() const override { return {300, 190}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Finger {
        bool active = false;
        int  id = 0;
        QPointF pos;                 // 0..1
        std::deque<QPointF> trail;
    };
    std::array<Finger, 2> fingers_{};
    bool click_ = false;
};

/// Motion sensors: tilt (from the accelerometer) and gyro / accel graphs.
class MotionPanel : public QWidget {
public:
    explicit MotionPanel(QWidget* parent = nullptr);
    void setFrame(const QJsonObject& state);
    void clear();
    [[nodiscard]] QSize minimumSizeHint() const override { return {300, 250}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    using Vec3 = std::array<double, 3>;
    std::deque<Vec3> gyro_, accel_;  // deg/s, g
};

} // namespace ds4linux::gui
