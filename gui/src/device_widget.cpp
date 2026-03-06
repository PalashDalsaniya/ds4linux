// ds4linux GUI — DeviceWidget implementation

#include "ds4linux/device_widget.h"

#include <QHBoxLayout>
#include <QFont>

namespace ds4linux::gui {

DeviceWidget::DeviceWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);

    nameLabel_ = new QLabel("No device", this);
    QFont bold = nameLabel_->font();
    bold.setBold(true);
    nameLabel_->setFont(bold);

    profileLabel_ = new QLabel("Profile: —", this);

    batteryBar_ = new QProgressBar(this);
    batteryBar_->setRange(0, 100);
    batteryBar_->setValue(0);
    batteryBar_->setMaximumWidth(120);
    batteryBar_->setTextVisible(true);
    batteryBar_->setFormat("%p% 🔋");

    layout->addWidget(nameLabel_, 1);
    layout->addWidget(profileLabel_);
    layout->addWidget(batteryBar_);

    setLayout(layout);
    setFrameStyle();
}

void DeviceWidget::setDeviceName(const QString& name) {
    nameLabel_->setText(name);
}

void DeviceWidget::setDevicePath(const QString& path) {
    path_ = path;
}

void DeviceWidget::setBatteryPercent(int pct) {
    if (pct < 0) {
        batteryBar_->setValue(0);
        batteryBar_->setFormat("N/A");
    } else {
        batteryBar_->setValue(pct);
        batteryBar_->setFormat("%p%");
    }
}

void DeviceWidget::setProfileName(const QString& name) {
    profileLabel_->setText("Profile: " + name);
}

// Private helpers
void DeviceWidget::setFrameStyle() {
    setStyleSheet(
        "DeviceWidget {"
        "  border: 1px solid #555;"
        "  border-radius: 6px;"
        "  background: #2b2b2b;"
        "  padding: 4px;"
        "}"
    );
}

} // namespace ds4linux::gui
