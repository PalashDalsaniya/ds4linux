// ds4linux GUI — ProfileEditor implementation

#include "ds4linux/profile_editor.h"
#include "ds4linux/ipc_client.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QVBoxLayout>

namespace ds4linux::gui {

ProfileEditor::ProfileEditor(IpcClient* ipc, QWidget* parent)
    : QWidget(parent)
    , ipc_(ipc)
{
    auto* mainLayout = new QVBoxLayout(this);

    // ── Profile selector ─────────────────────────────────────────────────────
    auto* selectorGroup = new QGroupBox("Profile", this);
    auto* selectorLayout = new QHBoxLayout(selectorGroup);

    profileCombo_ = new QComboBox(this);
    profileCombo_->setMinimumWidth(200);

    activateBtn_ = new QPushButton("Activate", this);
    connect(activateBtn_, &QPushButton::clicked, this, &ProfileEditor::onActivateClicked);

    selectorLayout->addWidget(profileCombo_, 1);
    selectorLayout->addWidget(activateBtn_);

    // ── Editor fields ────────────────────────────────────────────────────────
    auto* editorGroup = new QGroupBox("Edit Profile", this);
    auto* form = new QFormLayout(editorGroup);

    nameEdit_ = new QLineEdit("Default", this);
    form->addRow("Name:", nameEdit_);

    outputModeCombo_ = new QComboBox(this);
    outputModeCombo_->addItem("DualShock 4", 0);
    outputModeCombo_->setEnabled(false); // DS4-only for now
    form->addRow("Output Mode:", outputModeCombo_);

    touchpadCheck_ = new QCheckBox("Use touchpad as mouse", this);
    form->addRow("Touchpad:", touchpadCheck_);

    rumbleSpin_ = new QDoubleSpinBox(this);
    rumbleSpin_->setRange(0.0, 1.0);
    rumbleSpin_->setSingleStep(0.1);
    rumbleSpin_->setValue(1.0);
    form->addRow("Rumble Strength:", rumbleSpin_);

    saveBtn_ = new QPushButton("Save Profile", this);
    connect(saveBtn_, &QPushButton::clicked, this, &ProfileEditor::onSaveClicked);

    mainLayout->addWidget(selectorGroup);
    mainLayout->addWidget(editorGroup);
    mainLayout->addWidget(saveBtn_);
    mainLayout->addStretch();
    setLayout(mainLayout);
}

void ProfileEditor::refreshProfileList() {
    ipc_->sendCommand("ListProfiles");
}

void ProfileEditor::onActivateClicked() {
    QString name = profileCombo_->currentText();
    if (name.isEmpty()) return;

    QJsonObject extra;
    extra["name"] = name;
    ipc_->sendCommand("SetActiveProfile", extra);
    emit profileActivated(name);
}

void ProfileEditor::onSaveClicked() {
    QJsonObject prof;
    prof["name"]               = nameEdit_->text();
    prof["output_mode"]        = outputModeCombo_->currentData().toInt();
    prof["touchpad_as_mouse"]  = touchpadCheck_->isChecked();
    prof["rumble_strength"]    = rumbleSpin_->value();
    prof["lightbar_color"]     = QJsonObject{{"r", 0}, {"g", 0}, {"b", 255}};

    QJsonObject msg;
    msg["profile"] = QString::fromUtf8(QJsonDocument(prof).toJson(QJsonDocument::Compact));
    ipc_->sendCommand("SaveProfile", msg);
}

} // namespace ds4linux::gui
