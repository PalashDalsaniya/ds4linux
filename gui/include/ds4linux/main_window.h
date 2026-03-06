#pragma once
// ds4linux GUI — Main window

#include <QMainWindow>
#include <QTimer>
#include <QVBoxLayout>
#include <QStatusBar>

namespace ds4linux::gui {

class IpcClient;
class DeviceWidget;
class ProfileEditor;
class LedPicker;

/// Top-level application window.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onDaemonConnected();
    void onDaemonDisconnected();
    void onDaemonMessage(const QString& json);
    void pollDevices();

private:
    void setupUi();
    void connectToDaemon();

    IpcClient*     ipc_          = nullptr;
    ProfileEditor* profileEditor_= nullptr;
    LedPicker*     ledPicker_    = nullptr;
    QWidget*       devicesPanel_ = nullptr;
    QVBoxLayout*   devicesLayout_= nullptr;
    QTimer*        pollTimer_    = nullptr;
    QStatusBar*    statusBar_    = nullptr;
};

} // namespace ds4linux::gui
