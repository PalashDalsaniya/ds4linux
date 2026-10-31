#pragma once
// ds4linux GUI — Controller tester: live view of a controller's input

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QTimer;

namespace ds4linux::gui {

class IpcClient;

/// Paints a DS4-style controller from an input state object.
class ControllerView : public QWidget {
    Q_OBJECT
public:
    explicit ControllerView(QWidget* parent = nullptr);
    void setState(const QJsonObject& state);
    [[nodiscard]] QSize sizeHint() const override { return {640, 380}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QJsonObject state_;
};

class TesterPage : public QWidget {
    Q_OBJECT
public:
    explicit TesterPage(IpcClient* ipc, QWidget* parent = nullptr);

    /// Update the device chooser from a ListDevices result.
    void setDevices(const QJsonArray& devices);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void poll();

    IpcClient*      ipc_     = nullptr;
    QComboBox*      device_  = nullptr;
    QButtonGroup*   view_    = nullptr;   // 0 = input, 1 = output
    ControllerView* pad_     = nullptr;
    QLabel*         motion_  = nullptr;
    QLabel*         hint_    = nullptr;
    QTimer*         timer_   = nullptr;
    bool            waiting_ = false;
};

} // namespace ds4linux::gui
