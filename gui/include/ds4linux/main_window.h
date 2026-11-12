#pragma once
// ds4linux GUI — Main window

#include <QMainWindow>
#include <QString>

class QCheckBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTimer;

namespace ds4linux::gui {

class ControllersPage;
class DaemonControl;
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
    void onServiceFinished(const QString& action, const QString& error);
    void refreshAutostart();

    QString          socketPath_;
    IpcClient*       ipc_         = nullptr;
    QTabWidget*      tabs_        = nullptr;
    ControllersPage* controllers_ = nullptr;
    ProfileEditor*   profiles_    = nullptr;
    TesterPage*      tester_      = nullptr;
    DaemonControl*   daemon_      = nullptr;
    QWidget*         offline_     = nullptr;
    QLabel*          offlineText_ = nullptr;
    QPushButton*     startBtn_    = nullptr;
    QPushButton*     stopBtn_     = nullptr;
    QCheckBox*       bootBox_     = nullptr;
    bool             triedAutoStart_ = false;
    QTimer*          pollTimer_   = nullptr;
    QTimer*          retryTimer_  = nullptr;
};

} // namespace ds4linux::gui
