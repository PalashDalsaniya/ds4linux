#pragma once
// ds4linux GUI — Profiles page: create, edit and delete profiles

#include <ds4linux/profile.h>

#include <QJsonObject>
#include <QStringList>
#include <QWidget>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QTableWidget;

namespace ds4linux::gui {

class IpcClient;
class LedPicker;

class ProfileEditor : public QWidget {
    Q_OBJECT
public:
    explicit ProfileEditor(IpcClient* ipc, QWidget* parent = nullptr);

    /// Reload the profile list from the daemon.
    void refreshProfileList();

signals:
    /// The set of profile names changed.
    void profilesChanged(const QStringList& names);

private:
    struct DeadzoneControls {
        QComboBox*      type  = nullptr;   // sticks only
        QDoubleSpinBox* inner = nullptr;
        QDoubleSpinBox* outer = nullptr;
    };

    void loadProfile(const QString& name);
    void showProfile(const Profile& p);
    [[nodiscard]] Profile collect() const;
    void saveProfile(const Profile& p, const std::function<void()>& onSaved);
    void save();
    void createProfile(bool copyCurrent);
    void deleteProfile();
    void setDirty(bool dirty);
    DeadzoneControls makeDeadzone(bool stick);
    void setDeadzone(const DeadzoneControls& c, const DeadzoneParams& dz) const;
    [[nodiscard]] DeadzoneParams getDeadzone(const DeadzoneControls& c) const;

    IpcClient*   ipc_       = nullptr;
    QListWidget* list_      = nullptr;
    QWidget*     editor_    = nullptr;
    QLabel*      title_     = nullptr;
    QComboBox*   output_    = nullptr;
    LedPicker*   lightbar_  = nullptr;
    QSlider*     rumble_    = nullptr;
    QLabel*      rumbleLbl_ = nullptr;
    DeadzoneControls leftStick_, rightStick_, l2_, r2_;
    QTableWidget* remap_    = nullptr;
    QPushButton* saveBtn_   = nullptr;
    QPushButton* revertBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;

    QString     current_;
    QStringList names_;
    bool        dirty_   = false;
    bool        loading_ = false;
};

} // namespace ds4linux::gui
