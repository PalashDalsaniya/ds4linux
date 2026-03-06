#pragma once
// ds4linux GUI — LED color picker widget

#include <QWidget>
#include <QColor>
#include <QPushButton>
#include <QLabel>

namespace ds4linux::gui {

/// Simple color picker for the lightbar.
class LedPicker : public QWidget {
    Q_OBJECT

public:
    explicit LedPicker(QWidget* parent = nullptr);

    [[nodiscard]] QColor currentColor() const { return color_; }
    void setColor(const QColor& c);

signals:
    void colorChanged(const QColor& color);

private slots:
    void openColorDialog();

private:
    QColor       color_{0, 0, 255};
    QPushButton* pickBtn_   = nullptr;
    QLabel*      preview_   = nullptr;
};

} // namespace ds4linux::gui
