#pragma once
// ds4linux GUI — Main window

#include <QMainWindow>
#include <QString>

class QLabel;
class QTabWidget;
class QTimer;

namespace ds4linux::gui {

class ControllersPage;
class IpcClient;
class ProfileEditor;
class TesterPage;

/// Top-level application window: Controllers, Profiles and Tester tabs.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QString socketPath, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void connectToDaemon();
    void onConnected();
    void onDisconnected();

    QString          socketPath_;
    IpcClient*       ipc_         = nullptr;
    QTabWidget*      tabs_        = nullptr;
    ControllersPage* controllers_ = nullptr;
    ProfileEditor*   profiles_    = nullptr;
    TesterPage*      tester_      = nullptr;
    QLabel*          offline_     = nullptr;
    QTimer*          pollTimer_   = nullptr;
    QTimer*          retryTimer_  = nullptr;
};

} // namespace ds4linux::gui
