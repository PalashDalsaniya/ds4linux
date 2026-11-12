#pragma once
// ds4linux GUI — Controller test: live view of a controller's input

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QTimer;

namespace ds4linux::gui {

class ControllerView;
class IpcClient;
class MotionPanel;
class StickPanel;
class TouchpadPanel;
class TriggerPanel;

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
    void selectionChanged();
    void updateInfo();
    void loadProfile();
    void clearView();
    [[nodiscard]] QJsonObject currentDevice() const;

    IpcClient*      ipc_      = nullptr;
    QComboBox*      device_   = nullptr;
    QLabel*         info_     = nullptr;
    QButtonGroup*   view_     = nullptr;   // 0 = input, 1 = output
    ControllerView* pad_      = nullptr;
    StickPanel*     left_     = nullptr;
    StickPanel*     right_    = nullptr;
    TriggerPanel*   triggers_ = nullptr;
    TouchpadPanel*  touch_    = nullptr;
    MotionPanel*    motion_   = nullptr;
    QWidget*        body_     = nullptr;
    QLabel*         hint_     = nullptr;
    QTimer*         timer_    = nullptr;
    QJsonArray      devices_;
    bool            waiting_  = false;
};

} // namespace ds4linux::gui
