#pragma once
// ds4linux GUI — DeviceWidget: shows connected controller status

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

namespace ds4linux::gui {

/// Widget displaying one connected controller's status.
class DeviceWidget : public QWidget {
    Q_OBJECT

public:
    explicit DeviceWidget(QWidget* parent = nullptr);

    void setDeviceName(const QString& name);
    void setDevicePath(const QString& path);
    void setBatteryPercent(int pct);
    void setProfileName(const QString& name);

    [[nodiscard]] QString devicePath() const { return path_; }

private:
    void setFrameStyle();

    QString       path_;
    QLabel*       nameLabel_    = nullptr;
    QLabel*       profileLabel_ = nullptr;
    QProgressBar* batteryBar_   = nullptr;
};

} // namespace ds4linux::gui
