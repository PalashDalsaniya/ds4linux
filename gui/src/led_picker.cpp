// ds4linux GUI — LedPicker implementation

#include "ds4linux/led_picker.h"

#include <QColorDialog>
#include <QHBoxLayout>

namespace ds4linux::gui {

LedPicker::LedPicker(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);

    preview_ = new QLabel(this);
    preview_->setFixedSize(32, 32);
    setColor(color_);

    pickBtn_ = new QPushButton("Change Lightbar Color", this);
    connect(pickBtn_, &QPushButton::clicked, this, &LedPicker::openColorDialog);

    layout->addWidget(preview_);
    layout->addWidget(pickBtn_);
    layout->addStretch();
    setLayout(layout);
}

void LedPicker::setColor(const QColor& c) {
    color_ = c;
    preview_->setStyleSheet(
        QString("background-color: %1; border: 1px solid #888; border-radius: 4px;")
            .arg(c.name())
    );
}

void LedPicker::openColorDialog() {
    QColor chosen = QColorDialog::getColor(color_, this, "Lightbar Color");
    if (chosen.isValid()) {
        setColor(chosen);
        emit colorChanged(chosen);
    }
}

} // namespace ds4linux::gui
