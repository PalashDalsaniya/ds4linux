#pragma once
// ds4linux GUI — Controllers page: connected controllers, battery, profile

#include <QJsonArray>
#include <QStringList>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace ds4linux::gui {

class IpcClient;

class ControllersPage : public QWidget {
    Q_OBJECT
public:
    explicit ControllersPage(IpcClient* ipc, QWidget* parent = nullptr);

    /// Refresh the controller list from the daemon.
    void refresh();
    /// Update the profile choices offered in the table.
    void setProfiles(const QStringList& profiles);

signals:
    /// The device list changed (for the tester's device chooser).
    void devicesChanged(const QJsonArray& devices);

private:
    void populate(const QJsonArray& devices);
    void updateBatteries(const QJsonArray& devices);
    [[nodiscard]] QString selectedPath() const;

    IpcClient*    ipc_      = nullptr;
    QTableWidget* table_    = nullptr;
    QLabel*       empty_    = nullptr;
    QPushButton*  rumbleBtn_ = nullptr;
    QPushButton*  colorBtn_ = nullptr;
    QStringList   profiles_;
    QJsonArray    devices_;
    QString       layoutKey_;   // what the table rows were last built from
};

} // namespace ds4linux::gui
