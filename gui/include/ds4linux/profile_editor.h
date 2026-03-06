#pragma once
// ds4linux GUI — Profile editor panel

#include <QWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>

namespace ds4linux::gui {

class IpcClient;

/// Panel for editing and switching profiles.
class ProfileEditor : public QWidget {
    Q_OBJECT

public:
    explicit ProfileEditor(IpcClient* ipc, QWidget* parent = nullptr);

    void refreshProfileList();

signals:
    void profileActivated(const QString& name);

private slots:
    void onActivateClicked();
    void onSaveClicked();

private:
    IpcClient*      ipc_            = nullptr;
    QComboBox*      profileCombo_   = nullptr;
    QLineEdit*      nameEdit_       = nullptr;
    QComboBox*      outputModeCombo_= nullptr;
    QCheckBox*      touchpadCheck_  = nullptr;
    QDoubleSpinBox* rumbleSpin_     = nullptr;
    QPushButton*    activateBtn_    = nullptr;
    QPushButton*    saveBtn_        = nullptr;
};

} // namespace ds4linux::gui
