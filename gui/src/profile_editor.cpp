// ds4linux GUI — ProfileEditor implementation

#include "ds4linux/profile_editor.h"
#include "ds4linux/ipc_client.h"
#include "ds4linux/led_picker.h"

#include <ds4linux/profile.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ds4linux::gui {

namespace {

// Display names for the remap table, in DS4Button order.
const char* const kButtonLabels[] = {
    "Cross", "Circle", "Square", "Triangle",
    "L1", "R1", "L2", "R2",
    "Share", "Options", "L3", "R3", "PS", "Touchpad click",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right",
};
static_assert(std::size(kButtonLabels) == kDS4ButtonCount);

QString buttonId(std::size_t i) {
    return QString::fromLatin1(to_string(static_cast<DS4Button>(i)).data());
}

bool isError(QWidget* parent, const QJsonObject& r) {
    if (r.value("type").toString() != "Error") return false;
    QMessageBox::warning(parent, "DS4Linux", r.value("message").toString());
    return true;
}

} // namespace

ProfileEditor::ProfileEditor(IpcClient* ipc, QWidget* parent)
    : QWidget(parent), ipc_(ipc)
{
    auto* root = new QHBoxLayout(this);

    // ── Left: profile list ───────────────────────────────────────────────────
    auto* left = new QVBoxLayout;
    list_ = new QListWidget(this);
    list_->setMinimumWidth(170);
    left->addWidget(list_);
    auto* newBtn  = new QPushButton("New", this);
    auto* copyBtn = new QPushButton("Duplicate", this);
    deleteBtn_    = new QPushButton("Delete", this);
    auto* listBtns = new QHBoxLayout;
    listBtns->addWidget(newBtn);
    listBtns->addWidget(copyBtn);
    listBtns->addWidget(deleteBtn_);
    left->addLayout(listBtns);
    root->addLayout(left);

    // ── Right: editor ────────────────────────────────────────────────────────
    editor_ = new QWidget(this);
    auto* form = new QVBoxLayout(editor_);
    title_ = new QLabel(editor_);
    title_->setStyleSheet("font-size: 16px; font-weight: bold;");
    form->addWidget(title_);

    auto* general = new QGroupBox("General", editor_);
    auto* gl = new QFormLayout(general);
    output_ = new QComboBox(general);
    output_->addItem("DualShock 4", 0);
    output_->addItem("Xbox 360", 1);
    output_->setToolTip("Which virtual controller games see");
    gl->addRow("Emulated controller", output_);
    lightbar_ = new LedPicker(general);
    gl->addRow("Lightbar", lightbar_);
    auto* rumbleRow = new QHBoxLayout;
    rumble_ = new QSlider(Qt::Horizontal, general);
    rumble_->setRange(0, 100);
    rumbleLbl_ = new QLabel(general);
    rumbleLbl_->setMinimumWidth(40);
    rumbleRow->addWidget(rumble_);
    rumbleRow->addWidget(rumbleLbl_);
    gl->addRow("Rumble strength", rumbleRow);
    form->addWidget(general);

    auto* dz = new QGroupBox("Deadzones", editor_);
    auto* dzl = new QGridLayout(dz);
    dzl->addWidget(new QLabel("<b>Type</b>"), 0, 1);
    dzl->addWidget(new QLabel("<b>Inner %</b>"), 0, 2);
    dzl->addWidget(new QLabel("<b>Outer %</b>"), 0, 3);
    leftStick_  = makeDeadzone(true);
    rightStick_ = makeDeadzone(true);
    l2_         = makeDeadzone(false);
    r2_         = makeDeadzone(false);
    const std::pair<const char*, DeadzoneControls*> rows[] = {
        {"Left stick", &leftStick_}, {"Right stick", &rightStick_},
        {"L2 trigger", &l2_}, {"R2 trigger", &r2_},
    };
    int r = 1;
    for (const auto& [label, c] : rows) {
        dzl->addWidget(new QLabel(label), r, 0);
        if (c->type) dzl->addWidget(c->type, r, 1);
        dzl->addWidget(c->inner, r, 2);
        dzl->addWidget(c->outer, r, 3);
        ++r;
    }
    dz->setToolTip("Input below Inner reads as zero; input above Outer reads as full.");
    form->addWidget(dz);

    auto* remapBox = new QGroupBox("Button remapping", editor_);
    auto* rl = new QVBoxLayout(remapBox);
    remap_ = new QTableWidget(static_cast<int>(kDS4ButtonCount), 2, remapBox);
    remap_->setHorizontalHeaderLabels({"Button", "Acts as"});
    remap_->verticalHeader()->hide();
    remap_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    remap_->setSelectionMode(QAbstractItemView::NoSelection);
    remap_->setMinimumHeight(260);
    for (std::size_t i = 0; i < kDS4ButtonCount; ++i) {
        auto* name = new QTableWidgetItem(kButtonLabels[i]);
        name->setFlags(Qt::ItemIsEnabled);
        remap_->setItem(static_cast<int>(i), 0, name);
        auto* target = new QComboBox;
        for (std::size_t t = 0; t < kDS4ButtonCount; ++t) {
            target->addItem(t == i ? QString("%1 (default)").arg(kButtonLabels[t])
                                   : QString(kButtonLabels[t]), buttonId(t));
        }
        target->addItem("Disabled", "none");
        target->setCurrentIndex(static_cast<int>(i));
        connect(target, &QComboBox::currentIndexChanged, this, [this]() { setDirty(true); });
        remap_->setCellWidget(static_cast<int>(i), 1, target);
    }
    rl->addWidget(remap_);
    auto* resetRemap = new QPushButton("Reset all buttons", remapBox);
    connect(resetRemap, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < remap_->rowCount(); ++i) {
            qobject_cast<QComboBox*>(remap_->cellWidget(i, 1))->setCurrentIndex(i);
        }
    });
    rl->addWidget(resetRemap, 0, Qt::AlignLeft);
    form->addWidget(remapBox);

    auto* actions = new QHBoxLayout;
    revertBtn_ = new QPushButton("Revert", editor_);
    saveBtn_   = new QPushButton("Save", editor_);
    saveBtn_->setDefault(true);
    actions->addStretch();
    actions->addWidget(revertBtn_);
    actions->addWidget(saveBtn_);
    form->addLayout(actions);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(editor_);
    root->addWidget(scroll, 1);

    // ── Signals ──────────────────────────────────────────────────────────────
    connect(list_, &QListWidget::currentTextChanged, this, [this](const QString& name) {
        if (name.isEmpty() || name == current_) return;
        if (dirty_) {
            const auto answer = QMessageBox::question(
                this, "DS4Linux", QString("Discard unsaved changes to \"%1\"?").arg(current_));
            if (answer != QMessageBox::Yes) {
                QSignalBlocker block(list_);
                const auto items = list_->findItems(current_, Qt::MatchExactly);
                if (!items.isEmpty()) list_->setCurrentItem(items.first());
                return;
            }
        }
        loadProfile(name);
    });
    connect(newBtn,  &QPushButton::clicked, this, [this]() { createProfile(false); });
    connect(copyBtn, &QPushButton::clicked, this, [this]() { createProfile(true); });
    connect(deleteBtn_, &QPushButton::clicked, this, &ProfileEditor::deleteProfile);
    connect(saveBtn_,   &QPushButton::clicked, this, &ProfileEditor::save);
    connect(revertBtn_, &QPushButton::clicked, this, [this]() { loadProfile(current_); });
    connect(output_, &QComboBox::currentIndexChanged, this, [this]() { setDirty(true); });
    connect(lightbar_, &LedPicker::colorChanged, this, [this]() { setDirty(true); });
    connect(rumble_, &QSlider::valueChanged, this, [this](int v) {
        rumbleLbl_->setText(QString("%1%").arg(v));
        setDirty(true);
    });

    editor_->setEnabled(false);
    setDirty(false);
}

ProfileEditor::DeadzoneControls ProfileEditor::makeDeadzone(bool stick) {
    DeadzoneControls c;
    if (stick) {
        c.type = new QComboBox(this);
        c.type->addItem("None", 0);
        c.type->addItem("Circular", 1);
        c.type->addItem("Square", 2);
        connect(c.type, &QComboBox::currentIndexChanged, this, [this]() { setDirty(true); });
    }
    auto spin = [&](double min, double max) {
        auto* s = new QDoubleSpinBox(this);
        s->setRange(min, max);
        s->setDecimals(0);
        s->setSuffix(" %");
        connect(s, &QDoubleSpinBox::valueChanged, this, [this]() { setDirty(true); });
        return s;
    };
    c.inner = spin(0, 95);
    c.outer = spin(5, 100);
    return c;
}

void ProfileEditor::setDeadzone(const DeadzoneControls& c, const QJsonObject& dz) const {
    if (c.type) c.type->setCurrentIndex(dz.value("type").toInt(1));
    c.inner->setValue(dz.value("inner").toDouble(0) * 100.0);
    c.outer->setValue(dz.value("outer").toDouble(1) * 100.0);
}

QJsonObject ProfileEditor::getDeadzone(const DeadzoneControls& c) const {
    const double inner = c.inner->value() / 100.0;
    const double outer = std::max(c.outer->value() / 100.0, inner + 0.05);
    int type = 1;
    if (c.type) type = c.type->currentData().toInt();
    else if (inner <= 0.0 && outer >= 1.0) type = 0;  // triggers: no deadzone
    return {{"type", type}, {"inner", inner}, {"outer", std::min(outer, 1.0)}};
}

void ProfileEditor::refreshProfileList() {
    ipc_->request("ListProfiles", {}, [this](const QJsonObject& r) {
        if (r.value("type").toString() != "ProfileList") return;
        QStringList names;
        for (const auto& v : r.value("profiles").toArray()) names << v.toString();
        names_ = names;
        emit profilesChanged(names);

        QSignalBlocker block(list_);
        list_->clear();
        list_->addItems(names);
        QString select = names.contains(current_) ? current_ : names.value(0);
        const auto items = list_->findItems(select, Qt::MatchExactly);
        if (!items.isEmpty()) list_->setCurrentItem(items.first());
        if (select != current_ || !dirty_) loadProfile(select);
    });
}

void ProfileEditor::loadProfile(const QString& name) {
    if (name.isEmpty()) return;
    ipc_->request("LoadProfile", {{"name", name}}, [this](const QJsonObject& r) {
        if (isError(this, r)) return;
        showProfile(r.value("profile").toObject());
    });
}

void ProfileEditor::showProfile(const QJsonObject& p) {
    loading_ = true;
    current_ = p.value("name").toString();
    title_->setText(current_);
    output_->setCurrentIndex(p.value("output_mode").toInt(0) == 1 ? 1 : 0);
    const auto c = p.value("lightbar_color").toObject();
    lightbar_->setColor(QColor(c.value("r").toInt(0), c.value("g").toInt(0), c.value("b").toInt(255)));
    rumble_->setValue(qRound(p.value("rumble_strength").toDouble(1.0) * 100.0));
    rumbleLbl_->setText(QString("%1%").arg(rumble_->value()));
    setDeadzone(leftStick_,  p.value("left_stick_dz").toObject());
    setDeadzone(rightStick_, p.value("right_stick_dz").toObject());
    setDeadzone(l2_, p.value("left_trigger_dz").toObject());
    setDeadzone(r2_, p.value("right_trigger_dz").toObject());

    const auto map = p.value("button_map").toObject();
    for (int i = 0; i < remap_->rowCount(); ++i) {
        auto* combo = qobject_cast<QComboBox*>(remap_->cellWidget(i, 1));
        const auto target = map.value(buttonId(static_cast<std::size_t>(i))).toString();
        const int idx = target.isEmpty() ? i : combo->findData(target);
        combo->setCurrentIndex(idx >= 0 ? idx : i);
    }

    deleteBtn_->setEnabled(current_ != "Default");
    editor_->setEnabled(true);
    loading_ = false;
    setDirty(false);
}

QJsonObject ProfileEditor::collect() const {
    const QColor c = lightbar_->currentColor();
    QJsonObject map;
    for (int i = 0; i < remap_->rowCount(); ++i) {
        auto* combo = qobject_cast<QComboBox*>(remap_->cellWidget(i, 1));
        if (combo->currentIndex() != i) {
            map[buttonId(static_cast<std::size_t>(i))] = combo->currentData().toString();
        }
    }
    return {
        {"name", current_},
        {"output_mode", output_->currentData().toInt()},
        {"lightbar_color", QJsonObject{{"r", c.red()}, {"g", c.green()}, {"b", c.blue()}}},
        {"rumble_strength", rumble_->value() / 100.0},
        {"left_stick_dz", getDeadzone(leftStick_)},
        {"right_stick_dz", getDeadzone(rightStick_)},
        {"left_trigger_dz", getDeadzone(l2_)},
        {"right_trigger_dz", getDeadzone(r2_)},
        {"button_map", map},
    };
}

void ProfileEditor::save() {
    if (current_.isEmpty()) return;
    ipc_->request("SaveProfile", {{"profile", collect()}}, [this](const QJsonObject& r) {
        if (isError(this, r)) return;
        setDirty(false);
    });
}

void ProfileEditor::createProfile(bool copyCurrent) {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, copyCurrent ? "Duplicate profile" : "New profile", "Profile name:",
        QLineEdit::Normal, copyCurrent ? current_ + " copy" : QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (!is_valid_profile_name(name.toStdString())) {
        QMessageBox::warning(this, "DS4Linux",
                             "Use up to 64 letters, digits, spaces, '_', '.' or '-'.");
        return;
    }
    if (names_.contains(name)) {
        QMessageBox::warning(this, "DS4Linux", QString("\"%1\" already exists.").arg(name));
        return;
    }

    auto saveAs = [this, name](QJsonObject p) {
        p["name"] = name;
        ipc_->request("SaveProfile", {{"profile", p}}, [this, name](const QJsonObject& r) {
            if (isError(this, r)) return;
            current_ = name;
            dirty_ = false;
            refreshProfileList();
        });
    };
    if (copyCurrent && !current_.isEmpty()) {
        saveAs(collect());
    } else {
        ipc_->request("LoadProfile", {{"name", "Default"}}, [saveAs](const QJsonObject& r) {
            saveAs(r.value("profile").toObject());
        });
    }
}

void ProfileEditor::deleteProfile() {
    if (current_.isEmpty() || current_ == "Default") return;
    if (QMessageBox::question(this, "DS4Linux",
                              QString("Delete profile \"%1\"? Controllers using it switch to Default.")
                                  .arg(current_)) != QMessageBox::Yes) {
        return;
    }
    ipc_->request("DeleteProfile", {{"name", current_}}, [this](const QJsonObject& r) {
        if (isError(this, r)) return;
        current_.clear();
        dirty_ = false;
        refreshProfileList();
    });
}

void ProfileEditor::setDirty(bool dirty) {
    if (loading_) return;
    dirty_ = dirty;
    saveBtn_->setEnabled(dirty);
    revertBtn_->setEnabled(dirty);
    title_->setText(current_ + (dirty ? "  •  unsaved changes" : ""));
}

} // namespace ds4linux::gui
