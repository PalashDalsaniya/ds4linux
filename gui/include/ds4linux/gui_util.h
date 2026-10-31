#pragma once
// ds4linux GUI — small helpers shared by the pages

#include <ds4linux/profile.h>

#include <QJsonObject>
#include <QMessageBox>
#include <QString>
#include <QWidget>

#include <string_view>

namespace ds4linux::gui {

inline QString toQString(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

/// "USB" / "Bluetooth" for a device's "connection" field.
inline QString connectionLabel(const QJsonObject& device) {
    return device.value("connection").toString() == "bluetooth" ? "Bluetooth" : "USB";
}

/// Display name of an emulated controller type.
inline QString outputModeLabel(OutputMode m) {
    return m == OutputMode::X360 ? "Xbox 360" : "DualShock 4";
}

/// If `response` is an Error, show its message and return true.
inline bool showIpcError(QWidget* parent, const QJsonObject& response) {
    if (response.value("type").toString() != "Error") return false;
    QMessageBox::warning(parent, "DS4Linux", response.value("message").toString());
    return true;
}

} // namespace ds4linux::gui
